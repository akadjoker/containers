#include <ct/flatmap.hpp>
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/treemap.hpp>
#include <ct/string.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    struct MTracked
    {
        static int live;
        int value;
        MTracked() : value(0) { ++live; }
        explicit MTracked(int v) : value(v) { ++live; }
        MTracked(const MTracked &o) : value(o.value) { ++live; }
        MTracked(MTracked &&o) noexcept : value(o.value) { ++live; }
        MTracked &operator=(const MTracked &) = default;
        MTracked &operator=(MTracked &&) = default;
        ~MTracked() { --live; }
    };
    int MTracked::live = 0;

    struct BadHash
    {
        std::uint64_t operator()(int k) const { return static_cast<std::uint64_t>(k % 4); }
    };
} 

TEST(HashMap, InsertFindBasic)
{
    ct::HashMap<int, int> m;
    EXPECT_TRUE(m.empty());
    m.put(1, 100);
    m.put(2, 200);
    m[3] = 300;
    EXPECT_EQ(m.size(), 3u);
    ASSERT_NE(m.find(2), nullptr);
    EXPECT_EQ(*m.find(2), 200);
    EXPECT_EQ(m.find(99), nullptr);
    EXPECT_TRUE(m.contains(3));
    EXPECT_EQ(m.get(3, -1), 300);
    EXPECT_EQ(m.get(99, -1), -1);
}

TEST(HashMap, PutReplaces)
{
    ct::HashMap<int, int> m;
    m.put(7, 1);
    m.put(7, 2);
    EXPECT_EQ(m.size(), 1u);
    EXPECT_EQ(*m.find(7), 2);
    m[7] += 10; 
    EXPECT_EQ(*m.find(7), 12);
}

TEST(HashMap, GrowthKeepsEverything)
{
    ct::HashMap<int, int> m;
    for (int i = 0; i < 100000; ++i)
        m.put(i, i * 3);
    EXPECT_EQ(m.size(), 100000u);
    for (int i = 0; i < 100000; ++i)
    {
        ASSERT_NE(m.find(i), nullptr) << i;
        ASSERT_EQ(*m.find(i), i * 3);
    }
    EXPECT_EQ(m.find(100001), nullptr);
}

TEST(HashMap, EraseBackwardShift)
{
    ct::HashMap<int, int> m;
    for (int i = 0; i < 1000; ++i)
        m.put(i, i);
    for (int i = 0; i < 1000; i += 2)
        EXPECT_TRUE(m.erase(i));
    EXPECT_FALSE(m.erase(0));
    EXPECT_EQ(m.size(), 500u);
    for (int i = 0; i < 1000; ++i)
    {
        if (i % 2)
            ASSERT_NE(m.find(i), nullptr) << i;
        else
            ASSERT_EQ(m.find(i), nullptr) << i;
    }
}

TEST(HashMap, CollisionHeavyFuzz)
{

    ct::HashMap<int, int, BadHash> m;
    std::unordered_map<int, int> ref;
    unsigned seed = 55;
    for (int i = 0; i < 20000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        int k = static_cast<int>(seed % 500);
        int op = (seed >> 8) % 3;
        if (op == 0)
        {
            m.put(k, i);
            ref[k] = i;
        }
        else if (op == 1)
        {
            EXPECT_EQ(m.erase(k), ref.erase(k) > 0);
        }
        else
        {
            int *v = m.find(k);
            auto it = ref.find(k);
            if (it == ref.end())
                ASSERT_EQ(v, nullptr);
            else
            {
                ASSERT_NE(v, nullptr);
                ASSERT_EQ(*v, it->second);
            }
        }
        ASSERT_EQ(m.size(), ref.size());
    }
}

TEST(HashMap, StringKeys)
{
    ct::HashMap<ct::String, int> m;
    for (int i = 0; i < 5000; ++i)
        m.put(ct::String("chave_") + ct::String::number(i), i);
    EXPECT_EQ(m.size(), 5000u);
    for (int i = 0; i < 5000; i += 7)
    {
        int *v = m.find(ct::String("chave_") + ct::String::number(i));
        ASSERT_NE(v, nullptr) << i;
        ASSERT_EQ(*v, i);
    }
    EXPECT_EQ(m.find(ct::String("nao_existe")), nullptr);
}

