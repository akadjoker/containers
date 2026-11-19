// BTreeMap e BTreeSet: API, diferencial contra o TreeMap (1e6 operacoes, sementes fixas, com contadores que provam que
// cada caminho de dividir, emprestar e fundir foi percorrido), casos limite de tempo de vida, alinhamento, autoreferencia.
// Este ficheiro nao usa a biblioteca standard (so o gtest e cabecalhos C).
#include <ct/btreemap.hpp>
#include <ct/btreeset.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Lcg
    {
        uint64_t s;
        explicit Lcg(uint64_t seed) : s(seed) {}
        uint32_t next()
        {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<uint32_t>(s >> 33);
        }
    };

    // Conta cada acontecimento de rebalanceamento (a sonda e um parametro do BTreeMap, sem custo quando e a omissao)
    struct CovProbe
    {
        static uint64_t n[ct::detail::kBtEventCount];
        static void hit(int e) { ++n[e]; }
        static void reset() { memset(n, 0, sizeof n); }
    };
    uint64_t CovProbe::n[ct::detail::kBtEventCount];

    const char *const kEventNames[] = {"LeafShiftLeft",  "LeafShiftRight",  "LeafSplit",     "LeafSplitAppend",     "InternalSplit",
                                       "RootGrow",       "LeafMergeLeft",   "LeafMergeRight", "LeafBorrowLeft",     "LeafBorrowRight",
                                       "InternalMergeLeft", "InternalMergeRight", "InternalBorrowLeft", "InternalBorrowRight",
                                       "RootShrink",     "RootEmpty"};

    void expect_every_event_hit()
    {
        for (int e = 0; e < ct::detail::kBtEventCount; ++e)
        {
            printf("  btree %-20s %llu\n", kEventNames[e], static_cast<unsigned long long>(CovProbe::n[e]));
            EXPECT_GT(CovProbe::n[e], 0u) << "caminho nunca percorrido: " << kEventNames[e];
        }
    }

    // nos minimos: 8 itens por folha e 8 chaves por no interno, a arvore fica alta com poucas chaves
    typedef ct::BTreeMap<int, int, ct::Less<int>, 64, ct::HeapAlloc, CovProbe> TinyMap;

    int g_live = 0;
    struct Counted
    {
        int v;
        Counted() : v(0) { ++g_live; }
        explicit Counted(int x) : v(x) { ++g_live; }
        Counted(const Counted &o) : v(o.v) { ++g_live; }
        Counted(Counted &&o) noexcept : v(o.v)
        {
            o.v = -1;
            ++g_live;
        }
        Counted &operator=(const Counted &o)
        {
            v = o.v;
            return *this;
        }
        Counted &operator=(Counted &&o) noexcept
        {
            v = o.v;
            o.v = -1;
            return *this;
        }
        ~Counted() { --g_live; }
        bool operator<(const Counted &o) const { return v < o.v; }
    };

    struct MoveOnly
    {
        int v;
        explicit MoveOnly(int x = 0) : v(x) { ++g_live; }
        MoveOnly(MoveOnly &&o) noexcept : v(o.v)
        {
            o.v = -1;
            ++g_live;
        }
        MoveOnly &operator=(MoveOnly &&o) noexcept
        {
            v = o.v;
            o.v = -1;
            return *this;
        }
        MoveOnly(const MoveOnly &) = delete;
        MoveOnly &operator=(const MoveOnly &) = delete;
        ~MoveOnly() { --g_live; }
    };

    struct alignas(64) Aligned64
    {
        int v;
        Aligned64() : v(0) {}
        explicit Aligned64(int x) : v(x) {}
    };
    struct alignas(32) Aligned32
    {
        long long v[2];
        Aligned32() { v[0] = v[1] = 0; }
    };
    struct Huge
    {
        char bytes[4096];
        Huge() { memset(bytes, 0, sizeof(bytes)); }
        explicit Huge(char c) { memset(bytes, c, sizeof(bytes)); }
    };

    bool aligned(const void *p, size_t a) { return (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0; }

    template <typename M, typename T>
    bool same_contents(M &b, T &t)
    {
        typename M::iterator it = b.begin();
        for (typename T::iterator e = t.begin(); e != t.end(); ++e, ++it)
        {
            if (it == b.end() || it->key != e->key || it->value != e->value)
                return false;
        }
        return it == b.end() && b.size() == t.size();
    }

    // conta bytes pedidos e vivos
    struct CountingAlloc
    {
        static size_t live_bytes;
        static size_t calls;
        void *allocate(size_t bytes, size_t align)
        {
            live_bytes += bytes;
            ++calls;
            void *p = nullptr;
            if (align <= 16)
                p = malloc(bytes ? bytes : 1);
            else if (posix_memalign(&p, align, bytes) != 0)
                p = nullptr;
            return p;
        }
        void deallocate(void *p, size_t bytes)
        {
            live_bytes -= bytes;
            free(p);
        }
        void *reallocate(void *p, size_t old_bytes, size_t new_bytes, size_t align)
        {
            void *q = allocate(new_bytes, align);
            memcpy(q, p, old_bytes < new_bytes ? old_bytes : new_bytes);
            deallocate(p, old_bytes);
            return q;
        }
    };
    size_t CountingAlloc::live_bytes = 0;
    size_t CountingAlloc::calls = 0;
}

