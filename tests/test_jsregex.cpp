#include <ct/jsregex.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using ct::JsCaptures;
using ct::JsRegex;
using ct::StringView;

namespace
{

    struct Case
    {
        const char16_t *pattern;
        int pattern_len;
        const char *flags;
        const char16_t *text;
        int text_len;
        int last_index;
        const char *expected;
    };

    const Case kCases[] = {
#include "jsregex_cases.inc"
    };

    unsigned parse_flags(const char *s, bool *sticky, bool *gy)
    {
        unsigned f = 0;
        *sticky = *gy = false;
        for (; *s; ++s)
        {
            switch (*s)
            {
            case 'g': f |= JsRegex::Global; *gy = true; break;
            case 'i': f |= JsRegex::IgnoreCase; break;
            case 'm': f |= JsRegex::Multiline; break;
            case 's': f |= JsRegex::DotAll; break;
            case 'u': f |= JsRegex::Unicode; break;
            case 'v': f |= JsRegex::UnicodeSets; break;
            case 'y': f |= JsRegex::Sticky; *sticky = true; *gy = true; break;
            case 'd': f |= JsRegex::HasIndices; break;
            }
        }
        return f;
    }

    void json_string(std::string &out, StringView v)
    {
        out += '"';
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            char c = v[i];
            if (c == '"' || c == '\\')
                out += '\\';
            out += c;
        }
        out += '"';
    }

    void span(std::string &out, const JsCaptures &c, std::size_t g)
    {
        if (!c.matched(g))
            out += "null";
        else
            out += "[" + std::to_string(c.start(g)) + "," + std::to_string(c.end(g)) + "]";
    }

    std::string format(const JsRegex &re, const JsCaptures &caps)
    {
        std::string out = "{\"r\":[";
        for (std::size_t g = 0; g <= re.group_count(); ++g)
        {
            if (g)
                out += ',';
            span(out, caps, g);
        }
        out += ']';
        bool named = false;
        for (std::size_t g = 1; g <= re.group_count(); ++g)
        {
            if (re.group_name(g).size() == 0)
                continue;
            out += named ? "," : ",\"g\":{";
            named = true;
            json_string(out, re.group_name(g));
            out += ':';
            span(out, caps, g);
        }
        if (named)
            out += '}';
        return out + "}";
    }

    std::string run16(const std::u16string &pat, const char *flags, const std::u16string &text, std::size_t last,
                      JsRegex::ExecResult *status = nullptr)
    {
        bool sticky, gy;
        unsigned f = parse_flags(flags, &sticky, &gy);
        JsRegex::Error err;
        JsRegex re = JsRegex::compile(pat.data(), pat.size(), f, &err);
        if (!re)
        {
            std::string out = "{\"e\":";
            json_string(out, StringView(err.message));
            return out + "}";
        }
        JsCaptures caps;
        JsRegex::ExecResult r = re.exec(text.data(), text.size(), gy ? last : 0, sticky, caps);
        if (status)
            *status = r;
        if (r == JsRegex::TooComplex)
            return "{\"tc\":1}";
        return r == JsRegex::NoMatch ? "null" : format(re, caps);
    }

    std::string run8(const std::u16string &pat, const char *flags, const std::u16string &text, std::size_t last)
    {
        bool sticky, gy;
        unsigned f = parse_flags(flags, &sticky, &gy);
        JsRegex re = JsRegex::compile(pat.data(), pat.size(), f);
        std::vector<unsigned char> t8;
        for (std::size_t i = 0; i < text.size(); ++i)
            t8.push_back(static_cast<unsigned char>(text[i]));
        JsCaptures caps;
        JsRegex::ExecResult r = re.exec(t8.data(), t8.size(), gy ? last : 0, sticky, caps);
        return r == JsRegex::NoMatch ? "null" : format(re, caps);
    }

    std::string one(const char16_t *pat, const char *flags, const char16_t *text, std::size_t last = 0)
    {
        return run16(pat, flags, text, last);
    }

    bool latin1(const std::u16string &s)
    {
        for (std::size_t i = 0; i < s.size(); ++i)
            if (s[i] > 255)
                return false;
        return true;
    }

} // namespace

TEST(JsRegex, NodeCases)
{
    int failed = 0, checked8 = 0;
    for (const Case &c : kCases)
    {
        std::u16string pat(c.pattern, static_cast<std::size_t>(c.pattern_len));
        std::u16string text(c.text, static_cast<std::size_t>(c.text_len));
        std::string got = run16(pat, c.flags, text, static_cast<std::size_t>(c.last_index));
        if (got != c.expected)
        {
            if (++failed <= 20)
            {
                std::string p8;
                for (char16_t u : pat)
                    p8 += u < 128 ? static_cast<char>(u) : '?';
                ADD_FAILURE() << "pattern=" << p8 << " flags=" << c.flags << " text_len=" << c.text_len
                              << " last=" << c.last_index << "\n  got      " << got << "\n  expected " << c.expected;
            }
            continue;
        }
        if (latin1(text) && std::strncmp(c.expected, "{\"e\"", 4) != 0)
        {
            ++checked8;
            std::string got8 = run8(pat, c.flags, text, static_cast<std::size_t>(c.last_index));
            if (got8 != c.expected && ++failed <= 20)
                ADD_FAILURE() << "latin1 mismatch flags=" << c.flags << "\n  got      " << got8 << "\n  expected " << c.expected;
        }
    }
    EXPECT_EQ(failed, 0);
    EXPECT_GT(checked8, 500);
}