TEST(HashMap, IterationCoversAll)
{
    ct::HashMap<int, int> m;
    for (int i = 0; i < 777; ++i)
        m.put(i, i);
    std::vector<bool> seen(777, false);
    std::size_t n = 0;
    for (auto &e : m)
    {
        ASSERT_FALSE(seen[e.key]);
        seen[e.key] = true;
        ASSERT_EQ(e.value, e.key);
        ++n;
    }
    EXPECT_EQ(n, 777u);
}

TEST(HashMap, IterationEmptyMap)
{
    ct::HashMap<int, int> m;
    for (auto &e : m)
    {
        (void)e;
        FAIL() << "map vazio não devia iterar nada";
    }
}

TEST(HashMap, CopyAndMove)
{
    ct::HashMap<int, int> a;
    for (int i = 0; i < 100; ++i)
        a.put(i, i * 2);
    ct::HashMap<int, int> b(a);
    EXPECT_EQ(b.size(), 100u);
    b.put(0, 999);
    EXPECT_EQ(*a.find(0), 0) << "cópia independente";
    ct::HashMap<int, int> c(static_cast<ct::HashMap<int, int> &&>(a));
    EXPECT_EQ(c.size(), 100u);
    EXPECT_EQ(a.size(), 0u);
}

TEST(HashMap, NoLeaksWithNonTrivialValues)
{
    MTracked::live = 0;
    {
        ct::HashMap<int, MTracked> m;
        for (int i = 0; i < 1000; ++i)
            m.put(i, MTracked(i));
        for (int i = 0; i < 500; ++i)
            m.erase(i);
        m.put(2000, MTracked(1));
        ct::HashMap<int, MTracked> copy(m);
        m.clear();
        EXPECT_EQ(m.size(), 0u);
    }
    EXPECT_EQ(MTracked::live, 0);
}

TEST(HashMap, ReserveAvoidsRehash)
{
    ct::HashMap<int, int> m;
    m.reserve(1000);
    std::size_t cap = m.capacity();
    for (int i = 0; i < 1000; ++i)
        m.put(i, i);
    EXPECT_EQ(m.capacity(), cap) << "reserve devia evitar rehash";
}

TEST(HashMap, PutWithEntryReferencesDoesNotRehashOrDangle)
{
    ct::HashMap<ct::String, ct::String> m;
    for (int i = 0; i < 12; ++i)
        m.put(ct::String("key_") + ct::String::number(i),
              ct::String("value_") + ct::String::number(i));

    auto &entry = *m.begin();
    const ct::String key = entry.key;
    const ct::String value = entry.value;
    const std::size_t capacity = m.capacity();
    m.put(entry.key, entry.value);

    EXPECT_EQ(m.size(), 12u);
    EXPECT_EQ(m.capacity(), capacity);
    ASSERT_NE(m.find(key), nullptr);
    EXPECT_EQ(*m.find(key), value);

    auto &same_entry = *m.begin();
    const ct::String same_key = same_entry.key;
    ct::String &found = m[same_entry.key];
    EXPECT_EQ(m.size(), 12u);
    EXPECT_EQ(m.capacity(), capacity);
    EXPECT_EQ(found, *m.find(same_key));
}

TEST(FlatMap, InsertFindBasic)
{
    ct::FlatMap<int, int> m;
    m.put(5, 50);
    m.put(1, 10);
    m.put(3, 30);
    EXPECT_EQ(m.size(), 3u);
    ASSERT_NE(m.find(3), nullptr);
    EXPECT_EQ(*m.find(3), 30);
    EXPECT_EQ(m.find(2), nullptr);
    EXPECT_EQ(m.get(1, -1), 10);
    EXPECT_EQ(m.get(9, -1), -1);
}