TEST(BTreeMapBasic, EmptyAndSingleElement)
{
    ct::BTreeMap<int, int> m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.size(), 0u);
    EXPECT_TRUE(m.begin() == m.end());
    EXPECT_EQ(m.find(1), nullptr);
    EXPECT_FALSE(m.contains(1));
    EXPECT_EQ(m.get(1, 77), 77);
    EXPECT_FALSE(m.erase(1));
    EXPECT_TRUE(m.lower_bound(0) == m.end());
    EXPECT_TRUE(m.upper_bound(0) == m.end());
    EXPECT_EQ(m.first(), nullptr);
    EXPECT_EQ(m.last(), nullptr);
    EXPECT_EQ(m.erase_range(0, 100), 0u);
    EXPECT_EQ(m.erase_from(0), 0u);
    EXPECT_TRUE(m.validate());
    m.clear();
    m.put(5, 50);
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(*m.find(5), 50);
    EXPECT_EQ(m.first()->key, 5);
    EXPECT_EQ(m.last()->value, 50);
    EXPECT_TRUE(m.lower_bound(5) != m.end());
    EXPECT_TRUE(m.upper_bound(5) == m.end());
    EXPECT_TRUE(m.lower_bound(6) == m.end());
    EXPECT_TRUE(m.validate());
    EXPECT_TRUE(m.erase(5));
    EXPECT_TRUE(m.empty());
    EXPECT_TRUE(m.validate());
    m.put(1, 1);
    EXPECT_EQ(m.size(), 1u);
}

TEST(BTreeMapBasic, PutReplacesGetOperatorBracket)
{
    ct::BTreeMap<int, int> m;
    for (int i = 0; i < 1000; ++i)
        m.put((i * 7919) % 1000, i);
    EXPECT_EQ(m.size(), 1000u);
    m.put(10, -1);
    EXPECT_EQ(m.size(), 1000u);
    EXPECT_EQ(*m.find(10), -1);
    EXPECT_EQ(m.get(10, 5), -1);
    EXPECT_EQ(m.get(5000, 5), 5);
    m[2000] = 3;
    m[2000] += 4;
    EXPECT_EQ(m[2000], 7);
    EXPECT_EQ(m[3000], 0);
    EXPECT_EQ(m.size(), 1002u);
    const ct::BTreeMap<int, int> &cm = m;
    EXPECT_EQ(*cm.find(2000), 7);
    EXPECT_TRUE(cm.contains(3000));
    EXPECT_TRUE(m.validate());
}

