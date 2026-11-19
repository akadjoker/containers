// Casos limite e comparacoes medidas. Este ficheiro nao usa a biblioteca standard (so o gtest e cabecalhos C).
#include <ct/sort.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

// Ordenada ou ordenada ao contrario: uma passagem (antes eram 1,3 e 3,4 milhoes de comparacoes para 100000).
TEST(EdgeSort, SortedAndReversedInputCostOnePass)
{
    ct::Vector<int> v;
    fill_pattern(v, 1, 100000);
    g_cmps = 0;
    ct::sort(v.begin(), v.end(), counted_less);
    EXPECT_LE(g_cmps, 100000u);
    fill_pattern(v, 2, 100000);
    g_cmps = 0;
    ct::sort(v.begin(), v.end(), counted_less);
    EXPECT_LE(g_cmps, 200000u);
    for (size_t k = 1; k < v.size(); ++k)
        ASSERT_LE(v[k - 1], v[k]);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[99999], 100000);
    // quase ordenada: o primeiro par fora de ordem a meio nao pode estragar o resultado
    fill_pattern(v, 1, 5000);
    int t = v[2500];
    v[2500] = v[2501];
    v[2501] = t;
    ct::sort(v.begin(), v.end(), counted_less);
    for (size_t k = 0; k < v.size(); ++k)
        ASSERT_EQ(v[k], static_cast<int>(k));
    // com repetidos ordenados ao contrario (nao estrito) tambem tem de dar o resultado certo
    v.clear();
    for (int i = 0; i < 3000; ++i)
        v.push_back((3000 - i) / 3);
    ct::sort(v.begin(), v.end(), counted_less);
    for (size_t k = 1; k < v.size(); ++k)
        ASSERT_LE(v[k - 1], v[k]);
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
// em uso (os pedidos arredondados ao bloco do malloc); o tcache pode devolver uns blocos ja contados, por isso
// 30 a 32 e nao 48. Com ASan o malloc e outro e o teste fica de fora.
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
    EXPECT_GE(per_block, 30u);
    EXPECT_LE(per_block, 32u);
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

// Tabelas de 2, 4 e 8 slots (reserve pequeno) tem menos de um grupo de 8 bytes de meta: o ultimo grupo e parcial.
TEST(EdgeHashMap, IterationAtTinyAndExactCapacities)
{
    for (size_t reserve = 0; reserve <= 70; ++reserve)
        for (size_t fill = 0; fill <= reserve + 3; ++fill)
        {
            ct::HashMap<int, int> m;
            ct::HashSet<int> s;
            if (reserve)
            {
                m.reserve(reserve);
                s.reserve(reserve);
            }
            for (size_t i = 0; i < fill; ++i)
            {
                m.put(static_cast<int>(i * 40503u), static_cast<int>(i));
                s.insert(static_cast<int>(i * 40503u));
            }
            size_t n = 0, sum = 0;
            for (auto &e : m)
            {
                ++n;
                sum += static_cast<size_t>(e.value);
            }
            ASSERT_EQ(n, fill) << reserve << " " << fill;
            ASSERT_EQ(sum, fill * (fill ? fill - 1 : 0) / 2);
            n = 0;
            for (auto k : s)
            {
                (void)k;
                ++n;
            }
            ASSERT_EQ(n, fill);
            const ct::HashMap<int, int> &cm = m;
            ct::HashMap<int, int>::const_iterator it = m.begin();
            ASSERT_TRUE(it == cm.begin());
            n = 0;
            for (it = cm.begin(); it != cm.end(); ++it)
                ++n;
            ASSERT_EQ(n, fill);
        }
}

// Um HashSet com capacidade 2 (reserve(1), ou a copia de um conjunto de 1 elemento) ficava 100 % cheio ao segundo insert e o
// terceiro nunca acabava (a sondagem so para num slot livre). O filho tem um alarme: se ficar preso morre por sinal e o
// teste falha, em vez de pendurar a bateria.
#if !defined(_WIN32)
#include <unistd.h>
namespace
{
    void set_fill_with_tiny_capacity()
    {
        alarm(5);
        ct::HashSet<int> a;
        a.reserve(1);
        for (int i = 0; i < 50; ++i)
            a.insert(i * 7);
        ct::HashSet<int> one;
        one.insert(1);
        ct::HashSet<int> c(one);
        for (int i = 0; i < 50; ++i)
        {
            if (c.contains(1000 + i))
                _exit(2);
            c.insert(1000 + i);
        }
        ct::HashSet<int> d;
        d.reserve(1);
        d.insert(1);
        d.insert(2);
        if (d.contains(3))
            _exit(3);
        for (int i = 0; i < 50; ++i)
            if (!c.contains(1000 + i))
                _exit(4);
        _exit(a.size() == 50 && c.size() == 51 ? 0 : 5);
    }
}

TEST(EdgeHashMap, SetWithCapacityTwoDoesNotFillUpAndSpin)
{
    EXPECT_EXIT(set_fill_with_tiny_capacity(), ::testing::ExitedWithCode(0), "");
}
#endif

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

// ---------------------------------------------------------------------------------------------------------------------
// Vida dos elementos: cada construcao tem de ter a sua destruicao, exatamente uma vez
// ---------------------------------------------------------------------------------------------------------------------
#include <ct/deque.hpp>
#include <ct/flatmap.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>

namespace
{
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
        bool operator==(const Counted &o) const { return v == o.v; }
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
        bool operator==(const Aligned64 &o) const { return v == o.v; }
    };

    struct alignas(32) Aligned32
    {
        long long v[2];
        Aligned32() { v[0] = v[1] = 0; }
        bool operator==(const Aligned32 &o) const { return v[0] == o.v[0]; }
    };

    struct Huge
    {
        char bytes[4096];
        Huge() { memset(bytes, 0, sizeof(bytes)); }
        explicit Huge(char c) { memset(bytes, c, sizeof(bytes)); }
    };
}

