#pragma once

// Nucleo de BTreeMap e BTreeSet: uma B+ arvore (os itens so nas folhas, as folhas ligadas para a frente) com nos de tamanho
// fixo tirados de um slab. Sem `std::` de proposito: ficheiro novo, e o tools/check.sh nostd nao deixa juntar usos de std.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "slab.hpp"
#include "utils.hpp"

namespace ct
{
    namespace detail
    {
        // Os acontecimentos que um teste conta para mostrar que cada caminho de rebalanceamento foi percorrido.
        enum BTreeEvent
        {
            kBtLeafShiftLeft,
            kBtLeafShiftRight,
            kBtLeafSplit,
            kBtLeafSplitAppend,
            kBtInternalSplit,
            kBtRootGrow,
            kBtLeafMergeLeft,
            kBtLeafMergeRight,
            kBtLeafBorrowLeft,
            kBtLeafBorrowRight,
            kBtInternalMergeLeft,
            kBtInternalMergeRight,
            kBtInternalBorrowLeft,
            kBtInternalBorrowRight,
            kBtRootShrink,
            kBtRootEmpty,
            kBtEventCount
        };

        // Sonda por omissao: nao faz nada (o compilador apaga as chamadas). Um teste passa a sua, que conta.
        struct BTreeNoProbe
        {
            static void hit(int) noexcept {}
        };

        template <bool B, typename A, typename C>
        struct bt_pick
        {
            typedef A type;
        };
        template <typename A, typename C>
        struct bt_pick<false, A, C>
        {
            typedef C type;
        };

        constexpr size_t bt_max(size_t a, size_t b) { return a > b ? a : b; }
        constexpr size_t bt_round_up(size_t x, size_t a) { return (x + a - 1) / a * a; }
        // bytes de um no interno com n chaves: cabecalho, n chaves, n + 1 filhos
        constexpr size_t bt_internal_bytes(size_t hdr, size_t ksz, size_t n)
        {
            return bt_round_up(hdr + n * ksz, sizeof(void *)) + (n + 1) * sizeof(void *);
        }
        constexpr size_t bt_calc_ic(size_t hdr, size_t ksz, size_t bytes)
        {
            size_t n = 0;
            while (bt_internal_bytes(hdr, ksz, n + 1) <= bytes)
                ++n;
            return n;
        }

        // Geometria dos nos: o mesmo tamanho para folhas e internos (um so slab). Pelo menos 8 itens por folha e 8 chaves por
        // no interno (a altura fica abaixo de 32), por isso itens muito grandes alargam o no.
        template <typename Item, typename K, size_t NodeBytes>
        struct BTreeGeom
        {
            static constexpr size_t kAlign = bt_max(bt_max(alignof(Item), alignof(K)), alignof(void *));
            static constexpr size_t kHdr = bt_round_up(sizeof(void *) + sizeof(uint32_t), kAlign);
            static constexpr size_t kBytes =
                bt_round_up(bt_max(bt_max(NodeBytes, kHdr + 8 * sizeof(Item)), bt_internal_bytes(kHdr, sizeof(K), 8)), kAlign);
            static constexpr size_t kLeafCap = (kBytes - kHdr) / sizeof(Item);
            static constexpr size_t kIntCap = bt_calc_ic(kHdr, sizeof(K), kBytes);
            static constexpr size_t kKidsOff = bt_round_up(kHdr + kIntCap * sizeof(K), sizeof(void *));
        };

        // ---- deslocar e mover elementos de um vetor (so os de dentro de um no; sem sobreposicao nos moves "para o lado") ----
        template <typename T>
        inline void bt_reloc(T *dst, T *src)
        {
            ::new (static_cast<void *>(dst)) T(detail::move(*src));
            src->~T();
        }
        // move-constroi dst a partir de src e so destroi src se nao for o temporario do chamador
        template <typename T>
        inline void bt_take(T *dst, T *src, T *keep)
        {
            ::new (static_cast<void *>(dst)) T(detail::move(*src));
            if (src != keep)
                src->~T();
        }
        // [from, cnt) sobe `by` posicoes (os slots de destino estao por construir; os de origem ficam por construir)
        template <typename T>
        inline void bt_up(T *a, unsigned from, unsigned cnt, unsigned by, true_type)
        {
            if (cnt > from)
                memmove(static_cast<void *>(a + from + by), static_cast<const void *>(a + from), (cnt - from) * sizeof(T));
        }
        template <typename T>
        inline void bt_up(T *a, unsigned from, unsigned cnt, unsigned by, false_type)
        {
            for (unsigned i = cnt; i > from; --i)
                bt_reloc(a + i - 1 + by, a + i - 1);
        }
        template <typename T>
        inline void bt_up(T *a, unsigned from, unsigned cnt, unsigned by)
        {
            bt_up(a, from, cnt, by, is_trivially_copyable_t<T>());
        }
        // [from, cnt) desce `by` posicoes (os slots de destino estao por construir)
        template <typename T>
        inline void bt_down(T *a, unsigned from, unsigned cnt, unsigned by, true_type)
        {
            if (cnt > from)
                memmove(static_cast<void *>(a + from - by), static_cast<const void *>(a + from), (cnt - from) * sizeof(T));
        }
        template <typename T>
        inline void bt_down(T *a, unsigned from, unsigned cnt, unsigned by, false_type)
        {
            for (unsigned i = from; i < cnt; ++i)
                bt_reloc(a + i - by, a + i);
        }
        template <typename T>
        inline void bt_down(T *a, unsigned from, unsigned cnt, unsigned by)
        {
            bt_down(a, from, cnt, by, is_trivially_copyable_t<T>());
        }
        // move n elementos entre vetores distintos
        template <typename T>
        inline void bt_move_n(T *dst, T *src, unsigned n, true_type)
        {
            if (n)
                memcpy(static_cast<void *>(dst), static_cast<const void *>(src), n * sizeof(T));
        }
        template <typename T>
        inline void bt_move_n(T *dst, T *src, unsigned n, false_type)
        {
            for (unsigned i = 0; i < n; ++i)
                bt_reloc(dst + i, src + i);
        }
        template <typename T>
        inline void bt_move_n(T *dst, T *src, unsigned n)
        {
            bt_move_n(dst, src, n, is_trivially_copyable_t<T>());
        }

