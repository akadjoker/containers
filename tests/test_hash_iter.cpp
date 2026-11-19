// HashMap e HashSet: erase_if (cada elemento visitado uma vez, o ciclo certo para apagar enquanto se percorre) e, em
// compilacoes de depuracao (CT_DEBUG_ITERATORS), a verificacao que aborta se um iterador for usado depois de a tabela
// mudar (apagar ou inserir durante um ciclo for).
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>

namespace
{
    struct ConstantHash
    {
        uint64_t operator()(int) const { return 7; }
    };

    uint64_t next_rand(uint64_t &s)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
} // namespace

TEST(HashEraseIf, MapVisitsEachElementOnceAndKeepsTheRest)
{
    for (int n : {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 100, 1000, 5000})
        for (int divisor : {1, 2, 3, 5})
        {
            ct::HashMap<int, int> m;
            for (int i = 0; i < n; ++i)
                m.put(i * 7 + 1, i);
            const size_t cap = m.capacity();
            ct::Vector<int> visited;
            int expect_removed = 0;
            for (int i = 0; i < n; ++i)
                if (i % divisor == 0)
                    ++expect_removed;
            const size_t removed = m.erase_if([&](const int &k, int &v) {
                visited.push_back(k);
                return v % divisor == 0;
            });
            ASSERT_EQ(removed, static_cast<size_t>(expect_removed)) << n << " " << divisor;
            ASSERT_EQ(visited.size(), static_cast<size_t>(n)); // cada um uma vez
            ASSERT_EQ(m.size(), static_cast<size_t>(n - expect_removed));
            ASSERT_EQ(m.capacity(), cap);
            for (int i = 0; i < n; ++i)
            {
                const int *p = m.find(i * 7 + 1);
                if (i % divisor == 0)
                    ASSERT_EQ(p, nullptr) << i;
                else
                {
                    ASSERT_NE(p, nullptr) << i;
                    ASSERT_EQ(*p, i);
                }
            }
            size_t counted = 0;
            for (const auto &e : m)
            {
                (void)e;
                ++counted;
            }
            ASSERT_EQ(counted, m.size());
            // e a tabela continua a servir
            for (int i = 0; i < 50; ++i)
                m.put(-1 - i, i);
            for (int i = 0; i < 50; ++i)
                ASSERT_NE(m.find(-1 - i), nullptr);
        }
}

TEST(HashEraseIf, SetAndStringKeysAndValueEdits)
{
    ct::HashSet<int> s;
    for (int i = 0; i < 3000; ++i)
        s.insert(i);
    size_t calls = 0;
    EXPECT_EQ(s.erase_if([&](const int &k) {
                  ++calls;
                  return k % 3 != 0;
              }),
              2000u);
    EXPECT_EQ(calls, 3000u);
    EXPECT_EQ(s.size(), 1000u);
    for (int i = 0; i < 3000; ++i)
        EXPECT_EQ(s.contains(i), i % 3 == 0) << i;
    EXPECT_EQ(s.erase_if([](const int &) { return false; }), 0u);
    EXPECT_EQ(s.erase_if([](const int &) { return true; }), 1000u);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.erase_if([](const int &) { return true; }), 0u);

    ct::HashMap<ct::String, int> m;
    for (int i = 0; i < 2000; ++i)
        m.put(ct::String("chave-") + ct::String::number(i), i);
    // o valor pode mudar dentro do pred
    m.erase_if([](const ct::String &, int &v) {
        v += 1000;
        return false;
    });
    EXPECT_EQ(*m.find(ct::String("chave-5")), 1005);
    // chave-0..9 tem 7 caracteres (impar), 10..99 tem 8 (par), 100..999 tem 9, 1000..1999 tem 10 (par): 90 + 1000 pares
    EXPECT_EQ(m.erase_if([](const ct::String &k, int &) { return k.size() % 2 == 0; }), 1090u);
    for (int i = 0; i < 2000; ++i)
    {
        const ct::String k = ct::String("chave-") + ct::String::number(i);
        EXPECT_EQ(m.find(k) != nullptr, k.size() % 2 != 0) << i;
    }
}