TEST(BTreeMapBasic, IterationIsOrderedAndBoundsAreExact)
{
    ct::BTreeMap<int, int> m;
    for (int i = 0; i < 2000; ++i)
        m.put(((i * 37) % 2000) * 2, i); // chaves pares 0..3998
    int prev = -2;
    size_t n = 0;
    for (auto &e : m)
    {
        ASSERT_GT(e.key, prev);
        prev = e.key;
        ++n;
    }
    EXPECT_EQ(n, 2000u);
    for (int q = -3; q < 4003; ++q)
    {
        auto lo = m.lower_bound(q);
        auto up = m.upper_bound(q);
        const int want_lo = q <= 0 ? 0 : (q + 1) / 2 * 2;
        if (want_lo > 3998)
            ASSERT_TRUE(lo == m.end()) << q;
        else
        {
            ASSERT_TRUE(lo != m.end()) << q;
            ASSERT_EQ(lo->key, want_lo) << q;
        }
        const int want_up = q < 0 ? 0 : (q / 2 + 1) * 2;
        if (want_up > 3998)
            ASSERT_TRUE(up == m.end()) << q;
        else
        {
            ASSERT_TRUE(up != m.end()) << q;
            ASSERT_EQ(up->key, want_up) << q;
        }
    }
    EXPECT_EQ(m.first()->key, 0);
    EXPECT_EQ(m.last()->key, 3998);
    // o iterador percorre de lower_bound ate ao fim sem saltar nem repetir
    auto it = m.lower_bound(1000);
    int expect = 1000;
    for (; it != m.end(); ++it, expect += 2)
        ASSERT_EQ(it->key, expect);
    EXPECT_EQ(expect, 4000);
}

TEST(BTreeMapBasic, CopyMoveAssignSwap)
{
    ct::BTreeMap<int, ct::String> a;
    for (int i = 0; i < 500; ++i)
        a.put(i, ct::String("a string long enough to leave the small buffer, number ") + ct::String::number(i));
    ct::BTreeMap<int, ct::String> b(a);
    ASSERT_TRUE(b.validate());
    ASSERT_EQ(b.size(), 500u);
    b.put(7, ct::String("changed"));
    EXPECT_FALSE(*a.find(7) == ct::String("changed"));
    ct::BTreeMap<int, ct::String> c;
    c.put(1, ct::String("x"));
    c = a;
    ASSERT_EQ(c.size(), 500u);
    ASSERT_TRUE(c.validate());
    ct::BTreeMap<int, ct::String> d(static_cast<ct::BTreeMap<int, ct::String> &&>(c));
    EXPECT_TRUE(c.empty());
    EXPECT_TRUE(c.validate());
    ASSERT_EQ(d.size(), 500u);
    c.put(3, ct::String("reuse"));
    EXPECT_EQ(c.size(), 1u);
    ct::BTreeMap<int, ct::String> e;
    e = static_cast<ct::BTreeMap<int, ct::String> &&>(d);
    EXPECT_TRUE(d.empty());
    ASSERT_EQ(e.size(), 500u);
    e.swap(c);
    EXPECT_EQ(e.size(), 1u);
    EXPECT_EQ(c.size(), 500u);
    EXPECT_TRUE(e.validate());
    EXPECT_TRUE(c.validate());
    for (int i = 0; i < 500; ++i)
        ASSERT_TRUE(a.find(i)->size() > 30u);
}

TEST(BTreeMapRange, EraseRangeEraseFromAndIteratorRange)
{
    for (int variant = 0; variant < 3; ++variant)
    {
        TinyMap m;
        ct::TreeMap<int, int> t;
        for (int i = 0; i < 600; ++i)
        {
            m.put(i * 3, i);
            t.put(i * 3, i);
        }
        Lcg rng(100 + static_cast<uint64_t>(variant));
        for (int round = 0; round < 60; ++round)
        {
            int lo = static_cast<int>(rng.next() % 1900) - 20;
            int hi = lo + static_cast<int>(rng.next() % 200);
            size_t removed = 0;
            if (variant == 0)
                removed = m.erase_range(lo, hi);
            else if (variant == 1)
                removed = m.erase(m.lower_bound(lo), m.lower_bound(hi));
            else
            {
                removed = round % 5 == 0 ? m.erase_from(lo) : m.erase_range(lo, hi);
                if (round % 5 == 0)
                    hi = 1 << 30;
            }
            size_t want = 0;
            for (int k = lo < 0 ? 0 : lo; k < hi && k <= 2000; ++k)
                if (t.find(k))
                {
                    t.erase(k);
                    ++want;
                }
            ASSERT_EQ(removed, want) << round;
            ASSERT_TRUE(m.validate()) << round;
            ASSERT_TRUE(same_contents(m, t)) << round;
            if (round % 7 == 6)
                for (int i = 0; i < 40; ++i)
                {
                    m.put(i * 3 + 1, i);
                    t.put(i * 3 + 1, i);
                }
        }
        // apagar tudo
        EXPECT_EQ(m.erase(m.begin(), m.end()), t.size());
        EXPECT_TRUE(m.empty());
        EXPECT_TRUE(m.validate());
    }
}