        // Os itens de um BTreeMap: chave e valor lado a lado (o iterador da uma Entry como o do TreeMap).
        template <typename K, typename V>
        struct BTreeMapTraits
        {
            struct Item
            {
                K key;
                V value;
            };
            static const K &key(const Item &i) { return i.key; }
            template <typename S>
            static const K &src_key(const S &s) { return s.key; }
            template <typename S>
            static void construct(void *dst, const S &s) { ::new (dst) Item{s.key, s.value}; }
        };
        // Os itens de um BTreeSet: so a chave
        template <typename K>
        struct BTreeSetTraits
        {
            typedef K Item;
            static const K &key(const Item &i) { return i; }
            template <typename S>
            static const K &src_key(const S &s) { return s; }
            template <typename S>
            static void construct(void *dst, const S &s) { ::new (dst) Item(s); }
        };

        // B+ arvore: so as folhas guardam itens; os nos internos guardam copias das chaves separadoras (a primeira chave da
        // folha da direita no momento da divisao) e os filhos; as folhas ligam-se para a frente. Garantias (mais fracas que
        // as do TreeMap): inserir e apagar invalidam iteradores, ponteiros e referencias para itens (os itens movem-se
        // entre nos), por isso so percorrer, procurar e as pesquisas de intervalo sao seguras sem mudar a arvore.
        template <typename Traits, typename K, typename L, size_t NodeBytes, typename Alloc, typename Probe>
        class BTreeCore : private L, private Alloc
        {
        public:
            typedef typename Traits::Item Item;
            typedef size_t size_type;

        protected:
            typedef BTreeGeom<Item, K, NodeBytes> G;
            static constexpr unsigned kLeafCap = static_cast<unsigned>(G::kLeafCap);
            static constexpr unsigned kIntCap = static_cast<unsigned>(G::kIntCap);
            static constexpr unsigned kLeafMin = kLeafCap / 2;
            static constexpr unsigned kIntMin = kIntCap / 2;
            static constexpr unsigned kMaxH = 32;
            static_assert(kLeafCap >= 8 && kIntCap >= 8, "ct::BTree: no pequeno demais");

            struct Node
            {
                Node *next; // so nas folhas
                uint32_t count;
            };
            struct alignas(G::kAlign) Block
            {
                unsigned char b[G::kBytes];
            };

            static Item *items(Node *n) noexcept { return reinterpret_cast<Item *>(reinterpret_cast<unsigned char *>(n) + G::kHdr); }
            static K *ikeys(Node *n) noexcept { return reinterpret_cast<K *>(reinterpret_cast<unsigned char *>(n) + G::kHdr); }
            static Node **kids(Node *n) noexcept { return reinterpret_cast<Node **>(reinterpret_cast<unsigned char *>(n) + G::kKidsOff); }

            struct Path
            {
                Node *node[kMaxH];
                unsigned idx[kMaxH];
                unsigned depth;
            };
            struct Loc
            {
                Path p;
                Node *leaf;
                unsigned pos;
                bool found;
            };

        public:
            // ---- iteradores (so para a frente; invalidados por inserir e apagar) ----
            template <bool Const>
            class Iter
            {
                friend class BTreeCore;
                typedef typename bt_pick<Const, const Item, Item>::type Ref;
                Node *leaf_;
                unsigned idx_;
#if defined(CT_DEBUG_ITERATORS)
                const size_t *version_;
                size_t seen_;
                void check() const
                {
                    if (CT_UNLIKELY(*version_ != seen_))
                        fatal("ct::BTreeMap/BTreeSet: iterador usado depois de a arvore mudar (inserir ou apagar invalida os "
                              "iteradores; para apagar varios use erase_range)");
                }
#endif
            public:
                Iter() noexcept : leaf_(nullptr), idx_(0)
#if defined(CT_DEBUG_ITERATORS)
                                  , version_(nullptr), seen_(0)
#endif
                {
                }
#if defined(CT_DEBUG_ITERATORS)
                Iter(Node *l, unsigned i, const size_t *v) noexcept : leaf_(l), idx_(i), version_(v), seen_(*v) {}
#else
                Iter(Node *l, unsigned i) noexcept : leaf_(l), idx_(i) {}
#endif
                template <bool C2, typename = typename enable_if<Const && !C2>::type>
                Iter(const Iter<C2> &o) noexcept : leaf_(o.leaf_), idx_(o.idx_)
#if defined(CT_DEBUG_ITERATORS)
                                                  , version_(o.version_), seen_(o.seen_)
#endif
                {
                }
                Ref &operator*() const
                {
#if defined(CT_DEBUG_ITERATORS)
                    check();
#endif
                    return items(leaf_)[idx_];
                }
                Ref *operator->() const
                {
#if defined(CT_DEBUG_ITERATORS)
                    check();
#endif
                    return items(leaf_) + idx_;
                }
                Iter &operator++()
                {
#if defined(CT_DEBUG_ITERATORS)
                    check();
#endif
                    if (++idx_ >= leaf_->count)
                    {
                        leaf_ = leaf_->next;
                        idx_ = 0;
                    }
                    return *this;
                }
                bool operator==(const Iter &o) const noexcept { return leaf_ == o.leaf_ && idx_ == o.idx_; }
                bool operator!=(const Iter &o) const noexcept { return !(*this == o); }
                template <bool C2>
                friend class Iter;
            };
            typedef Iter<false> raw_iterator;
            typedef Iter<true> raw_const_iterator;

            BTreeCore() : L(), Alloc(), slab_(), root_(nullptr), head_(nullptr), size_(0), height_(0) {}
            explicit BTreeCore(const L &less) : L(less), Alloc(), slab_(), root_(nullptr), head_(nullptr), size_(0), height_(0) {}
            BTreeCore(const L &less, const Alloc &alloc)
                : L(less), Alloc(alloc), slab_(), root_(nullptr), head_(nullptr), size_(0), height_(0)
            {
            }

