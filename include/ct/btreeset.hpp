#pragma once

#include "detail/btree.hpp"

namespace ct
{

    // Conjunto ordenado por uma B+ arvore: o BTreeMap sem valores. As garantias sao as do BTreeMap (ver btreemap.hpp):
    // inserir e apagar invalidam todos os iteradores e referencias; percorrer e pesquisar nao. K tem de ser copiavel.
    CT_ABI_DBGITER_BEGIN
    template <typename K, typename L = Less<K>, size_t NodeBytes = 512, typename Alloc = HeapAlloc,
              typename Probe = detail::BTreeNoProbe>
    class BTreeSet : public detail::BTreeCore<detail::BTreeSetTraits<K>, K, L, NodeBytes, Alloc, Probe>
    {
        typedef detail::BTreeCore<detail::BTreeSetTraits<K>, K, L, NodeBytes, Alloc, Probe> Core;
        typedef typename Core::Loc Loc;

    public:
        typedef K value_type;
        typedef typename Core::size_type size_type;
        typedef typename Core::raw_const_iterator iterator;
        typedef typename Core::raw_const_iterator const_iterator;

        BTreeSet() {}
        explicit BTreeSet(const L &less) : Core(less) {}
        BTreeSet(const L &less, const Alloc &alloc) : Core(less, alloc) {}

        using Core::erase;

        bool contains(const K &k) const noexcept { return this->find_item(k) != nullptr; }

        // true se a chave nao existia
        template <typename KK>
        bool insert(KK &&k)
        {
            const K &kk = k;
            Loc loc;
            this->locate(kk, loc);
            if (loc.found)
                return false;
            K tmp(detail::forward<KK>(k));
            this->insert_located(loc, tmp);
            return true;
        }

        const_iterator begin() const noexcept { return this->iter_begin(); }
        const_iterator end() const noexcept { return this->iter_end(); }
        const_iterator cbegin() const noexcept { return this->iter_begin(); }
        const_iterator cend() const noexcept { return this->iter_end(); }

        const_iterator lower_bound(const K &k) const noexcept { return this->iter_lower(k); }
        const_iterator upper_bound(const K &k) const noexcept { return this->iter_upper(k); }
        const_iterator lower_bound_it(const K &k) const noexcept { return this->iter_lower(k); }

        const K *first() const noexcept { return this->first_item(); }
        const K *last() const noexcept { return this->last_item(); }

        size_type erase(const_iterator first, const_iterator last)
        {
            if (first == last)
                return 0;
            const K lo(*first);
            if (last == this->iter_end())
                return this->erase_from(lo);
            const K hi(*last);
            return this->erase_range(lo, hi);
        }

        // Substitui o conteudo por [first, last) estritamente crescente: O(n), nos cheios; senao, um insert por elemento
        template <typename It>
        void assign_sorted(It first, It last)
        {
            if (this->build_sorted(first, last))
                return;
            this->clear();
            for (; first != last; ++first)
                insert(*first);
        }
    };
    CT_ABI_DBGITER_END

} // namespace ct
