// Casos limite e comparacoes medidas. Este ficheiro nao usa a biblioteca standard (so o gtest e cabecalhos C).
#include <ct/sort.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

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

    uint64_t g_cmps = 0;

    bool counted_less(int a, int b)
    {
        ++g_cmps;
        return a < b;
    }

    void fill_pattern(ct::Vector<int> &v, int pattern, int n)
    {
        Lcg rng(12345);
        v.clear();
        for (int i = 0; i < n; ++i)
        {
            int k = 0;
            switch (pattern)
            {
            case 0: k = static_cast<int>(rng.next()); break;
            case 1: k = i; break;
            case 2: k = n - i; break;
            case 3: k = static_cast<int>(rng.next() & 15u); break;
            case 4: k = i < n / 2 ? i : n - i; break;
            default: k = 7; break;
            }
            v.push_back(k);
        }
    }
}

// Com o particionamento de Lomuto de antes, os repetidos iam todos para o mesmo lado e uma entrada com 16 valores
// distintos gastava a profundidade e caia no heapsort: 3,5 milhoes de comparacoes para 100000 elementos. Com a particao de
// Hoare, que para nos iguais ao pivo, ficam em 1,5 milhoes.
TEST(EdgeSort, ManyDuplicatesStayNearNLogN)
{
    ct::Vector<int> v;
    const int patterns[] = {3, 5, 2};
    const char *names[] = {"dups16", "allequal", "reversed"};
    for (int i = 0; i < 3; ++i)
    {
        fill_pattern(v, patterns[i], 100000);
        g_cmps = 0;
        ct::sort(v.begin(), v.end(), counted_less);
        for (size_t k = 1; k < v.size(); ++k)
            ASSERT_LE(v[k - 1], v[k]) << names[i];
        EXPECT_LT(g_cmps, 2200000u) << names[i];
    }
}

TEST(EdgeSort, AllPatternsAllSizesAroundTheSmallSortLimit)
{
    ct::Vector<int> v;
    for (int pattern = 0; pattern < 6; ++pattern)
        for (int n = 0; n < 130; ++n)
        {
            fill_pattern(v, pattern, n);
            uint64_t sum = 0, x = 0;
            for (size_t k = 0; k < v.size(); ++k)
            {
                sum += static_cast<uint64_t>(v[k]);
                x ^= static_cast<uint64_t>(v[k]) * 0x9e3779b97f4a7c15ull;
            }
            ct::sort(v.begin(), v.end(), [](int a, int b) { return a < b; });
            uint64_t sum2 = 0, x2 = 0;
            for (size_t k = 0; k < v.size(); ++k)
            {
                if (k)
                    ASSERT_LE(v[k - 1], v[k]);
                sum2 += static_cast<uint64_t>(v[k]);
                x2 ^= static_cast<uint64_t>(v[k]) * 0x9e3779b97f4a7c15ull;
            }
            ASSERT_EQ(sum, sum2);
            ASSERT_EQ(x, x2);
        }
}

// Um comparador que nao e uma ordem fraca estrita (<=) nao pode fazer a ordenacao ler fora do intervalo: o resultado
// nao e garantido, mas a memoria sim (corre com ASan nas etapas do check.sh).
TEST(EdgeSort, NonStrictComparatorStaysInBounds)
{
    ct::Vector<int> v;
    for (int pattern = 0; pattern < 6; ++pattern)
        for (int n : {0, 1, 2, 3, 24, 25, 26, 100, 5000})
        {
            fill_pattern(v, pattern, n);
            ct::sort(v.begin(), v.end(), [](int a, int b) { return a <= b; });
            EXPECT_EQ(v.size(), static_cast<size_t>(n));
        }
}