TEST(EdgeLifetime, VectorEveryOperationBalancesConstructionsAndDestructions)
{
    g_live = 0;
    {
        ct::Vector<Counted> v;
        for (int i = 0; i < 100; ++i)
        {
            v.push_back(Counted(i));
            ASSERT_EQ(g_live, static_cast<int>(v.size()));
        }
        v.emplace_back(5);
        v.insert(v.begin() + 3, Counted(77));
        v.erase(v.begin() + 10);
        v.erase(v.begin() + 5, v.begin() + 20);
        ASSERT_EQ(g_live, static_cast<int>(v.size()));
        v.resize(10);
        ASSERT_EQ(g_live, 10);
        v.resize(500, Counted(9));
        ASSERT_EQ(g_live, 500);
        ct::Vector<Counted> c(v);
        ASSERT_EQ(g_live, 1000);
        c.assign(7, Counted(3));
        ASSERT_EQ(g_live, 507);
        ct::Vector<Counted> m(static_cast<ct::Vector<Counted> &&>(v));
        ASSERT_EQ(g_live, 507);
        ASSERT_EQ(v.size(), 0u);
        v = c;
        ASSERT_EQ(g_live, 514);
        v.shrink_to_fit();
        v.pop_back();
        v.clear();
        ASSERT_EQ(g_live, 507);
        c.swap(m);
        c = static_cast<ct::Vector<Counted> &&>(m);
        m.push_back(Counted(1));
        ASSERT_EQ(g_live, static_cast<int>(c.size() + m.size() + v.size()));
        c.reserve(5000);
        ASSERT_EQ(g_live, static_cast<int>(c.size() + m.size()));
    }
    EXPECT_EQ(g_live, 0);
}

TEST(EdgeLifetime, MoveOnlyElementsInEveryContainer)
{
    g_live = 0;
    {
        ct::Vector<MoveOnly> v;
        for (int i = 0; i < 200; ++i)
            v.push_back(MoveOnly(i));
        v.insert(v.begin(), MoveOnly(-5));
        v.erase(v.begin() + 100);
        ASSERT_EQ(g_live, static_cast<int>(v.size()));
        ct::Deque<MoveOnly> d;
        for (int i = 0; i < 200; ++i)
        {
            d.push_back(MoveOnly(i));
            d.push_front(MoveOnly(-i));
        }
        d.pop_front();
        d.pop_back();
        ASSERT_EQ(g_live, static_cast<int>(v.size() + d.size()));
        ct::HashMap<int, MoveOnly> h;
        for (int i = 0; i < 200; ++i)
            h.put(i, MoveOnly(i));
        for (int i = 0; i < 200; i += 3)
            h.erase(i);
        h.put(1, MoveOnly(1000));
        ASSERT_EQ(g_live, static_cast<int>(v.size() + d.size() + h.size()));
        ct::TreeMap<int, MoveOnly> t;
        for (int i = 0; i < 200; ++i)
            t.put(i, MoveOnly(i));
        for (int i = 0; i < 200; i += 2)
            t.erase(i);
        ASSERT_TRUE(t.validate());
        ASSERT_EQ(g_live, static_cast<int>(v.size() + d.size() + h.size() + t.size()));
        ct::HashMap<int, MoveOnly> h2(static_cast<ct::HashMap<int, MoveOnly> &&>(h));
        ASSERT_EQ(g_live, static_cast<int>(v.size() + d.size() + h2.size() + t.size()));
    }
    EXPECT_EQ(g_live, 0);
}

TEST(EdgeLifetime, DequeHashMapTreeMapFlatMapBalance)
{
    g_live = 0;
    {
        ct::Deque<Counted> d;
        for (int i = 0; i < 300; ++i)
        {
            if (i % 3)
                d.push_back(Counted(i));
            else
                d.push_front(Counted(i));
            ASSERT_EQ(g_live, static_cast<int>(d.size()));
        }
        ct::Deque<Counted> dc(d);
        d.resize(50);
        d.resize(400, Counted(1));
        while (d.size() > 10)
            d.pop_front();
        ASSERT_EQ(g_live, static_cast<int>(d.size() + dc.size()));
        ct::HashMap<int, Counted> h;
        for (int i = 0; i < 1000; ++i)
            h.put(i % 300, Counted(i));
        for (int i = 0; i < 300; i += 2)
            h.erase(i);
        ct::HashMap<int, Counted> hc(h);
        h.clear();
        ASSERT_EQ(g_live, static_cast<int>(d.size() + dc.size() + hc.size()));
        ct::TreeMap<int, Counted> t;
        ct::FlatMap<int, Counted> f;
        for (int i = 0; i < 500; ++i)
        {
            t.put((i * 7) % 211, Counted(i));
            f.put((i * 7) % 211, Counted(i));
        }
        ASSERT_TRUE(t.validate());
        for (int i = 0; i < 211; i += 3)
        {
            t.erase(i);
            f.erase(i);
        }
        ASSERT_TRUE(t.validate());
        ct::TreeMap<int, Counted> tc(t);
        ct::FlatMap<int, Counted> fc(f);
        ASSERT_EQ(g_live, static_cast<int>(d.size() + dc.size() + hc.size() + t.size() * 2 + f.size() * 2));
        t.clear();
        ASSERT_EQ(g_live, static_cast<int>(d.size() + dc.size() + hc.size() + tc.size() + f.size() * 2));
    }
    EXPECT_EQ(g_live, 0);
}