            ~BTreeCore() { release(); }

            // A copia mantem a forma da arvore (e o seu enchimento)
            BTreeCore(const BTreeCore &o)
                : L(static_cast<const L &>(o)), Alloc(static_cast<const Alloc &>(o)), slab_(), root_(nullptr), head_(nullptr),
                  size_(0), height_(0)
            {
                copy_from(o);
            }

            BTreeCore(BTreeCore &&o) noexcept
                : L(static_cast<L &&>(o)), Alloc(static_cast<Alloc &&>(o)), slab_(static_cast<BlockSlab<Block> &&>(o.slab_)),
                  root_(o.root_), head_(o.head_), size_(o.size_), height_(o.height_)
            {
                o.root_ = o.head_ = nullptr;
                o.size_ = 0;
                o.height_ = 0;
#if defined(CT_DEBUG_ITERATORS)
                version_ = o.version_;
                ++o.version_;
#endif
            }

            BTreeCore &operator=(const BTreeCore &o)
            {
                if (this != &o)
                {
                    release();
                    static_cast<L &>(*this) = static_cast<const L &>(o);
                    copy_from(o);
                }
                return *this;
            }

            BTreeCore &operator=(BTreeCore &&o) noexcept
            {
                if (this != &o)
                {
                    release();
                    static_cast<L &>(*this) = static_cast<L &&>(o);
                    static_cast<Alloc &>(*this) = static_cast<Alloc &&>(o);
                    slab_.adopt(o.slab_);
                    root_ = o.root_;
                    head_ = o.head_;
                    size_ = o.size_;
                    height_ = o.height_;
                    o.root_ = o.head_ = nullptr;
                    o.size_ = 0;
                    o.height_ = 0;
                    touch();
                    o.touch();
                }
                return *this;
            }

            size_type size() const noexcept { return size_; }
            bool empty() const noexcept { return size_ == 0; }

            // Capacidades por no (para testes e para quem quer saber o enchimento)
            static size_type leaf_capacity() noexcept { return kLeafCap; }
            static size_type internal_capacity() noexcept { return kIntCap; }
            static size_type node_bytes() noexcept { return G::kBytes; }
            // Niveis acima das folhas (0 com a raiz a ser uma folha)
            unsigned height() const noexcept { return height_; }

            void clear()
            {
                release();
            }

            void swap(BTreeCore &o) noexcept
            {
                BTreeCore tmp(static_cast<BTreeCore &&>(o));
                o = static_cast<BTreeCore &&>(*this);
                *this = static_cast<BTreeCore &&>(tmp);
            }

            raw_iterator iter_begin() noexcept { return make_iter<false>(head_, 0); }
            raw_iterator iter_end() noexcept { return make_iter<false>(nullptr, 0); }
            raw_const_iterator iter_begin() const noexcept { return make_iter<true>(head_, 0); }
            raw_const_iterator iter_end() const noexcept { return make_iter<true>(nullptr, 0); }

            // O primeiro item com chave >= k, e o primeiro com chave > k
            raw_const_iterator iter_lower(const K &k) const noexcept { return bound_iter<true>(k, false); }
            raw_const_iterator iter_upper(const K &k) const noexcept { return bound_iter<true>(k, true); }
            raw_iterator iter_lower(const K &k) noexcept { return bound_iter<false>(k, false); }
            raw_iterator iter_upper(const K &k) noexcept { return bound_iter<false>(k, true); }

            // O primeiro e o ultimo item (nullptr se vazio)
            Item *first_item() const noexcept { return head_ ? items(head_) : nullptr; }
            Item *last_item() const noexcept
            {
                if (!root_)
                    return nullptr;
                Node *n = root_;
                for (unsigned h = height_; h; --h)
                    n = kids(n)[n->count];
                return items(n) + (n->count - 1);
            }

            Item *find_item(const K &k) const noexcept
            {
                Node *n = root_;
                if (!n)
                    return nullptr;
                for (unsigned h = height_; h; --h)
                    n = kids(n)[node_upper(ikeys(n), n->count, k)];
                const unsigned c = n->count;
                Item *it = items(n);
                const unsigned p = leaf_lower(it, c, k);
                return (p < c && !less(k, Traits::key(it[p]))) ? it + p : nullptr;
            }

            bool erase(const K &k)
            {
                Loc loc;
                locate(k, loc);
                if (!loc.found)
                    return false;
                erase_located(loc, loc.pos, 1);
                return true;
            }

            // Apaga as chaves em [lo, hi); devolve quantas. (A versao com iteradores e erase_from/erase_range do BTreeMap.)
            size_type erase_range(const K &lo, const K &hi)
            {
                return erase_between(lo, &hi);
            }
            // Apaga as chaves >= lo
            size_type erase_from(const K &lo)
            {
                return erase_between(lo, nullptr);
            }

            // Verifica todos os invariantes: ordem, contagens minimas e maximas, altura igual, separadores, ligacao das folhas
            bool validate() const
            {
                if (!root_)
                    return size_ == 0 && head_ == nullptr && height_ == 0;
                if (head_ == nullptr)
                    return false;
                size_type total = 0;
                Node *prev_leaf = nullptr;
                if (!check_node(root_, height_, nullptr, nullptr, total, prev_leaf, true))
                    return false;
                if (total != size_)
                    return false;
                // a cadeia de folhas visita todos os itens por ordem
                size_type n = 0;
                const Item *prev = nullptr;
                for (Node *lf = head_; lf; lf = lf->next)
                {
                    if (lf->count == 0)
                        return false;
                    for (unsigned i = 0; i < lf->count; ++i)
                    {
                        const Item *cur = items(lf) + i;
                        if (prev && !less(Traits::key(*prev), Traits::key(*cur)))
                            return false;
                        prev = cur;
                        ++n;
                    }
                }
                return n == size_;
            }