TEST(BTreeMapBulk, AssignSortedPacksNodesAndMatches)
{
    ct::Vector<ct::BTreeMap<int, int>::Entry> in;
    for (int n : {0, 1, 2, 7, 29, 30, 31, 60, 61, 62, 63, 400, 1000, 12345})
    {
        in.clear();
        for (int i = 0; i < n; ++i)
            in.push_back({i * 2, i});
        ct::BTreeMap<int, int> m;
        m.put(-1, -1); // o conteudo de antes desaparece
        m.assign_sorted(in.begin(), in.end());
        ASSERT_EQ(m.size(), static_cast<size_t>(n)) << n;
        ASSERT_TRUE(m.validate()) << n;
        int k = 0;
        for (auto &e : m)
        {
            ASSERT_EQ(e.key, k * 2);
            ASSERT_EQ(e.value, k);
            ++k;
        }
        ASSERT_EQ(k, n);
        // e continua a aceitar inserir e apagar
        m.put(1, 1);
        m.erase(0);
        ASSERT_TRUE(m.validate()) << n;
        // a partir de outro mapa
        ct::BTreeMap<int, int> c;
        c.assign_sorted(m.begin(), m.end());
        ASSERT_EQ(c.size(), m.size());
        ASSERT_TRUE(c.validate());
    }
    // so as folhas cheias: 62 itens por folha com nos de 512 bytes (para `int`), 100 folhas
    ct::BTreeMap<int, int> full;
    in.clear();
    for (int i = 0; i < 62 * 100; ++i)
        in.push_back({i, i});
    full.assign_sorted(in.begin(), in.end());
    EXPECT_EQ(full.leaf_capacity(), 62u);
    EXPECT_EQ(full.node_bytes(), 512u);
    EXPECT_TRUE(full.validate());
}

TEST(BTreeMapBulk, UnsortedOrRepeatedInputFallsBackToPut)
{
    ct::Vector<ct::BTreeMap<int, int>::Entry> in;
    ct::TreeMap<int, int> t;
    Lcg rng(9);
    for (int i = 0; i < 3000; ++i)
    {
        int k = static_cast<int>(rng.next() % 500);
        in.push_back({k, i});
        t.put(k, i); // a ultima ocorrencia ganha
    }
    ct::BTreeMap<int, int> m;
    m.put(-5, 1);
    m.assign_sorted(in.begin(), in.end());
    EXPECT_TRUE(m.validate());
    EXPECT_TRUE(same_contents(m, t));
    // descendente e com um repetido no fim
    in.clear();
    for (int i = 100; i > 0; --i)
        in.push_back({i, i});
    ct::BTreeMap<int, int> d;
    d.assign_sorted(in.begin(), in.end());
    EXPECT_EQ(d.size(), 100u);
    EXPECT_TRUE(d.validate());
    in.clear();
    for (int i = 0; i < 50; ++i)
        in.push_back({i, i});
    in.push_back({49, 999});
    d.assign_sorted(in.begin(), in.end());
    EXPECT_EQ(d.size(), 50u);
    EXPECT_EQ(*d.find(49), 999);
}