// push_back/insert/emplace/assign/resize de um elemento do proprio vetor, em cada capacidade (a realocacao nao pode
// deixar a referencia a apontar para memoria solta).
TEST(EdgeSelfReference, VectorSelfInsertionAtEveryCapacityBoundary)
{
    for (int n = 1; n <= 70; ++n)
    {
        for (int mode = 0; mode < 6; ++mode)
        {
            ct::Vector<ct::String> v;
            ct::Vector<int> w;
            v.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                ct::String s;
                s.append("element-number-with-a-long-enough-text-");
                s.append_number(i);
                v.push_back(s);
                w.push_back(i);
            }
            const int src = n / 2;
            ct::String want = v[static_cast<size_t>(src)];
            switch (mode)
            {
            case 0: v.push_back(v[static_cast<size_t>(src)]); w.push_back(w[static_cast<size_t>(src)]); break;
            case 1: v.insert(v.begin(), v[static_cast<size_t>(src)]); w.insert(w.begin(), w[static_cast<size_t>(src)]); break;
            case 2: v.emplace_back(v[static_cast<size_t>(src)]); w.emplace_back(w[static_cast<size_t>(src)]); break;
            case 3: v.insert(v.begin() + src, v.back()); want = v[static_cast<size_t>(src)]; w.insert(w.begin() + src, w.back()); break;
            case 4: v.resize(static_cast<size_t>(n) * 2 + 1, v[static_cast<size_t>(src)]); w.resize(static_cast<size_t>(n) * 2 + 1, w[static_cast<size_t>(src)]); break;
            default: v.assign(3, v[static_cast<size_t>(src)]); w.assign(3, w[static_cast<size_t>(src)]); break;
            }
            ASSERT_EQ(v.size(), w.size()) << "n=" << n << " mode=" << mode;
            for (size_t i = 0; i < w.size(); ++i)
            {
                ct::String expect;
                expect.append("element-number-with-a-long-enough-text-");
                expect.append_number(w[i]);
                ASSERT_TRUE(v[i] == expect) << "n=" << n << " mode=" << mode << " i=" << i;
            }
        }
    }
}