        protected:
            // ---- a localizacao de uma chave (com o caminho, para inserir e apagar) ----
            void locate(const K &k, Loc &loc) const noexcept
            {
                Node *n = root_;
                loc.p.depth = 0;
                if (!n)
                {
                    loc.leaf = nullptr;
                    loc.pos = 0;
                    loc.found = false;
                    return;
                }
                unsigned d = 0;
                for (unsigned h = height_; h; --h)
                {
                    const unsigned i = node_upper(ikeys(n), n->count, k);
                    loc.p.node[d] = n;
                    loc.p.idx[d] = i;
                    ++d;
                    n = kids(n)[i];
                }
                loc.p.depth = d;
                loc.leaf = n;
                const unsigned c = n->count;
                Item *it = items(n);
                const unsigned p = leaf_lower(it, c, k);
                loc.pos = p;
                loc.found = p < c && !less(k, Traits::key(it[p]));
            }

            Item *located_item(const Loc &loc) const noexcept { return items(loc.leaf) + loc.pos; }

            // Insere tmp (a chave nao existe) na posicao loc e devolve o endereco do item. tmp so e movido para dentro
            // (o chamador destroi-o), por isso uma referencia para dentro da arvore que lhe deu origem nao se perde.
            Item *insert_located(Loc &loc, Item &tmp)
            {
                touch();
                Node *lf = loc.leaf;
                if (!lf)
                {
                    lf = new_node();
                    root_ = head_ = lf;
                    height_ = 0;
                    ::new (static_cast<void *>(items(lf))) Item(detail::move(tmp));
                    lf->count = 1;
                    size_ = 1;
                    return items(lf);
                }
                ++size_;
                const unsigned pos = loc.pos;
                const unsigned cnt = lf->count;
                Item *it = items(lf);
                if (cnt < kLeafCap)
                {
                    bt_up(it, pos, cnt, 1);
                    ::new (static_cast<void *>(it + pos)) Item(detail::move(tmp));
                    lf->count = cnt + 1;
                    return it + pos;
                }
                return insert_full_leaf(loc, tmp);
            }

            // a bound: devolve o iterador do primeiro item >= k (upper: > k)
            template <bool Const>
            Iter<Const> bound_iter(const K &k, bool upper) const noexcept
            {
                Node *n = root_;
                if (!n)
                    return make_iter<Const>(nullptr, 0);
                for (unsigned h = height_; h; --h)
                    n = kids(n)[node_upper(ikeys(n), n->count, k)];
                const unsigned c = n->count;
                Item *it = items(n);
                unsigned p = upper ? leaf_upper(it, c, k) : leaf_lower(it, c, k);
                if (p >= c)
                {
                    n = n->next;
                    p = 0;
                }
                return make_iter<Const>(n, n ? p : 0);
            }

            template <bool Const>
            Iter<Const> make_iter(Node *l, unsigned i) const noexcept
            {
#if defined(CT_DEBUG_ITERATORS)
                return Iter<Const>(l, i, &version_);
#else
                return Iter<Const>(l, i);
#endif
            }

            // Constroi a arvore a partir de um intervalo estritamente crescente (pass 1 verifica; devolve false sem mexer na
            // arvore se nao for). As folhas ficam cheias; os niveis de cima enchem-se tambem.
            template <typename It>
            bool build_sorted(It first, It last)
            {
                size_type n = 0;
                if (first != last)
                {
                    It a = first;
                    It b = a;
                    n = 1;
                    for (++b; b != last; ++a, ++b, ++n)
                        if (!less(Traits::src_key(*a), Traits::src_key(*b)))
                            return false;
                }
                release();
                if (n == 0)
                    return true;
                touch();
                const size_type nl = leaf_count_for(n, kLeafCap, kLeafMin);
                const size_type base = n / nl, extra = n % nl;
                Node **cur = static_cast<Node **>(allocator().allocate(nl * sizeof(Node *), alignof(Node *)));
                Node *prev = nullptr;
                It src = first;
                for (size_type j = 0; j < nl; ++j)
                {
                    Node *lf = new_node();
                    const unsigned c = static_cast<unsigned>(base + (j < extra ? 1 : 0));
                    Item *it = items(lf);
                    for (unsigned i = 0; i < c; ++i, ++src)
                        Traits::construct(static_cast<void *>(it + i), *src);
                    lf->count = c;
                    if (prev)
                        prev->next = lf;
                    prev = lf;
                    cur[j] = lf;
                }
                head_ = cur[0];
                size_ = n;
                size_type count = nl;
                unsigned level = 0;
                while (count > 1)
                {
                    const size_type np = leaf_count_for(count, kIntCap + 1, kIntMin + 1);
                    const size_type cb = count / np, ce = count % np;
                    size_type from = 0;
                    for (size_type j = 0; j < np; ++j)
                    {
                        const unsigned cc = static_cast<unsigned>(cb + (j < ce ? 1 : 0)); // filhos deste no
                        Node *in = new_node();
                        Node **kd = kids(in);
                        for (unsigned i = 0; i < cc; ++i)
                            kd[i] = cur[from + i];
                        K *kk = ikeys(in);
                        for (unsigned i = 1; i < cc; ++i)
                            ::new (static_cast<void *>(kk + i - 1)) K(Traits::key(*leftmost_item(kd[i], level)));
                        in->count = cc - 1;
                        cur[j] = in;
                        from += cc;
                    }
                    count = np;
                    ++level;
                }
                root_ = cur[0];
                height_ = level;
                allocator().deallocate(cur, nl * sizeof(Node *));
                return true;
            }

            Alloc &allocator() noexcept { return static_cast<Alloc &>(*this); }

            bool less(const K &a, const K &b) const { return (*static_cast<const L *>(this))(a, b); }

            void touch() noexcept
            {
#if defined(CT_DEBUG_ITERATORS)
                ++version_;
#endif
            }

        private:
            BlockSlab<Block> slab_;
            Node *root_;
            Node *head_;
            size_type size_;
            unsigned height_;
#if defined(CT_DEBUG_ITERATORS)
            size_t version_ = 0;
#endif

            static size_type leaf_count_for(size_type n, size_type cap, size_type min)
            {
                if (n <= cap)
                    return 1;
                size_type lo = (n + cap - 1) / cap; // pelo menos isto para caber
                size_type hi = n / min;             // no maximo isto para cada um ter o minimo
                return lo <= hi ? lo : hi;          // lo <= hi sempre que min <= cap / 2 + 1
            }

