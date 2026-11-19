// PriorityQueue com a tecnica do buraco (um move por nivel, o pop desce ate a folha e sobe): a ordem de saida e a
// do modelo (um Vector ordenado) com duplicados, strings, um tipo so de move e a construcao a partir de um Vector.
#include <ct/priority_queue.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>

namespace
{
    struct MoveOnly
    {
        static int live;
        int key;
        int payload;
        MoveOnly() : key(0), payload(0) { ++live; }
        MoveOnly(int k, int p) : key(k), payload(p) { ++live; }
        MoveOnly(const MoveOnly &) = delete;
        MoveOnly &operator=(const MoveOnly &) = delete;
        MoveOnly(MoveOnly &&o) noexcept : key(o.key), payload(o.payload)
        {
            o.key = -1;
            ++live;
        }
        MoveOnly &operator=(MoveOnly &&o) noexcept
        {
            key = o.key;
            payload = o.payload;
            o.key = -1;
            return *this;
        }
        ~MoveOnly() { --live; }
    };
    int MoveOnly::live = 0;
    struct MoveOnlyLess
    {
        bool operator()(const MoveOnly &a, const MoveOnly &b) const { return a.key < b.key; }
    };

    uint64_t next_rand(uint64_t &s)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
} // namespace

TEST(PriorityQueueHole, IntsAgainstSortedModelWithDuplicates)
{
    uint64_t s = 99;
    for (int round = 0; round < 200; ++round)
    {
        ct::PriorityQueue<int> pq;
        ct::Vector<int> model;
        const int ops = 1 + static_cast<int>(next_rand(s) % 400);
        for (int i = 0; i < ops; ++i)
        {
            if (model.empty() || next_rand(s) % 3 != 0)
            {
                const int v = static_cast<int>(next_rand(s) % 50) - 25; // muitos repetidos
                pq.push(v);
                model.push_back(v);
            }
            else
            {
                int best = 0;
                for (size_t k = 1; k < model.size(); ++k)
                    if (model[k] > model[static_cast<size_t>(best)])
                        best = static_cast<int>(k);
                ASSERT_EQ(pq.top(), model[static_cast<size_t>(best)]);
                pq.pop();
                model.erase(model.begin() + best);
            }
            ASSERT_EQ(pq.size(), model.size());
        }
        // esvazia: sai por ordem decrescente
        int prev = 1000;
        while (!pq.empty())
        {
            ASSERT_LE(pq.top(), prev);
            prev = pq.top();
            pq.pop();
        }
    }
}

TEST(PriorityQueueHole, StringsWithCustomCompare)
{
    struct ByLength
    {
        bool operator()(const ct::String &a, const ct::String &b) const { return a.size() < b.size(); }
    };
    ct::PriorityQueue<ct::String, ByLength> pq;
    uint64_t s = 5;
    for (int i = 0; i < 500; ++i)
        pq.push(ct::String(static_cast<size_t>(next_rand(s) % 60), 'x'));
    size_t prev = 1000;
    while (!pq.empty())
    {
        ASSERT_LE(pq.top().size(), prev);
        prev = pq.top().size();
        pq.pop();
    }
}

TEST(PriorityQueueHole, MoveOnlyTypeKeepsEveryObjectAndAllPayloads)
{
    MoveOnly::live = 0;
    {
        ct::PriorityQueue<MoveOnly, MoveOnlyLess> pq;
        uint64_t s = 17;
        long sum_in = 0, sum_out = 0;
        for (int i = 0; i < 300; ++i)
        {
            const int key = static_cast<int>(next_rand(s) % 40);
            pq.emplace(key, i);
            sum_in += i;
        }
        EXPECT_EQ(MoveOnly::live, 300);
        int prev = 1000;
        while (!pq.empty())
        {
            ASSERT_LE(pq.top().key, prev);
            prev = pq.top().key;
            sum_out += pq.top().payload;
            pq.pop();
        }
        EXPECT_EQ(sum_in, sum_out);
        EXPECT_EQ(MoveOnly::live, 0);
    }
    EXPECT_EQ(MoveOnly::live, 0);
}

TEST(PriorityQueueHole, BuiltFromVectorAndSizes)
{
    for (int n = 0; n <= 70; ++n)
    {
        ct::Vector<int> v;
        uint64_t s = static_cast<uint64_t>(n) + 3;
        for (int i = 0; i < n; ++i)
            v.push_back(static_cast<int>(next_rand(s) % 100));
        ct::PriorityQueue<int> pq(v);
        ASSERT_EQ(pq.size(), static_cast<size_t>(n));
        int prev = 1000;
        for (int i = 0; i < n; ++i)
        {
            ASSERT_LE(pq.top(), prev);
            prev = pq.top();
            pq.pop();
        }
        ASSERT_TRUE(pq.empty());
    }
}
