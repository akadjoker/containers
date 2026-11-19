#pragma once

#include "detail/btree.hpp"

namespace ct
{

    // Mapa ordenado por uma B+ arvore (nos de NodeBytes bytes, 512 por omissao): a alternativa ao TreeMap quando o que
    // importa e a velocidade e a memoria com muitos elementos (menos saltos de ponteiro, cerca de 10 bytes por elemento de
    // `int`->`int` contra 41 do TreeMap).
    //
    // A API e a do TreeMap (put, find, get, contains, operator[], erase, clear, begin/end, lower_bound_it, validate) mais
    // lower_bound, upper_bound, first/last, erase_range, erase_from, erase(first, last) e assign_sorted.
    //
    // GARANTIAS MAIS FRACAS que as do TreeMap (que promete ponteiros e iteradores estaveis): inserir e apagar invalidam
    // todos os iteradores e todos os ponteiros e referencias para chaves e valores (os itens movem-se entre nos). So
    // percorrer, procurar e as pesquisas de intervalo sao seguras sem mudar o mapa. Em compilacoes de depuracao
    // (CT_DEBUG_ITERATORS=1, explicito: nunca por omissao nem por causa de NDEBUG) usar um iterador depois de o mapa mudar aborta.
    // Uma referencia ao valor devolvida por put e operator[] vale ate a proxima insercao ou remocao.
    // K tem de ser copiavel (as chaves separadoras dos nos internos sao copias).
    // As chaves sao unicas; `put` de uma chave que existe substitui o valor.
    CT_ABI_DBGITER_BEGIN
    template <typename K, typename V, typename L = Less<K>, size_t NodeBytes = 512, typename Alloc = HeapAlloc,
              typename Probe = detail::BTreeNoProbe>
    class BTreeMap : public detail::BTreeCore<detail::BTreeMapTraits<K, V>, K, L, NodeBytes, Alloc, Probe>
    {
        typedef detail::BTreeCore<detail::BTreeMapTraits<K, V>, K, L, NodeBytes, Alloc, Probe> Core;
        typedef typename Core::Loc Loc;

    public:
        typedef typename Core::Item Entry; // { K key; V value; }
        typedef typename Core::size_type size_type;
        typedef typename Core::raw_iterator iterator;
        typedef typename Core::raw_const_iterator const_iterator;

        BTreeMap() {}
        explicit BTreeMap(const L &less) : Core(less) {}
        BTreeMap(const L &less, const Alloc &alloc) : Core(less, alloc) {}

        using Core::erase;

        V *find(const K &k) noexcept
        {
            Entry *e = this->find_item(k);
            return e ? &e->value : nullptr;
        }
        const V *find(const K &k) const noexcept
        {
            const Entry *e = this->find_item(k);
            return e ? &e->value : nullptr;
        }

        bool contains(const K &k) const noexcept { return this->find_item(k) != nullptr; }

        const V &get(const K &k, const V &fallback) const noexcept
        {
            const Entry *e = this->find_item(k);
            return e ? e->value : fallback;
        }

        // Substitui o valor se a chave existe. A referencia devolvida vale ate a proxima insercao ou remocao.
        template <typename KK, typename VV>
        V &put(KK &&k, VV &&v)
        {
            const K &kk = k;
            Loc loc;
            this->locate(kk, loc);
            if (loc.found)
            {
                Entry *e = this->located_item(loc);
                e->value = detail::forward<VV>(v);
                return e->value;
            }
            // o item e construido antes de mexer na arvore: k e v podem apontar para dentro dela
            Entry tmp{detail::forward<KK>(k), detail::forward<VV>(v)};
            return this->insert_located(loc, tmp)->value;
        }

        V &operator[](const K &k)
        {
            Loc loc;
            this->locate(k, loc);
            if (loc.found)
                return this->located_item(loc)->value;
            Entry tmp{k, V()};
            return this->insert_located(loc, tmp)->value;
        }

        iterator begin() noexcept { return this->iter_begin(); }
        iterator end() noexcept { return this->iter_end(); }
        const_iterator begin() const noexcept { return this->iter_begin(); }
        const_iterator end() const noexcept { return this->iter_end(); }
        const_iterator cbegin() const noexcept { return this->iter_begin(); }
        const_iterator cend() const noexcept { return this->iter_end(); }

        // O primeiro item com chave >= k (end() se nao ha), e o primeiro com chave > k
        iterator lower_bound(const K &k) noexcept { return this->iter_lower(k); }
        const_iterator lower_bound(const K &k) const noexcept { return this->iter_lower(k); }
        iterator upper_bound(const K &k) noexcept { return this->iter_upper(k); }
        const_iterator upper_bound(const K &k) const noexcept { return this->iter_upper(k); }
        iterator lower_bound_it(const K &k) noexcept { return this->iter_lower(k); } // como no TreeMap

        // O item de menor e o de maior chave (nullptr se vazio)
        Entry *first() noexcept { return this->first_item(); }
        const Entry *first() const noexcept { return this->first_item(); }
        Entry *last() noexcept { return this->last_item(); }
        const Entry *last() const noexcept { return this->last_item(); }

        // Apaga os itens de first (inclusive) a last (exclusive) e devolve quantos; ambos tem de ser iteradores de agora.
        // Invalida todos os iteradores.
        size_type erase(const_iterator first, const_iterator last)
        {
            if (first == last)
                return 0;
            const K lo(first->key);
            if (last == this->iter_end())
                return this->erase_from(lo);
            const K hi(last->key);
            return this->erase_range(lo, hi);
        }

        // Substitui o conteudo por [first, last), um intervalo estritamente crescente de elementos com .key e .value (por
        // exemplo outro BTreeMap, ou um Vector de Entry): O(n), com os nos cheios. Se nao for estritamente crescente o
        // resultado e o de um put por elemento (a ultima ocorrencia de uma chave ganha).
        template <typename It>
        void assign_sorted(It first, It last)
        {
            if (this->build_sorted(first, last))
                return;
            this->clear();
            for (; first != last; ++first)
                put(first->key, first->value);
        }
    };
    CT_ABI_DBGITER_END

} // namespace ct
