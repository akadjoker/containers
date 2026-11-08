#include <ct/unicode.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{
    using ct::Vector;
    namespace uc = ct::unicode;

    struct CaseRow
    {
        std::uint32_t cp;
        const char *upper;
        const char *lower;
    };

    struct CaseStr
    {
        const char *input;
        const char *upper;
        const char *lower;
    };

#include "unicode_case_cases.inc"

    typedef std::u16string U16;

    U16 from_hex(const char *s)
    {
        U16 out;
        while (*s)
        {
            while (*s == ' ')
                ++s;
            if (!*s)
                break;
            char *end;
            unsigned long v = std::strtoul(s, &end, 16);
            s = end;
            out.push_back(static_cast<char16_t>(v));
        }
        return out;
    }

    U16 from_cps(const std::vector<std::uint32_t> &cps)
    {
        U16 out;
        for (std::uint32_t cp : cps)
        {
            if (cp < 0x10000)
                out.push_back(static_cast<char16_t>(cp));
            else
            {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
        }
        return out;
    }

    std::string show(const U16 &s)
    {
        std::string out;
        char buf[8];
        for (char16_t c : s)
        {
            std::snprintf(buf, sizeof buf, "%04X ", static_cast<unsigned>(c));
            out += buf;
        }
        return out;
    }

    U16 to_u16(const Vector<char16_t> &v)
    {
        return U16(v.data(), v.size());
    }

    U16 upper(const U16 &s)
    {
        Vector<char16_t> out;
        uc::to_upper(s.data(), s.size(), out);
        return to_u16(out);
    }

    U16 lower(const U16 &s)
    {
        Vector<char16_t> out;
        uc::to_lower(s.data(), s.size(), out);
        return to_u16(out);
    }

    U16 norm(uc::Form f, const U16 &s)
    {
        Vector<char16_t> out;
        uc::normalize(f, s.data(), s.size(), out);
        return to_u16(out);
    }

    bool is_norm(uc::Form f, const U16 &s)
    {
        return uc::is_normalized(f, s.data(), s.size());
    }

    bool fits_latin1(const U16 &s)
    {
        for (char16_t c : s)
            if (c > 0xFF)
                return false;
        return true;
    }

    std::vector<unsigned char> to_l1(const U16 &s)
    {
        std::vector<unsigned char> out;
        for (char16_t c : s)
            out.push_back(static_cast<unsigned char>(c));
        return out;
    }

    U16 from_l1(const Vector<unsigned char> &v)
    {
        U16 out;
        for (std::size_t i = 0; i < v.size(); ++i)
            out.push_back(v[i]);
        return out;
    }

    U16 upper_l1(const U16 &s)
    {
        std::vector<unsigned char> in = to_l1(s);
        Vector<unsigned char> o8;
        Vector<char16_t> o16;
        bool wide = uc::to_upper(in.data(), in.size(), o8, o16);
        return wide ? to_u16(o16) : from_l1(o8);
    }

    U16 lower_l1(const U16 &s)
    {
        std::vector<unsigned char> in = to_l1(s);
        Vector<unsigned char> o8;
        uc::to_lower(in.data(), in.size(), o8);
        return from_l1(o8);
    }

    U16 norm_l1(uc::Form f, const U16 &s, bool *wide_out = nullptr)
    {
        std::vector<unsigned char> in = to_l1(s);
        Vector<unsigned char> o8;
        Vector<char16_t> o16;
        bool wide = uc::normalize(f, in.data(), in.size(), o8, o16);
        if (wide_out)
            *wide_out = wide;
        return wide ? to_u16(o16) : from_l1(o8);
    }

    std::vector<std::uint32_t> split_cps(const char *s)
    {
        std::vector<std::uint32_t> out;
        while (*s)
        {
            char *end;
            out.push_back(static_cast<std::uint32_t>(std::strtoul(s, &end, 16)));
            s = end;
            while (*s == ' ')
                ++s;
        }
        return out;
    }

    const uc::Form kForms[] = {uc::NFC, uc::NFD, uc::NFKC, uc::NFKD};
    const char *const kFormNames[] = {"NFC", "NFD", "NFKC", "NFKD"};

    std::uint32_t rng_state = 20251108;

    std::uint32_t rnd()
    {
        rng_state = rng_state * 1664525u + 1013904223u;
        return rng_state >> 8;
    }
}

