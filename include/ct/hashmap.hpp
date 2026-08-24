
#pragma once

#include "detail/utils.hpp"

namespace ct
{

    namespace detail
    {

        inline std::uint64_t hash_mix(std::uint64_t x)
        {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            return x;
        }
    } 

    template <typename K>
    struct Hash
    {
        std::uint64_t operator()(const K &k) const { return k.hash(); }
    };

#define CT_INT_HASH(T)                                                    \
    template <>                                                           \
    struct Hash<T>                                                        \
    {                                                                     \
        std::uint64_t operator()(T k) const                               \
        {                                                                 \
            return detail::hash_mix(static_cast<std::uint64_t>(k));      \
        }                                                                 \
    };
    CT_INT_HASH(char)
    CT_INT_HASH(signed char)
    CT_INT_HASH(unsigned char)
    CT_INT_HASH(short)
    CT_INT_HASH(unsigned short)
    CT_INT_HASH(int)
    CT_INT_HASH(unsigned)
    CT_INT_HASH(long)
    CT_INT_HASH(unsigned long)
    CT_INT_HASH(long long)
    CT_INT_HASH(unsigned long long)
#undef CT_INT_HASH

    template <typename T>
    struct Hash<T *>
    {
        std::uint64_t operator()(T *p) const
        {
            return detail::hash_mix(reinterpret_cast<std::uintptr_t>(p));
        }
    };

    template <typename K, typename V, typename H = Hash<K>, typename Alloc = HeapAlloc>
    class HashMap : private H, private Alloc
    {
    public:
        struct Entry
        {
            K key;
            V value;
        };

        using size_type = std::size_t;

        HashMap() noexcept
            : H(), Alloc(), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        explicit HashMap(const H &h) noexcept
            : H(h), Alloc(), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        HashMap(const H &h, const Alloc &alloc) noexcept
            : H(h), Alloc(alloc), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        ~HashMap()
        {
            destroy_all();
            release_slots();
        }

        HashMap(const HashMap &o) : H(static_cast<const H &>(o)), Alloc(static_cast<const Alloc &>(o)),
                                    slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
            if (o.size_)
            {
                reserve(o.size_);
                for (size_type i = 0; i <= o.mask_; ++i)
                    if (o.meta_[i])
                        put(o.slots_[i].key, o.slots_[i].value);
            }
        }

        HashMap(HashMap &&o) noexcept
            : H(static_cast<H &&>(o)),
              Alloc(static_cast<Alloc &&>(o)), slots_(o.slots_), meta_(o.meta_), mask_(o.mask_),
              size_(o.size_)
        {
            o.slots_ = nullptr;
            o.meta_ = nullptr;
            o.mask_ = 0;
            o.size_ = 0;
        }

        HashMap &operator=(const HashMap &o)
        {
            if (this != &o)
            {
                clear();
                if (o.size_)
                {
                    reserve(o.size_);
                    for (size_type i = 0; i <= o.mask_; ++i)
                        if (o.meta_[i])
                            put(o.slots_[i].key, o.slots_[i].value);
                }
            }
            return *this;
        }

        HashMap &operator=(HashMap &&o) noexcept
        {
            if (this != &o)
            {
                destroy_all();
                release_slots();
                static_cast<H &>(*this) = static_cast<H &&>(o);
                static_cast<Alloc &>(*this) = static_cast<Alloc &&>(o);
                slots_ = o.slots_;
                meta_ = o.meta_;
                mask_ = o.mask_;
                size_ = o.size_;
                o.slots_ = nullptr;
                o.meta_ = nullptr;
                o.mask_ = 0;
                o.size_ = 0;
            }
            return *this;
        }