TEST(HashEraseIf, AllKeysCollidingAndEmptyTable)
{
    ct::HashMap<int, int, ConstantHash> m;
    for (int i = 0; i < 300; ++i)
        m.put(i, i);
    EXPECT_EQ(m.erase_if([](const int &k, int &) { return k % 2 == 0; }), 150u);
    for (int i = 0; i < 300; ++i)
        EXPECT_EQ(m.find(i) != nullptr, i % 2 == 1) << i;
    ct::HashMap<int, int> empty;
    EXPECT_EQ(empty.erase_if([](const int &, int &) { return true; }), 0u);
    uint64_t s = 3;
    ct::HashSet<int> r;
    ct::Vector<int> model;
    for (int round = 0; round < 50; ++round)
    {
        for (int i = 0; i < 200; ++i)
        {
            const int v = static_cast<int>(next_rand(s) % 5000);
            if (r.insert(v))
                model.push_back(v);
        }
        const int mod = 2 + static_cast<int>(next_rand(s) % 4);
        r.erase_if([&](const int &k) { return k % mod == 0; });
        ct::Vector<int> kept;
        for (int v : model)
            if (v % mod != 0)
                kept.push_back(v);
        model = kept;
        ASSERT_EQ(r.size(), model.size());
        for (int v : model)
            ASSERT_TRUE(r.contains(v));
    }
}

// o padrao seguro: juntar as chaves e apagar depois (e um break logo a seguir a apagar, sem usar mais o iterador)
TEST(HashEraseIf, SafePatternsStillWork)
{
    ct::HashMap<int, int> m;
    for (int i = 0; i < 100; ++i)
        m.put(i, i);
    ct::Vector<int> keys;
    for (const auto &e : m)
        if (e.value % 2 == 0)
            keys.push_back(e.key);
    for (int k : keys)
        EXPECT_TRUE(m.erase(k));
    EXPECT_EQ(m.size(), 50u);
    for (auto it = m.begin(); it != m.end(); ++it)
        if (it->value == 51)
        {
            m.erase(it->key);
            break;
        }
    EXPECT_EQ(m.size(), 49u);
    // inserir um valor que ja existe nao muda a estrutura: o iterador continua valido
    auto it = m.begin();
    m.put(it->key, 12345);
    ++it;
}

#if defined(CT_DEBUG_ITERATORS)
namespace
{
    void erase_map_while_iterating()
    {
        ct::HashMap<int, int> m;
        for (int i = 0; i < 50; ++i)
            m.put(i, i);
        for (auto it = m.begin(); it != m.end(); ++it)
            if (it->value % 2 == 0)
                m.erase(it->key);
    }
    void erase_set_while_iterating()
    {
        ct::HashSet<int> s;
        for (int i = 0; i < 50; ++i)
            s.insert(i);
        for (auto it = s.begin(); it != s.end(); ++it)
            if (*it % 2 == 0)
                s.erase(*it);
    }
    void insert_while_iterating()
    {
        ct::HashMap<int, int> m;
        for (int i = 0; i < 10; ++i)
            m.put(i, i);
        for (auto it = m.begin(); it != m.end(); ++it)
            m.put(1000 + it->key, 0);
    }
    void use_after_clear()
    {
        ct::HashMap<int, int> m;
        m.put(1, 1);
        auto it = m.begin();
        m.clear();
        (void)*it;
    }
    void const_iterator_after_erase()
    {
        ct::HashMap<int, int> k;
        k.put(1, 1);
        k.put(2, 2);
        ct::HashMap<int, int>::const_iterator c = k.begin();
        k.erase(1);
        ++c;
    }
} // namespace

TEST(HashDebugIterators, EraseWhileIteratingAborts)
{
    EXPECT_DEATH(erase_map_while_iterating(), "iterador usado depois de a tabela mudar");
    EXPECT_DEATH(erase_set_while_iterating(), "iterador usado depois de a tabela mudar");
}

TEST(HashDebugIterators, InsertWhileIteratingAborts)
{
    EXPECT_DEATH(insert_while_iterating(), "iterador usado depois de a tabela mudar");
    EXPECT_DEATH(use_after_clear(), "iterador usado depois de a tabela mudar");
}

TEST(HashDebugIterators, ConstAndConvertedIteratorsAreChecked)
{
    ct::HashMap<int, int> m;
    for (int i = 0; i < 10; ++i)
        m.put(i, i);
    const ct::HashMap<int, int> &cm = m;
    ct::HashMap<int, int>::const_iterator ci = m.begin(); // iterator para const_iterator
    int n = 0;
    for (auto it = cm.begin(); it != cm.end(); ++it)
        n += it->value;
    EXPECT_EQ(n, 45);
    (void)ci;
    EXPECT_DEATH(const_iterator_after_erase(), "iterador usado depois de a tabela mudar");
}
#endif