TEST(Unicode, NormalizationTestFile)
{
    std::string path = std::string(CT_UCD_DIR) + "/NormalizationTest.txt";
    std::ifstream in(path);
    if (!in)
        GTEST_SKIP() << "missing " << path << " (python3 tests/gen_unicode_tables.py downloads it)";
    std::string line;
    int part = -1;
    std::size_t lines = 0, bad = 0;
    std::set<std::uint32_t> listed;
    while (std::getline(in, line))
    {
        if (line.compare(0, 5, "@Part") == 0)
        {
            part = line[5] - '0';
            continue;
        }
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<U16> col;
        std::size_t pos = 0;
        std::vector<std::uint32_t> first;
        for (int k = 0; k < 5; ++k)
        {
            std::size_t semi = line.find(';', pos);
            std::string field = line.substr(pos, semi - pos);
            std::vector<std::uint32_t> cps = split_cps(field.c_str());
            if (k == 0)
                first = cps;
            col.push_back(from_cps(cps));
            pos = semi + 1;
        }
        ++lines;
        if (part == 1 && first.size() == 1)
            listed.insert(first[0]);
        auto check = [&](bool ok, const char *what, int c)
        {
            if (!ok)
            {
                if (++bad <= 20)
                    ADD_FAILURE() << "line " << lines << " part " << part << " " << what << " col " << c + 1 << ": " << line.substr(0, 80);
            }
        };
        for (int c = 0; c < 3; ++c)
        {
            check(norm(uc::NFC, col[c]) == col[1], "NFC", c);
            check(norm(uc::NFD, col[c]) == col[2], "NFD", c);
        }
        for (int c = 3; c < 5; ++c)
        {
            check(norm(uc::NFC, col[c]) == col[3], "NFC", c);
            check(norm(uc::NFD, col[c]) == col[4], "NFD", c);
        }
        for (int c = 0; c < 5; ++c)
        {
            check(norm(uc::NFKC, col[c]) == col[3], "NFKC", c);
            check(norm(uc::NFKD, col[c]) == col[4], "NFKD", c);
        }
        const U16 *expect[4] = {&col[1], &col[2], &col[3], &col[4]};
        for (int f = 0; f < 4; ++f)
            check(is_norm(kForms[f], *expect[f]), kFormNames[f], f);
        check(is_norm(uc::NFC, col[1]) && is_norm(uc::NFD, col[2]) && is_norm(uc::NFKC, col[3]) && is_norm(uc::NFKD, col[4]), "is_normalized", 0);
        for (int c = 0; c < 5; ++c)
            for (int f = 0; f < 4; ++f)
            {
                bool want = norm(kForms[f], col[c]) == col[c];
                check(is_norm(kForms[f], col[c]) == want, kFormNames[f], 10 + c);
            }
    }
    EXPECT_GT(lines, 19000u);
    EXPECT_EQ(bad, 0u);
    std::size_t other = 0;
    for (std::uint32_t cp = 0; cp <= 0x10FFFF; ++cp)
    {
        if ((cp >= 0xD800 && cp <= 0xDFFF) || listed.count(cp))
            continue;
        U16 s = from_cps({cp});
        for (int f = 0; f < 4; ++f)
            if (norm(kForms[f], s) != s || !is_norm(kForms[f], s))
            {
                if (++other <= 10)
                    ADD_FAILURE() << "code point " << std::hex << cp << " not listed in Part1 changes under " << kFormNames[f];
            }
    }
    EXPECT_EQ(other, 0u);
}

