// HashMap: as entradas grandes (mais de 128 bytes) ficam fora da tabela, e as chaves que nao sao inteiros guardam a
// distancia ao slot de origem na meta (a remocao nao recalcula o hash e a sondagem so compara onde a distancia bate).
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Big
    {
        static int live;
        uint64_t v[32];
        Big() { ++live; memset(v, 0, sizeof v); }
        explicit Big(uint64_t x)
        {
            ++live;
            for (int i = 0; i < 32; ++i)
                v[i] = x + static_cast<uint64_t>(i);
        }
        Big(const Big &o)
        {
            ++live;
            memcpy(v, o.v, sizeof v);
        }
        Big &operator=(const Big &o)
        {
            memcpy(v, o.v, sizeof v);
            return *this;
        }
        ~Big() { --live; }
    };
    int Big::live = 0;

    // conta bytes pedidos e vivos
    struct CountingAlloc
    {
        static size_t requested;
        static size_t live_bytes;
        static size_t calls;
        void *allocate(size_t bytes, size_t)
        {
            requested += bytes;
            live_bytes += bytes;
            ++calls;
            return malloc(bytes ? bytes : 1);
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
    size_t CountingAlloc::requested = 0;
    size_t CountingAlloc::live_bytes = 0;
    size_t CountingAlloc::calls = 0;

    struct ConstantStringHash
    {
        uint64_t operator()(const ct::String &) const { return 12345; }
    };

    uint64_t next_rand(uint64_t &s)
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }

    ct::String key_of(uint64_t i)
    {
        return ct::String("chave/") + ct::String::number(i * 2654435761ull % 100003);
    }
} // namespace

TEST(HashLayoutBig, ReferencesToValuesSurviveRehashAndEverythingWorks)
{
    Big::live = 0;
    {
        ct::HashMap<int, Big> m;
        static_assert(sizeof(ct::detail::HashEntry<int, Big, false>) > 128, "o teste precisa de uma entrada grande");
        static_assert(ct::detail::HashMode<int, Big>::value == 1, "chave pequena: a chave fica na tabela e o valor num bloco");
        m.put(0, Big(1000));
        Big *first = m.find(0);
        ASSERT_NE(first, nullptr);
        for (int i = 1; i < 5000; ++i)
            m.put(i, Big(static_cast<uint64_t>(i) * 100));
        EXPECT_EQ(m.find(0), first) << "o valor nao muda de endereco no rehash";
        EXPECT_EQ(first->v[0], 1000u);
        EXPECT_EQ(m.size(), 5000u);
        EXPECT_EQ(Big::live, 5000);
        for (int i = 0; i < 5000; ++i)
        {
            const Big *b = m.find(i);
            ASSERT_NE(b, nullptr) << i;
            ASSERT_EQ(b->v[31], (i == 0 ? 1000u : static_cast<uint64_t>(i) * 100) + 31) << i;
        }
        EXPECT_EQ(m.find(99999), nullptr);
        // percorrer, const e nao const
        uint64_t sum = 0;
        size_t count = 0;
        for (auto &e : m)
        {
            sum += static_cast<uint64_t>(e.key);
            ++count;
        }
        EXPECT_EQ(count, 5000u);
        EXPECT_EQ(sum, 4999ull * 5000 / 2);
        const auto &cm = m;
        count = 0;
        for (auto it = cm.begin(); it != cm.end(); ++it)
            count += it->value.v[0] >= 0 ? 1 : 0;
        EXPECT_EQ(count, 5000u);
        // apagar, o resto continua la
        for (int i = 0; i < 5000; i += 2)
            ASSERT_TRUE(m.erase(i));
        EXPECT_EQ(m.size(), 2500u);
        EXPECT_EQ(Big::live, 2500);
        for (int i = 0; i < 5000; ++i)
            ASSERT_EQ(m.find(i) != nullptr, i % 2 == 1) << i;
        // copia, move, atribuicao
        ct::HashMap<int, Big> copy(m);
        EXPECT_EQ(Big::live, 5000);
        ASSERT_EQ(copy.size(), 2500u);
        EXPECT_NE(copy.find(1), m.find(1));
        EXPECT_EQ(copy.find(1)->v[5], m.find(1)->v[5]);
        ct::HashMap<int, Big> moved(static_cast<ct::HashMap<int, Big> &&>(copy));
        EXPECT_EQ(Big::live, 5000);
        EXPECT_EQ(moved.size(), 2500u);
        EXPECT_TRUE(copy.empty());
        ct::HashMap<int, Big> assigned;
        assigned.put(7, Big(7));
        assigned = m;
        EXPECT_EQ(assigned.size(), 2500u);
        EXPECT_EQ(Big::live, 7500);
        // erase_if e clear
        EXPECT_EQ(assigned.erase_if([](const int &k, Big &) { return k % 3 == 0; }), 833u);
        Big out;
        EXPECT_TRUE(m.erase(1, out));
        EXPECT_EQ(out.v[0], 100u);
        m.clear();
        EXPECT_TRUE(m.empty());
        m.put(5, Big(5));
        EXPECT_NE(m.find(5), nullptr);
    }
    EXPECT_EQ(Big::live, 0);
}

