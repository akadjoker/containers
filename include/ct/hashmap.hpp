
#pragma once

#include "detail/utils.hpp"
#include "span.hpp"
#include "string.hpp"

namespace ct
{

    template <typename K>
    struct Hash
    {
        std::uint64_t operator()(const K &k) const
        {
            return call(k, detail::integral_constant<bool, std::is_enum<K>::value>());
        }

    private:
        static std::uint64_t call(const K &k, detail::false_type) { return k.hash(); }
        static std::uint64_t call(const K &k, detail::true_type)
        {
            return detail::hash_mix(static_cast<std::uint64_t>(
                static_cast<typename std::underlying_type<K>::type>(k)));
        }
    };

    inline std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t h) noexcept
    {
        return seed ^ (h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    }

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
    CT_INT_HASH(wchar_t)
    CT_INT_HASH(char16_t)
    CT_INT_HASH(char32_t)
#undef CT_INT_HASH

    template <>
    struct Hash<bool>
    {
        std::uint64_t operator()(bool k) const { return detail::hash_mix(k ? 1u : 0u); }
    };

    template <>
    struct Hash<float>
    {
        std::uint64_t operator()(float k) const
        {
            if (k == 0.0f)
                k = 0.0f;
            std::uint32_t bits;
            std::memcpy(&bits, &k, sizeof(bits));
            return detail::hash_mix(bits);
        }
    };

    template <>
    struct Hash<double>
    {
        std::uint64_t operator()(double k) const
        {
            if (k == 0.0)
                k = 0.0;
            std::uint64_t bits;
            std::memcpy(&bits, &k, sizeof(bits));
            return detail::hash_mix(bits);
        }
    };

    template <>
    struct Hash<StringView>
    {
        std::uint64_t operator()(StringView s) const noexcept
        {
            return detail::hash_bytes(s.data(), s.size());
        }
    };

    template <>
    struct Hash<String>
    {
        using is_transparent = void;

        std::uint64_t operator()(const String &s) const noexcept
        {
            return detail::hash_bytes(s.data(), s.size());
        }
        std::uint64_t operator()(StringView s) const noexcept
        {
            return detail::hash_bytes(s.data(), s.size());
        }
        std::uint64_t operator()(const char *s) const noexcept { return (*this)(StringView(s)); }

        static bool equal(const String &a, StringView b) noexcept
        {
            return a.size() == b.size() && (b.size() == 0 || std::memcmp(a.data(), b.data(), b.size()) == 0);
        }
        static bool equal(const String &a, const char *b) noexcept { return equal(a, StringView(b)); }
    };

    template <typename T>
    struct Hash<T *>
    {
        std::uint64_t operator()(T *p) const
        {
            return detail::hash_mix(reinterpret_cast<std::uintptr_t>(p));
        }
    };

    namespace detail
    {
        template <bool B, typename A, typename C>
        struct hash_pick
        {
            using type = A;
        };
        template <typename A, typename C>
        struct hash_pick<false, A, C>
        {
            using type = C;
        };

        // Entradas com mais de 128 bytes ficam fora da tabela, cada uma no seu bloco (a tabela guarda ponteiros): a
        // tabela fica com 9 bytes por slot em vez de sizeof(entrada) + 1, por isso nao desperdica 25 a 60 % de memoria
        // em slots vazios, cresce sem mover as entradas e uma referencia a um valor sobrevive ao rehash.
        // (CT_HASH_INLINE_MAX muda o limite; com um valor enorme nenhuma entrada fica fora e a tabela e a de antes.)
#if !defined(CT_HASH_INLINE_MAX)
#define CT_HASH_INLINE_MAX 128
#define CT_ABI_HASHINL_BEGIN
#define CT_ABI_HASHINL_END
#else
        // um valor escolhido pela aplicacao muda o layout: o valor entra no nome do tipo (tem de ser um literal inteiro)
#define CT_ABI_HASHINL_BEGIN inline namespace CT_ABI_PASTE(abi_hashinline_, CT_HASH_INLINE_MAX) {
#define CT_ABI_HASHINL_END }
#endif
        constexpr std::size_t kHashInlineMax = CT_HASH_INLINE_MAX;
    } // namespace detail

    // Mapa de hash de enderecamento aberto com sondagem linear e remocao por deslocamento (sem marcadores).
    //
    // - Referencias, ponteiros e iteradores para elementos NAO sobrevivem a put, operator[] nem a erase (o rehash e o
    //   deslocamento movem elementos), excepto quando a entrada tem mais de 128 bytes (ficam fora da tabela, ver
    //   detail::kHashInlineMax): ai uma referencia a um valor sobrevive ao rehash e a insercoes, mas nao ao erase desse elemento.
    // - A meta (um byte por slot) diz se o slot esta ocupado; para chaves que nao sao inteiros, enumeracoes nem ponteiros
    //   (strings, chaves compostas, entradas grandes) guarda tambem a distancia do elemento ao seu slot de origem (0x81 + dist, e
    //   0xFF para 126 ou mais), de modo que a sondagem so compara chaves onde a distancia bate (uma falha quase nunca
    //   compara strings) e a remocao desloca os seguintes sem recalcular o hash.
    CT_ABI_DBGITER_BEGIN
    CT_ABI_HASHINL_BEGIN
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

    private:
        static constexpr bool kIndirect = sizeof(Entry) > detail::kHashInlineMax;
        static constexpr bool kDist = kIndirect || !detail::is_bytewise_comparable<K>::value;
        using Slot = typename detail::hash_pick<kIndirect, Entry *, Entry>::type;

    public:
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
                copy_entries(o, true);
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
                    copy_entries(o, false);
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
                CT_HASH_TOUCH();
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
            return meta_[i] ? &entry(i).value : nullptr;
        }
        const V *find(const K &k) const noexcept
        {
            return const_cast<HashMap *>(this)->find(k);
        }

        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        V *find(const Q &k) noexcept
        {
            if (!size_)
                return nullptr;
            size_type i = probe_as(k);
            return meta_[i] ? &entry(i).value : nullptr;
        }
        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        const V *find(const Q &k) const noexcept
        {
            return const_cast<HashMap *>(this)->find(k);
        }

        bool contains(const K &k) const noexcept { return find(k) != nullptr; }

        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        bool contains(const Q &k) const noexcept
        {
            return find(k) != nullptr;
        }

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
            size_type dist;
            size_type i = probe(k, dist);
            if (meta_[i])
                entry(i).value = detail::forward<VV>(v);
            else
            {
                construct_at(i, detail::forward<KK>(k), detail::forward<VV>(v));
                meta_[i] = meta_for(dist);
                ++size_;
                CT_HASH_TOUCH();
            }
            return entry(i).value;
        }

        V &operator[](const K &k)
        {
            if (CT_UNLIKELY(need_grow()))
                return index_slow(k);
            size_type dist;
            size_type i = probe(k, dist);
            if (!meta_[i])
            {
                construct_at(i, k, V());
                meta_[i] = meta_for(dist);
                ++size_;
                CT_HASH_TOUCH();
            }
            return entry(i).value;
        }

        /* Removes k and, when it was present, moves its value into out. Saves
           the caller a second probe when the removed value is still needed. */
        bool erase(const K &k, V &out)
        {
            if (!size_)
                return false;
            size_type i = probe(k);
            if (!meta_[i])
                return false;
            out = detail::move(entry(i).value);
            erase_at(i);
            return true;
        }

        bool erase(const K &k)
        {
            if (!size_)
                return false;
            size_type i = probe(k);
            if (!meta_[i])
                return false;
            erase_at(i);
            return true;
        }

        void erase_at(size_type i)
        {
            destroy_at(i);

            size_type j = i;
            if (kDist)
            {
                // a distancia de cada seguinte esta na meta (0xFF = 126 ou mais, e calcula-se pelo hash)
                for (;;)
                {
                    j = (j + 1) & mask_;
                    const std::uint8_t m = meta_[j];
                    if (!m)
                        break;
                    const size_type gap = (j - i) & mask_;
                    const size_type follower_dist = m != 255
                                                        ? static_cast<size_type>(m) - 0x81
                                                        : (j - (static_cast<size_type>(hash_of(entry(j).key)) & mask_)) & mask_;
                    if (follower_dist >= gap)
                    {
                        move_slot(i, j);
                        meta_[i] = meta_for(follower_dist - gap);
                        i = j;
                    }
                }
            }
            else
            {
                for (;;)
                {
                    j = (j + 1) & mask_;
                    if (!meta_[j])
                        break;
                    size_type ideal = static_cast<size_type>(hash_of(entry(j).key)) & mask_;

                    if (((j - ideal) & mask_) >= ((j - i) & mask_))
                    {
                        move_slot(i, j);
                        i = j;
                    }
                }
            }
            meta_[i] = 0;
            --size_;
            CT_HASH_TOUCH();
        }

        void clear()
        {
            if (slots_)
            {
                destroy_all();
                std::memset(meta_, 0, mask_ + 1);
                size_ = 0;
                CT_HASH_TOUCH();
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

        // Percorrer: a ordem e a dos slots (sem significado). Apagar ou inserir durante o ciclo e INVALIDO: a remocao
        // desloca os elementos seguintes para tras (o ciclo salta ou repete elementos) e a insercao pode fazer rehash
        // (invalida todos os iteradores e referencias). Para apagar varios use erase_if(pred), que visita cada elemento
        // uma vez; ou junte as chaves e apague depois. Com CT_DEBUG_ITERATORS=1 (explicito: nunca por omissao nem por causa de
        // NDEBUG, e igual em todas as unidades do programa; o tipo passa a ct::abi_dbgiter::HashMap) usar um iterador depois de a
        // tabela mudar aborta com uma mensagem; sem ela nao ha verificacao e o iterador e o SlotIter simples.
#if defined(CT_DEBUG_ITERATORS)
        using iterator = typename detail::hash_pick<kIndirect, detail::IndirectIter<detail::CheckedSlotIter<Entry *, kDist>, Entry>,
                                                    detail::CheckedSlotIter<Entry, kDist>>::type;
        using const_iterator = typename detail::hash_pick<kIndirect, detail::IndirectIter<detail::CheckedSlotIter<Entry *const, kDist>, const Entry>,
                                                          detail::CheckedSlotIter<const Entry, kDist>>::type;
#else
        using iterator = typename detail::hash_pick<kIndirect, detail::IndirectIter<detail::SlotIter<Entry *, kDist>, Entry>,
                                                    detail::SlotIter<Entry, kDist>>::type;
        using const_iterator = typename detail::hash_pick<kIndirect, detail::IndirectIter<detail::SlotIter<Entry *const, kDist>, const Entry>,
                                                          detail::SlotIter<const Entry, kDist>>::type;
#endif

        iterator begin() noexcept
        {
            return iterator(slots_, meta_, meta_ ? meta_ + mask_ + 1 : nullptr CT_HASH_ITER_ARGS);
        }
        iterator end() noexcept
        {
            std::uint8_t *me = meta_ ? meta_ + mask_ + 1 : nullptr;
            return iterator(slots_ ? slots_ + mask_ + 1 : nullptr, me, me CT_HASH_ITER_ARGS);
        }
        const_iterator begin() const noexcept
        {
            return const_iterator(slots_, meta_, meta_ ? meta_ + mask_ + 1 : nullptr CT_HASH_ITER_ARGS);
        }
        const_iterator end() const noexcept
        {
            const std::uint8_t *me = meta_ ? meta_ + mask_ + 1 : nullptr;
            return const_iterator(slots_ ? slots_ + mask_ + 1 : nullptr, me, me CT_HASH_ITER_ARGS);
        }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        // Apaga todos os elementos para que pred(chave, valor) da true e devolve quantos foram. Cada elemento e
        // visitado exactamente uma vez (percorre os slots sem os deslocar e refaz a tabela no fim, em vez de apagar um
        // a um com o deslocamento), por isso e o ciclo certo para apagar enquanto se percorre. pred nao pode mexer na tabela.
        template <typename Pred>
        size_type erase_if(Pred pred)
        {
            if (!size_)
                return 0;
            size_type removed = 0;
            for (size_type i = 0; i <= mask_; ++i)
                if (meta_[i] && pred(static_cast<const K &>(entry(i).key), entry(i).value))
                {
                    destroy_at(i);
                    meta_[i] = 0;
                    ++removed;
                }
            if (removed)
            {
                size_ -= removed;
                CT_HASH_TOUCH();
                rehash(mask_ + 1);
            }
            return removed;
        }

    private:
        Slot *slots_;
        std::uint8_t *meta_;
        size_type mask_;
        size_type size_;
#if defined(CT_DEBUG_ITERATORS)
        size_type version_ = 0;
#endif

        // A meta de um elemento a distancia dist do seu slot de origem: ocupado (1), ou, para as chaves com distancia,
        // 0x81 + dist (0xFF quando dist >= 126: o bit alto e o de ocupado).
        static std::uint8_t meta_for(size_type dist) noexcept
        {
            return kDist ? static_cast<std::uint8_t>(dist >= 126 ? 255 : 0x81 + dist) : static_cast<std::uint8_t>(1);
        }

        Entry &entry(size_type i) const noexcept { return entry_ref(slots_[i], detail::integral_constant<bool, kIndirect>()); }
        static Entry &entry_ref(Entry &e, detail::false_type) noexcept { return e; }
        static Entry &entry_ref(Entry *e, detail::true_type) noexcept { return *e; }

        template <typename KK, typename VV>
        void construct_at(size_type i, KK &&k, VV &&v)
        {
            construct_slot(i, detail::integral_constant<bool, kIndirect>(), detail::forward<KK>(k), detail::forward<VV>(v));
        }
        template <typename KK, typename VV>
        void construct_slot(size_type i, detail::false_type, KK &&k, VV &&v)
        {
            ::new (static_cast<void *>(&slots_[i])) Entry{detail::forward<KK>(k), detail::forward<VV>(v)};
        }
        template <typename KK, typename VV>
        void construct_slot(size_type i, detail::true_type, KK &&k, VV &&v)
        {
            void *block = allocator().allocate(sizeof(Entry), alignof(Entry));
            ::new (block) Entry{detail::forward<KK>(k), detail::forward<VV>(v)};
            slots_[i] = static_cast<Entry *>(block);
        }

        void destroy_at(size_type i) { destroy_slot(i, detail::integral_constant<bool, kIndirect>()); }
        void destroy_slot(size_type i, detail::false_type) { slots_[i].~Entry(); }
        void destroy_slot(size_type i, detail::true_type)
        {
            slots_[i]->~Entry();
            allocator().deallocate(slots_[i], sizeof(Entry));
        }

        // Move o elemento do slot from para o slot to (que esta por construir) e deixa from por construir
        void move_slot(size_type to, size_type from) { move_slot_impl(to, from, detail::integral_constant<bool, kIndirect>()); }
        void move_slot_impl(size_type to, size_type from, detail::false_type)
        {
            ::new (static_cast<void *>(&slots_[to])) Entry{detail::move(slots_[from].key), detail::move(slots_[from].value)};
            slots_[from].~Entry();
        }
        void move_slot_impl(size_type to, size_type from, detail::true_type) { slots_[to] = slots_[from]; }

        // Pre-condicao: esta tabela esta vazia e o.size_ > 0. Quando o hasher e o da origem (a copia construida) e a
        // capacidade que fica e a dela (o caso de uma tabela que cresceu por dobragem), cada entrada volta ao mesmo
        // indice: nao ha hash nem sondagem, e a distribuicao das chaves, que ja e valida para esta mascara, fica igual.
        void copy_entries(const HashMap &o, bool same_hasher)
        {
            reserve(o.size_);
            if (same_hasher && mask_ == o.mask_)
            {
                const std::uint8_t *const src_meta = o.meta_;
                Slot *const src = o.slots_;
                Slot *const dst = slots_;
                const size_type last = mask_;
                for (size_type i = 0; i <= last; ++i)
                    if (src_meta[i])
                        copy_slot(dst, i, entry_ref(src[i], detail::integral_constant<bool, kIndirect>()),
                                  detail::integral_constant<bool, kIndirect>());
                std::memcpy(meta_, o.meta_, mask_ + 1);
                size_ = o.size_;
                CT_HASH_TOUCH();
                return;
            }
            copy_by_put(o);
        }
        CT_NOINLINE void copy_by_put(const HashMap &o)
        {
            for (size_type i = 0; i <= o.mask_; ++i)
                if (o.meta_[i])
                    put(o.entry(i).key, o.entry(i).value);
        }
        static void copy_slot(Slot *dst, size_type i, const Entry &e, detail::false_type) { ::new (static_cast<void *>(&dst[i])) Entry(e); }
        void copy_slot(Slot *dst, size_type i, const Entry &e, detail::true_type)
        {
            void *block = allocator().allocate(sizeof(Entry), alignof(Entry));
            ::new (block) Entry(e);
            dst[i] = static_cast<Entry *>(block);
        }

        template <typename KK, typename VV>
        CT_NOINLINE V &put_slow(KK &&k, VV &&v)
        {
            size_type dist;
            if (slots_ && size_ < mask_ + 1) /* at least one empty slot: probe terminates */
            {
                size_type i = probe(k, dist);
                if (meta_[i])
                {
                    entry(i).value = detail::forward<VV>(v);
                    return entry(i).value;
                }
            }
            K stable_key(detail::forward<KK>(k));
            V stable_value(detail::forward<VV>(v));
            grow();
            size_type i = probe(stable_key, dist);
            construct_at(i, detail::move(stable_key), detail::move(stable_value));
            meta_[i] = meta_for(dist);
            ++size_;
            CT_HASH_TOUCH();
            return entry(i).value;
        }

        CT_NOINLINE V &index_slow(const K &k)
        {
            size_type dist;
            if (slots_ && size_ < mask_ + 1) /* at least one empty slot: probe terminates */
            {
                size_type i = probe(k, dist);
                if (meta_[i])
                    return entry(i).value;
            }
            K stable_key(k);
            grow();
            size_type i = probe(stable_key, dist);
            construct_at(i, detail::move(stable_key), V());
            meta_[i] = meta_for(dist);
            ++size_;
            CT_HASH_TOUCH();
            return entry(i).value;
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
            /* A table must NEVER be 100% full: probe() stops only at the key
               or at an empty slot, so a full table and a missing key spin
               forever. For capacity 2 (reserve(1), e.g. a copy of a 1-entry
               map) `capacity - capacity / 4` IS the whole table, which let
               the second insert fill it and the third hang. */
            return size_ == (std::numeric_limits<size_type>::max)() ||
                   size_ + 1 >= capacity ||
                   size_ + 1 > capacity - capacity / 4;
        }

        template <typename Q>
        size_type probe_as(const Q &k) const
        {
            const H &hasher = *static_cast<const H *>(this);
            size_type i = static_cast<size_type>(hasher(k)) & mask_;
            if (kDist)
            {
                size_type d = 0x81;
                for (;;)
                {
                    const std::uint8_t m = meta_[i];
                    if (!m || ((m == d || m == 255) && hasher.equal(entry(i).key, k)))
                        return i;
                    i = (i + 1) & mask_;
                    ++d;
                }
            }
            while (meta_[i])
            {
                if (hasher.equal(entry(i).key, k))
                    return i;
                i = (i + 1) & mask_;
            }
            return i;
        }

        // O slot da chave, ou o primeiro livre da sondagem. As chaves sem distancia (inteiros, enumeracoes, ponteiros)
        // sondam como sempre, sem contador; probe(k, dist) devolve tambem a distancia desse slot ao de origem, que so
        // as chaves com distancia calculam (para a guardar na meta de quem se insere).
        size_type probe(const K &k) const
        {
            if (kDist)
            {
                size_type d;
                return probe_dist(k, d);
            }
            size_type i = static_cast<size_type>(hash_of(k)) & mask_;
            while (meta_[i])
            {
                if (entry(i).key == k)
                    return i;
                i = (i + 1) & mask_;
            }
            return i;
        }

        size_type probe(const K &k, size_type &dist) const
        {
            if (kDist)
                return probe_dist(k, dist);
            dist = 0;
            return probe(k);
        }

        size_type probe_dist(const K &k, size_type &dist) const
        {
            size_type i = static_cast<size_type>(hash_of(k)) & mask_;
            size_type d = 0x81;
            for (;;)
            {
                const std::uint8_t m = meta_[i];
                if (!m || ((m == d || m == 255) && entry(i).key == k))
                    break;
                i = (i + 1) & mask_;
                ++d;
            }
            dist = d - 0x81;
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
            CT_HASH_TOUCH();
            if (new_cap < 2 || !detail::is_power_of_two(new_cap))
                detail::fatal("ct::HashMap: capacidade invalida");
            Slot *old_slots = slots_;
            std::uint8_t *old_meta = meta_;
            size_type old_cap = slots_ ? mask_ + 1 : 0;

            size_type entries_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(new_cap, sizeof(Slot), entries_bytes) ||
                !detail::checked_add(entries_bytes, new_cap, bytes))
                detail::fatal("ct::HashMap: tamanho invalido");
            void *blk = allocator().allocate(bytes, alignof(Slot));
            slots_ = static_cast<Slot *>(blk);
            meta_ = reinterpret_cast<std::uint8_t *>(slots_ + new_cap);
            std::memset(meta_, 0, new_cap);
            mask_ = new_cap - 1;

            for (size_type i = 0; i < old_cap; ++i)
                if (old_meta[i])
                {
                    Entry &e = entry_ref(old_slots[i], detail::integral_constant<bool, kIndirect>());
                    size_type j = static_cast<size_type>(hash_of(e.key)) & mask_;
                    size_type d = 0;
                    while (meta_[j])
                    {
                        j = (j + 1) & mask_;
                        ++d;
                    }
                    relocate_into(j, old_slots[i], detail::integral_constant<bool, kIndirect>());
                    meta_[j] = meta_for(d);
                }
            if (old_slots)
            {
                size_type old_entries_bytes = 0;
                size_type old_bytes = 0;
                if (!detail::checked_mul(old_cap, sizeof(Slot), old_entries_bytes) ||
                    !detail::checked_add(old_entries_bytes, old_cap, old_bytes))
                    detail::fatal("ct::HashMap: tamanho invalido");
                allocator().deallocate(old_slots, old_bytes);
            }
        }
        // move o elemento de um slot da tabela antiga para o slot j da nova
        void relocate_into(size_type j, Entry &from, detail::false_type)
        {
            ::new (static_cast<void *>(&slots_[j])) Entry{detail::move(from.key), detail::move(from.value)};
            from.~Entry();
        }
        void relocate_into(size_type j, Entry *from, detail::true_type) { slots_[j] = from; }

        void destroy_all()
        {
            if (slots_ && size_)
                for (size_type i = 0; i <= mask_; ++i)
                    if (meta_[i])
                        destroy_at(i);
        }

        Alloc &allocator() { return static_cast<Alloc &>(*this); }

        void release_slots()
        {
            if (!slots_)
                return;
            size_type entries_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(mask_ + 1, sizeof(Slot), entries_bytes) ||
                !detail::checked_add(entries_bytes, mask_ + 1, bytes))
                detail::fatal("ct::HashMap: tamanho invalido");
            allocator().deallocate(slots_, bytes);
            slots_ = nullptr;
            meta_ = nullptr;
            mask_ = 0;
        }
    };
    CT_ABI_HASHINL_END
    CT_ABI_DBGITER_END

}