TEST(Unicode, HangulAlgorithmic)
{
    for (std::uint32_t s = 0xAC00; s <= 0xD7A3; ++s)
    {
        U16 syl = from_cps({s});
        std::uint32_t si = s - 0xAC00;
        std::vector<std::uint32_t> jamo = {0x1100 + si / 588, 0x1161 + (si % 588) / 28};
        if (si % 28)
            jamo.push_back(0x11A7 + si % 28);
        U16 d = from_cps(jamo);
        ASSERT_EQ(norm(uc::NFD, syl), d);
        ASSERT_EQ(norm(uc::NFKD, syl), d);
        ASSERT_EQ(norm(uc::NFC, d), syl);
        ASSERT_EQ(norm(uc::NFKC, d), syl);
        ASSERT_TRUE(is_norm(uc::NFC, syl));
        ASSERT_FALSE(is_norm(uc::NFD, syl));
        ASSERT_FALSE(is_norm(uc::NFC, d));
        ASSERT_TRUE(is_norm(uc::NFD, d));
    }
    EXPECT_EQ(norm(uc::NFC, from_cps({0x1100, 0x1161, 0x11A8, 0x11A8})), from_cps({0xAC01, 0x11A8}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0xAC00, 0x11A8})), from_cps({0xAC01}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0xAC01, 0x11A8})), from_cps({0xAC01, 0x11A8}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0x1100, 0x0301, 0x1161})), from_cps({0x1100, 0x0301, 0x1161}));
}

TEST(Unicode, NormalizationBasics)
{
    EXPECT_EQ(norm(uc::NFC, from_cps({0x65, 0x301})), from_cps({0xE9}));
    EXPECT_EQ(norm(uc::NFD, from_cps({0xE9})), from_cps({0x65, 0x301}));
    EXPECT_EQ(norm(uc::NFKC, from_cps({0xFB01})), from_cps({0x66, 0x69}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0xFB01})), from_cps({0xFB01}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0x212B})), from_cps({0xC5}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0x61, 0x323, 0x302})), from_cps({0x1EAD}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0x61, 0x302, 0x323})), from_cps({0x1EAD}));
    EXPECT_EQ(norm(uc::NFD, from_cps({0x1EAD})), from_cps({0x61, 0x323, 0x302}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0x61, 0xD800, 0x301})), from_cps({0x61, 0xD800, 0x301}));
    EXPECT_EQ(norm(uc::NFD, from_cps({0xDC00, 0xC0})), from_cps({0xDC00, 0x41, 0x300}));
    EXPECT_EQ(norm(uc::NFC, from_cps({0x1D15E})), from_cps({0x1D157, 0x1D165}));
    EXPECT_EQ(norm(uc::NFKC, from_cps({0x1D400})), from_cps({0x41}));
    EXPECT_TRUE(is_norm(uc::NFC, U16()));
    EXPECT_TRUE(is_norm(uc::NFC, from_cps({0x41, 0x1F600, 0x4E00})));
    EXPECT_EQ(norm(uc::NFD, U16()), U16());
}

TEST(Unicode, CaseSingles)
{
    std::set<std::uint32_t> changed;
    for (const CaseRow &r : kCaseSingles)
    {
        U16 s = from_cps({r.cp});
        ASSERT_EQ(upper(s), from_hex(r.upper)) << std::hex << r.cp;
        ASSERT_EQ(lower(s), from_hex(r.lower)) << std::hex << r.cp;
        changed.insert(r.cp);
        if (r.cp < 256)
        {
            ASSERT_EQ(upper_l1(s), from_hex(r.upper)) << std::hex << r.cp;
            ASSERT_EQ(lower_l1(s), from_hex(r.lower)) << std::hex << r.cp;
        }
    }
    for (std::uint32_t cp = 0; cp <= 0x10FFFF; ++cp)
    {
        if (cp >= 0xD800 && cp <= 0xDFFF)
        {
            U16 s(1, static_cast<char16_t>(cp));
            ASSERT_EQ(upper(s), s);
            ASSERT_EQ(lower(s), s);
            continue;
        }
        if (changed.count(cp))
            continue;
        U16 s = from_cps({cp});
        ASSERT_EQ(upper(s), s) << std::hex << cp;
        ASSERT_EQ(lower(s), s) << std::hex << cp;
    }
    EXPECT_GT(changed.size(), 2800u);
}