// 100000 entradas de 4 + 256 bytes com um alocador que conta: 16 + 1 bytes de tabela por slot (a chave e a referencia ao valor)
// e o valor num bloco de um slab (sem cabecalho de malloc, um pedaco de 16 blocos por alocacao), em vez de sizeof(entrada) + 1
// por slot de uma tabela a 38 % de carga (o que dava 700 bytes por elemento). A 38 % de carga os 17 bytes por slot
// custam 44,6 por elemento; a 1e3 e a 1e6 elementos a carga e maior e o total fica abaixo dos 296 da ronda 2.
TEST(HashLayoutBig, BytesPerElementAreCloseToTheEntrySize)
{
    CountingAlloc::requested = CountingAlloc::live_bytes = CountingAlloc::calls = 0;
    {
        ct::HashMap<int, Big, ct::Hash<int>, CountingAlloc> m;
        for (int i = 0; i < 100000; ++i)
            m.put(i, Big(static_cast<uint64_t>(i)));
        const double per_elem = static_cast<double>(CountingAlloc::live_bytes) / 100000.0;
        EXPECT_LT(per_elem, 305.0) << "bytes vivos por elemento";
        EXPECT_GT(per_elem, 260.0);
        EXPECT_LT(CountingAlloc::calls, 100000u / 10) << "os valores vem de pedacos, nao ha uma alocacao por entrada";
        m.erase_if([](const int &k, Big &) { return k >= 50000; });
        EXPECT_EQ(m.size(), 50000u);
        // os blocos dos valores apagados ficam no mapa (uma lista de livres) e os proximos put reutilizam-nos
        const size_t before = CountingAlloc::live_bytes;
        for (int i = 100000; i < 150000; ++i)
            m.put(i, Big(static_cast<uint64_t>(i)));
        EXPECT_EQ(m.size(), 100000u);
        EXPECT_LT(CountingAlloc::live_bytes, before + before / 100) << "os blocos libertados foram reutilizados";
        // o clear devolve os blocos ao alocador (a tabela fica com a capacidade, como sempre)
        m.erase_if([](const int &, Big &) { return true; });
        EXPECT_EQ(m.size(), 0u);
        m.clear();
        EXPECT_LT(CountingAlloc::live_bytes, 5u * 1000 * 1000); // so a tabela
    }
    EXPECT_EQ(CountingAlloc::live_bytes, 0u) << "tudo libertado";
}

TEST(HashLayoutDist, StringKeysAgainstModelWithManyOperations)
{
    ct::HashMap<ct::String, int> m;
    ct::Vector<int> model(100003, -1);
    uint64_t s = 424242;
    size_t live = 0;
    for (int op = 0; op < 400000; ++op)
    {
        const uint64_t i = next_rand(s) % 5000;
        const ct::String k = key_of(i);
        const size_t slot = static_cast<size_t>(i * 2654435761ull % 100003);
        switch (next_rand(s) % 4)
        {
        case 0:
        case 1:
            m.put(k, op);
            if (model[slot] < 0)
                ++live;
            model[slot] = op;
            break;
        case 2:
            if (m.erase(k))
            {
                ASSERT_GE(model[slot], 0);
                model[slot] = -1;
                --live;
            }
            else
                ASSERT_LT(model[slot], 0);
            break;
        default:
        {
            const int *p = m.find(k);
            if (model[slot] < 0)
                ASSERT_EQ(p, nullptr);
            else
            {
                ASSERT_NE(p, nullptr);
                ASSERT_EQ(*p, model[slot]);
            }
        }
        }
        ASSERT_EQ(m.size(), live);
    }
    // percorrer da todos uma vez
    size_t count = 0;
    for (const auto &e : m)
    {
        (void)e;
        ++count;
    }
    EXPECT_EQ(count, live);
    // copia com a mesma capacidade e com outra
    ct::HashMap<ct::String, int> c1(m);
    ct::HashMap<ct::String, int> c2;
    c2 = m;
    for (uint64_t i = 0; i < 5000; ++i)
    {
        const ct::String k = key_of(i);
        const size_t slot = static_cast<size_t>(i * 2654435761ull % 100003);
        ASSERT_EQ(c1.find(k) != nullptr, model[slot] >= 0);
        ASSERT_EQ(c2.find(k) != nullptr, model[slot] >= 0);
    }
}