TEST(JsRegex, SpecWhereV8Differs)
{
    EXPECT_EQ(one(u"[\\q{a}]", "iv", u"A"), "{\"r\":[[0,1]]}");
    EXPECT_EQ(one(u"[a--z]", "iv", u"A"), "{\"r\":[[0,1]]}");
    EXPECT_EQ(one(u"[a&&a]", "iv", u"A"), "{\"r\":[[0,1]]}");
    EXPECT_EQ(one(u"\\p{ASCII}", "iv", u"\x17F"), "{\"r\":[[0,1]]}");
    EXPECT_EQ(one(u"[\\p{ASCII}]", "iu", u"\x17F"), "{\"r\":[[0,1]]}");
    EXPECT_EQ(one(u"\\B", "u", u"a\xD83D\xDE00"), "{\"r\":[[3,3]]}");
    EXPECT_EQ(one(u"\\B", "gu", u"a\xD83D\xDE00", 2), "{\"r\":[[3,3]]}");
}

TEST(JsRegex, UnsupportedAndFlags)
{
    JsRegex::Error err;
    JsRegex re = JsRegex::compile(u"[\\q{ab}]", 8, JsRegex::UnicodeSets, &err);
    EXPECT_FALSE(re.valid());
    EXPECT_STREQ(err.message, "unsupported");
    re = JsRegex::compile(u"\\p{RGI_Emoji}", 13, JsRegex::UnicodeSets, &err);
    EXPECT_STREQ(err.message, "unsupported");
    re = JsRegex::compile(u"[^\\p{RGI_Emoji}]", 16, JsRegex::UnicodeSets, &err);
    EXPECT_STREQ(err.message, "Negated character class may contain strings");
    re = JsRegex::compile(u"a", 1, JsRegex::Unicode | JsRegex::UnicodeSets, &err);
    EXPECT_STREQ(err.message, "Invalid flags");
    re = JsRegex::compile(u"[\\q{a|b}--\\q{a}]", 16, JsRegex::UnicodeSets, &err);
    EXPECT_TRUE(re.valid());
    re = JsRegex::compile(u"(", 1, 0, &err);
    EXPECT_STREQ(err.message, "Unterminated group");
    EXPECT_EQ(err.offset, 1u);
}

TEST(JsRegex, Api)
{
    JsRegex::Error err;
    JsRegex re = JsRegex::compile(StringView("(?<year>\\d{4})-(?<mon>\\d\\d)(x)?"), JsRegex::Global | JsRegex::HasIndices, &err);
    ASSERT_TRUE(re.valid());
    EXPECT_EQ(re.flags(), JsRegex::Global | JsRegex::HasIndices);
    EXPECT_EQ(re.group_count(), 3u);
    EXPECT_TRUE(re.has_named_groups());
    EXPECT_EQ(re.group_index(StringView("mon")), 2);
    EXPECT_EQ(re.group_index(StringView("nope")), -1);
    EXPECT_EQ(re.group_name(1), StringView("year"));
    EXPECT_EQ(re.group_name(3).size(), 0u);
    EXPECT_EQ(re.source().size(), 31u);
    const char *t = "on 2024-05 and 1999-12x";
    JsCaptures c;
    ASSERT_EQ(re.exec(reinterpret_cast<const unsigned char *>(t), std::strlen(t), 0, false, c), JsRegex::Matched);
    EXPECT_EQ(c.start(), 3u);
    EXPECT_EQ(c.end(), 10u);
    EXPECT_FALSE(c.matched(3));
    EXPECT_EQ(c.groups(), 3u);
    ASSERT_EQ(re.exec(reinterpret_cast<const unsigned char *>(t), std::strlen(t), 10, false, c), JsRegex::Matched);
    EXPECT_EQ(c.start(), 15u);
    EXPECT_TRUE(c.matched(3));
    EXPECT_EQ(re.exec(reinterpret_cast<const unsigned char *>(t), std::strlen(t), 99, false, c), JsRegex::NoMatch);
    EXPECT_EQ(re.exec(reinterpret_cast<const unsigned char *>(t), std::strlen(t), 4, true, c), JsRegex::NoMatch);
}

