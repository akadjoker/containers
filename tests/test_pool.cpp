#include <ct/pool.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

namespace
{

    struct Bullet 
    {
        float x, y, vx, vy;
        int damage;
    };

    struct PTracked
    {
        static int live;
        int value;
        explicit PTracked(int v) : value(v) { ++live; }
        ~PTracked() { --live; }
    };
    int PTracked::live = 0;

    bool is_aligned(const void *p, std::size_t align)
    {
        return (reinterpret_cast<std::uintptr_t>(p) & (align - 1)) == 0;
    }

} 

TEST(Pool, AllocateGivesDistinctAlignedSlots)
{
    ct::Pool<Bullet> pool;
    std::set<Bullet *> seen;
    for (int i = 0; i < 10000; ++i)
    {
        Bullet *b = pool.allocate();
        ASSERT_TRUE(is_aligned(b, alignof(Bullet)));
        ASSERT_TRUE(seen.insert(b).second) << "slot repetido sem deallocate";
    }
    EXPECT_EQ(pool.live(), 10000u);
    EXPECT_GE(pool.capacity(), 10000u);
}

TEST(Pool, NoOverlapPatternCheck)
{
    ct::Pool<Bullet> pool;
    std::vector<Bullet *> all;
    for (int i = 0; i < 5000; ++i)
    {
        Bullet *b = pool.allocate();
        b->damage = i; 
        b->x = float(i);
        all.push_back(b);
    }
    for (int i = 0; i < 5000; ++i)
    {
        ASSERT_EQ(all[i]->damage, i) << "slots sobrepostos";
        ASSERT_EQ(all[i]->x, float(i));
    }
}

TEST(Pool, DeallocateRecyclesLIFO)
{
    ct::Pool<Bullet> pool;
    Bullet *a = pool.allocate();
    Bullet *b = pool.allocate();
    pool.deallocate(b);
    pool.deallocate(a);

    EXPECT_EQ(pool.allocate(), a);
    EXPECT_EQ(pool.allocate(), b);
    EXPECT_EQ(pool.live(), 2u);
}

TEST(Pool, ReuseKeepsCapacityStable)
{
    ct::Pool<Bullet> pool;
    std::vector<Bullet *> v;
    for (int i = 0; i < 1000; ++i)
        v.push_back(pool.allocate());
    std::size_t cap = pool.capacity();

    for (int gen = 0; gen < 100; ++gen)
    {
        for (Bullet *b : v)
            pool.deallocate(b);
        v.clear();
        for (int i = 0; i < 1000; ++i)
            v.push_back(pool.allocate());
    }
    EXPECT_EQ(pool.capacity(), cap) << "reciclar não deve reservar mais memória";
    EXPECT_EQ(pool.live(), 1000u);
}

TEST(Pool, InterleavedChurnAgainstReference)
{

    ct::Pool<std::uint64_t> pool;
    std::vector<std::uint64_t *> alive;
    unsigned seed = 99;
    std::uint64_t stamp = 0;
    for (int i = 0; i < 100000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        if (alive.empty() || (seed & 3) != 0) 
        {
            std::uint64_t *p = pool.allocate();
            *p = ++stamp; 
            alive.push_back(p);
        }
        else
        {
            std::size_t idx = (seed >> 8) % alive.size();

            std::uint64_t *p = alive[idx];
            ASSERT_NE(*p, 0u);
            *p = 0;
            pool.deallocate(p);
            alive[idx] = alive.back();
            alive.pop_back();
        }
        ASSERT_EQ(pool.live(), alive.size());
    }

    for (std::uint64_t *p : alive)
        ASSERT_NE(*p, 0u);
}

TEST(Pool, CreateDestroyCallCtorsWhenWanted)
{
    PTracked::live = 0;
    ct::Pool<PTracked> pool;
    std::vector<PTracked *> v;
    for (int i = 0; i < 100; ++i)
        v.push_back(pool.create(i));
    EXPECT_EQ(PTracked::live, 100);
    EXPECT_EQ(v[42]->value, 42);
    for (PTracked *p : v)
        pool.destroy(p);
    EXPECT_EQ(PTracked::live, 0);
    EXPECT_EQ(pool.live(), 0u);
}

TEST(Pool, ClearReusesChunks)
{
    ct::Pool<Bullet> pool;
    for (int i = 0; i < 5000; ++i)
        pool.allocate();
    std::size_t cap = pool.capacity();
    pool.clear();
    EXPECT_EQ(pool.live(), 0u);
    EXPECT_EQ(pool.capacity(), cap) << "clear mantém os chunks";
    std::set<Bullet *> seen;
    for (int i = 0; i < 5000; ++i)
        ASSERT_TRUE(seen.insert(pool.allocate()).second);
    EXPECT_EQ(pool.capacity(), cap) << "refill não devia pedir mais chunks";
}

TEST(Pool, TinyTypeSlotAtLeastPointerSize)
{
    ct::Pool<char> pool; 
    EXPECT_GE(ct::Pool<char>::slot_size(), sizeof(void *));
    char *a = pool.allocate();
    char *b = pool.allocate();
    *a = 'x';
    *b = 'y';
    pool.deallocate(a);
    char *c = pool.allocate(); 
    (void)c;
    EXPECT_EQ(*b, 'y');
}

TEST(Pool, OverAlignedType)
{
    struct alignas(32) Wide
    {
        float m[8];
    };
    ct::Pool<Wide> pool;
    for (int i = 0; i < 100; ++i)
        ASSERT_TRUE(is_aligned(pool.allocate(), 32));
}

TEST(Pool, BigObjectSmallChunk)
{
    struct Big
    {
        char data[10000];
    };
    ct::Pool<Big> pool(2); 
    Big *a = pool.allocate();
    Big *b = pool.allocate();
    Big *c = pool.allocate(); 
    std::memset(a->data, 1, sizeof(a->data));
    std::memset(b->data, 2, sizeof(b->data));
    std::memset(c->data, 3, sizeof(c->data));
    EXPECT_EQ(a->data[9999], 1);
    EXPECT_EQ(b->data[9999], 2);
    EXPECT_EQ(c->data[9999], 3);
    EXPECT_EQ(pool.capacity(), 4u);
}