        size_type size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }
        size_type capacity() const noexcept { return slots_ ? mask_ + 1 : 0; }

        V *find(const K &k) noexcept
        {
            if (!size_)
                return nullptr;
            size_type i = probe(k);
            return meta_[i] ? &slots_[i].value : nullptr;
        }
        const V *find(const K &k) const noexcept
        {
            return const_cast<HashMap *>(this)->find(k);
        }

        bool contains(const K &k) const noexcept { return find(k) != nullptr; }

        const V &get(const K &k, const V &fallback) const noexcept
        {
            const V *v = find(k);
            return v ? *v : fallback;
        }

        template <typename KK, typename VV>
        V &put(KK &&k, VV &&v)
        {
            if (CT_UNLIKELY(need_grow()))
                return put_slow(detail::forward<KK>(k), detail::forward<VV>(v));
            size_type i = probe(k);
            if (meta_[i])
                slots_[i].value = detail::forward<VV>(v);
            else
            {
                ::new (static_cast<void *>(&slots_[i]))
                    Entry{detail::forward<KK>(k), detail::forward<VV>(v)};
                meta_[i] = 1;
                ++size_;
            }
            return slots_[i].value;
        }

        V &operator[](const K &k)
        {
            if (CT_UNLIKELY(need_grow()))
                return index_slow(k);
            size_type i = probe(k);
            if (!meta_[i])
            {
                ::new (static_cast<void *>(&slots_[i])) Entry{k, V()};
                meta_[i] = 1;
                ++size_;
            }
            return slots_[i].value;
        }

        bool erase(const K &k)
        {
            if (!size_)
                return false;
            size_type i = probe(k);
            if (!meta_[i])
                return false;
            slots_[i].~Entry();

            size_type j = i;
            for (;;)
            {
                j = (j + 1) & mask_;
                if (!meta_[j])
                    break;
                size_type ideal = static_cast<size_type>(hash_of(slots_[j].key)) & mask_;

                if (((j - ideal) & mask_) >= ((j - i) & mask_))
                {
                    ::new (static_cast<void *>(&slots_[i]))
                        Entry{detail::move(slots_[j].key), detail::move(slots_[j].value)};
                    slots_[j].~Entry();
                    i = j;
                }
            }
            meta_[i] = 0;
            --size_;
            return true;
        }

        void clear()
        {
            if (slots_)
            {
                destroy_all();
                std::memset(meta_, 0, mask_ + 1);
                size_ = 0;
            }
        }

        void reserve(size_type n)
        {
            size_type scaled = 0;
            size_type requested = 0;
            size_type want = 0;
            if (!detail::checked_add(n, n / 3, scaled) ||
                !detail::checked_add(scaled, 1, requested) ||
                !detail::checked_pow2(requested, want))
                detail::fatal("ct::HashMap: capacidade invalida");
            if (want > capacity())
                rehash(want);
        }

        class iterator
        {
            Entry *e_;
            std::uint8_t *m_;
            std::uint8_t *mend_;

        public:
            iterator(Entry *e, std::uint8_t *m, std::uint8_t *mend)
                : e_(e), m_(m), mend_(mend)
            {
                skip();
            }
            Entry &operator*() const { return *e_; }
            Entry *operator->() const { return e_; }
            iterator &operator++()
            {
                ++e_;
                ++m_;
                skip();
                return *this;
            }
            bool operator==(const iterator &o) const { return m_ == o.m_; }
            bool operator!=(const iterator &o) const { return m_ != o.m_; }

        private:
            void skip()
            {
                while (m_ != mend_ && !*m_)
                {
                    ++m_;
                    ++e_;
                }
            }
        };

        iterator begin() noexcept
        {
            return iterator(slots_, meta_, meta_ ? meta_ + mask_ + 1 : nullptr);
        }
        iterator end() noexcept
        {
            std::uint8_t *me = meta_ ? meta_ + mask_ + 1 : nullptr;
            return iterator(slots_ ? slots_ + mask_ + 1 : nullptr, me, me);
        }

    private:
        Entry *slots_;
        std::uint8_t *meta_; 
        size_type mask_;     
        size_type size_;

        template <typename KK, typename VV>
        CT_NOINLINE V &put_slow(KK &&k, VV &&v)
        {
            if (slots_)
            {
                size_type i = probe(k);
                if (meta_[i])
                {
                    slots_[i].value = detail::forward<VV>(v);
                    return slots_[i].value;
                }
            }
            K stable_key(detail::forward<KK>(k));
            V stable_value(detail::forward<VV>(v));
            grow();
            size_type i = probe(stable_key);
            ::new (static_cast<void *>(&slots_[i]))
                Entry{detail::move(stable_key), detail::move(stable_value)};
            meta_[i] = 1;
            ++size_;
            return slots_[i].value;
        }

        CT_NOINLINE V &index_slow(const K &k)
        {
            if (slots_)
            {
                size_type i = probe(k);
                if (meta_[i])
                    return slots_[i].value;
            }
            K stable_key(k);
            grow();
            size_type i = probe(stable_key);
            ::new (static_cast<void *>(&slots_[i])) Entry{detail::move(stable_key), V()};
            meta_[i] = 1;
            ++size_;
            return slots_[i].value;
        }

        std::uint64_t hash_of(const K &k) const
        {
            return (*static_cast<const H *>(this))(k);
        }

        bool need_grow() const
        {
            if (!slots_)
                return true;
            const size_type capacity = mask_ + 1;
            return size_ == (std::numeric_limits<size_type>::max)() ||
                   size_ + 1 > capacity - capacity / 4;
        }

        size_type probe(const K &k) const
        {
            size_type i = static_cast<size_type>(hash_of(k)) & mask_;
            while (meta_[i])
            {
                if (slots_[i].key == k)
                    return i;
                i = (i + 1) & mask_;
            }
            return i;
        }

        CT_NOINLINE void grow()
        {
            const size_type capacity = slots_ ? mask_ + 1 : 16;
            if (capacity > (std::numeric_limits<size_type>::max)() / 2)
                detail::fatal("ct::HashMap: capacidade invalida");
            rehash(slots_ ? capacity * 2 : capacity);
        }

        void rehash(size_type new_cap)
        {
            if (new_cap < 2 || !detail::is_power_of_two(new_cap))
                detail::fatal("ct::HashMap: capacidade invalida");
            Entry *old_slots = slots_;
            std::uint8_t *old_meta = meta_;
            size_type old_cap = slots_ ? mask_ + 1 : 0;

            size_type entries_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(new_cap, sizeof(Entry), entries_bytes) ||
                !detail::checked_add(entries_bytes, new_cap, bytes))
                detail::fatal("ct::HashMap: tamanho invalido");
            void *blk = allocator().allocate(bytes, alignof(Entry));
            slots_ = static_cast<Entry *>(blk);
            meta_ = reinterpret_cast<std::uint8_t *>(slots_ + new_cap);
            std::memset(meta_, 0, new_cap);
            mask_ = new_cap - 1;

            for (size_type i = 0; i < old_cap; ++i)
                if (old_meta[i])
                {
                    size_type j = static_cast<size_type>(hash_of(old_slots[i].key)) & mask_;
                    while (meta_[j])
                        j = (j + 1) & mask_;
                    ::new (static_cast<void *>(&slots_[j]))
                        Entry{detail::move(old_slots[i].key),
                              detail::move(old_slots[i].value)};
                    meta_[j] = 1;
                    old_slots[i].~Entry();
                }
            if (old_slots)
            {
                size_type old_entries_bytes = 0;
                size_type old_bytes = 0;
                if (!detail::checked_mul(old_cap, sizeof(Entry), old_entries_bytes) ||
                    !detail::checked_add(old_entries_bytes, old_cap, old_bytes))
                    detail::fatal("ct::HashMap: tamanho invalido");
                allocator().deallocate(old_slots, old_bytes);
            }
        }

        void destroy_all()
        {
            if (slots_ && size_)
                for (size_type i = 0; i <= mask_; ++i)
                    if (meta_[i])
                        slots_[i].~Entry();
        }

        Alloc &allocator() { return static_cast<Alloc &>(*this); }

        void release_slots()
        {
            if (!slots_)
                return;
            size_type entries_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(mask_ + 1, sizeof(Entry), entries_bytes) ||
                !detail::checked_add(entries_bytes, mask_ + 1, bytes))
                detail::fatal("ct::HashMap: tamanho invalido");
            allocator().deallocate(slots_, bytes);
            slots_ = nullptr;
            meta_ = nullptr;
            mask_ = 0;
        }
    };

}