            Node *new_node()
            {
                Node *n = reinterpret_cast<Node *>(slab_.take(allocator()));
                n->next = nullptr;
                n->count = 0;
                return n;
            }
            void free_node(Node *n) { slab_.give(allocator(), reinterpret_cast<Block *>(n)); }

            const Item *leftmost_item(Node *n, unsigned level) const noexcept
            {
                for (unsigned h = level; h; --h)
                    n = kids(n)[0];
                return items(n);
            }

            // ---- pesquisa dentro de um no: binaria sem saltos (o resultado de cada passo escolhe o inicio com um cmov) ----
            // Medida contra a contagem linear (quantas chaves sao menores, sem dependencias entre iteracoes) e contra a
            // pesquisa linear com saida antecipada da btree do abseil: 1e6 pesquisas de `int` com 300000 chaves: 386, 650 e 371
            // instrucoes e 1,6, 5,0 e 5,2 mispredicoes de salto por pesquisa. Mais comparacoes numa pesquisa linear, e cada
            // comparacao de texto custa; fica a binaria para todas as chaves.
            unsigned leaf_lower(const Item *it, unsigned n, const K &k) const
            {
                unsigned base = 0;
                while (n > 1)
                {
                    const unsigned half = n >> 1;
                    base = less(Traits::key(it[base + half - 1]), k) ? base + half : base;
                    n -= half;
                }
                return base + ((n == 1 && less(Traits::key(it[base]), k)) ? 1u : 0u);
            }
            unsigned leaf_upper(const Item *it, unsigned n, const K &k) const
            {
                unsigned base = 0;
                while (n > 1)
                {
                    const unsigned half = n >> 1;
                    base = !less(k, Traits::key(it[base + half - 1])) ? base + half : base;
                    n -= half;
                }
                return base + ((n == 1 && !less(k, Traits::key(it[base]))) ? 1u : 0u);
            }
            // o indice do filho: quantas chaves separadoras sao <= k
            unsigned node_upper(const K *ks, unsigned n, const K &k) const
            {
                unsigned base = 0;
                while (n > 1)
                {
                    const unsigned half = n >> 1;
                    base = !less(k, ks[base + half - 1]) ? base + half : base;
                    n -= half;
                }
                return base + ((n == 1 && !less(k, ks[base])) ? 1u : 0u);
            }

            // ---- inserir numa folha cheia ----
            // O item novo, na posicao pos, de uma sequencia virtual T de cnt + 1 itens (os cnt da folha com tmp no meio)
            static Item *vsrc(Item *it, unsigned pos, Item *nw, unsigned j) noexcept
            {
                return j < pos ? it + j : (j == pos ? nw : it + (j - 1));
            }

            void set_sep(Node *p, unsigned i, const K &src)
            {
                K *dst = ikeys(p) + i;
                dst->~K();
                ::new (static_cast<void *>(dst)) K(src);
            }

            Item *insert_full_leaf(Loc &loc, Item &tmp)
            {
                Node *lf = loc.leaf;
                const unsigned pos = loc.pos;
                const unsigned cnt = lf->count; // == kLeafCap
                const unsigned pd = loc.p.depth;
                // acrescentar no fim da ultima folha (inserir por ordem crescente): so o novo vai para a folha nova e as outras
                // ficam cheias
                if (!(lf->next == nullptr && pos == cnt) && pd)
                {
                    Node *parent = loc.p.node[pd - 1];
                    const unsigned ci = loc.p.idx[pd - 1];
                    if (ci > 0)
                    {
                        Node *s = kids(parent)[ci - 1];
                        if (s->count < kLeafCap)
                            return shift_left(lf, s, parent, ci, pos, tmp);
                    }
                    if (ci < parent->count)
                    {
                        Node *r = kids(parent)[ci + 1];
                        if (r->count < kLeafCap)
                            return shift_right(lf, r, parent, ci, pos, tmp);
                    }
                }
                return split_leaf(loc, tmp);
            }

            // A folha lf (cheia) passa a s (a esquerda, com lugar) os primeiros m itens da sequencia T
            Item *shift_left(Node *lf, Node *s, Node *parent, unsigned ci, unsigned pos, Item &tmp)
            {
                Probe::hit(kBtLeafShiftLeft);
                const unsigned cnt = lf->count;
                const unsigned sc = s->count;
                const unsigned m = (kLeafCap - sc + 1) / 2;
                Item *it = items(lf);
                Item *sit = items(s);
                for (unsigned j = 0; j < m; ++j)
                    bt_take(sit + sc + j, vsrc(it, pos, &tmp, j), &tmp);
                for (unsigned j = m; j <= cnt; ++j)
                {
                    Item *d = it + (j - m);
                    Item *sp = vsrc(it, pos, &tmp, j);
                    if (sp != d)
                        bt_take(d, sp, &tmp);
                }
                s->count = sc + m;
                lf->count = cnt + 1 - m;
                set_sep(parent, ci - 1, Traits::key(it[0]));
                return pos < m ? sit + sc + pos : it + (pos - m);
            }

            // A folha lf (cheia) passa a r (a direita, com lugar) os ultimos m itens da sequencia T
            Item *shift_right(Node *lf, Node *r, Node *parent, unsigned ci, unsigned pos, Item &tmp)
            {
                Probe::hit(kBtLeafShiftRight);
                const unsigned cnt = lf->count;
                const unsigned rc = r->count;
                const unsigned m = (kLeafCap - rc + 1) / 2;
                const unsigned keep = cnt + 1 - m; // quantos ficam em lf
                Item *it = items(lf);
                Item *rit = items(r);
                bt_up(rit, 0, rc, m);
                for (unsigned t = keep; t <= cnt; ++t)
                    bt_take(rit + (t - keep), vsrc(it, pos, &tmp, t), &tmp);
                r->count = rc + m;
                if (pos < keep)
                {
                    for (unsigned j = keep - 1; j > pos; --j)
                        bt_reloc(it + j, it + (j - 1));
                    bt_take(it + pos, &tmp, &tmp);
                }
                lf->count = keep;
                set_sep(parent, ci, Traits::key(rit[0]));
                return pos < keep ? it + pos : rit + (pos - keep);
            }