TEST(EdgeSort, RandomAgainstInsertionReference)
{
    Lcg rng(99);
    for (int round = 0; round < 300; ++round)
    {
        const int n = static_cast<int>(rng.next() % 600);
        const uint32_t range = (round & 1) ? 8u : 1000000u;
        ct::Vector<int> a, b;
        for (int i = 0; i < n; ++i)
        {
            int k = static_cast<int>(rng.next() % range);
            a.push_back(k);
            b.push_back(k);
        }
        ct::sort(a.begin(), a.end(), [](int x, int y) { return x < y; });
        for (int i = 1; i < n; ++i)
        {
            int t = b[i], j = i;
            while (j > 0 && t < b[j - 1])
            {
                b[j] = b[j - 1];
                --j;
            }
            b[j] = t;
        }
        for (int i = 0; i < n; ++i)
            ASSERT_EQ(a[i], b[i]);
    }
}

// Sem cabecalho por alocacao: 1000 blocos de 24 bytes pedidos com o alinhamento de um ponteiro ocupam 32 bytes cada no
// malloc do glibc, e nao os 48 de antes (que pedia 32). Em Windows o cabecalho continua. mallinfo2 conta os bytes
// em uso (os pedidos arredondados ao bloco do malloc), por isso nao depende da arena nem do tcache; com ASan o malloc
// e outro e o teste fica de fora.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CT_EDGE_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define CT_EDGE_ASAN 1
#endif
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) && !defined(CT_EDGE_ASAN)
TEST(EdgeHeapAlloc, NoPerAllocationHeaderOnPosix)
{
    ct::HeapAlloc a;
    void *blocks[1000];
    const size_t before = mallinfo2().uordblks;
    for (int i = 0; i < 1000; ++i)
        blocks[i] = a.allocate(24, 8);
    const size_t per_block = (mallinfo2().uordblks - before) / 1000;
    EXPECT_EQ(per_block, 32u);
    for (int i = 0; i < 1000; ++i)
        a.deallocate(blocks[i], 24);
}
#endif

TEST(EdgeHeapAlloc, AlignmentsAndReallocatePreserveContents)
{
    ct::HeapAlloc a;
    for (size_t align = 1; align <= 4096; align *= 2)
        for (size_t bytes : {size_t(0), size_t(1), size_t(7), size_t(24), size_t(100), size_t(4097)})
        {
            unsigned char *p = static_cast<unsigned char *>(a.allocate(bytes, align));
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % align, 0u);
            const size_t used = bytes ? bytes : 1;
            memset(p, 0xAB, used);
            const size_t bigger = used * 3 + 5;
            unsigned char *q = static_cast<unsigned char *>(a.reallocate(p, used, bigger, align));
            EXPECT_EQ(reinterpret_cast<uintptr_t>(q) % align, 0u);
            for (size_t i = 0; i < used; ++i)
                ASSERT_EQ(q[i], 0xAB);
            memset(q, 0xCD, bigger);
            unsigned char *r = static_cast<unsigned char *>(a.reallocate(q, bigger, used, align));
            EXPECT_EQ(reinterpret_cast<uintptr_t>(r) % align, 0u);
            for (size_t i = 0; i < used; ++i)
                ASSERT_EQ(r[i], 0xCD);
            a.deallocate(r, used);
        }
    a.deallocate(nullptr, 0);
}

TEST(EdgeHeapAlloc, BadAlignmentAndHugeSizeAbort)
{
    ct::HeapAlloc a;
    EXPECT_DEATH(a.allocate(16, 3), "alinhamento");
    volatile size_t huge = static_cast<size_t>(-1);
    EXPECT_DEATH(a.allocate(huge, 8), "");
    EXPECT_DEATH(a.allocate(huge - 100, 64), "");
}

// ---------------------------------------------------------------------------------------------------------------------
// HashMap e HashSet
// ---------------------------------------------------------------------------------------------------------------------
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>

namespace
{
    struct CollideHash
    {
        uint64_t operator()(int) const { return 42; }
    };