// todas as chaves no mesmo slot: as distancias passam de 125 e a meta fica a 0xFF, onde se compara sempre e a remocao
// calcula a distancia pelo hash
TEST(HashLayoutDist, AllStringKeysCollidingSaturateTheDistance)
{
    ct::HashMap<ct::String, int, ConstantStringHash> m;
    const int n = 1200;
    for (int i = 0; i < n; ++i)
        m.put(ct::String("k") + ct::String::number(i), i);
    ASSERT_EQ(m.size(), static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const int *p = m.find(ct::String("k") + ct::String::number(i));
        ASSERT_NE(p, nullptr) << i;
        ASSERT_EQ(*p, i);
    }
    EXPECT_EQ(m.find(ct::String("nao")), nullptr);
    uint64_t s = 9;
    ct::Vector<bool> present(static_cast<size_t>(n), true);
    for (int op = 0; op < 3000; ++op)
    {
        const int i = static_cast<int>(next_rand(s) % n);
        const ct::String k = ct::String("k") + ct::String::number(i);
        if (next_rand(s) & 1)
        {
            EXPECT_EQ(m.erase(k), present[static_cast<size_t>(i)]);
            present[static_cast<size_t>(i)] = false;
        }
        else
        {
            m.put(k, i + 1000000);
            present[static_cast<size_t>(i)] = true;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const int *p = m.find(ct::String("k") + ct::String::number(i));
        ASSERT_EQ(p != nullptr, present[static_cast<size_t>(i)]) << i;
    }
    // erase_if tambem refaz a tabela com a distancia certa
    m.erase_if([](const ct::String &, int &v) { return (v & 1) == 0; });
    for (int i = 0; i < n; ++i)
    {
        const int *p = m.find(ct::String("k") + ct::String::number(i));
        if (p)
            ASSERT_EQ(*p & 1, 1) << i;
    }
}

TEST(HashLayoutDist, TransparentLookupAndStringViewKeys)
{
    ct::HashMap<ct::String, int> m;
    for (int i = 0; i < 3000; ++i)
        m.put(ct::String("chave-") + ct::String::number(i), i);
    for (int i = 0; i < 3000; i += 7)
    {
        const ct::String k = ct::String("chave-") + ct::String::number(i);
        const int *p = m.find(ct::StringView(k.data(), k.size()));
        ASSERT_NE(p, nullptr) << i;
        ASSERT_EQ(*p, i);
        ASSERT_TRUE(m.contains(k.c_str()));
    }
    EXPECT_EQ(m.find(ct::StringView("zzz")), nullptr);
}

TEST(HashLayoutDist, StringSetAgainstModelAndCollisions)
{
    ct::HashSet<ct::String> s;
    ct::Vector<int> model(100003, 0);
    uint64_t r = 77;
    size_t live = 0;
    for (int op = 0; op < 300000; ++op)
    {
        const uint64_t i = next_rand(r) % 4000;
        const ct::String k = key_of(i);
        const size_t slot = static_cast<size_t>(i * 2654435761ull % 100003);
        switch (next_rand(r) % 3)
        {
        case 0:
            ASSERT_EQ(s.insert(k), model[slot] == 0);
            if (!model[slot])
                ++live;
            model[slot] = 1;
            break;
        case 1:
            ASSERT_EQ(s.erase(k), model[slot] != 0);
            if (model[slot])
                --live;
            model[slot] = 0;
            break;
        default:
            ASSERT_EQ(s.contains(k), model[slot] != 0);
            ASSERT_EQ(s.contains(k.c_str()), model[slot] != 0);
        }
        ASSERT_EQ(s.size(), live);
    }
    size_t count = 0;
    for (const auto &e : s)
    {
        (void)e;
        ++count;
    }
    EXPECT_EQ(count, live);
    ct::HashSet<ct::String> copy(s);
    EXPECT_EQ(copy.size(), live);
    // todas a colidir
    struct ConstHash
    {
        uint64_t operator()(const ct::String &) const { return 3; }
    };
    ct::HashSet<ct::String, ConstHash> c;
    for (int i = 0; i < 900; ++i)
        c.insert(ct::String("c") + ct::String::number(i));
    for (int i = 0; i < 900; i += 3)
        ASSERT_TRUE(c.erase(ct::String("c") + ct::String::number(i)));
    for (int i = 0; i < 900; ++i)
        ASSERT_EQ(c.contains(ct::String("c") + ct::String::number(i)), i % 3 != 0) << i;
    EXPECT_EQ(c.erase_if([](const ct::String &k) { return k.size() == 3; }), 90u - 30u);
}

// ---- as tres disposicoes da tabela (entrada inteira no slot, chave no slot e valor num bloco, entrada inteira num bloco) ----
namespace
{
    struct BigKey // 48 bytes: a chave nao e pequena (mais de 32), por isso a entrada inteira vai para um bloco do slab
    {
        uint64_t a[6];
        bool operator==(const BigKey &o) const { return memcmp(a, o.a, sizeof a) == 0; }
        uint64_t hash() const { return ct::detail::hash_bytes(reinterpret_cast<const char *>(a), sizeof a); }
        static BigKey make(uint64_t x)
        {
            BigKey k;
            for (int i = 0; i < 6; ++i)
                k.a[i] = x * 31 + static_cast<uint64_t>(i);
            return k;
        }
    };

    int g_mo_live = 0;
    struct MoveOnlyBig // 256 bytes, so move
    {
        uint64_t v[32];
        explicit MoveOnlyBig(uint64_t x = 0)
        {
            ++g_mo_live;
            for (int i = 0; i < 32; ++i)
                v[i] = x + static_cast<uint64_t>(i);
        }
        MoveOnlyBig(MoveOnlyBig &&o) noexcept
        {
            ++g_mo_live;
            memcpy(v, o.v, sizeof v);
            o.v[0] = 0xdead;
        }
        MoveOnlyBig &operator=(MoveOnlyBig &&o) noexcept
        {
            memcpy(v, o.v, sizeof v);
            o.v[0] = 0xdead;
            return *this;
        }
        MoveOnlyBig(const MoveOnlyBig &) = delete;
        MoveOnlyBig &operator=(const MoveOnlyBig &) = delete;
        ~MoveOnlyBig() { --g_mo_live; }
    };

    struct alignas(64) AlignedBig
    {
        uint64_t v[24];
        AlignedBig() { memset(v, 0, sizeof v); }
        explicit AlignedBig(uint64_t x)
        {
            for (int i = 0; i < 24; ++i)
                v[i] = x + static_cast<uint64_t>(i);
        }
    };
}

TEST(HashLayoutModes, TheThreeLayoutsAreChosenByKeyAndValueSize)
{
    EXPECT_EQ((ct::detail::HashMode<int, int>::value), 0);
    EXPECT_EQ((ct::detail::HashMode<ct::String, int>::value), 0);
    EXPECT_EQ((ct::detail::HashMode<int, Big>::value), 1);
    EXPECT_EQ((ct::detail::HashMode<ct::String, Big>::value), 1); // String tem 24 bytes: chave pequena
    EXPECT_EQ((ct::detail::HashMode<BigKey, Big>::value), 2);     // chave de 48 bytes: a entrada inteira num bloco
    EXPECT_EQ((ct::detail::HashMode<BigKey, int>::value), 0);     // 56 bytes: cabe inline
}

TEST(HashLayoutModes, EntireEntryInASlabBlockWhenTheKeyIsBig)
{
    Big::live = 0;
    {
        ct::HashMap<BigKey, Big> m;
        for (uint64_t i = 0; i < 3000; ++i)
            m.put(BigKey::make(i), Big(i * 7));
        Big *first = m.find(BigKey::make(0));
        ASSERT_NE(first, nullptr);
        for (uint64_t i = 3000; i < 9000; ++i)
            m.put(BigKey::make(i), Big(i * 7));
        EXPECT_EQ(m.find(BigKey::make(0)), first) << "o bloco nao se move no rehash";
        ASSERT_EQ(m.size(), 9000u);
        EXPECT_EQ(Big::live, 9000);
        for (uint64_t i = 0; i < 9000; i += 3)
            ASSERT_TRUE(m.erase(BigKey::make(i)));
        EXPECT_EQ(Big::live, 6000);
        size_t n = 0;
        for (auto &e : m)
        {
            ASSERT_EQ(e.value.v[0], e.key.a[0] / 31 * 7);
            ++n;
        }
        EXPECT_EQ(n, 6000u);
        ct::HashMap<BigKey, Big> copy(m);
        EXPECT_EQ(Big::live, 12000);
        for (uint64_t i = 1; i < 9000; i += 3)
            ASSERT_EQ(copy.find(BigKey::make(i))->v[1], i * 7 + 1);
        EXPECT_EQ(copy.erase_if([](const BigKey &k, Big &) { return k.a[0] % 2 == 0; }), 3000u);
        copy.clear();
        EXPECT_EQ(Big::live, 6000);
        m = copy; // atribuicao de uma vazia
        EXPECT_TRUE(m.empty());
        EXPECT_EQ(Big::live, 0);
    }
    EXPECT_EQ(Big::live, 0);
}

TEST(HashLayoutModes, MoveOnlyAndOverAlignedBigValues)
{
    g_mo_live = 0;
    {
        ct::HashMap<int, MoveOnlyBig> m;
        for (int i = 0; i < 2000; ++i)
            m.put(i, MoveOnlyBig(static_cast<uint64_t>(i)));
        for (int i = 0; i < 2000; i += 2)
            ASSERT_TRUE(m.erase(i));
        EXPECT_EQ(g_mo_live, 1000);
        MoveOnlyBig out;
        ASSERT_TRUE(m.erase(1, out));
        EXPECT_EQ(out.v[5], 6u);
        ct::HashMap<int, MoveOnlyBig> moved(static_cast<ct::HashMap<int, MoveOnlyBig> &&>(m));
        EXPECT_TRUE(m.empty());
        m.put(7, MoveOnlyBig(7)); // o mapa movido de pode voltar a ser usado
        ct::HashMap<int, MoveOnlyBig> assigned;
        assigned.put(3, MoveOnlyBig(3));
        assigned = static_cast<ct::HashMap<int, MoveOnlyBig> &&>(moved);
        EXPECT_EQ(assigned.size(), 999u);
        EXPECT_EQ(assigned.find(5)->v[0], 5u);
        assigned.erase_if([](const int &k, MoveOnlyBig &) { return k > 100; });
        EXPECT_EQ(g_mo_live, static_cast<int>(assigned.size() + m.size()) + 1);
    }
    EXPECT_EQ(g_mo_live, 0);
    ct::HashMap<int, AlignedBig> a;
    for (int i = 0; i < 1500; ++i)
        a.put(i, AlignedBig(static_cast<uint64_t>(i)));
    for (int i = 0; i < 1500; i += 3)
        a.erase(i);
    ct::HashMap<int, AlignedBig> c(a);
    for (auto &e : c)
        ASSERT_EQ(reinterpret_cast<uintptr_t>(&e.value) % 64, 0u);
    for (int i = 1; i < 1500; i += 3)
        ASSERT_EQ(a.find(i)->v[23], static_cast<uint64_t>(i) + 23);
}

TEST(HashLayoutModes, RandomOperationsOnABigValueMapAgainstADirectArrayModel)
{
    Big::live = 0;
    const int keys = 3000;
    ct::Vector<char> has;
    ct::Vector<uint64_t> val;
    has.resize(keys);
    val.resize(keys);
    size_t count = 0;
    uint64_t s = 99;
    ct::HashMap<int, Big> m;
    for (int op = 0; op < 400000; ++op)
    {
        const int k = static_cast<int>(next_rand(s) % keys);
        const uint64_t what = next_rand(s) % 100;
        if (what < 45)
        {
            const uint64_t v = next_rand(s);
            m.put(k, Big(v));
            if (!has[static_cast<size_t>(k)])
                ++count;
            has[static_cast<size_t>(k)] = 1;
            val[static_cast<size_t>(k)] = v;
        }
        else if (what < 70)
        {
            const bool had = has[static_cast<size_t>(k)] != 0;
            ASSERT_EQ(m.erase(k), had);
            if (had)
                --count;
            has[static_cast<size_t>(k)] = 0;
        }
        else if (what < 98)
        {
            Big *p = m.find(k);
            ASSERT_EQ(p != nullptr, has[static_cast<size_t>(k)] != 0);
            if (p)
                ASSERT_EQ(p->v[3], val[static_cast<size_t>(k)] + 3);
        }
        else if (what == 98)
        {
            ct::HashMap<int, Big> c(m);
            ASSERT_EQ(c.size(), count);
            m = c;
        }
        else
            m = ct::HashMap<int, Big>(m);
        ASSERT_EQ(m.size(), count);
        ASSERT_EQ(Big::live, static_cast<int>(count));
    }
}