// O diferencial longo: 1e6 operacoes contra o TreeMap, em fases que enchem e esvaziam a arvore (para percorrer cada caminho
// de dividir, emprestar e fundir), com os nos minimos para a arvore ficar alta com poucas chaves.
TEST(BTreeMapDifferential, MillionOperationsAgainstTreeMapHitEveryRebalancePath)
{
    CovProbe::reset();
    TinyMap b;
    ct::TreeMap<int, int> t;
    Lcg rng(2024);
    const int phases = 20;
    const int per_phase = 50000;
    for (int ph = 0; ph < phases; ++ph)
    {
        // chaves em 100, 600, 3000 e 20000 valores; a razao entre inserir e apagar muda de fase para fase
        static const int ranges[] = {100, 600, 3000, 20000};
        const int range = ranges[ph % 4];
        const int ins = ph % 5 == 0 ? 20 : (ph % 5 == 1 ? 70 : (ph % 5 == 2 ? 45 : (ph % 5 == 3 ? 30 : 55)));
        for (int i = 0; i < per_phase; ++i)
        {
            int k = static_cast<int>(rng.next() % static_cast<uint32_t>(range));
            if (ph % 6 == 4)
                k = ph * 1000 + i % 1500; // varrimento por ordem crescente
            else if (ph % 6 == 5)
                k = 100000 - (i % 4000); // e decrescente
            const int what = static_cast<int>(rng.next() % 100);
            if (what < ins)
            {
                b.put(k, i);
                t.put(k, i);
            }
            else if (what < 96)
            {
                ASSERT_EQ(b.erase(k), t.erase(k)) << "fase " << ph << " passo " << i;
            }
            else if (what < 98)
            {
                int *p = b.find(k);
                int *q = t.find(k);
                ASSERT_EQ(p != nullptr, q != nullptr);
                if (p)
                    ASSERT_EQ(*p, *q);
            }
            else
            {
                auto it = b.lower_bound(k);
                auto qt = t.lower_bound_it(k);
                ASSERT_EQ(it == b.end(), qt == t.end());
                if (it != b.end())
                    ASSERT_EQ(it->key, qt->key);
            }
            ASSERT_EQ(b.size(), t.size());
            if (i % 997 == 0)
            {
                ASSERT_TRUE(b.validate()) << "fase " << ph << " passo " << i;
                ASSERT_TRUE(same_contents(b, t)) << "fase " << ph << " passo " << i;
            }
        }
        ASSERT_TRUE(b.validate());
        ASSERT_TRUE(same_contents(b, t));
        // no fim de cada fase esvazia-se uma parte (das pontas ou do meio) para a arvore encolher
        if (ph % 3 == 2)
        {
            while (b.size() > 40)
            {
                int k = b.first()->key;
                if (ph % 2)
                    k = b.last()->key;
                ASSERT_TRUE(b.erase(k));
                ASSERT_TRUE(t.erase(k));
            }
            ASSERT_TRUE(b.validate());
        }
    }
    // e por fim tudo
    while (!t.empty())
    {
        int k = t.begin()->key;
        ASSERT_TRUE(t.erase(k));
        ASSERT_TRUE(b.erase(k));
    }
    EXPECT_TRUE(b.empty());
    EXPECT_TRUE(b.validate());
    expect_every_event_hit();
}

TEST(BTreeMapDifferential, DefaultNodeSizeWithStringKeys)
{
    ct::BTreeMap<ct::String, int> b;
    ct::TreeMap<ct::String, int> t;
    Lcg rng(77);
    for (int i = 0; i < 200000; ++i)
    {
        char buf[64];
        snprintf(buf, sizeof buf, "key/%05u/%s", rng.next() % 4000, (rng.next() & 1) ? "a-long-suffix-to-leave-the-sbo" : "s");
        ct::String k(buf);
        const uint32_t what = rng.next() % 100;
        if (what < 50)
        {
            b.put(k, i);
            t.put(k, i);
        }
        else if (what < 85)
            ASSERT_EQ(b.erase(k), t.erase(k));
        else
        {
            int *p = b.find(k);
            int *q = t.find(k);
            ASSERT_EQ(p != nullptr, q != nullptr);
            if (p)
                ASSERT_EQ(*p, *q);
        }
        if (i % 5003 == 0)
        {
            ASSERT_TRUE(b.validate());
            ASSERT_EQ(b.size(), t.size());
        }
    }
    ASSERT_TRUE(b.validate());
    ASSERT_TRUE(same_contents(b, t));
}