            Item *split_leaf(Loc &loc, Item &tmp)
            {
                Node *lf = loc.leaf;
                const unsigned pos = loc.pos;
                const unsigned cnt = lf->count;
                const bool append = lf->next == nullptr && pos == cnt;
                Probe::hit(append ? kBtLeafSplitAppend : kBtLeafSplit);
                Node *nn = new_node();
                Item *it = items(lf);
                Item *nit = items(nn);
                const unsigned k = append ? cnt : (cnt + 2) / 2; // quantos ficam em lf
                for (unsigned t = k; t <= cnt; ++t)
                    bt_take(nit + (t - k), vsrc(it, pos, &tmp, t), &tmp);
                nn->count = cnt + 1 - k;
                if (pos < k)
                {
                    for (unsigned j = k - 1; j > pos; --j)
                        bt_reloc(it + j, it + (j - 1));
                    bt_take(it + pos, &tmp, &tmp);
                }
                lf->count = k;
                nn->next = lf->next;
                lf->next = nn;
                Item *res = pos < k ? it + pos : nit + (pos - k);
                // a chave separadora (uma copia) sobe; o endereco do item novo nao muda com isso (so mexe em nos internos)
                alignas(K) unsigned char sbuf[sizeof(K)];
                K *sep = ::new (static_cast<void *>(sbuf)) K(Traits::key(nit[0]));
                add_child(loc, loc.p.depth, *sep, nn);
                sep->~K();
                return res;
            }

            // Mete no pai (o no loc.p.node[pd - 1]) a chave sep e o filho right a direita do filho por onde se desceu; se
            // pd == 0 o no que se dividiu e a raiz. sep so e movida para dentro (o chamador destroi-a).
            void add_child(Loc &loc, unsigned pd, K &sep, Node *right)
            {
                if (pd == 0)
                {
                    Probe::hit(kBtRootGrow);
                    Node *r = new_node();
                    ::new (static_cast<void *>(ikeys(r))) K(detail::move(sep));
                    kids(r)[0] = root_;
                    kids(r)[1] = right;
                    r->count = 1;
                    root_ = r;
                    ++height_;
                    return;
                }
                Node *p = loc.p.node[pd - 1];
                const unsigned i = loc.p.idx[pd - 1];
                const unsigned c = p->count;
                K *pk = ikeys(p);
                Node **pc = kids(p);
                if (c < kIntCap)
                {
                    bt_up(pk, i, c, 1);
                    ::new (static_cast<void *>(pk + i)) K(detail::move(sep));
                    bt_up(pc, i + 1, c + 1, 1);
                    pc[i + 1] = right;
                    p->count = c + 1;
                    return;
                }
                Probe::hit(kBtInternalSplit);
                // T: chaves virtuais j < i: pk[j], j == i: sep, j > i: pk[j-1] (c + 1 chaves); filhos virtuais j <= i: pc[j],
                // j == i+1: right, j > i+1: pc[j-1] (c + 2 filhos). A chave mid sobe; as de depois vao para o no novo.
                const unsigned mid = (c + 1) / 2;
                const unsigned nkeys = c - mid;
                Node *nn = new_node();
                K *nk = ikeys(nn);
                Node **nc = kids(nn);
                alignas(K) unsigned char ubuf[sizeof(K)];
                K *up = reinterpret_cast<K *>(ubuf);
                bt_take(up, ksrc(pk, i, &sep, mid), &sep);
                for (unsigned x = 0; x < nkeys; ++x)
                    bt_take(nk + x, ksrc(pk, i, &sep, mid + 1 + x), &sep);
                for (unsigned x = 0; x <= nkeys; ++x)
                    nc[x] = csrc(pc, i, right, mid + 1 + x);
                if (i < mid)
                {
                    for (unsigned j = mid - 1; j > i; --j)
                        bt_reloc(pk + j, pk + (j - 1));
                    bt_take(pk + i, &sep, &sep);
                    for (unsigned j = mid; j >= i + 2; --j)
                        pc[j] = pc[j - 1];
                    pc[i + 1] = right;
                }
                p->count = mid;
                nn->count = nkeys;
                add_child(loc, pd - 1, *up, nn);
                up->~K();
            }
            static K *ksrc(K *pk, unsigned i, K *sep, unsigned j) noexcept
            {
                return j < i ? pk + j : (j == i ? sep : pk + (j - 1));
            }
            static Node *csrc(Node **pc, unsigned i, Node *right, unsigned j) noexcept
            {
                return j <= i ? pc[j] : (j == i + 1 ? right : pc[j - 1]);
            }

            // ---- apagar ----
            void erase_located(Loc &loc, unsigned pos, unsigned m)
            {
                touch();
                Node *lf = loc.leaf;
                Item *it = items(lf);
                const unsigned cnt = lf->count;
                destroy_n(it + pos, m);
                bt_down(it, pos + m, cnt, m);
                lf->count = cnt - m;
                size_ -= m;
                if (lf == root_)
                {
                    if (lf->count == 0)
                    {
                        Probe::hit(kBtRootEmpty);
                        free_node(lf);
                        root_ = head_ = nullptr;
                        height_ = 0;
                    }
                    return;
                }
                if (lf->count >= kLeafMin)
                    return;
                rebalance_leaf(loc);
            }

