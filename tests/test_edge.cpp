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