TEST(BTreeMapDifferential, AscendingAndDescendingAndRandomDefaultGeometry)
{
    ct::BTreeMap<int, int> b;
    ct::TreeMap<int, int> t;
    for (int i = 0; i < 100000; ++i)
    {
        b.put(i, i);
        t.put(i, i);
    }
    ASSERT_TRUE(b.validate());
    // inserir por ordem crescente deixa as folhas cheias (so a ultima tem menos): menos de 12 bytes por elemento
    for (int i = 0; i < 100000; i += 2)
    {
        ASSERT_TRUE(b.erase(i));
        t.erase(i);
    }
    ASSERT_TRUE(b.validate());
    ASSERT_TRUE(same_contents(b, t));
    for (int i = 200000; i > 100000; --i)
    {
        b.put(i, -i);
        t.put(i, -i);
    }
    ASSERT_TRUE(b.validate());
    ASSERT_TRUE(same_contents(b, t));
    Lcg rng(5);
    for (int i = 0; i < 300000; ++i)
    {
        const int k = static_cast<int>(rng.next() % 300000);
        if (rng.next() & 1)
        {
            b.put(k, i);
            t.put(k, i);
        }
        else
            ASSERT_EQ(b.erase(k), t.erase(k));
    }
    ASSERT_TRUE(b.validate());
    ASSERT_TRUE(same_contents(b, t));
}

TEST(BTreeSetBasic, InsertContainsEraseOrderAndBounds)
{
    ct::BTreeSet<int> s;
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.begin() == s.end());
    for (int i = 0; i < 3000; ++i)
        EXPECT_TRUE(s.insert((i * 7919) % 3000 * 2));
    EXPECT_FALSE(s.insert(10));
    EXPECT_EQ(s.size(), 3000u);
    EXPECT_TRUE(s.contains(10));
    EXPECT_FALSE(s.contains(11));
    int prev = -1;
    for (auto &k : s)
    {
        ASSERT_GT(k, prev);
        prev = k;
    }
    EXPECT_EQ(*s.first(), 0);
    EXPECT_EQ(*s.last(), 5998);
    EXPECT_EQ(*s.lower_bound(11), 12);
    EXPECT_EQ(*s.upper_bound(12), 14);
    EXPECT_TRUE(s.lower_bound(6000) == s.end());
    for (int i = 0; i < 3000; i += 3)
        EXPECT_TRUE(s.erase(i * 2));
    EXPECT_FALSE(s.erase(0));
    EXPECT_EQ(s.size(), 2000u);
    EXPECT_TRUE(s.validate());
    EXPECT_EQ(s.erase_range(100, 200), 33u); // 50 pares em [100,200), 17 deles multiplos de 6 ja apagados
    ct::BTreeSet<int> c(s);
    EXPECT_EQ(c.size(), s.size());
    EXPECT_TRUE(c.validate());
    ct::Vector<int> sorted;
    for (auto &k : s)
        sorted.push_back(k);
    ct::BTreeSet<int> d;
    d.assign_sorted(sorted.begin(), sorted.end());
    EXPECT_EQ(d.size(), s.size());
    EXPECT_TRUE(d.validate());
    s.clear();
    EXPECT_TRUE(s.empty());
}

TEST(BTreeSetBasic, StringSetAgainstTreeMapModel)
{
    ct::BTreeSet<ct::String> s;
    ct::TreeMap<ct::String, int> t;
    Lcg rng(31);
    for (int i = 0; i < 100000; ++i)
    {
        char buf[32];
        snprintf(buf, sizeof buf, "k%06u", rng.next() % 5000);
        ct::String k(buf);
        if (rng.next() % 3)
        {
            ASSERT_EQ(s.insert(k), t.find(k) == nullptr);
            t.put(k, 1);
        }
        else
            ASSERT_EQ(s.erase(k), t.erase(k));
    }
    ASSERT_TRUE(s.validate());
    ASSERT_EQ(s.size(), t.size());
    auto it = s.begin();
    for (auto e = t.begin(); e != t.end(); ++e, ++it)
        ASSERT_TRUE(*it == e->key);
}

// ---- casos limite como os de tests/test_edge.cpp ----

