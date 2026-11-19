// Vector::resize_for_overwrite e append_uninitialized: o que ja la esta fica, a capacidade cresce como no resize, e os
// elementos novos de um tipo trivial nao sao escritos pelo contentor (um alocador que enche a memoria com 0xCD mostra-o).
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // malloc que enche o que entrega (e o que o realloc acrescenta) com 0xCD
    struct PatternAlloc
    {
        void *allocate(size_t bytes, size_t)
        {
            void *p = malloc(bytes ? bytes : 1);
            memset(p, 0xCD, bytes);
            return p;
        }
        void deallocate(void *p, size_t) { free(p); }
        void *reallocate(void *p, size_t old_bytes, size_t new_bytes, size_t align)
        {
            void *q = allocate(new_bytes, align);
            memcpy(q, p, old_bytes < new_bytes ? old_bytes : new_bytes);
            free(p);
            return q;
        }
    };

    struct Pod
    {
        int a;
        double b;
        char c[5];
    };

    struct Counted
    {
        static int live;
        static int constructed;
        int v;
        Counted() : v(42)
        {
            ++live;
            ++constructed;
        }
        Counted(const Counted &o) : v(o.v)
        {
            ++live;
            ++constructed;
        }
        Counted &operator=(const Counted &) = default;
        ~Counted() { --live; }
    };
    int Counted::live = 0;
    int Counted::constructed = 0;

    uint64_t next_rand(uint64_t &s)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }

    template <typename T>
    T make(int i);
    template <>
    int make<int>(int i)
    {
        return i * 7 + 1;
    }
    template <>
    Pod make<Pod>(int i)
    {
        Pod p;
        p.a = i;
        p.b = i * 0.5;
        memset(p.c, 'a' + i % 26, 5);
        return p;
    }
    template <>
    ct::String make<ct::String>(int i)
    {
        return ct::String("elemento numero ") + ct::String::number(i);
    }
    bool same(int a, int b) { return a == b; }
    bool same(const Pod &a, const Pod &b) { return a.a == b.a && a.b == b.b && memcmp(a.c, b.c, 5) == 0; }
    bool same(const ct::String &a, const ct::String &b) { return a == b; }

    // resize e resize_for_overwrite tem o mesmo tamanho e a mesma capacidade a cada passo, e o que ja la estava fica
    template <typename T>
    void check_capacity_rules(uint64_t seed)
    {
        for (int start = 0; start <= 70; ++start)
        {
            ct::Vector<T> a, b;
            for (int i = 0; i < start; ++i)
            {
                a.push_back(make<T>(i));
                b.push_back(make<T>(i));
            }
            uint64_t s = seed + static_cast<uint64_t>(start);
            for (int step = 0; step < 40; ++step)
            {
                const size_t old_size = a.size();
                const size_t n = static_cast<size_t>(next_rand(s) % 150);
                a.resize(n);
                b.resize_for_overwrite(n);
                ASSERT_EQ(a.size(), b.size()) << start << " " << step;
                ASSERT_EQ(a.capacity(), b.capacity()) << start << " " << step;
                const size_t keep = old_size < n ? old_size : n;
                for (size_t i = 0; i < keep; ++i)
                    ASSERT_TRUE(same(a[i], b[i])) << "elemento " << i << " depois de " << start << " " << step;
                // os novos de b escrevem-se antes de se ler (o resize de a ja os pos a zero)
                for (size_t i = keep; i < n; ++i)
                {
                    b[i] = make<T>(static_cast<int>(1000 + i));
                    a[i] = make<T>(static_cast<int>(1000 + i));
                }
            }
        }
    }
}

TEST(VectorOverwrite, CapacityRulesMatchResizeInt)
{
    check_capacity_rules<int>(11);
}

TEST(VectorOverwrite, CapacityRulesMatchResizePod)
{
    check_capacity_rules<Pod>(12);
}

TEST(VectorOverwrite, CapacityRulesMatchResizeString)
{
    check_capacity_rules<ct::String>(13);
}