TEST(EdgeSelfReference, StringAppendInsertAssignOfItself)
{
    for (size_t n = 0; n <= 80; ++n)
    {
        ct::String base;
        for (size_t i = 0; i < n; ++i)
            base.push_back(static_cast<char>('a' + i % 26));
        {
            ct::String s(base);
            s.append(s);
            ASSERT_EQ(s.size(), 2 * n);
            for (size_t i = 0; i < 2 * n; ++i)
                ASSERT_EQ(s[i], base[i % (n ? n : 1)]);
        }
        {
            ct::String s(base);
            s += s;
            ASSERT_EQ(s.size(), 2 * n);
        }
        {
            ct::String s(base);
            s.insert(0, s);
            ASSERT_EQ(s.size(), 2 * n);
            for (size_t i = 0; i < 2 * n; ++i)
                ASSERT_EQ(s[i], base[i % (n ? n : 1)]);
        }
        if (n >= 3)
        {
            ct::String s(base);
            s.insert(n / 2, s.c_str() + 1, n - 2);
            ASSERT_EQ(s.size(), 2 * n - 2);
            ct::String t(base);
            t.append(t.c_str() + 1, n - 2);
            ASSERT_EQ(t.size(), 2 * n - 2);
            ct::String u(base);
            u.assign(u.c_str() + 1, n - 1);
            ASSERT_EQ(u.size(), n - 1);
            for (size_t i = 0; i + 1 < n; ++i)
                ASSERT_EQ(u[i], base[i + 1]);
            ct::String w(base);
            w = w;
            ASSERT_TRUE(w == base);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Alinhamento, elementos enormes, limites de tamanho
// ---------------------------------------------------------------------------------------------------------------------
#include <ct/arena.hpp>
#include <ct/pool.hpp>

namespace
{
    template <typename T>
    bool aligned(const T *p, size_t a)
    {
        return reinterpret_cast<uintptr_t>(p) % a == 0;
    }
}

TEST(EdgeAlignment, OverAlignedElementsStayAlignedThroughGrowth)
{
    ct::Vector<Aligned64> v;
    ct::Deque<Aligned64> d;
    ct::HashMap<int, Aligned64> h;
    ct::TreeMap<int, Aligned64> t;
    ct::Vector<Aligned32> v32;
    for (int i = 0; i < 700; ++i)
    {
        v.push_back(Aligned64(i));
        d.push_back(Aligned64(i));
        d.push_front(Aligned64(-i));
        h.put(i, Aligned64(i));
        t.put(i, Aligned64(i));
        Aligned32 a;
        a.v[0] = i;
        v32.push_back(a);
        if (i % 50 == 0 || i < 40)
        {
            for (size_t k = 0; k < v.size(); ++k)
                ASSERT_TRUE(aligned(&v[k], 64));
            for (size_t k = 0; k < d.size(); ++k)
                ASSERT_TRUE(aligned(&d[k], 64));
            for (auto &e : h)
                ASSERT_TRUE(aligned(&e.value, 64));
            for (auto &e : t)
                ASSERT_TRUE(aligned(&e.value, 64));
            for (size_t k = 0; k < v32.size(); ++k)
                ASSERT_TRUE(aligned(&v32[k], 32));
        }
    }
    for (int i = 0; i < 700; ++i)
    {
        ASSERT_EQ(v[static_cast<size_t>(i)].v, i);
        ASSERT_EQ(h.find(i)->v, i);
        ASSERT_EQ(t.find(i)->v, i);
        ASSERT_EQ(v32[static_cast<size_t>(i)].v[0], i);
    }
    ct::Arena arena;
    for (int i = 0; i < 100; ++i)
    {
        Aligned64 *p = arena.allocate_array<Aligned64>(3);
        ASSERT_TRUE(aligned(p, 64));
    }
    ct::Pool<Aligned64> pool(16);
    for (int i = 0; i < 100; ++i)
        ASSERT_TRUE(aligned(pool.allocate(), 64));
}

TEST(EdgeAlignment, HugeElements)
{
    ct::Vector<Huge> v;
    ct::Deque<Huge> d;
    for (int i = 0; i < 300; ++i)
    {
        v.push_back(Huge(static_cast<char>(i)));
        d.push_back(Huge(static_cast<char>(i)));
        d.push_front(Huge(static_cast<char>(i + 1)));
    }
    for (int i = 0; i < 300; ++i)
    {
        ASSERT_EQ(v[static_cast<size_t>(i)].bytes[0], static_cast<char>(i));
        ASSERT_EQ(v[static_cast<size_t>(i)].bytes[4095], static_cast<char>(i));
        ASSERT_EQ(d[static_cast<size_t>(300 + i)].bytes[4095], static_cast<char>(i));
    }
    v.erase(v.begin(), v.begin() + 100);
    ASSERT_EQ(v.size(), 200u);
    ASSERT_EQ(v[0].bytes[17], static_cast<char>(100));
}

using IntMap = ct::HashMap<int, int>;

TEST(EdgeLimits, SizesNearTheMaximumFailCleanlyInsteadOfWrapping)
{
    EXPECT_DEATH({ ct::Vector<int> v; v.reserve(v.max_size()); }, "");
    EXPECT_DEATH({ ct::Vector<int> v; v.resize(v.max_size()); }, "");
    EXPECT_DEATH({ ct::Vector<Huge> v; v.reserve(v.max_size()); }, "");
    EXPECT_DEATH({ ct::String s; s.reserve(s.max_size()); }, "");
    EXPECT_DEATH({ ct::String s("abc"); s.append(s.max_size(), 'x'); }, "");
    EXPECT_DEATH({ ct::String s; s.resize(s.max_size()); }, "");
    EXPECT_DEATH({ ct::Deque<int> d; d.reserve(d.max_size()); }, "");
    EXPECT_DEATH({ IntMap m; m.reserve(static_cast<size_t>(-1) / 2 + 1); }, "");
}

// ---------------------------------------------------------------------------------------------------------------------
// Strings: NUL no meio, bytes que nao sao UTF-8, o limite do SSO
// ---------------------------------------------------------------------------------------------------------------------
TEST(EdgeString, EmbeddedNulAndInvalidUtf8AreJustBytes)
{
    const char raw[] = {'a', '\0', 'b', '\xff', '\xc0', '\x80', '\xed', '\xa0', '\x80', '\0', '\0', 'z'};
    ct::String s(raw, sizeof(raw));
    ASSERT_EQ(s.size(), sizeof(raw));
    ASSERT_EQ(memcmp(s.data(), raw, sizeof(raw)), 0);
    EXPECT_EQ(s.find('b'), 2u);
    EXPECT_EQ(s.find('\0'), 1u);
    EXPECT_EQ(s.find(ct::String("\xff\xc0\x80", 3)), 3u);
    EXPECT_EQ(s.rfind('\0'), 10u);
    ct::String t(s);
    EXPECT_TRUE(s == t);
    t.push_back('\0');
    EXPECT_FALSE(s == t);
    EXPECT_TRUE(s < t);
    EXPECT_EQ(s.hash(), ct::String(raw, sizeof(raw)).hash());
    EXPECT_NE(s.hash(), t.hash());
    ct::String u("a\0b", 3);
    EXPECT_EQ(u.size(), 3u);
    EXPECT_FALSE(u == ct::String("a"));
    EXPECT_TRUE(ct::String("a\0b", 3).compare(ct::String("a\0c", 3)) < 0);
    ct::String sub = s.substr(1, 4);
    EXPECT_EQ(sub.size(), 4u);
    EXPECT_EQ(sub[0], '\0');
    ct::Vector<ct::String> parts = ct::String("x\0y\0\0z", 6).split('\0', true);
    EXPECT_EQ(parts.size(), 4u);
    EXPECT_EQ(ct::String("12\0003", 4).to_int(), 12);
    ct::String all;
    for (int i = 0; i < 256; ++i)
        all.push_back(static_cast<char>(i));
    EXPECT_EQ(all.size(), 256u);
    for (int i = 0; i < 256; ++i)
        ASSERT_EQ(static_cast<unsigned char>(all[static_cast<size_t>(i)]), static_cast<unsigned>(i));
    ct::String copy(all);
    EXPECT_TRUE(copy == all);
}

TEST(EdgeString, LengthsAroundTheSmallBufferLimit)
{
    for (size_t n = 0; n <= 70; ++n)
    {
        ct::String s;
        for (size_t i = 0; i < n; ++i)
        {
            s.push_back(static_cast<char>('A' + i % 26));
            ASSERT_EQ(s.size(), i + 1);
            ASSERT_GE(s.capacity(), s.size());
            ASSERT_EQ(s.is_small(), s.size() <= 23u) << "i=" << i;
            ASSERT_EQ(s.c_str()[s.size()], '\0');
        }
        ct::String c(s);
        ASSERT_TRUE(c == s);
        ASSERT_EQ(c.c_str()[c.size()], '\0');
        ct::String m(static_cast<ct::String &&>(c));
        ASSERT_TRUE(m == s);
        ASSERT_EQ(c.size(), 0u);
        c.push_back('x');
        ASSERT_EQ(c.size(), 1u);
        ct::String r(s.c_str(), n);
        ASSERT_TRUE(r == s);
        ct::String fill(n, 'q');
        ASSERT_EQ(fill.size(), n);
        if (n)
        {
            s.resize(n / 2);
            ASSERT_EQ(s.size(), n / 2);
            ASSERT_EQ(s.c_str()[s.size()], '\0');
            s.resize(n + 5, 'z');
            ASSERT_EQ(s[n + 4], 'z');
        }
        ct::String big;
        big.reserve(n);
        ASSERT_GE(big.capacity(), n);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Numeros nos limites, contra valores conhecidos
// ---------------------------------------------------------------------------------------------------------------------
#include <float.h>
#include <limits.h>
#include <ct/ini.hpp>
#include <ct/json.hpp>

namespace
{
    uint32_t bits_of(float f)
    {
        uint32_t b;
        memcpy(&b, &f, 4);
        return b;
    }
    uint64_t bits_of(double d)
    {
        uint64_t b;
        memcpy(&b, &d, 8);
        return b;
    }
    float to_f(const char *s) { return ct::String(s).to_float(); }
    int to_i(const char *s) { return ct::String(s).to_int(); }
}

TEST(EdgeNumbers, StringToIntKnownValues)
{
    EXPECT_EQ(to_i("0"), 0);
    EXPECT_EQ(to_i("-0"), 0);
    EXPECT_EQ(to_i("42"), 42);
    EXPECT_EQ(to_i("  -17"), -17);
    EXPECT_EQ(to_i("+9"), 9);
    EXPECT_EQ(to_i("2147483647"), INT_MAX);
    EXPECT_EQ(to_i("-2147483648"), INT_MIN);
    EXPECT_EQ(to_i("12abc"), 12);
    EXPECT_EQ(to_i("abc"), 0);
    EXPECT_EQ(to_i(""), 0);
    EXPECT_EQ(to_i("   "), 0);
    EXPECT_EQ(to_i("0x10"), 0);
    EXPECT_EQ(to_i("1e3"), 1);
    EXPECT_EQ(to_i("- 5"), 0);
    EXPECT_EQ(to_i("007"), 7);
}

// to_int tem de dar o mesmo que o strtol, com o resultado preso ao intervalo do int (INT_MIN e INT_MAX em vez de dar a
// volta), em qualquer texto: diferencial com 1e6 textos.
namespace
{
    int saturated_strtol(const char *s)
    {
        const long long wide = strtoll(s, nullptr, 10);
        return wide > INT_MAX ? INT_MAX : (wide < INT_MIN ? INT_MIN : static_cast<int>(wide));
    }
}

TEST(EdgeNumbers, StringToIntMatchesStrtolOnRandomText)
{
    Lcg rng(4242);
    const char alphabet[] = "0123456789  +-\t\n\v\f\rabcxX.e";
    for (int round = 0; round < 1000000; ++round)
    {
        char buf[48];
        const size_t n = rng.next() % 40;
        const uint32_t digits_bias = rng.next() % 4;
        for (size_t i = 0; i < n; ++i)
        {
            const uint32_t r = rng.next();
            buf[i] = (digits_bias && (r & 3)) ? static_cast<char>('0' + (r >> 4) % 10)
                                                : alphabet[(r >> 4) % (sizeof(alphabet) - 1)];
        }
        buf[n] = '\0';
        ASSERT_EQ(ct::String(buf).to_int(), saturated_strtol(buf)) << buf;
    }
    const char *edge[] = {"9223372036854775807", "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
                          "99999999999999999999999", "-99999999999999999999999", "2147483648", "-2147483649", "4294967296"};
    for (const char *e : edge)
        ASSERT_EQ(ct::String(e).to_int(), saturated_strtol(e)) << e;
    EXPECT_EQ(ct::String("2147483648").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("4294967296").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("3000000000").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("-2147483649").to_int(), INT_MIN);
}

TEST(EdgeNumbers, IntegerToTextMatchesPrintfOnRandomValues)
{
    Lcg rng(777);
    for (int round = 0; round < 500000; ++round)
    {
        const uint64_t raw = (static_cast<uint64_t>(rng.next()) << 32) | rng.next();
        const int shift = static_cast<int>(rng.next() % 64);
        const long long v = static_cast<long long>(raw >> shift) * ((round & 1) ? -1 : 1);
        char want[40];
        snprintf(want, sizeof(want), "%lld", v);
        ASSERT_TRUE(ct::String::number(v) == want) << want;
        char wantu[40];
        snprintf(wantu, sizeof(wantu), "%llu", static_cast<unsigned long long>(raw >> shift));
        ASSERT_TRUE(ct::String::number(static_cast<unsigned long long>(raw >> shift)) == wantu) << wantu;
        ASSERT_TRUE(ct::String::number(static_cast<int>(v)) == [&] { char b[40]; snprintf(b, sizeof(b), "%d", static_cast<int>(v)); return ct::String(b); }());
    }
}

TEST(EdgeNumbers, StringToFloatKnownValues)
{
    EXPECT_EQ(bits_of(to_f("0")), 0u);
    EXPECT_EQ(bits_of(to_f("-0")), 0x80000000u);
    EXPECT_EQ(bits_of(to_f("1.5")), bits_of(1.5f));
    EXPECT_EQ(bits_of(to_f("  2.5xyz")), bits_of(2.5f));
    EXPECT_EQ(bits_of(to_f(".5")), bits_of(0.5f));
    EXPECT_EQ(bits_of(to_f("5.")), bits_of(5.0f));
    EXPECT_EQ(bits_of(to_f("1e")), bits_of(1.0f));
    EXPECT_EQ(bits_of(to_f("1e-45")), 1u);
    EXPECT_EQ(bits_of(to_f("1.17549435e-38")), 0x00800000u);
    EXPECT_EQ(bits_of(to_f("1e-46")), 0u);
    EXPECT_EQ(bits_of(to_f("3.4028235e38")), bits_of(FLT_MAX));
    EXPECT_EQ(bits_of(to_f("1e39")), 0x7f800000u);
    EXPECT_EQ(bits_of(to_f("-1e39")), 0xff800000u);
    EXPECT_EQ(bits_of(to_f("inf")), 0x7f800000u);
    EXPECT_EQ(bits_of(to_f("-Infinity")), 0xff800000u);
    const float nan = to_f("nan");
    EXPECT_TRUE(nan != nan);
    EXPECT_EQ(bits_of(to_f("")), 0u);
    EXPECT_EQ(bits_of(to_f("abc")), 0u);
    EXPECT_EQ(bits_of(to_f("0.1")), bits_of(0.1f));
    EXPECT_EQ(bits_of(to_f("16777217")), bits_of(16777216.0f));
}

TEST(EdgeNumbers, StringNumberToTextAndBack)
{
    const long long values[] = {0, 1, -1, 9, 10, 99, 100, 2147483647LL, -2147483648LL, 4294967295LL,
                                9223372036854775807LL, (-9223372036854775807LL - 1)};
    const char *texts[] = {"0", "1", "-1", "9", "10", "99", "100", "2147483647", "-2147483648", "4294967295",
                           "9223372036854775807", "-9223372036854775808"};
    for (int i = 0; i < 12; ++i)
    {
        ct::String s = ct::String::number(values[i]);
        EXPECT_TRUE(s == texts[i]) << texts[i] << " vs " << s.c_str();
    }
    EXPECT_TRUE(ct::String::number(18446744073709551615ull) == "18446744073709551615");
    EXPECT_TRUE(ct::String::number(0.5) == "0.5");
    EXPECT_TRUE(ct::String::number(1e300) == "1e+300");
    EXPECT_TRUE(ct::String::number(-0.0) == "-0");
    EXPECT_TRUE(ct::String::number(123456789.0, 9) == "123456789");
    EXPECT_TRUE(ct::String::number(0.1, 17) == "0.10000000000000001");
}

TEST(EdgeNumbers, JsonIntegersAndRealsAtTheLimits)
{
    ct::Json::Error err;
    ct::Json j = ct::Json::parse("9223372036854775807", &err);
    ASSERT_FALSE(static_cast<bool>(err));
    EXPECT_TRUE(j.is_int());
    EXPECT_EQ(j.as_int(), INT64_MAX);
    j = ct::Json::parse("-9223372036854775808", &err);
    ASSERT_FALSE(static_cast<bool>(err));
    EXPECT_TRUE(j.is_int());
    EXPECT_EQ(j.as_int(), INT64_MIN);
    j = ct::Json::parse("9223372036854775808", &err);
    ASSERT_FALSE(static_cast<bool>(err));
    EXPECT_TRUE(j.is_uint());
    EXPECT_EQ(j.as_uint(), 9223372036854775808ull);
    j = ct::Json::parse("18446744073709551615", &err);
    EXPECT_TRUE(j.is_uint());
    EXPECT_EQ(j.as_uint(), UINT64_MAX);
    j = ct::Json::parse("18446744073709551616", &err);
    ASSERT_FALSE(static_cast<bool>(err));
    EXPECT_TRUE(j.is_real());
    EXPECT_EQ(bits_of(j.as_double()), bits_of(18446744073709551616.0));
    j = ct::Json::parse("-9223372036854775809", &err);
    EXPECT_TRUE(j.is_real());
    EXPECT_EQ(bits_of(j.as_double()), bits_of(-9223372036854775808.0));
    j = ct::Json::parse("5e-324", &err);
    ASSERT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(bits_of(j.as_double()), 1u);
    j = ct::Json::parse("2.2250738585072014e-308", &err);
    EXPECT_EQ(bits_of(j.as_double()), bits_of(DBL_MIN));
    j = ct::Json::parse("1.7976931348623157e308", &err);
    EXPECT_EQ(bits_of(j.as_double()), bits_of(DBL_MAX));
    j = ct::Json::parse("0.1", &err);
    EXPECT_EQ(bits_of(j.as_double()), bits_of(0.1));
    j = ct::Json::parse("-0", &err);
    ASSERT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(j.as_double(), 0.0);
    j = ct::Json::parse("1e309", &err);
    if (!err)
        EXPECT_TRUE(j.as_double() > DBL_MAX);
    const char *bad[] = {"01", "+1", ".5", "1.", "1e", "0x10", "NaN", "Infinity", "-Infinity", "nan", "inf", "1 2",
                         "1,", "[1,]", "--1", "1e+", "-", "", "  "};
    for (const char *b : bad)
    {
        ct::Json::Error e2;
        ct::Json r = ct::Json::parse(b, &e2);
        EXPECT_TRUE(static_cast<bool>(e2)) << "aceitou: '" << b << "'";
    }
}

TEST(EdgeNumbers, IniIntegersRefuseWhatDoesNotFit)
{
    ct::Ini ini = ct::Ini::parse("[s]\na=9223372036854775807\nb=9223372036854775808\nc=-9223372036854775808\nd=12abc\ne=\nf=0x10\ng= 7 \nh=-0\n");
    EXPECT_EQ(ini.get_int("s", "a", 5), INT64_MAX);
    EXPECT_EQ(ini.get_int("s", "b", 5), 5);
    EXPECT_EQ(ini.get_int("s", "c", 5), INT64_MIN);
    EXPECT_EQ(ini.get_int("s", "d", 5), 5);
    EXPECT_EQ(ini.get_int("s", "e", 5), 5);
    EXPECT_EQ(ini.get_int("s", "f", 5), 5);
    EXPECT_EQ(ini.get_int("s", "g", 5), 7);
    EXPECT_EQ(ini.get_int("s", "h", 5), 0);
    EXPECT_EQ(ini.get_int("s", "missing", 5), 5);
}

// ---------------------------------------------------------------------------------------------------------------------
// Diferenciais contra modelos simples (sementes fixas, 1e6 operacoes cada)
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
    // Modelo: um vetor de capacidade fixa com copias a mao.
    struct ModelVec
    {
        int a[600];
        size_t n;
        ModelVec() : n(0) {}
        void insert(size_t pos, int x)
        {
            for (size_t i = n; i > pos; --i)
                a[i] = a[i - 1];
            a[pos] = x;
            ++n;
        }
        void erase(size_t pos, size_t count)
        {
            for (size_t i = pos; i + count < n; ++i)
                a[i] = a[i + count];
            n -= count;
        }
    };
}

TEST(EdgeDifferential, VectorAgainstFixedArrayModel)
{
    Lcg rng(31);
    ct::Vector<int> v;
    ct::Vector<ct::String> sv;
    ModelVec m;
    for (int op = 0; op < 1000000; ++op)
    {
        const uint32_t what = rng.next() % 100;
        const int x = static_cast<int>(rng.next() % 100000);
        if (what < 30 && m.n < 500)
        {
            v.push_back(x);
            m.insert(m.n, x);
        }
        else if (what < 40 && m.n)
        {
            v.pop_back();
            m.erase(m.n - 1, 1);
        }
        else if (what < 55 && m.n < 500)
        {
            const size_t pos = rng.next() % (m.n + 1);
            v.insert(v.begin() + static_cast<ptrdiff_t>(pos), x);
            m.insert(pos, x);
        }
        else if (what < 70 && m.n)
        {
            const size_t pos = rng.next() % m.n;
            const size_t cnt = 1 + rng.next() % (m.n - pos < 5 ? m.n - pos : 5);
            v.erase(v.begin() + static_cast<ptrdiff_t>(pos), v.begin() + static_cast<ptrdiff_t>(pos + cnt));
            m.erase(pos, cnt);
        }
        else if (what < 75)
        {
            const size_t n = rng.next() % 450;
            v.resize(n, x);
            for (size_t i = m.n; i < n; ++i)
                m.a[i] = x;
            m.n = n;
        }
        else if (what < 78 && m.n)
        {
            const size_t k = rng.next() % m.n;
            if (m.n < 500)
            {
                v.push_back(v[k]);
                m.insert(m.n, m.a[k]);
            }
        }
        else if (what < 80)
        {
            ct::Vector<int> c(v);
            ct::Vector<int> mv(static_cast<ct::Vector<int> &&>(c));
            v = static_cast<ct::Vector<int> &&>(mv);
        }
        else if (what < 82)
        {
            v.clear();
            m.n = 0;
        }
        else if (what < 84)
        {
            const size_t n = rng.next() % 40;
            v.assign(n, x);
            for (size_t i = 0; i < n; ++i)
                m.a[i] = x;
            m.n = n;
        }
        else if (what < 86)
        {
            ct::Vector<int> other;
            other.push_back(1);
            v.swap(other);
            v.swap(other);
        }
        else if (what < 90 && m.n)
        {
            // erase enquanto se percorre: o iterador devolvido continua valido
            size_t i = 0;
            for (ct::Vector<int>::iterator it = v.begin(); it != v.end();)
            {
                if (*it % 7 == 0)
                {
                    it = v.erase(it);
                    m.erase(i, 1);
                }
                else
                {
                    ++it;
                    ++i;
                }
            }
        }
        if (op % 11 == 0)
        {
            ASSERT_EQ(v.size(), m.n) << op;
            ASSERT_GE(v.capacity(), v.size());
            for (size_t i = 0; i < m.n; ++i)
                ASSERT_EQ(v[i], m.a[i]) << op << " i=" << i;
        }
        // as strings seguem o mesmo vetor, para exercitar o caminho nao trivial
        if (op % 97 == 0)
        {
            sv.clear();
            for (size_t i = 0; i < m.n && i < 40; ++i)
                sv.push_back(ct::String::number(m.a[i]));
            if (sv.size())
                sv.push_back(sv[0]);
            sv.insert(sv.begin(), ct::String("front"));
            ASSERT_TRUE(sv[0] == "front");
        }
    }
}

TEST(EdgeDifferential, DequeAgainstShiftingModel)
{
    Lcg rng(77);
    ct::Deque<int> d;
    ModelVec m;
    for (int op = 0; op < 1000000; ++op)
    {
        const uint32_t what = rng.next() % 100;
        const int x = static_cast<int>(rng.next() % 100000);
        if (what < 30 && m.n < 500)
        {
            d.push_back(x);
            m.insert(m.n, x);
        }
        else if (what < 55 && m.n < 500)
        {
            d.push_front(x);
            m.insert(0, x);
        }
        else if (what < 70 && m.n)
        {
            d.pop_front();
            m.erase(0, 1);
        }
        else if (what < 85 && m.n)
        {
            d.pop_back();
            m.erase(m.n - 1, 1);
        }
        else if (what < 88)
        {
            const size_t n = rng.next() % 450;
            d.resize(n, x);
            for (size_t i = m.n; i < n; ++i)
                m.a[i] = x;
            m.n = n;
        }
        else if (what < 90)
        {
            ct::Deque<int> c(d);
            d = static_cast<ct::Deque<int> &&>(c);
        }
        else if (what < 91)
        {
            d.clear();
            m.n = 0;
        }
        if (op % 7 == 0)
        {
            ASSERT_EQ(d.size(), m.n) << op;
            if (m.n)
            {
                ASSERT_EQ(d.front(), m.a[0]);
                ASSERT_EQ(d.back(), m.a[m.n - 1]);
            }
            for (size_t i = 0; i < m.n; i += 1 + m.n / 16)
                ASSERT_EQ(d[i], m.a[i]) << op;
        }
    }
}

TEST(EdgeDifferential, StringAgainstCharArrayModel)
{
    Lcg rng(123);
    ct::String s;
    char model[400];
    size_t n = 0;
    for (int op = 0; op < 1000000; ++op)
    {
        const uint32_t what = rng.next() % 100;
        const char c = static_cast<char>('a' + rng.next() % 26);
        if (what < 25 && n < 300)
        {
            s.push_back(c);
            model[n++] = c;
        }
        else if (what < 40 && n < 300)
        {
            char piece[8];
            const size_t len = rng.next() % 8;
            for (size_t i = 0; i < len; ++i)
                piece[i] = static_cast<char>('A' + rng.next() % 26);
            s.append(piece, len);
            memcpy(model + n, piece, len);
            n += len;
        }
        else if (what < 55 && n < 300)
        {
            const size_t pos = rng.next() % (n + 1);
            char piece[6];
            const size_t len = 1 + rng.next() % 6;
            for (size_t i = 0; i < len; ++i)
                piece[i] = static_cast<char>('0' + rng.next() % 10);
            s.insert(pos, piece, len);
            memmove(model + pos + len, model + pos, n - pos);
            memcpy(model + pos, piece, len);
            n += len;
        }
        else if (what < 70 && n)
        {
            const size_t pos = rng.next() % n;
            const size_t len = rng.next() % (n - pos + 1);
            s.erase(pos, len);
            memmove(model + pos, model + pos + len, n - pos - len);
            n -= len;
        }
        else if (what < 75 && n)
        {
            s.pop_back();
            --n;
        }
        else if (what < 80)
        {
            const size_t nn = rng.next() % 200;
            s.resize(nn, c);
            for (size_t i = n; i < nn; ++i)
                model[i] = c;
            n = nn;
        }
        else if (what < 83 && n && n < 150)
        {
            s.append(s);
            memcpy(model + n, model, n);
            n *= 2;
        }
        else if (what < 86)
        {
            ct::String t(s);
            ct::String u(static_cast<ct::String &&>(t));
            s = static_cast<ct::String &&>(u);
        }
        else if (what < 88)
        {
            s.clear();
            n = 0;
        }
        else if (what < 92 && n)
        {
            const size_t pos = rng.next() % n;
            const size_t len = rng.next() % (n - pos + 1);
            ct::String sub = s.substr(pos, len);
            ASSERT_EQ(sub.size(), len);
            ASSERT_EQ(memcmp(sub.data(), model + pos, len), 0);
        }
        else if (what < 96)
        {
            const size_t at = s.find(c);
            size_t want = ct::String::npos;
            for (size_t i = 0; i < n; ++i)
                if (model[i] == c)
                {
                    want = i;
                    break;
                }
            ASSERT_EQ(at, want);
        }
        if (op % 5 == 0)
        {
            ASSERT_EQ(s.size(), n) << op;
            ASSERT_EQ(memcmp(s.data(), model, n), 0) << op;
            ASSERT_EQ(s.c_str()[n], '\0') << op;
        }
    }
}

TEST(EdgeDifferential, TreeMapAndFlatMapAgainstDirectArray)
{
    const int keys = 2048;
    ct::Vector<char> has;
    ct::Vector<int> val;
    has.resize(keys);
    val.resize(keys);
    ct::TreeMap<int, int> t;
    ct::FlatMap<int, int> f;
    size_t count = 0;
    Lcg rng(555);
    for (int op = 0; op < 1000000; ++op)
    {
        const int k = static_cast<int>(rng.next() % keys);
        const uint32_t what = rng.next() % 100;
        if (what < 40)
        {
            const int v = static_cast<int>(rng.next());
            t.put(k, v);
            f.put(k, v);
            if (!has[k])
                ++count;
            has[k] = 1;
            val[k] = v;
        }
        else if (what < 70)
        {
            const bool had = has[k] != 0;
            ASSERT_EQ(t.erase(k), had);
            ASSERT_EQ(f.erase(k), had);
            if (had)
                --count;
            has[k] = 0;
        }
        else if (what < 98)
        {
            int *p = t.find(k);
            int *q = f.find(k);
            ASSERT_EQ(p != nullptr, has[k] != 0);
            ASSERT_EQ(q != nullptr, has[k] != 0);
            if (p)
            {
                ASSERT_EQ(*p, val[k]);
                ASSERT_EQ(*q, val[k]);
            }
        }
        else if (what == 98)
        {
            ct::TreeMap<int, int> tc(t);
            ct::FlatMap<int, int> fc(f);
            ASSERT_EQ(tc.size(), count);
            ASSERT_EQ(fc.size(), count);
            if (op % 4 == 0)
            {
                t = tc;
                f = fc;
            }
        }
        else
        {
            int prev = -1;
            size_t n = 0;
            for (auto &e : t)
            {
                ASSERT_GT(e.key, prev);
                ASSERT_TRUE(has[e.key] != 0);
                ASSERT_EQ(e.value, val[e.key]);
                prev = e.key;
                ++n;
            }
            ASSERT_EQ(n, count);
            prev = -1;
            n = 0;
            for (auto &e : f)
            {
                ASSERT_GT(e.key, prev);
                ASSERT_EQ(e.value, val[e.key]);
                prev = e.key;
                ++n;
            }
            ASSERT_EQ(n, count);
            ASSERT_TRUE(t.validate());
            if (op % 30000 == 99)
            {
                t.clear();
                f = ct::FlatMap<int, int>();
                for (int i = 0; i < keys; ++i)
                    has[i] = 0;
                count = 0;
            }
        }
    }
}

TEST(EdgeSelfReference, SelfAssignmentOfEveryContainerKeepsTheContents)
{
    ct::Vector<ct::String> v;
    ct::Deque<ct::String> d;
    ct::HashMap<int, ct::String> h;
    ct::HashSet<int> hs;
    ct::TreeMap<int, ct::String> t;
    ct::FlatMap<int, ct::String> f;
    ct::String s("a string long enough to live on the heap, not in the small buffer");
    for (int i = 0; i < 50; ++i)
    {
        v.push_back(s);
        d.push_back(s);
        h.put(i, s);
        hs.insert(i);
        t.put(i, s);
        f.put(i, s);
    }
    ct::Vector<ct::String> &vr = v;
    ct::Deque<ct::String> &dr = d;
    ct::HashMap<int, ct::String> &hr = h;
    ct::HashSet<int> &hsr = hs;
    ct::TreeMap<int, ct::String> &tr = t;
    ct::FlatMap<int, ct::String> &fr = f;
    ct::String &sr = s;
    v = vr;
    d = dr;
    h = hr;
    hs = hsr;
    t = tr;
    f = fr;
    s = sr;
    v = static_cast<ct::Vector<ct::String> &&>(vr);
    d = static_cast<ct::Deque<ct::String> &&>(dr);
    h = static_cast<ct::HashMap<int, ct::String> &&>(hr);
    hs = static_cast<ct::HashSet<int> &&>(hsr);
    t = static_cast<ct::TreeMap<int, ct::String> &&>(tr);
    f = static_cast<ct::FlatMap<int, ct::String> &&>(fr);
    s = static_cast<ct::String &&>(sr);
    ASSERT_EQ(v.size(), 50u);
    ASSERT_EQ(d.size(), 50u);
    ASSERT_EQ(h.size(), 50u);
    ASSERT_EQ(hs.size(), 50u);
    ASSERT_EQ(t.size(), 50u);
    ASSERT_EQ(f.size(), 50u);
    ASSERT_EQ(s.size(), strlen("a string long enough to live on the heap, not in the small buffer"));
    for (int i = 0; i < 50; ++i)
    {
        ASSERT_TRUE(v[static_cast<size_t>(i)] == s);
        ASSERT_TRUE(d[static_cast<size_t>(i)] == s);
        ASSERT_TRUE(*h.find(i) == s);
        ASSERT_TRUE(hs.contains(i));
        ASSERT_TRUE(*t.find(i) == s);
        ASSERT_TRUE(*f.find(i) == s);
    }
}