TEST(BTreeEdge, ConstructionsAndDestructionsBalance)
{
    g_live = 0;
    {
        ct::BTreeMap<int, Counted> m;
        for (int i = 0; i < 700; ++i)
        {
            m.put((i * 7) % 311, Counted(i));
            ASSERT_EQ(g_live, static_cast<int>(m.size()));
        }
        for (int i = 0; i < 311; i += 3)
            m.erase(i);
        ASSERT_EQ(g_live, static_cast<int>(m.size()));
        ct::BTreeMap<int, Counted> c(m);
        ASSERT_EQ(g_live, static_cast<int>(m.size() * 2));
        c.erase_range(50, 150);
        m.erase(m.lower_bound(10), m.lower_bound(20));
        ASSERT_EQ(g_live, static_cast<int>(m.size() + c.size()));
        ct::BTreeMap<int, Counted> mv(static_cast<ct::BTreeMap<int, Counted> &&>(c));
        ASSERT_EQ(g_live, static_cast<int>(m.size() + mv.size()));
        c = m;
        ASSERT_EQ(g_live, static_cast<int>(m.size() * 2 + mv.size()));
        m.clear();
        ASSERT_EQ(g_live, static_cast<int>(c.size() + mv.size()));
        ct::Vector<ct::BTreeMap<int, Counted>::Entry> in;
        for (int i = 0; i < 100; ++i)
            in.push_back({i, Counted(i)});
        mv.assign_sorted(in.begin(), in.end());
        ASSERT_EQ(g_live, static_cast<int>(c.size() + mv.size() + in.size()));
        ct::BTreeSet<Counted> s;
        for (int i = 0; i < 400; ++i)
            s.insert(Counted((i * 13) % 97));
        ASSERT_EQ(g_live, static_cast<int>(c.size() + mv.size() + in.size() + s.size()));
        ASSERT_TRUE(mv.validate());
        ASSERT_TRUE(c.validate());
        ASSERT_TRUE(s.validate());
    }
    EXPECT_EQ(g_live, 0);
}

TEST(BTreeEdge, MoveOnlyValues)
{
    g_live = 0;
    {
        typedef ct::BTreeMap<int, MoveOnly, ct::Less<int>, 64> M;
        M m;
        for (int i = 0; i < 500; ++i)
            m.put((i * 37) % 500, MoveOnly(i));
        for (int i = 0; i < 500; i += 2)
            m.erase(i);
        m[1001] = MoveOnly(5);
        ASSERT_EQ(g_live, static_cast<int>(m.size()));
        ASSERT_TRUE(m.validate());
        M m2(static_cast<M &&>(m));
        ASSERT_EQ(g_live, static_cast<int>(m2.size()));
        M m3;
        m3 = static_cast<M &&>(m2);
        ASSERT_EQ(g_live, static_cast<int>(m3.size()));
        ASSERT_TRUE(m3.validate());
        ASSERT_TRUE(m.empty());
        m.put(1, MoveOnly(9));
        m3.swap(m);
        ASSERT_EQ(m.size(), 251u);
        ASSERT_EQ(m3.size(), 1u);
    }
    EXPECT_EQ(g_live, 0);
}

TEST(BTreeEdge, OverAlignedAndHugeValues)
{
    ct::BTreeMap<int, Aligned64> a;
    ct::BTreeMap<int, Aligned32> b;
    ct::BTreeMap<int, Huge> h;
    for (int i = 0; i < 700; ++i)
    {
        a.put((i * 31) % 700, Aligned64(i));
        Aligned32 x;
        x.v[0] = i;
        b.put((i * 31) % 700, x);
        if (i < 200)
            h.put(i * 3 % 200, Huge(static_cast<char>(i)));
    }
    for (int i = 0; i < 700; i += 3)
    {
        a.erase(i);
        b.erase(i);
    }
    for (auto &e : a)
        ASSERT_TRUE(aligned(&e.value, 64));
    for (auto &e : b)
        ASSERT_TRUE(aligned(&e.value, 32));
    ASSERT_TRUE(a.validate());
    ASSERT_TRUE(b.validate());
    ASSERT_TRUE(h.validate());
    ASSERT_EQ(h.size(), 200u);
    ct::BTreeMap<int, Aligned64> ac(a);
    for (auto &e : ac)
        ASSERT_TRUE(aligned(&e.value, 64));
    for (int i = 0; i < 700; ++i)
        if (i % 3)
            ASSERT_EQ(a.find(i)->v, ac.find(i)->v);
    EXPECT_GE(h.leaf_capacity(), 8u);
    EXPECT_EQ(h.find(7)->bytes[100], static_cast<char>((7 * 67) % 200)); // 7 = i * 3 % 200 com i = 67
}