    template <typename M>
    uint64_t sum_values(M &m)
    {
        uint64_t s = 0;
        for (auto &e : m)
            s += static_cast<uint64_t>(e.value) * 31u + static_cast<uint64_t>(e.key);
        return s;
    }
}

// Iterar salta os buracos de 8 em 8 bytes: tem de visitar exatamente os ocupados, com qualquer densidade e posicao.
TEST(EdgeHashMap, IterationVisitsExactlyTheOccupiedSlots)
{
    Lcg rng(5);
    for (int round = 0; round < 200; ++round)
    {
        ct::HashMap<int, int> m;
        const int total = 1 + static_cast<int>(rng.next() % 600);
        for (int i = 0; i < total; ++i)
            m.put(static_cast<int>(rng.next()), i);
        // apaga quase tudo (ou nada) para deixar tabelas esparsas de todas as densidades
        const uint32_t keep = (round % 5 == 0) ? 1000u : (round % 5 == 1) ? 2u : (round % 5 == 2) ? 20u : 4u;
        ct::Vector<int> doomed;
        for (auto &e : m)
            if (rng.next() % keep != 0)
                doomed.push_back(e.key);
        for (size_t i = 0; i < doomed.size(); ++i)
            m.erase(doomed[i]);
        size_t visited = 0;
        uint64_t sum = 0;
        for (auto &e : m)
        {
            ++visited;
            sum += static_cast<uint64_t>(e.value) + static_cast<uint64_t>(static_cast<uint32_t>(e.key));
            ASSERT_NE(m.find(e.key), nullptr);
        }
        ASSERT_EQ(visited, m.size());
        const ct::HashMap<int, int> &cm = m;
        size_t cvisited = 0;
        for (auto &e : cm)
        {
            (void)e;
            ++cvisited;
        }
        ASSERT_EQ(cvisited, m.size());
        (void)sum;
    }
}

TEST(EdgeHashMap, EmptyAndSingleElement)
{
    ct::HashMap<int, int> m;
    EXPECT_TRUE(m.begin() == m.end());
    EXPECT_EQ(m.find(1), nullptr);
    EXPECT_FALSE(m.erase(1));
    m.clear();
    EXPECT_TRUE(m.begin() == m.end());
    m.put(7, 70);
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(*m.find(7), 70);
    size_t n = 0;
    for (auto &e : m)
    {
        EXPECT_EQ(e.key, 7);
        ++n;
    }
    EXPECT_EQ(n, 1u);
    ct::HashMap<int, int> c(m);
    EXPECT_EQ(*c.find(7), 70);
    EXPECT_TRUE(m.erase(7));
    EXPECT_TRUE(m.begin() == m.end());
    EXPECT_EQ(m.size(), 0u);
    ct::HashSet<int> s;
    EXPECT_TRUE(s.begin() == s.end());
    EXPECT_TRUE(s.insert(3));
    EXPECT_FALSE(s.insert(3));
    EXPECT_TRUE(s.contains(3));
    EXPECT_TRUE(s.erase(3));
    EXPECT_TRUE(s.begin() == s.end());
}