TEST(FlatMap, IterationIsOrdered)
{
    ct::FlatMap<int, int> m;
    int keys[] = {42, 7, 100, 1, 55, 3, 99, 0, -5};
    for (int k : keys)
        m.put(k, k * 10);
    int prev = -1000000;
    std::size_t n = 0;
    for (auto &e : m)
    {
        ASSERT_LT(prev, e.key) << "iteração tem de ser por ordem crescente";
        ASSERT_EQ(e.value, e.key * 10);
        prev = e.key;
        ++n;
    }
    EXPECT_EQ(n, 9u);
}

TEST(FlatMap, FuzzVsStdMap)
{
    ct::FlatMap<int, int> m;
    std::map<int, int> ref;
    unsigned seed = 77;
    for (int i = 0; i < 20000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        int k = static_cast<int>(seed % 800);
        int op = (seed >> 8) % 3;
        if (op == 0)
        {
            m.put(k, i);
            ref[k] = i;
        }
        else if (op == 1)
        {
            ASSERT_EQ(m.erase(k), ref.erase(k) > 0);
        }
        else
        {
            int *v = m.find(k);
            auto it = ref.find(k);
            if (it == ref.end())
                ASSERT_EQ(v, nullptr);
            else
            {
                ASSERT_NE(v, nullptr);
                ASSERT_EQ(*v, it->second);
            }
        }
        ASSERT_EQ(m.size(), ref.size());
    }

    auto it = ref.begin();
    for (auto &e : m)
    {
        ASSERT_EQ(e.key, it->first);
        ASSERT_EQ(e.value, it->second);
        ++it;
    }
}

TEST(FlatMap, StringKeysOrdered)
{
    ct::FlatMap<ct::String, int> m;
    m.put(ct::String("banana"), 2);
    m.put(ct::String("ananas"), 1);
    m.put(ct::String("cereja"), 3);
    auto it = m.begin();
    EXPECT_TRUE(it->key == "ananas");
    ++it;
    EXPECT_TRUE(it->key == "banana");
    ++it;
    EXPECT_TRUE(it->key == "cereja");
    EXPECT_EQ(*m.find(ct::String("banana")), 2);
}

TEST(FlatMap, LowerBoundRangeQuery)
{
    ct::FlatMap<int, int> m;
    for (int k : {10, 20, 30, 40, 50})
        m.put(k, k);
    auto it = m.lower_bound_it(25); 
    ASSERT_NE(it, m.end());
    EXPECT_EQ(it->key, 30);

    int sum = 0;
    for (; it != m.end(); ++it)
        sum += it->key;
    EXPECT_EQ(sum, 30 + 40 + 50);
}

TEST(FlatMap, OperatorBracketAndErase)
{
    ct::FlatMap<int, int> m;
    m[5] = 55;
    m[5] += 1;
    EXPECT_EQ(*m.find(5), 56);
    EXPECT_TRUE(m.erase(5));
    EXPECT_FALSE(m.erase(5));
    EXPECT_TRUE(m.empty());
}

TEST(FlatMap, NoLeaks)
{
    MTracked::live = 0;
    {
        ct::FlatMap<int, MTracked> m;
        for (int i = 0; i < 500; ++i)
            m.put(i * 7 % 500, MTracked(i));
        for (int i = 0; i < 250; ++i)
            m.erase(i);
        m.clear();
    }
    EXPECT_EQ(MTracked::live, 0);
}

TEST(TreeMap, InsertFindBasic)
{
    ct::TreeMap<int, int> m;
    EXPECT_TRUE(m.empty());
    m.put(5, 50);
    m.put(1, 10);
    m[3] = 30;
    EXPECT_EQ(m.size(), 3u);
    ASSERT_NE(m.find(3), nullptr);
    EXPECT_EQ(*m.find(3), 30);
    EXPECT_EQ(m.find(2), nullptr);
    EXPECT_EQ(m.get(1, -1), 10);
    EXPECT_EQ(m.get(9, -1), -1);
    m.put(5, 99); 
    EXPECT_EQ(*m.find(5), 99);
    EXPECT_EQ(m.size(), 3u);
    EXPECT_TRUE(m.validate());
}

