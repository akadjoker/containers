// ct::sort com particao por blocos (tipos pequenos e trivialmente copiaveis) e ct::stable_sort com fusao sem salto.
// Este ficheiro nao usa a biblioteca standard (so o gtest e cabecalhos C).
#include <ct/sort.hpp>
#include <ct/string.hpp>
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

    struct Rec // 16 bytes, trivialmente copiavel: vai pelos blocos; idx mostra a estabilidade
    {
        int key;
        int idx;
    };
    struct Rec48 // 48 bytes: acima do limite dos blocos, vai pela particao de Hoare
    {
        int key;
        int idx;
        char pad[40];
    };
    template <typename T>
    bool key_less(const T &a, const T &b) { return a.key < b.key; }

    uint64_t g_cmps = 0;

    int pattern_value(int pattern, int i, int n, Lcg &rng)
    {
        switch (pattern)
        {
        case 0: return static_cast<int>(rng.next());
        case 1: return i;
        case 2: return n - i;
        case 3: return static_cast<int>(rng.next() & 15u);
        case 4: return 7;
        case 5: return i < n / 2 ? i : n - i;     // sino
        case 6: return (i * 37) % 101;           // dente de serra
        case 7: return static_cast<int>(rng.next() & 1u);
        default: return i % 2 ? i : n - i;
        }
    }

    template <typename T>
    void fill(ct::Vector<T> &v, int pattern, int n, Lcg &rng)
    {
        v.clear();
        for (int i = 0; i < n; ++i)
        {
            T t;
            memset(static_cast<void *>(&t), 0, sizeof t);
            t.key = pattern_value(pattern, i, n, rng);
            t.idx = i;
            v.push_back(t);
        }
    }

    template <typename T>
    void check_sorted_and_permutation(const ct::Vector<T> &v, uint64_t sum_keys, uint64_t sum_idx, bool stable, const char *what)
    {
        uint64_t sk = 0, si = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            sk += static_cast<uint64_t>(v[i].key);
            si += static_cast<uint64_t>(v[i].idx) * 2654435761u;
            if (i)
            {
                ASSERT_LE(v[i - 1].key, v[i].key) << what << " posicao " << i;
                if (stable && v[i - 1].key == v[i].key)
                    ASSERT_LT(v[i - 1].idx, v[i].idx) << what << " estabilidade na posicao " << i;
            }
        }
        ASSERT_EQ(sk, sum_keys) << what;
        ASSERT_EQ(si, sum_idx) << what;
    }

    template <typename T>
    void sums(const ct::Vector<T> &v, uint64_t &sk, uint64_t &si)
    {
        sk = si = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            sk += static_cast<uint64_t>(v[i].key);
            si += static_cast<uint64_t>(v[i].idx) * 2654435761u;
        }
    }

    template <typename T>
    void sort_all_sizes(const char *name)
    {
        Lcg rng(99);
        ct::Vector<T> v;
        static const int sizes[] = {0, 1, 2, 3, 23, 24, 25, 63, 64, 65, 126, 127, 128, 129, 130, 191, 192, 193, 255, 256, 257,
                                    319, 320, 321, 511, 512, 513, 1000, 1023, 1024, 1025, 4097, 20000};
        for (int pattern = 0; pattern < 9; ++pattern)
            for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k)
            {
                const int n = sizes[k];
                fill(v, pattern, n, rng);
                uint64_t sk, si;
                sums(v, sk, si);
                ct::sort(v.begin(), v.end(), key_less<T>);
                check_sorted_and_permutation(v, sk, si, false, name);
                fill(v, pattern, n, rng);
                sums(v, sk, si);
                ct::stable_sort(v.begin(), v.end(), key_less<T>);
                check_sorted_and_permutation(v, sk, si, true, name);
            }
        // todos os tamanhos pequenos e uma amostra dos medios, com cada padrao
        for (int pattern = 0; pattern < 9; ++pattern)
            for (int n = 0; n < 700; n += (n < 140 ? 1 : 13))
            {
                fill(v, pattern, n, rng);
                uint64_t sk, si;
                sums(v, sk, si);
                ct::sort(v.begin(), v.end(), key_less<T>);
                check_sorted_and_permutation(v, sk, si, false, name);
                fill(v, pattern, n, rng);
                sums(v, sk, si);
                ct::stable_sort(v.begin(), v.end(), key_less<T>);
                check_sorted_and_permutation(v, sk, si, true, name);
            }
    }

    bool counted_key_less(const Rec &a, const Rec &b)
    {
        ++g_cmps;
        return a.key < b.key;
    }
}

TEST(SortBlocks, AllPatternsAndSizesAroundTheBlockBoundariesTriviallyCopyable)
{
    sort_all_sizes<Rec>("Rec");
}

TEST(SortBlocks, AllPatternsAndSizesWithTheHoareFallbackForLargeElements)
{
    sort_all_sizes<Rec48>("Rec48");
}

TEST(SortBlocks, IntegersWithACustomComparatorUseTheBlocksToo)
{
    Lcg rng(5);
    ct::Vector<int> v;
    for (int n : {100, 129, 1000, 65537})
    {
        v.clear();
        for (int i = 0; i < n; ++i)
            v.push_back(static_cast<int>(rng.next() % 1000) - 500);
        ct::sort(v.begin(), v.end(), [](int a, int b) { return a > b; }); // decrescente
        for (size_t i = 1; i < v.size(); ++i)
            ASSERT_GE(v[i - 1], v[i]);
    }
}