TEST(Unicode, CaseStrings)
{
    for (const CaseStr &r : kCaseStrings)
    {
        U16 in = from_hex(r.input);
        ASSERT_EQ(upper(in), from_hex(r.upper)) << r.input;
        ASSERT_EQ(lower(in), from_hex(r.lower)) << r.input;
        if (fits_latin1(in))
        {
            ASSERT_EQ(upper_l1(in), from_hex(r.upper)) << r.input;
            ASSERT_EQ(lower_l1(in), from_hex(r.lower)) << r.input;
        }
    }
}

TEST(Unicode, CaseNodeFacts)
{
    EXPECT_EQ(lower(u"ΣΑΣ ΣΑ"), u"σας σα");
    EXPECT_EQ(lower(u"AΣ"), u"aς");
    EXPECT_EQ(lower(u"ΑΣ."), u"ας.");
    EXPECT_EQ(lower(u"ΑΣ" u"b"), u"ασ" u"b");
    EXPECT_EQ(lower(u"Σ"), u"σ");
    EXPECT_EQ(lower(u"ΑΣ́"), u"ας́");
    EXPECT_EQ(lower(u"Α.Σ"), u"α.ς");
    EXPECT_EQ(upper(u"ÿµ"), u"ŸΜ");
    EXPECT_EQ(lower(u"ẞ"), u"ß");
    EXPECT_EQ(lower(u"\U00010400"), u"\U00010428");
    EXPECT_EQ(upper(u"\U00010428"), u"\U00010400");
    EXPECT_EQ(upper(u"ß"), u"SS");
    EXPECT_EQ(upper(u"ŉ"), u"ʼN");
    EXPECT_EQ(upper(u"ﬁ"), u"FI");
    EXPECT_EQ(lower(u"İ"), u"i̇");
    EXPECT_EQ(upper(from_cps({0xD801, 0x78})), from_cps({0xD801, 0x58}));
    EXPECT_EQ(lower(from_cps({0xDC00, 0xD801})), from_cps({0xDC00, 0xD801}));
}

TEST(Unicode, Latin1Paths)
{
    std::vector<unsigned char> all;
    for (int i = 0; i < 256; ++i)
        all.push_back(static_cast<unsigned char>(i));
    Vector<unsigned char> o8;
    Vector<char16_t> o16;
    bool wide = uc::to_upper(all.data(), all.size(), o8, o16);
    EXPECT_TRUE(wide);
    U16 ref;
    for (int i = 0; i < 256; ++i)
        ref += static_cast<char16_t>(i);
    EXPECT_EQ(to_u16(o16), upper(ref));
    unsigned char plain[] = {'a', 0xE9, 0xDF, 'z'};
    EXPECT_FALSE(uc::to_upper(plain, 4, o8, o16));
    EXPECT_EQ(from_l1(o8), upper(from_hex("0061 00E9 00DF 007A")));
    for (int round = 0; round < 3000; ++round)
    {
        U16 s;
        int n = 1 + rnd() % 12;
        for (int i = 0; i < n; ++i)
            s += static_cast<char16_t>(rnd() % 4 == 0 ? 0x80 + rnd() % 128 : rnd() % 256);
        ASSERT_EQ(upper_l1(s), upper(s)) << show(s);
        ASSERT_EQ(lower_l1(s), lower(s)) << show(s);
        for (int f = 0; f < 4; ++f)
        {
            bool wide_n = false;
            ASSERT_EQ(norm_l1(kForms[f], s, &wide_n), norm(kForms[f], s)) << show(s);
            ASSERT_EQ(wide_n, !fits_latin1(norm(kForms[f], s)));
            std::vector<unsigned char> in = to_l1(s);
            ASSERT_EQ(uc::is_normalized(kForms[f], in.data(), in.size()), is_norm(kForms[f], s)) << show(s);
        }
    }
}