// A copia que mantem a capacidade da origem copia indice a indice, sem hash: tem de dar o mesmo conteudo em todas as
// capacidades, e a atribuicao (que nao copia o hasher) tem de continuar a dar um mapa utilizavel.
TEST(EdgeHashMap, CopyAndAssignKeepEveryEntry)
{
    ct::HashMap<int, int> src;
    ct::HashSet<int> sset;
    for (int n = 0; n <= 400; ++n)
    {
        if (n)
        {
            src.put(n * 7919, n);
            sset.insert(n * 7919);
        }
        ct::HashMap<int, int> copy(src);
        ct::HashSet<int> scopy(sset);
        ASSERT_EQ(copy.size(), src.size());
        ASSERT_EQ(scopy.size(), sset.size());
        for (int i = 1; i <= n; ++i)
        {
            ASSERT_NE(copy.find(i * 7919), nullptr);
            ASSERT_EQ(*copy.find(i * 7919), i);
            ASSERT_TRUE(scopy.contains(i * 7919));
        }
        ASSERT_EQ(copy.find(-1), nullptr);
        ASSERT_EQ(sum_values(copy), sum_values(src));
        copy.put(-5, 99);
        scopy.insert(-5);
        ASSERT_EQ(src.find(-5), nullptr);
        ASSERT_FALSE(sset.contains(-5));
        ASSERT_EQ(*copy.find(-5), 99);
        if (n % 37 == 0)
        {
            ct::HashMap<int, int> big;
            for (int i = 0; i < 5000; ++i)
                big.put(i, i);
            big = src;
            ASSERT_EQ(big.size(), src.size());
            for (int i = 1; i <= n; ++i)
                ASSERT_EQ(*big.find(i * 7919), i);
            ASSERT_EQ(big.find(3), nullptr);
            ct::HashMap<int, int> small;
            small.put(1, 1);
            small = src;
            ASSERT_EQ(small.size(), src.size());
            ASSERT_EQ(sum_values(small), sum_values(src));
            small = small;
            ASSERT_EQ(small.size(), src.size());
        }
    }
}

TEST(EdgeHashMap, CopyWithOwningValues)
{
    ct::HashMap<int, ct::String> m;
    for (int i = 0; i < 300; ++i)
    {
        ct::String v;
        v.append_number(i);
        v.append("-a-string-long-enough-to-live-on-the-heap");
        m.put(i, v);
    }
    ct::HashMap<int, ct::String> c(m);
    for (int i = 0; i < 300; ++i)
    {
        ASSERT_NE(c.find(i), nullptr);
        ASSERT_TRUE(*c.find(i) == *m.find(i));
        ASSERT_NE(c.find(i)->data(), m.find(i)->data());
    }
}