            void rebalance_leaf(Loc &loc)
            {
                const unsigned pd = loc.p.depth;
                Node *p = loc.p.node[pd - 1];
                const unsigned i = loc.p.idx[pd - 1];
                Node *lf = loc.leaf;
                Node *s = i > 0 ? kids(p)[i - 1] : nullptr;
                Node *r = i < p->count ? kids(p)[i + 1] : nullptr;
                const unsigned lc = lf->count;
                Item *it = items(lf);
                if (s && s->count + lc <= kLeafCap)
                {
                    Probe::hit(kBtLeafMergeLeft);
                    bt_move_n(items(s) + s->count, it, lc);
                    s->count += lc;
                    s->next = lf->next;
                    free_node(lf);
                    remove_child(p, i - 1);
                    fix_internal(loc, pd - 1);
                }
                else if (r && lc + r->count <= kLeafCap)
                {
                    Probe::hit(kBtLeafMergeRight);
                    bt_move_n(it + lc, items(r), r->count);
                    lf->count = lc + r->count;
                    lf->next = r->next;
                    free_node(r);
                    remove_child(p, i);
                    fix_internal(loc, pd - 1);
                }
                else if (s)
                {
                    Probe::hit(kBtLeafBorrowLeft);
                    const unsigned m = (s->count - lc) / 2;
                    Item *sit = items(s);
                    bt_up(it, 0, lc, m);
                    bt_move_n(it, sit + (s->count - m), m);
                    s->count -= m;
                    lf->count = lc + m;
                    set_sep(p, i - 1, Traits::key(it[0]));
                }
                else
                {
                    Probe::hit(kBtLeafBorrowRight);
                    const unsigned m = (r->count - lc) / 2;
                    Item *rit = items(r);
                    bt_move_n(it + lc, rit, m);
                    bt_down(rit, m, r->count, m);
                    r->count -= m;
                    lf->count = lc + m;
                    set_sep(p, i, Traits::key(rit[0]));
                }
            }

            // tira do no p a chave ki e o filho ki + 1
            void remove_child(Node *p, unsigned ki)
            {
                const unsigned c = p->count;
                K *pk = ikeys(p);
                pk[ki].~K();
                bt_down(pk, ki + 1, c, 1);
                bt_down(kids(p), ki + 2, c + 1, 1);
                p->count = c - 1;
            }

            // O no loc.p.node[lvl] perdeu uma chave: raiz que fica sem chaves, ou no que ficou abaixo do minimo
            void fix_internal(Loc &loc, unsigned lvl)
            {
                Node *x = loc.p.node[lvl];
                if (lvl == 0)
                {
                    if (x->count == 0)
                    {
                        Probe::hit(kBtRootShrink);
                        root_ = kids(x)[0];
                        free_node(x);
                        --height_;
                    }
                    return;
                }
                if (x->count >= kIntMin)
                    return;
                Node *p = loc.p.node[lvl - 1];
                const unsigned i = loc.p.idx[lvl - 1];
                Node *s = i > 0 ? kids(p)[i - 1] : nullptr;
                Node *r = i < p->count ? kids(p)[i + 1] : nullptr;
                const unsigned xc = x->count;
                K *xk = ikeys(x);
                Node **xc_ = kids(x);
                if (s && s->count + xc + 1 <= kIntCap)
                {
                    Probe::hit(kBtInternalMergeLeft);
                    const unsigned sc = s->count;
                    K *sk = ikeys(s);
                    Node **sc_ = kids(s);
                    bt_reloc(sk + sc, ikeys(p) + (i - 1));
                    bt_move_n(sk + sc + 1, xk, xc);
                    for (unsigned j = 0; j <= xc; ++j)
                        sc_[sc + 1 + j] = xc_[j];
                    s->count = sc + 1 + xc;
                    free_node(x);
                    // a chave i-1 de p ja foi movida para s: tirar so o filho (sem destruir a chave)
                    remove_child_moved(p, i - 1);
                    fix_internal(loc, lvl - 1);
                }
                else if (r && r->count + xc + 1 <= kIntCap)
                {
                    Probe::hit(kBtInternalMergeRight);
                    const unsigned rc = r->count;
                    K *rk = ikeys(r);
                    Node **rc_ = kids(r);
                    bt_reloc(xk + xc, ikeys(p) + i);
                    bt_move_n(xk + xc + 1, rk, rc);
                    for (unsigned j = 0; j <= rc; ++j)
                        xc_[xc + 1 + j] = rc_[j];
                    x->count = xc + 1 + rc;
                    free_node(r);
                    remove_child_moved(p, i);
                    fix_internal(loc, lvl - 1);
                }
                else if (s)
                {
                    Probe::hit(kBtInternalBorrowLeft);
                    const unsigned sc = s->count;
                    const unsigned m = (sc - xc) / 2;
                    K *sk = ikeys(s);
                    Node **sc_ = kids(s);
                    bt_up(xk, 0, xc, m);
                    bt_up(xc_, 0, xc + 1, m);
                    bt_reloc(xk + (m - 1), ikeys(p) + (i - 1));
                    bt_move_n(xk, sk + (sc - m + 1), m - 1);
                    for (unsigned j = 0; j < m; ++j)
                        xc_[j] = sc_[sc - m + 1 + j];
                    bt_reloc(ikeys(p) + (i - 1), sk + (sc - m));
                    s->count = sc - m;
                    x->count = xc + m;
                }
                else
                {
                    Probe::hit(kBtInternalBorrowRight);
                    const unsigned rc = r->count;
                    const unsigned m = (rc - xc) / 2;
                    K *rk = ikeys(r);
                    Node **rc_ = kids(r);
                    bt_reloc(xk + xc, ikeys(p) + i);
                    bt_move_n(xk + xc + 1, rk, m - 1);
                    for (unsigned j = 0; j < m; ++j)
                        xc_[xc + 1 + j] = rc_[j];
                    bt_reloc(ikeys(p) + i, rk + (m - 1));
                    bt_down(rk, m, rc, m);
                    bt_down(rc_, m, rc + 1, m);
                    r->count = rc - m;
                    x->count = xc + m;
                }
            }
            // como remove_child, mas a chave ki ja foi movida para outro lado (o slot esta por construir)
            void remove_child_moved(Node *p, unsigned ki)
            {
                const unsigned c = p->count;
                bt_down(ikeys(p), ki + 1, c, 1);
                bt_down(kids(p), ki + 2, c + 1, 1);
                p->count = c - 1;
            }