// A particao leva os iguais ao pivo todos para um lado de uma vez: poucos valores distintos ou todos iguais sao O(n log k).
TEST(SortBlocks, DuplicatesAreHandledInLinearishTime)
{
    Lcg rng(1);
    ct::Vector<Rec> v;
    const int n = 200000;
    const int patterns[] = {4, 7, 3};
    const uint64_t limits[] = {3u * n, 4u * n, 12u * n}; // todos iguais, dois valores, 16 valores
    for (int p = 0; p < 3; ++p)
    {
        fill(v, patterns[p], n, rng);
        g_cmps = 0;
        ct::sort(v.begin(), v.end(), counted_key_less);
        EXPECT_LT(g_cmps, limits[p]) << "padrao " << patterns[p];
        for (size_t i = 1; i < v.size(); ++i)
            ASSERT_LE(v[i - 1].key, v[i].key);
    }
}

// Ordenada ou ao contrario: uma passagem (como antes), e na ordenacao estavel tambem
TEST(SortBlocks, SortedAndReversedStillCostOnePassInBothSorts)
{
    Lcg rng(1);
    ct::Vector<Rec> v;
    for (int stable = 0; stable < 2; ++stable)
    {
        fill(v, 1, 100000, rng);
        g_cmps = 0;
        stable ? ct::stable_sort(v.begin(), v.end(), counted_key_less) : ct::sort(v.begin(), v.end(), counted_key_less);
        EXPECT_LE(g_cmps, 100000u);
        fill(v, 2, 100000, rng);
        g_cmps = 0;
        stable ? ct::stable_sort(v.begin(), v.end(), counted_key_less) : ct::sort(v.begin(), v.end(), counted_key_less);
        EXPECT_LE(g_cmps, 200000u);
        for (size_t i = 1; i < v.size(); ++i)
            ASSERT_LE(v[i - 1].key, v[i].key);
    }
}

// Um comparador que nao e uma ordem fraca estrita nao pode fazer ler fora do intervalo (corre com ASan): o resultado nao
// e garantido, a memoria sim. Vale para a particao por blocos, a de Hoare e as duas fusoes.
TEST(SortBlocks, BrokenComparatorsStayInBounds)
{
    Lcg rng(77);
    ct::Vector<Rec> v;
    ct::Vector<Rec48> w;
    for (int n : {5, 24, 25, 129, 300, 1000, 5000})
    {
        for (int which = 0; which < 4; ++which)
        {
            fill(v, 0, n, rng);
            fill(w, 0, n, rng);
            struct Cmp
            {
                int which;
                bool operator()(const Rec &a, const Rec &b) const
                {
                    return which == 0 ? a.key <= b.key : which == 1 ? true : which == 2 ? false : (a.idx & 1) != (b.idx & 1);
                }
                bool operator()(const Rec48 &a, const Rec48 &b) const
                {
                    return which == 0 ? a.key <= b.key : which == 1 ? true : which == 2 ? false : (a.idx & 1) != (b.idx & 1);
                }
            } cmp = {which};
            ct::sort(v.begin(), v.end(), cmp);
            ct::sort(w.begin(), w.end(), cmp);
            fill(v, 0, n, rng);
            ct::stable_sort(v.begin(), v.end(), cmp);
            ASSERT_EQ(v.size(), static_cast<size_t>(n));
        }
    }
}

TEST(SortBlocks, StableSortKeepsEqualElementsInOrderAtEveryRunLength)
{
    Lcg rng(31);
    ct::Vector<Rec> v;
    for (int n = 20; n < 400; ++n)
    {
        fill(v, 3, n, rng); // 16 chaves distintas: muitos iguais
        uint64_t sk, si;
        sums(v, sk, si);
        ct::stable_sort(v.begin(), v.end(), key_less<Rec>);
        check_sorted_and_permutation(v, sk, si, true, "estavel");
    }
    // dados ao acaso (fusao sem salto) e em blocos (fusao com salto), com tamanhos que nao sao potencias de 2
    for (int n : {1000, 4097, 33333})
        for (int pattern : {0, 3, 5, 6})
        {
            fill(v, pattern, n, rng);
            uint64_t sk, si;
            sums(v, sk, si);
            ct::stable_sort(v.begin(), v.end(), key_less<Rec>);
            check_sorted_and_permutation(v, sk, si, true, "estavel grande");
        }
}

TEST(SortBlocks, StringsAndDequeIteratorsStillWork)
{
    Lcg rng(8);
    ct::Vector<ct::String> v;
    for (int i = 0; i < 3000; ++i)
    {
        char b[32];
        snprintf(b, sizeof b, "k%07u", rng.next() % 100000);
        v.push_back(ct::String(b));
    }
    ct::sort(v.begin(), v.end(), [](const ct::String &a, const ct::String &b) { return a < b; });
    for (size_t i = 1; i < v.size(); ++i)
        ASSERT_FALSE(v[i] < v[i - 1]);
    ct::stable_sort(v.begin(), v.end(), [](const ct::String &a, const ct::String &b) { return b < a; });
    for (size_t i = 1; i < v.size(); ++i)
        ASSERT_FALSE(v[i - 1] < v[i]);
}