// o contentor nao escreve nos elementos novos de um tipo trivial: ficam com o 0xCD do alocador, no crescimento
// por realloc e na capacidade que ja chegava
TEST(VectorOverwrite, TrivialNewElementsAreLeftAlone)
{
    ct::Vector<int, PatternAlloc> v;
    for (int i = 0; i < 5; ++i)
        v.push_back(i);
    size_t high = 5; // acima do maior tamanho ja atingido a memoria e a do alocador (0xCD); abaixo pode ter valores velhos
    for (size_t n : {5u, 6u, 9u, 8u, 100u, 17u, 100u, 1000u, 999u})
    {
        const size_t old = v.size();
        v.resize_for_overwrite(n);
        ASSERT_EQ(v.size(), n);
        for (size_t i = 0; i < 5 && i < n; ++i)
            ASSERT_EQ(v[i], static_cast<int>(i)) << i;
        for (size_t i = (high > old ? high : old); i < n; ++i)
            ASSERT_EQ(static_cast<uint32_t>(v[i]), 0xCDCDCDCDu) << "elemento " << i << " de " << n;
        for (size_t i = (old < n ? old : n); i < n; ++i)
            if (i >= 5)
                v[i] = static_cast<int>(i);
        if (n > high)
            high = n;
    }
    // resize (e o resize com valor) enchem
    ct::Vector<int, PatternAlloc> w;
    w.resize(10);
    for (int x : w)
        EXPECT_EQ(x, 0);

    ct::Vector<Pod, PatternAlloc> p;
    p.resize_for_overwrite(3);
    for (size_t i = 0; i < 3; ++i)
    {
        uint8_t raw[sizeof(Pod)];
        memcpy(raw, &p[i], sizeof(Pod));
        for (size_t k = 0; k < sizeof(Pod); ++k)
            ASSERT_EQ(raw[k], 0xCD) << i << " " << k;
    }
}

TEST(VectorOverwrite, AppendUninitialized)
{
    ct::Vector<int, PatternAlloc> v;
    int *p = v.append_uninitialized(0);
    EXPECT_EQ(v.size(), 0u);
    (void)p;
    for (int i = 0; i < 4; ++i)
        v.push_back(i);
    p = v.append_uninitialized(0);
    EXPECT_EQ(p, v.data() + 4);
    EXPECT_EQ(v.size(), 4u);
    const size_t cap_before = v.capacity();
    p = v.append_uninitialized(1000);
    ASSERT_EQ(v.size(), 1004u);
    EXPECT_EQ(p, v.data() + 4);
    EXPECT_GE(v.capacity(), 1004u);
    EXPECT_GT(v.capacity(), cap_before);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(v[static_cast<size_t>(i)], i);
    for (size_t i = 0; i < 1000; ++i)
    {
        ASSERT_EQ(static_cast<uint32_t>(p[i]), 0xCDCDCDCDu) << i;
        p[i] = static_cast<int>(i);
    }
    // a mesma capacidade que resize(size + n)
    ct::Vector<int> a, b;
    uint64_t s = 5;
    for (int step = 0; step < 300; ++step)
    {
        const size_t n = static_cast<size_t>(next_rand(s) % 40);
        a.resize(a.size() + n);
        b.append_uninitialized(n);
        ASSERT_EQ(a.size(), b.size());
        ASSERT_EQ(a.capacity(), b.capacity());
    }
    // strings: constroi por omissao (valida para qualquer T)
    ct::Vector<ct::String> st;
    ct::String *q = st.append_uninitialized(3);
    ASSERT_EQ(st.size(), 3u);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(q[i].empty());
    q[1] = "ok";
    EXPECT_TRUE(st[1] == "ok");
}

TEST(VectorOverwrite, NonTrivialTypeConstructsLikeResize)
{
    Counted::live = 0;
    Counted::constructed = 0;
    {
        ct::Vector<Counted> a, b;
        a.resize(7);
        const int after_resize = Counted::constructed;
        b.resize_for_overwrite(7);
        EXPECT_EQ(Counted::constructed - after_resize, after_resize);
        for (size_t i = 0; i < 7; ++i)
            EXPECT_EQ(b[i].v, 42);
        EXPECT_EQ(Counted::live, 14);
        b.resize_for_overwrite(3); // encolhe: destroi
        EXPECT_EQ(Counted::live, 10);
        b.resize_for_overwrite(3);
        EXPECT_EQ(Counted::live, 10);
        b.append_uninitialized(2);
        EXPECT_EQ(Counted::live, 12);
        EXPECT_EQ(b[4].v, 42);
    }
    EXPECT_EQ(Counted::live, 0);
}

TEST(VectorOverwrite, ShrinkAndSameSizeAreResize)
{
    ct::Vector<int> v;
    for (int i = 0; i < 10; ++i)
        v.push_back(i);
    const size_t cap = v.capacity();
    v.resize_for_overwrite(10);
    EXPECT_EQ(v.size(), 10u);
    EXPECT_EQ(v.capacity(), cap);
    v.resize_for_overwrite(4);
    EXPECT_EQ(v.size(), 4u);
    EXPECT_EQ(v.capacity(), cap);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(v[static_cast<size_t>(i)], i);
    v.resize_for_overwrite(0);
    EXPECT_TRUE(v.empty());
}

TEST(VectorOverwrite, HugeSizesAbort)
{
    ct::Vector<int> v;
    v.push_back(1);
    EXPECT_DEATH(v.resize_for_overwrite(v.max_size() + 1), "");
    EXPECT_DEATH(v.append_uninitialized(static_cast<size_t>(-1)), "");
}