            size_type erase_between(const K &lo, const K *hi)
            {
                size_type removed = 0;
                for (;;)
                {
                    Loc loc;
                    locate(lo, loc);
                    if (!loc.leaf)
                        break;
                    if (loc.pos >= loc.leaf->count)
                    {
                        // todos os itens desta folha sao < lo: os candidatos comecam na folha seguinte (procura-se pela
                        // sua primeira chave, copiada: o no vai mudar)
                        Node *nx = loc.leaf->next;
                        if (!nx)
                            break;
                        K first(Traits::key(items(nx)[0]));
                        if (hi && !less(first, *hi))
                            break;
                        locate(first, loc);
                        if (!loc.found)
                            break;
                    }
                    Node *lf = loc.leaf;
                    const unsigned pos = loc.pos;
                    unsigned e = lf->count;
                    if (hi)
                        e = pos + leaf_lower(items(lf) + pos, lf->count - pos, *hi);
                    if (e == pos)
                        break;
                    const bool last_run = hi && e < lf->count; // o item seguinte desta folha ja e >= hi
                    erase_located(loc, pos, e - pos);
                    removed += e - pos;
                    if (last_run)
                        break;
                }
                return removed;
            }

            // ---- destruir ----
            void release()
            {
                if (root_)
                    destroy_subtree(root_, height_);
                slab_.release_all(allocator());
                root_ = head_ = nullptr;
                size_ = 0;
                height_ = 0;
                touch();
            }

            void destroy_subtree(Node *n, unsigned level)
            {
                if (is_trivially_destructible_t<Item>::value && is_trivially_destructible_t<K>::value)
                    return;
                if (level == 0)
                {
                    destroy_n(items(n), n->count);
                    return;
                }
                destroy_n(ikeys(n), n->count);
                Node **kd = kids(n);
                for (unsigned i = 0; i <= n->count; ++i)
                    destroy_subtree(kd[i], level - 1);
            }

            void copy_from(const BTreeCore &o)
            {
                if (!o.root_)
                    return;
                Node *prev = nullptr;
                root_ = clone(o.root_, o.height_, prev);
                head_ = first_leaf(root_, o.height_);
                size_ = o.size_;
                height_ = o.height_;
                touch();
            }
            Node *first_leaf(Node *n, unsigned level) const noexcept
            {
                for (; level; --level)
                    n = kids(n)[0];
                return n;
            }
            Node *clone(Node *src, unsigned level, Node *&prev_leaf)
            {
                Node *n = new_node();
                if (level == 0)
                {
                    Item *d = items(n);
                    const Item *s = items(src);
                    for (unsigned i = 0; i < src->count; ++i)
                        ::new (static_cast<void *>(d + i)) Item(s[i]);
                    n->count = src->count;
                    if (prev_leaf)
                        prev_leaf->next = n;
                    prev_leaf = n;
                    return n;
                }
                const K *sk = ikeys(src);
                K *dk = ikeys(n);
                for (unsigned i = 0; i < src->count; ++i)
                    ::new (static_cast<void *>(dk + i)) K(sk[i]);
                n->count = src->count;
                Node **sd = kids(src);
                Node **dd = kids(n);
                for (unsigned i = 0; i <= src->count; ++i)
                    dd[i] = clone(sd[i], level - 1, prev_leaf);
                return n;
            }

            // ---- invariantes (validate) ----
            // lo, hi: os limites que as chaves desta subarvore tem de respeitar (lo <= chave < hi; nullptr = sem limite).
            // Uma folha que nao e a raiz tem pelo menos kLeafMin itens, excepto a ultima (acrescentar por ordem crescente
            // deixa as folhas cheias e so a ultima com poucos itens).
            bool check_node(Node *n, unsigned level, const K *lo, const K *hi, size_type &total, Node *&prev_leaf, bool is_root) const
            {
                const unsigned c = n->count;
                if (level == 0)
                {
                    if (c > kLeafCap || (!is_root && n->next != nullptr && c < kLeafMin) || c == 0)
                        return false;
                    const Item *it = items(n);
                    for (unsigned i = 0; i < c; ++i)
                    {
                        const K &k = Traits::key(it[i]);
                        if (i && !less(Traits::key(it[i - 1]), k))
                            return false;
                        if (lo && less(k, *lo))
                            return false;
                        if (hi && !less(k, *hi))
                            return false;
                    }
                    if (prev_leaf && prev_leaf->next != n)
                        return false;
                    prev_leaf = n;
                    total += c;
                    return true;
                }
                if (c > kIntCap || (!is_root && c < kIntMin) || (is_root && c < 1))
                    return false;
                const K *ks = ikeys(n);
                Node *const *kd = kids(n);
                for (unsigned i = 0; i < c; ++i)
                {
                    if (i && !less(ks[i - 1], ks[i]))
                        return false;
                    if (lo && less(ks[i], *lo))
                        return false;
                    if (hi && !less(ks[i], *hi))
                        return false;
                }
                for (unsigned i = 0; i <= c; ++i)
                {
                    const K *clo = i ? ks + (i - 1) : lo;
                    const K *chi = i < c ? ks + i : hi;
                    if (!check_node(kd[i], level - 1, clo, chi, total, prev_leaf, false))
                        return false;
                }
                return true;
            }
        };

        template <typename Traits, typename K, typename L, size_t NodeBytes, typename Alloc, typename Probe>
        constexpr unsigned BTreeCore<Traits, K, L, NodeBytes, Alloc, Probe>::kLeafCap;
        template <typename Traits, typename K, typename L, size_t NodeBytes, typename Alloc, typename Probe>
        constexpr unsigned BTreeCore<Traits, K, L, NodeBytes, Alloc, Probe>::kIntCap;
        template <typename Traits, typename K, typename L, size_t NodeBytes, typename Alloc, typename Probe>
        constexpr unsigned BTreeCore<Traits, K, L, NodeBytes, Alloc, Probe>::kLeafMin;
        template <typename Traits, typename K, typename L, size_t NodeBytes, typename Alloc, typename Probe>
        constexpr unsigned BTreeCore<Traits, K, L, NodeBytes, Alloc, Probe>::kIntMin;
        template <typename Traits, typename K, typename L, size_t NodeBytes, typename Alloc, typename Probe>
        constexpr unsigned BTreeCore<Traits, K, L, NodeBytes, Alloc, Probe>::kMaxH;
    } // namespace detail
} // namespace ct