TEST(JsRegex, Utf8Pattern)
{
    JsRegex re = JsRegex::compile(StringView("caf\xC3\xA9|\xF0\x9F\x98\x80+"), JsRegex::Unicode);
    ASSERT_TRUE(re.valid());
    std::u16string t = u"x caf\xE9 \xD83D\xDE00\xD83D\xDE00";
    JsCaptures c;
    ASSERT_EQ(re.exec(t.data(), t.size(), 0, false, c), JsRegex::Matched);
    EXPECT_EQ(c.start(), 2u);
    EXPECT_EQ(c.end(), 6u);
    ASSERT_EQ(re.exec(t.data(), t.size(), 6, false, c), JsRegex::Matched);
    EXPECT_EQ(c.start(), 7u);
    EXPECT_EQ(c.end(), 11u);
    JsRegex bad = JsRegex::compile(StringView("(?<a"), 0);
    EXPECT_FALSE(bad.valid());
}

TEST(JsRegex, Limits)
{
    JsRegex re = JsRegex::compile(StringView("(a*)*b"), 0);
    ASSERT_TRUE(re.valid());
    std::string s(100000, 'a');
    std::vector<unsigned char> t(s.begin(), s.end());
    JsRegex::Limits lim;
    lim.max_steps = 1000;
    re.set_limits(lim);
    JsCaptures c;
    EXPECT_EQ(re.exec(t.data(), t.size(), 0, false, c), JsRegex::TooComplex);
    EXPECT_EQ(re.limits().max_steps, 1000u);

    JsRegex deep = JsRegex::compile(StringView("(?:a|b)*c"), 0);
    lim = JsRegex::Limits();
    lim.max_stack = 1000;
    deep.set_limits(lim);
    EXPECT_EQ(deep.exec(t.data(), t.size(), 0, false, c), JsRegex::TooComplex);

    JsRegex ok = JsRegex::compile(StringView("(a|b)*$"), 0);
    EXPECT_EQ(ok.exec(t.data(), t.size(), 0, false, c), JsRegex::Matched);
    EXPECT_EQ(c.end(), t.size());
}

TEST(JsRegex, CatastrophicIsLinearWithMemo)
{
    JsRegex re = JsRegex::compile(StringView("(?:a+)+b"), 0);
    std::string s(5000, 'a');
    std::vector<unsigned char> t(s.begin(), s.end());
    JsCaptures c;
    EXPECT_EQ(re.exec(t.data(), t.size(), 0, false, c), JsRegex::NoMatch);
    JsRegex re2 = JsRegex::compile(StringView("^(\\w+\\s?)*$"), 0);
    std::string w;
    for (int i = 0; i < 40; ++i)
        w += "word ";
    w += "!";
    std::vector<unsigned char> t2(w.begin(), w.end());
    EXPECT_EQ(re2.exec(t2.data(), t2.size(), 0, false, c), JsRegex::NoMatch);
}

TEST(JsRegex, LongInputsDoNotCrash)
{
    std::u16string big(2000000, u'a');
    JsRegex re = JsRegex::compile(StringView("a*b|a+$"), 0);
    JsCaptures c;
    EXPECT_NE(re.exec(big.data(), big.size(), 0, false, c), JsRegex::NoMatch);
    JsRegex re2 = JsRegex::compile(StringView("(?:a)*?$"), 0);
    EXPECT_EQ(re2.exec(big.data(), big.size(), 0, false, c), JsRegex::Matched);
    EXPECT_EQ(c.end(), big.size());
    JsRegex re3 = JsRegex::compile(StringView("$(?<=a+)"), 0);
    EXPECT_EQ(re3.exec(big.data(), big.size(), 0, false, c), JsRegex::Matched);
}

TEST(JsRegex, DeepNestingIsRejected)
{
    std::u16string p(5000, u'(');
    p += u'a';
    p.append(5000, u')');
    JsRegex::Error err;
    JsRegex re = JsRegex::compile(p.data(), p.size(), 0, &err);
    EXPECT_FALSE(re.valid());
    EXPECT_STREQ(err.message, "Regular expression too large");
}

TEST(JsRegex, UnicodeTables)
{
    using namespace ct::detail::jsre;
    namespace uni = ct::detail::uni;
    EXPECT_STREQ(uni::kVersion, "17.0.0");
    EXPECT_EQ(std::strcmp(uni::kGcOrder[general_category('A')], "Lu"), 0);
    EXPECT_EQ(std::strcmp(uni::kGcOrder[general_category(0x3b1)], "Ll"), 0);
    EXPECT_EQ(std::strcmp(uni::kGcOrder[general_category(0x10FFFF)], "Cn"), 0);
    EXPECT_EQ(canon_upper('a'), static_cast<std::uint32_t>('A'));
    EXPECT_EQ(canon_upper(0x17F), 0x17Fu);
    EXPECT_EQ(canon_upper(0xDF), 0xDFu);
    EXPECT_EQ(canon_fold(0x17F), static_cast<std::uint32_t>('s'));
    EXPECT_EQ(canon_fold(0x212A), static_cast<std::uint32_t>('k'));
    EXPECT_EQ(canon_fold(0x1E9E), 0xDFu);
    EXPECT_TRUE(is_id_start(0x3b1));
    EXPECT_FALSE(is_id_start('1'));
    EXPECT_TRUE(is_id_continue('1'));
}