TEST(TreeMap, IterationIsOrdered)
{
    ct::TreeMap<int, int> m;
    int keys[] = {42, 7, 100, 1, 55, 3, 99, 0, -5};
    for (int k : keys)
        m.put(k, k * 10);
    int prev = -1000000;
    std::size_t n = 0;
    for (auto &e : m)
    {
        ASSERT_LT(prev, e.key);
        ASSERT_EQ(e.value, e.key * 10);
        prev = e.key;
        ++n;
    }
    EXPECT_EQ(n, 9u);
    EXPECT_TRUE(m.validate());
}

TEST(TreeMap, SequentialInsertStaysBalanced)
{

    ct::TreeMap<int, int> m;
    for (int i = 0; i < 100000; ++i)
        m.put(i, i);
    EXPECT_TRUE(m.validate()) << "invariantes red-black violados";
    EXPECT_EQ(m.size(), 100000u);
    for (int i = 0; i < 100000; i += 997)
        ASSERT_EQ(*m.find(i), i);
}

TEST(TreeMap, FuzzVsStdMapWithValidation)
{
    ct::TreeMap<int, int> m;
    std::map<int, int> ref;
    unsigned seed = 91;
    for (int i = 0; i < 20000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        int k = static_cast<int>(seed % 800);
        int op = (seed >> 8) % 3;
        if (op == 0)
        {
            m.put(k, i);
            ref[k] = i;
        }
        else if (op == 1)
        {
            ASSERT_EQ(m.erase(k), ref.erase(k) > 0);
        }
        else
        {
            int *v = m.find(k);
            auto it = ref.find(k);
            if (it == ref.end())
                ASSERT_EQ(v, nullptr);
            else
            {
                ASSERT_NE(v, nullptr);
                ASSERT_EQ(*v, it->second);
            }
        }
        ASSERT_EQ(m.size(), ref.size());
        if (i % 1000 == 0)
        {
            ASSERT_TRUE(m.validate()) << "invariantes RB violados na op " << i;
        }
    }
    EXPECT_TRUE(m.validate());
    auto it = ref.begin();
    for (auto &e : m)
    {
        ASSERT_EQ(e.key, it->first);
        ASSERT_EQ(e.value, it->second);
        ++it;
    }
}

TEST(TreeMap, LowerBound)
{
    ct::TreeMap<int, int> m;
    for (int k : {10, 20, 30, 40, 50})
        m.put(k, k);
    auto it = m.lower_bound_it(25);
    ASSERT_NE(it, m.end());
    EXPECT_EQ(it->key, 30);
    int sum = 0;
    for (; it != m.end(); ++it)
        sum += it->key;
    EXPECT_EQ(sum, 120);
    EXPECT_EQ(m.lower_bound_it(51), m.end());
}

TEST(TreeMap, StringKeysOrdered)
{
    ct::TreeMap<ct::String, int> m;
    m.put(ct::String("banana"), 2);
    m.put(ct::String("ananas"), 1);
    m.put(ct::String("cereja"), 3);
    auto it = m.begin();
    EXPECT_TRUE(it->key == "ananas");
    ++it;
    EXPECT_TRUE(it->key == "banana");
    ++it;
    EXPECT_TRUE(it->key == "cereja");
    EXPECT_EQ(*m.find(ct::String("banana")), 2);
}

TEST(TreeMap, CopyAndMove)
{
    ct::TreeMap<int, int> a;
    for (int i = 0; i < 500; ++i)
        a.put(i * 13 % 500, i);
    ct::TreeMap<int, int> b(a);
    EXPECT_EQ(b.size(), a.size());
    EXPECT_TRUE(b.validate());
    b.put(0, 999);
    EXPECT_NE(*a.find(0), 999);

    std::size_t sz = a.size();
    ct::TreeMap<int, int> c(static_cast<ct::TreeMap<int, int> &&>(a));
    EXPECT_EQ(c.size(), sz);
    EXPECT_TRUE(c.validate());
    EXPECT_TRUE(a.empty());
    a.put(1, 1); 
    EXPECT_EQ(a.size(), 1u);
}