TEST(EdgeHashMap, AllKeysCollide)
{
    ct::HashMap<int, int, CollideHash> m;
    const int n = 1500;
    for (int i = 0; i < n; ++i)
        m.put(i, i * 2);
    ASSERT_EQ(m.size(), static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        ASSERT_EQ(*m.find(i), i * 2);
    ASSERT_EQ(m.find(n), nullptr);
    for (int i = 0; i < n; i += 2)
        ASSERT_TRUE(m.erase(i));
    for (int i = 0; i < n; ++i)
        ASSERT_EQ(m.find(i) != nullptr, (i % 2) == 1);
    ct::HashMap<int, int, CollideHash> c(m);
    for (int i = 1; i < n; i += 2)
        ASSERT_EQ(*c.find(i), i * 2);
    for (int i = 1; i < n; i += 2)
        ASSERT_TRUE(m.erase(i));
    ASSERT_EQ(m.size(), 0u);
    ASSERT_TRUE(m.begin() == m.end());
}

// A remocao desloca para tras (sem marcadores): 1e6 de entra-e-sai com no maximo 1000 vivos nao pode fazer a tabela crescer.
TEST(EdgeHashMap, InsertEraseChurnDoesNotGrowTheTable)
{
    ct::HashMap<int, int> m;
    ct::HashSet<int> s;
    for (int i = 0; i < 1000; ++i)
    {
        m.put(i, i);
        s.insert(i);
    }
    const size_t cap = m.capacity();
    const size_t scap = s.capacity();
    for (int i = 0; i < 1000000; ++i)
    {
        ASSERT_TRUE(m.erase(i));
        m.put(i + 1000, i);
        ASSERT_TRUE(s.erase(i));
        s.insert(i + 1000);
    }
    EXPECT_EQ(m.size(), 1000u);
    EXPECT_EQ(m.capacity(), cap);
    EXPECT_EQ(s.capacity(), scap);
    EXPECT_LE(cap, 2048u);
    for (int i = 1000000; i < 1001000; ++i)
        ASSERT_NE(m.find(i), nullptr);
}

// Modelo de referencia: um vetor direto indexado pela chave (espaco de 4096 chaves, para haver muitas colisoes e muitos
// apagados), 1e6 operacoes com semente fixa; de tempos a tempos copia, atribui, itera e compara.
TEST(EdgeHashMap, RandomDifferentialAgainstDirectArray)
{
    const int keys = 4096;
    ct::Vector<int> ref_val;
    ct::Vector<char> ref_has;
    ref_val.resize(keys);
    ref_has.resize(keys);
    ct::HashMap<int, int> m;
    ct::HashSet<int> s;
    size_t count = 0;
    Lcg rng(2024);
    for (int op = 0; op < 1000000; ++op)
    {
        const int k = static_cast<int>(rng.next() % keys);
        const uint32_t what = rng.next() % 100;
        if (what < 40)
        {
            const int v = static_cast<int>(rng.next());
            m.put(k * 977, v);
            s.insert(k * 977);
            if (!ref_has[k])
                ++count;
            ref_has[k] = 1;
            ref_val[k] = v;
        }
        else if (what < 70)
        {
            const bool had = ref_has[k] != 0;
            ASSERT_EQ(m.erase(k * 977), had);
            ASSERT_EQ(s.erase(k * 977), had);
            if (had)
                --count;
            ref_has[k] = 0;
        }
        else if (what < 98)
        {
            int *p = m.find(k * 977);
            ASSERT_EQ(p != nullptr, ref_has[k] != 0);
            ASSERT_EQ(s.contains(k * 977), ref_has[k] != 0);
            if (p)
                ASSERT_EQ(*p, ref_val[k]);
        }
        else if (what == 98)
        {
            ct::HashMap<int, int> c(m);
            ASSERT_EQ(c.size(), count);
            if (op % 5 == 0)
            {
                m = c;
                ct::HashSet<int> sc(s);
                s = sc;
            }
            else if (op % 7 == 0)
            {
                ct::HashMap<int, int> moved(static_cast<ct::HashMap<int, int> &&>(c));
                m = static_cast<ct::HashMap<int, int> &&>(moved);
            }
        }
        else
        {
            size_t n = 0;
            for (auto &e : m)
            {
                const int kk = e.key / 977;
                ASSERT_TRUE(ref_has[kk] != 0);
                ASSERT_EQ(e.value, ref_val[kk]);
                ++n;
            }
            ASSERT_EQ(n, count);
            ASSERT_EQ(m.size(), count);
            ASSERT_EQ(s.size(), count);
            if (op % 20000 == 99)
            {
                m.clear();
                s.clear();
                for (int i = 0; i < keys; ++i)
                    ref_has[i] = 0;
                count = 0;
            }
        }
    }
}

TEST(EdgeHashMap, MovedFromMapsAreEmptyAndReusable)
{
    ct::HashMap<int, int> a;
    for (int i = 0; i < 100; ++i)
        a.put(i, i);
    ct::HashMap<int, int> b(static_cast<ct::HashMap<int, int> &&>(a));
    EXPECT_EQ(a.size(), 0u);
    EXPECT_TRUE(a.begin() == a.end());
    EXPECT_EQ(a.find(1), nullptr);
    a.put(5, 6);
    EXPECT_EQ(*a.find(5), 6);
    EXPECT_EQ(b.size(), 100u);
    a = static_cast<ct::HashMap<int, int> &&>(b);
    EXPECT_EQ(b.size(), 0u);
    b.put(1, 1);
    EXPECT_EQ(a.size(), 100u);
    ct::HashSet<int> s;
    s.insert(1);
    ct::HashSet<int> t(static_cast<ct::HashSet<int> &&>(s));
    EXPECT_EQ(s.size(), 0u);
    s.insert(2);
    EXPECT_TRUE(s.contains(2));
    EXPECT_TRUE(t.contains(1));
}
