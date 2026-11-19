
#pragma once

#include "detail/utils.hpp"
#include "vector.hpp"

namespace ct
{

    template <typename K, typename V, typename L = Less<K>>
    class FlatMap : private L
    {
    public:
        // O que os iteradores mostram: a chave e const (uma chave alterada por um iterador desordena o mapa). A memoria e a do
        // Item que o vector guarda e desloca (mesmo tamanho, alinhamento e posicao dos membros: static_assert em baixo); o
        // value de um `const Entry` e const por ser um membro directo.
        struct Entry
        {
            const K key;
            V value;
        };

        using size_type = std::size_t;
        using iterator = Entry *;
        using const_iterator = const Entry *;

        FlatMap() = default;
        explicit FlatMap(const L &less) : L(less) {}

        size_type size() const noexcept { return entries_.size(); }
        bool empty() const noexcept { return entries_.empty(); }

        V *find(const K &k) noexcept
        {
            size_type i = lower_bound(k);
            if (i < entries_.size() && equal(entries_[i].key, k))
                return &entries_[i].value;
            return nullptr;
        }
        const V *find(const K &k) const noexcept
        {
            return const_cast<FlatMap *>(this)->find(k);
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
            size_type i = lower_bound(k);
            if (i < entries_.size() && equal(entries_[i].key, k))
            {
                entries_[i].value = detail::forward<VV>(v);
                return entries_[i].value;
            }
            entries_.emplace(entries_.begin() + i,
                             Item{detail::forward<KK>(k), detail::forward<VV>(v)});
            return entries_[i].value;
        }

        V &operator[](const K &k)
        {
            size_type i = lower_bound(k);
            if (i < entries_.size() && equal(entries_[i].key, k))
                return entries_[i].value;
            entries_.emplace(entries_.begin() + i, Item{k, V()});
            return entries_[i].value;
        }

        bool erase(const K &k)
        {
            size_type i = lower_bound(k);
            if (i < entries_.size() && equal(entries_[i].key, k))
            {
                entries_.erase(entries_.begin() + i);
                return true;
            }
            return false;
        }

        void clear() { entries_.clear(); }
        void reserve(size_type n) { entries_.reserve(n); }

        iterator begin() noexcept { return show(entries_.begin()); }
        iterator end() noexcept { return show(entries_.end()); }
        const_iterator begin() const noexcept { return show(entries_.begin()); }
        const_iterator end() const noexcept { return show(entries_.end()); }

        iterator lower_bound_it(const K &k) noexcept
        {
            return show(entries_.begin() + lower_bound(k));
        }

    private:
        // O que o vector guarda e desloca (a chave nao e const para poder mover-se)
        struct Item
        {
            K key;
            V value;
        };
        static_assert(sizeof(Entry) == sizeof(Item) && alignof(Entry) == alignof(Item), "ct::FlatMap: a vista tem de ter o layout do item");
        static Entry *show(Item *p) noexcept { return reinterpret_cast<Entry *>(p); }
        static const Entry *show(const Item *p) noexcept { return reinterpret_cast<const Entry *>(p); }

        Vector<Item> entries_;

        bool less(const K &a, const K &b) const
        {
            return (*static_cast<const L *>(this))(a, b);
        }
        bool equal(const K &a, const K &b) const
        {
            return !less(a, b) && !less(b, a);
        }

        size_type lower_bound(const K &k) const
        {
            size_type lo = 0, n = entries_.size();
            while (n > 0)
            {
                size_type half = n / 2;
                if (less(entries_[lo + half].key, k))
                {
                    lo += half + 1;
                    n -= half + 1;
                }
                else
                {
                    n = half;
                }
            }
            return lo;
        }
    };

} 