TEST(TreeMap, NoLeaks)
{
    MTracked::live = 0;
    {
        ct::TreeMap<int, MTracked> m;
        for (int i = 0; i < 1000; ++i)
            m.put(i * 7 % 1000, MTracked(i));
        for (int i = 0; i < 500; ++i)
            m.erase(i);
        ct::TreeMap<int, MTracked> copy(m);
        m.clear();
        EXPECT_EQ(m.size(), 0u);
        m.put(5, MTracked(5));
    }
    EXPECT_EQ(MTracked::live, 0);
}

#include <unordered_set>

TEST(HashSet, InsertContainsErase)
{
    ct::HashSet<int> s;
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.insert(5));
    EXPECT_FALSE(s.insert(5)) << "duplicado nao insere";
    EXPECT_TRUE(s.insert(7));
    EXPECT_EQ(s.size(), 2u);
    EXPECT_TRUE(s.contains(5));
    EXPECT_FALSE(s.contains(6));
    EXPECT_TRUE(s.erase(5));
    EXPECT_FALSE(s.erase(5));
    EXPECT_FALSE(s.contains(5));
    EXPECT_EQ(s.size(), 1u);
}

TEST(HashSet, GrowthAndFuzzVsStd)
{
    ct::HashSet<int> s;
    std::unordered_set<int> ref;
    unsigned seed = 13;
    for (int i = 0; i < 30000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        int k = static_cast<int>(seed % 2000);
        int op = (seed >> 8) % 3;
        if (op == 0)
            ASSERT_EQ(s.insert(k), ref.insert(k).second);
        else if (op == 1)
            ASSERT_EQ(s.erase(k), ref.erase(k) > 0);
        else
            ASSERT_EQ(s.contains(k), ref.count(k) > 0);
        ASSERT_EQ(s.size(), ref.size());
    }
    for (int k : ref)
        ASSERT_TRUE(s.contains(k));
    std::size_t n = 0;
    for (int k : s)
    {
        ASSERT_TRUE(ref.count(k) > 0);
        ++n;
    }
    ASSERT_EQ(n, ref.size());
}

TEST(HashSet, StringKeysAndCopyMove)
{
    ct::HashSet<ct::String> s;
    for (int i = 0; i < 2000; ++i)
        s.insert(ct::String("tag_") + ct::String::number(i));
    EXPECT_EQ(s.size(), 2000u);
    EXPECT_TRUE(s.contains(ct::String("tag_1234")));
    EXPECT_FALSE(s.contains(ct::String("tag_9999")));

    ct::HashSet<ct::String> copy(s);
    EXPECT_EQ(copy.size(), 2000u);
    ct::HashSet<ct::String> moved(static_cast<ct::HashSet<ct::String> &&>(s));
    EXPECT_EQ(moved.size(), 2000u);
    EXPECT_EQ(s.size(), 0u);
    EXPECT_TRUE(moved.contains(ct::String("tag_0")));
}

TEST(HashSet, ClearAndReserve)
{
    ct::HashSet<int> s;
    s.reserve(1000);
    std::size_t cap = s.capacity();
    for (int i = 0; i < 1000; ++i)
        s.insert(i);
    EXPECT_EQ(s.capacity(), cap);
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.insert(1));
}

TEST(HashSet, InsertWithInternalKeyDoesNotRehash)
{
    ct::HashSet<ct::String> s;
    for (int i = 0; i < 12; ++i)
        EXPECT_TRUE(s.insert(ct::String("key_") + ct::String::number(i)));

    const ct::String key = *s.begin();
    const std::size_t capacity = s.capacity();
    EXPECT_FALSE(s.insert(*s.begin()));
    EXPECT_EQ(s.size(), 12u);
    EXPECT_EQ(s.capacity(), capacity);
    EXPECT_TRUE(s.contains(key));
}