TEST(Unicode, IsNormalizedAgreesWithNormalize)
{
    std::vector<std::uint32_t> pool;
    for (std::uint32_t cp = 0x300; cp < 0x370; ++cp)
        pool.push_back(cp);
    const std::uint32_t extra[] = {0x41, 0x61, 0x65, 0x6F, 0xC0, 0xE9, 0x1EA0, 0x1EAD, 0x323, 0x302, 0x5B0, 0x5B1, 0x5BC, 0x5D1, 0x591,
                                   0x1100, 0x1161, 0x11A8, 0xAC00, 0xAC01, 0xD7A3, 0x1112, 0x1175, 0x11C2, 0x212B, 0x2126, 0xFB01, 0x1D15E,
                                   0x1D157, 0x1D165, 0x1D16D, 0x3099, 0x304B, 0x304C, 0x0F71, 0x0F72, 0x0F73, 0x0344, 0x0958, 0x0915, 0x093C,
                                   0x1F600, 0xD800, 0xDC00, 0x20, 0x2002, 0xA0, 0x1E0A, 0x1E0B, 0x1E0C, 0x0307, 0x0323, 0x0DDC, 0x0DCF, 0x0DCA};
    for (std::uint32_t cp : extra)
        pool.push_back(cp);
    for (int round = 0; round < 10000; ++round)
    {
        std::vector<std::uint32_t> cps;
        int n = 1 + rnd() % 10;
        for (int i = 0; i < n; ++i)
        {
            std::uint32_t cp = pool[rnd() % pool.size()];
            if (cp >= 0xD800 && cp < 0xE000)
            {
                cps.push_back(cp);
                continue;
            }
            cps.push_back(cp);
        }
        U16 s = from_cps(cps);
        for (int f = 0; f < 4; ++f)
        {
            U16 n1 = norm(kForms[f], s);
            ASSERT_EQ(is_norm(kForms[f], s), n1 == s) << kFormNames[f] << " " << show(s);
            ASSERT_TRUE(is_norm(kForms[f], n1)) << kFormNames[f] << " " << show(s);
            ASSERT_EQ(norm(kForms[f], n1), n1);
        }
        ASSERT_EQ(norm(uc::NFC, norm(uc::NFD, s)), norm(uc::NFC, s));
        ASSERT_EQ(norm(uc::NFD, norm(uc::NFC, s)), norm(uc::NFD, s));
        ASSERT_EQ(norm(uc::NFKC, norm(uc::NFKD, s)), norm(uc::NFKC, s));
    }
}

TEST(Unicode, SimpleMappings)
{
    EXPECT_EQ(uc::simple_upper('a'), 'A');
    EXPECT_EQ(uc::simple_upper(0xE9), 0xC9u);
    EXPECT_EQ(uc::simple_upper(0xDF), 0xDFu);
    EXPECT_EQ(uc::simple_upper(0xFF), 0x178u);
    EXPECT_EQ(uc::simple_lower('A'), 'a');
    EXPECT_EQ(uc::simple_lower(0x3A3), 0x3C3u);
    EXPECT_EQ(uc::simple_lower(0x130), 0x130u);
    EXPECT_EQ(uc::simple_fold(0x1E9E), 0xDFu);
    EXPECT_EQ(uc::simple_fold(0x3C2), 0x3C3u);
    EXPECT_EQ(uc::simple_fold(0x10400), 0x10428u);
    EXPECT_EQ(uc::combining_class(0x301), 230u);
    EXPECT_EQ(uc::combining_class(0x323), 220u);
    EXPECT_EQ(uc::combining_class('a'), 0u);
    EXPECT_TRUE(uc::is_cased('a'));
    EXPECT_FALSE(uc::is_cased('1'));
    EXPECT_TRUE(uc::is_case_ignorable(0x301));
}