// put com um valor que vive dentro da propria arvore, em cada tamanho e em cada posicao: os itens deslocam-se ao inserir
TEST(BTreeEdge, SelfReferenceInValueAndKey)
{
    for (int n = 1; n <= 130; ++n)
    {
        TinyMap m;
        for (int i = 0; i < n; ++i)
            m.put(i * 2, i * 100);
        for (int i = -1; i <= 2 * n; i += 2) // chaves impares: novas, em todas as posicoes
        {
            const int src = (i + 1) / 2 * 2 - ((i + 1) / 2 * 2 > 2 * (n - 1) ? 2 : 0);
            int *p = m.find(src < 0 ? 0 : src);
            ASSERT_NE(p, nullptr);
            const int want = *p;
            m.put(i, *p); // o valor e uma referencia para dentro da arvore
            ASSERT_EQ(*m.find(i), want) << n << " " << i;
        }
        ASSERT_TRUE(m.validate());
        // a chave de um elemento existente: substitui o valor por ele proprio
        for (auto &e : m)
            m.put(e.key, e.value);
        ASSERT_EQ(m.size(), static_cast<size_t>(2 * n + 1));
    }
    // auto-atribuicao e auto-movimento
    ct::BTreeMap<int, ct::String> a;
    for (int i = 0; i < 100; ++i)
        a.put(i, ct::String("a string long enough to live on the heap, not in the small buffer"));
    ct::BTreeMap<int, ct::String> &r = a;
    a = r;
    a = static_cast<ct::BTreeMap<int, ct::String> &&>(r);
    ASSERT_EQ(a.size(), 100u);
    ASSERT_TRUE(a.validate());
    // o valor de um elemento passado a insercao de outra chave com texto
    for (int i = 100; i < 200; ++i)
        a.put(i, *a.find(i - 100));
    ASSERT_EQ(a.size(), 200u);
}

TEST(BTreeEdge, NodeBytesBytesPerElementAndAllocationCount)
{
    // memoria: por elemento (nos de 512 bytes, sem cabecalho de malloc: um slab)
    typedef ct::BTreeMap<int, int, ct::Less<int>, 512, CountingAlloc> M;
    {
        CountingAlloc::live_bytes = 0;
        CountingAlloc::calls = 0;
        M m;
        Lcg rng(3);
        for (int i = 0; i < 200000; ++i)
            m.put(static_cast<int>(rng.next()), i);
        const double per = static_cast<double>(CountingAlloc::live_bytes) / static_cast<double>(m.size());
        EXPECT_LT(per, 11.1); // a btree do abseil gasta 11,1 com o mesmo uso
        EXPECT_LT(CountingAlloc::calls, m.size() / 100); // os nos vem de pedacos: muito menos alocacoes que nos
        ASSERT_TRUE(m.validate());
    }
    EXPECT_EQ(CountingAlloc::live_bytes, 0u); // tudo devolvido
    {
        CountingAlloc::live_bytes = 0;
        M m;
        ct::Vector<M::Entry> in;
        for (int i = 0; i < 200000; ++i)
            in.push_back({i, i});
        m.assign_sorted(in.begin(), in.end());
        const double per = static_cast<double>(CountingAlloc::live_bytes) / static_cast<double>(m.size());
        EXPECT_LT(per, 8.6); // folhas cheias: 512 / 62 bytes
    }
    EXPECT_EQ(CountingAlloc::live_bytes, 0u);
}

#if defined(CT_DEBUG_ITERATORS)
TEST(BTreeDebugIterators, UsingAnIteratorAfterTheTreeChangedAborts)
{
    ct::BTreeMap<int, int> m;
    for (int i = 0; i < 100; ++i)
        m.put(i, i);
    EXPECT_DEATH(
        {
            for (auto &e : m)
                if (e.key == 50)
                    m.erase(51);
        },
        "iterador usado depois de a arvore mudar");
    EXPECT_DEATH(
        {
            auto it = m.begin();
            m.put(1000, 1);
            ++it;
        },
        "iterador usado depois de a arvore mudar");
    // sem mudar nada o iterador continua valido
    int n = 0;
    for (auto &e : m)
    {
        (void)e;
        ++n;
    }
    EXPECT_EQ(n, 100);
}
#endif
