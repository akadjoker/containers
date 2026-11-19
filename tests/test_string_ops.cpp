// String: ==, compare, operator+, push_back e number() nos limites do SSO e dos inteiros (os caminhos curtos que
// evitam o append e o temporario), contra modelos simples.
#include <ct/string.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    ct::String make(size_t n, int seed)
    {
        ct::String s;
        for (size_t i = 0; i < n; ++i)
            s.push_back(static_cast<char>('a' + (static_cast<int>(i) * 7 + seed) % 26));
        return s;
    }

    int sign(int x) { return x < 0 ? -1 : (x > 0 ? 1 : 0); }

    int model_compare(const ct::String &a, const ct::String &b)
    {
        const size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; ++i)
        {
            const unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
            if (x != y)
                return x < y ? -1 : 1;
        }
        return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
    }
} // namespace

TEST(StringOps, EqualityAndCompareAcrossTheSsoBoundary)
{
    for (size_t na = 0; na <= 60; na += (na < 30 ? 1 : 7))
        for (size_t nb = 0; nb <= 60; nb += (nb < 30 ? 1 : 7))
            for (int variant = 0; variant < 4; ++variant)
            {
                ct::String a = make(na, 1);
                ct::String b = make(nb, variant == 0 ? 1 : 1);
                if (variant == 1 && nb > 0)
                    b[nb - 1] = static_cast<char>(b[nb - 1] + 1);
                if (variant == 2 && na > 0)
                    a[0] = static_cast<char>(a[0] + 1);
                if (variant == 3 && na > 2 && nb > 2)
                    a[na / 2] = static_cast<char>(0xFF); // byte alto: compara como unsigned char
                const int want = model_compare(a, b);
                ASSERT_EQ(sign(a.compare(b)), want) << na << " " << nb << " " << variant;
                ASSERT_EQ(a == b, want == 0);
                ASSERT_EQ(a != b, want != 0);
                ASSERT_EQ(a < b, want < 0);
                ASSERT_EQ(a > b, want > 0);
                ASSERT_EQ(a <= b, want <= 0);
                ASSERT_EQ(a >= b, want >= 0);
            }
    // com NULs no meio e vazias
    const ct::String n1("ab\0cd", 5), n2("ab\0ce", 5), e1, e2("");
    EXPECT_TRUE(n1 != n2);
    EXPECT_TRUE(n1 < n2);
    EXPECT_TRUE(e1 == e2);
    EXPECT_FALSE(e1 < e2);
    EXPECT_TRUE(e1 < n1);
    EXPECT_TRUE(e1.compare(e2) == 0);
    ct::String moved = make(40, 3);
    ct::String target(static_cast<ct::String &&>(moved));
    EXPECT_TRUE(moved == ct::String());
    EXPECT_TRUE(moved.compare(ct::String()) == 0);
    EXPECT_TRUE(target == make(40, 3));
}

TEST(StringOps, PlusMatchesAppendAcrossSizes)
{
    for (size_t na = 0; na <= 70; ++na)
        for (size_t nb = 0; nb <= 70; nb += (nb < 30 ? 1 : 5))
        {
            const ct::String a = make(na, 2), b = make(nb, 5);
            ct::String want = a;
            want.append(b);
            const ct::String got = a + b;
            ASSERT_TRUE(got == want) << na << " " << nb;
            ASSERT_EQ(got.size(), na + nb);
            ASSERT_EQ(got.c_str()[na + nb], '\0');
            ASSERT_TRUE(strlen(got.c_str()) == na + nb);
            if (na + nb > 23)
                ASSERT_EQ(got.capacity(), na + nb) << "capacidade exacta na heap";
            // com const char *
            const ct::String r1 = a + b.c_str();
            ASSERT_TRUE(r1 == want);
            const ct::String r2 = a.c_str() + b;
            ASSERT_TRUE(r2 == want);
            // e depois ainda cresce como sempre
            ct::String grown = got;
            grown.push_back('!');
            grown.append("xyz");
            ASSERT_EQ(grown.size(), na + nb + 4);
            ASSERT_EQ(grown[na + nb], '!');
        }
    const ct::String s = make(30, 4);
    EXPECT_TRUE(s + s == make(30, 4) + make(30, 4));
    EXPECT_EQ((s + s).size(), 60u);
    const ct::String with_nul("a\0b", 3);
    const ct::String joined = with_nul + with_nul;
    EXPECT_EQ(joined.size(), 6u);
    EXPECT_TRUE(joined == ct::String("a\0ba\0b", 6));
    EXPECT_TRUE(ct::String() + ct::String() == ct::String());
    EXPECT_TRUE((ct::String("abc") + "") == "abc");
    EXPECT_TRUE(("" + ct::String("abc")) == "abc");
}

TEST(StringOps, PushBackAtEveryCapacityEdge)
{
    for (size_t reserve = 0; reserve <= 70; ++reserve)
    {
        ct::String s;
        s.reserve(reserve);
        for (size_t i = 0; i < 80; ++i)
        {
            s.push_back(static_cast<char>('A' + i % 26));
            ASSERT_EQ(s.size(), i + 1);
            ASSERT_EQ(s.c_str()[i + 1], '\0') << reserve << " " << i;
            ASSERT_EQ(s[i], static_cast<char>('A' + i % 26));
            ASSERT_GE(s.capacity(), s.size());
        }
        for (size_t i = 0; i < 80; ++i)
            ASSERT_EQ(s[i], static_cast<char>('A' + i % 26));
    }
    // push_back da String em si mesma e depois de mover
    ct::String a = make(23, 0);
    ct::String b(static_cast<ct::String &&>(a));
    a.push_back('x');
    EXPECT_TRUE(a == "x");
    b.push_back(b[0]);
    EXPECT_EQ(b.size(), 24u);
    EXPECT_EQ(b[23], b[0]);
}

TEST(StringOps, NumberAtDigitBoundaries)
{
    char want[40];
    for (int e = 0; e <= 19; ++e)
    {
        unsigned long long p = 1;
        for (int i = 0; i < e; ++i)
            p *= 10;
        for (int d = -1; d <= 1; ++d)
        {
            const unsigned long long u = p + static_cast<unsigned long long>(static_cast<long long>(d));
            snprintf(want, sizeof want, "%llu", u);
            ASSERT_TRUE(ct::String::number(u) == want) << want;
            if (u <= 9223372036854775807ull)
            {
                snprintf(want, sizeof want, "%lld", static_cast<long long>(u));
                ASSERT_TRUE(ct::String::number(static_cast<long long>(u)) == want) << want;
                snprintf(want, sizeof want, "%lld", -static_cast<long long>(u));
                ASSERT_TRUE(ct::String::number(-static_cast<long long>(u)) == want) << want;
            }
        }
    }
    EXPECT_TRUE(ct::String::number(0) == "0");
    EXPECT_TRUE(ct::String::number(0ull) == "0");
    EXPECT_TRUE(ct::String::number(-9223372036854775807ll - 1) == "-9223372036854775808");
    EXPECT_TRUE(ct::String::number(18446744073709551615ull) == "18446744073709551615");
    EXPECT_TRUE(ct::String::number(static_cast<short>(-12)) == "-12");
    EXPECT_TRUE(ct::String::number(static_cast<unsigned char>(255)) == "255");
    EXPECT_TRUE(ct::String::number(static_cast<size_t>(1234567)) == "1234567");
    // o resultado e uma String normal: cresce depois
    ct::String s = ct::String::number(-9223372036854775807ll - 1);
    s.append("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    EXPECT_EQ(s.size(), 20u + 36u);
}
