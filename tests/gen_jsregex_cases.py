#!/usr/bin/env python3
"""Runs Node (V8) over a case table and writes tests/jsregex_cases.inc.

Every expectation is what `RegExp.prototype.exec` returned (match and group spans
in UTF-16 code units, or the SyntaxError message without the V8 prefix), so
test_jsregex.cpp checks "same as Node" instead of hand-written guesses.

    python3 tests/gen_jsregex_cases.py
"""
import os
import subprocess
import sys

from jsre_tools import node_run

HERE = os.path.dirname(os.path.abspath(__file__))
HI, LO = "\ud83d", "\ude00"
EMO = "\U0001F600"
DSR = "\U00010400"
DSR_L = "\U00010428"

# (pattern, flags, text, lastIndex)
CASES = []


def add(p, f="", t="", li=0):
    CASES.append((p, f, t, li))


def addm(patterns, flags, texts):
    for p in patterns:
        for f in flags:
            for t in texts:
                add(p, f, t)


# literals, alternation, empty
addm(["abc", "a|b", "a|ab", "ab|a", "(?:)", "", "a|", "|a", "a||b", "x"], ["", "i"], ["abc", "xabcx", "", "b", "ab"])
# quantifiers
addm(["a*", "a+", "a?", "a{2}", "a{2,}", "a{1,2}", "a{0}", "a*?", "a+?", "a??", "a{2,3}?", "a{0,}", "a{1}", "(?:ab)*", "(?:ab)+?"],
     ["", "u"], ["", "a", "aaaa", "baaab", "ababab"])
addm(["a{,2}", "a{", "a{1", "a{1,", "a{x}", "x{1}{", "{", "}", "]", "a{2,1", "{}", "a{1,2}{", "a|{"], [""], ["a{,2}", "a{", "aa", "{", "}", "]", "a{1", "a{1,", "a{x}"])
# captures and reset semantics
for p, t in [("(a)|(b)", "b"), ("(a)|(b)", "a"), ("(?:(a)|b)*", "ab"), ("(?:(a)|(b))+", "abab"), ("(a*)*", "b"), ("(a*)*", "aa"), ("(a*)+", "b"),
             ("(?:a?)*?", "aa"), ("(?:a?)*", "aa"), ("(a?)*?b", "aab"), ("(a|ab)(c|bcd)(d*)", "abcd"), ("(z)((a+)?(b+)?(c))*", "zaacbbbcac"),
             ("(a*)*b", "aaab"), ("(?:(a)|b){2}", "ba"), ("(?:(a)|b){2}", "ab"), ("((a)|(b))+", "ab"), ("((a)|(b)){0}", "ab"), ("(a){0,2}", "aaa"),
             ("(a+)?", "b"), ("(a*)?", "b"), ("(?:a*)*", "b"), ("(?:a*)+", "b"), ("(a*){2,3}", "aab"), ("(?:(a)|(b))*?c", "abc"),
             ("(?:()|a)*", "aa"), ("(?:()|a)+", "aa"), ("(a?){3}", "aa"), ("(a?){3,}", "aa"), ("(?:a|())*b", "aab"), ("(a|b)*?c", "abc")]:
    add(p, "", t)
    add(p, "u", t)
# anchors, dot, multiline, dotall, line terminators
addm(["^a", "a$", "^$", "^", "$", "a.c", ".", "^.$", "\\Aa", "a\\b", "\\ba", "\\Ba", "a\\B", "\\b", "\\B"],
     ["", "m", "s", "u"], ["a", "a\nb", "a\rb", "a b", "a b", "\n", "", "abc", "a c", "a b"])
# classes and escapes
addm(["\\d+", "\\D+", "\\w+", "\\W+", "\\s+", "\\S+", "[\\d]", "[^\\d]", "[\\w-]+", "[a-z]+", "[^a-z]+", "[a-zA-Z0-9_]+", "[\\s\\S]", "[^]", "[]", "[^\\s]+", "[\\b]", "\\x41", "\\u0041", "\\101", "\\0", "\\cA", "\\ca"],
     ["", "u", "i"], ["abc DEF 123_x-y", "àé    ﻿", "Αα中文 ٠", "\x00\x01\x41A", "\n \t\v\f\r"])
addm(["\\t", "\\n", "\\v", "\\f", "\\r", "\\/", "\\.", "\\*", "\\\\", "\\-", "\\z", "\\8", "\\9", "\\12", "\\377", "\\400", "\\1", "\\c", "\\cz", "\\c1", "[\\c1]", "[\\c_]", "[\\ca]", "\\x1", "\\xg", "\\u00", "\\uzzzz", "\\u{41}", "\\k", "\\k<a>", "[\\1]", "[\\12]", "[\\8]", "[\\-a]", "[a\\-z]", "[\\d-z]", "[a-\\d]", "[\\w-a]", "[--a]", "[a--]", "[+--]", "[\\B]", "[\\k]", "[\\p{L}]", "\\p{L}", "\\P{L}", "\\pL"],
     ["", "u"], ["\t\n\v\f\r/.*\\-zA89\x01\x0a\x11\x1aĀa<>{L}pk\x00 12x"])
addm(["[a-z]", "[A-Z]", "[a-z]+", "[^a-z]", "\\w", "\\W", "[\\w]", "[^\\w]", "\\bk\\b", "\\bs\\b", "[k]", "[K]", "[s]", "[S]", "\\u212a", "\\u017f", "k", "s", "K", "S", "ß", "ẞ", "ı", "İ", "ǅ", "Ǆ", "ǆ", "σ", "ς", "Σ", "å", "Å", "Å", "ͅ", "ι", "ι", "[σ]", "[Σ]", "[Å]", "\\W", "[^\\W]", "\\P{Lu}", "\\p{Lu}", "\\p{Ll}", "\\P{Ll}", "[^\\P{Lu}]", "[\\P{Lu}]"],
     ["i", "iu", "iv", "u"], ["kKsSKſßẞ", "σςΣ", "åÅÅ", "ͅιιΙ", "ıİiI", "Ǆǅǆ", "aAzZ_09 "])
for p in ["a", "A", "[a]", "(a)\\1", "(?<x>a)\\k<x>", "(\\w)\\1", "(.)\\1", "[^a]", "é", "É", "ÿ", "Ÿ", "µ", "μ", "Μ", "ǅ", "ẞ", "ß", "Ω", "Ω", "ω", "İ", "ı", "I", "i"]:
    for f in ["i", "iu", "iv", ""]:
        add(p, f, "aAaAéÉÿŸµμΜǅẞßΩΩωİıIi")
add("(a)\\1", "i", "aA")
add("(a)\\1", "", "aA")
add("(é)\\1", "i", "éÉ")
add("(ß)\\1", "iu", "ßẞ")
add("(ß)\\1", "i", "ßẞ")
add("(ſ)\\1", "iu", "ſs")
add("(ſ)\\1", "i", "ſs")
add("(s)\\1", "i", "sſ")
add("(s)\\1", "iu", "sſ")
add("\\b", "iu", "ſ")
add("\\b", "i", "ſ")
add("\\w", "iu", "ſ")
add("\\w", "i", "ſ")
add("\\w", "iu", "K")
add("\\W", "iu", "K")
add("[^\\W]", "iu", "K")
add("[a-z]", "i", "K")
add("[a-z]", "iu", "K")
add("[a-z]", "i", "ſ")
add("[a-z]", "iu", "ſ")
add("ſ", "i", "s")
add("ſ", "iu", "s")
add("K", "iu", "K")
add("K", "i", "K")
# backreferences
for p, f, t in [("(a)\\1", "", "aa"), ("\\1(a)", "", "a"), ("(a\\1)", "", "aa"), ("(?:(a)|b)\\1", "", "b"), ("(a)|\\1b", "", "b"), ("\\2(a)(b)", "", "ab"),
                ("(a)\\2", "", "a\x02"), ("(a)\\2", "u", "aa"), ("\\1", "", "\x01"), ("\\1", "u", ""), ("(a)\\10", "", "a\x08"), ("(a)\\8", "", "a8"), ("(((((((((((a)))))))))))\\11", "", "aa"),
                ("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)\\10", "", "abcdefghijj"), ("(a)\\1*", "", "aaaa"), ("(a*)\\1", "", "aaaa"), ("(a*)b\\1+", "", "baaaac"),
                ("(?<n>a)\\k<n>", "", "aa"), ("\\k<n>(?<n>a)", "", "a"), ("(?<n>a)\\k<m>", "", "aa"), ("(?<n>a)\\k", "", "aa"), ("(?<n>a)\\k<n", "", "aa"), ("\\k<n>", "", "k<n>"),
                ("\\k<n>", "u", "k<n>"), ("(?<n>a)|\\k<n>b", "", "b"), ("(?<a>x)(?<b>y)\\k<b>\\k<a>", "", "xyyx"), ("(?<é>a)\\k<é>", "", "aa"),
                ("(?<\\u{61}b>x)\\k<ab>", "", "xx"), ("(?<a\\u0062>x)\\k<ab>", "", "xx"), ("(?<$_>x)", "", "x"), ("(?<中>x)", "", "x"), ("(?<\U00010400>x)", "", "x"),
                ("(?<𐐀>x)", "", "x"), ("(?<a>x)(?<a>y)", "", "xy"), ("(?<>x)", "", "x"), ("(?<1a>x)", "", "x"), ("(?<a-b>x)", "", "x"), ("(?<a", "", "x"), ("(?<a>", "", "x"),
                ("(?<=(a))b\\1", "", "ab"), ("(?<=\\1(a))b", "", "aab"), ("(?<=\\1(a))b", "", "ab")]:
    add(p, f, t)
# lookahead / lookbehind
for p, t in [("a(?=b)", "ab"), ("a(?=b)", "ac"), ("a(?!b)", "ab"), ("a(?!b)", "ac"), ("(?=(a))", "a"), ("(?=(a))\\1", "a"), ("(?!(a))", "b"), ("(?!(a))\\1", "b"), ("(?=a)*", "a"), ("(?=a)+", "a"), ("(?=a){2}", "a"),
             ("(?!a)*b", "b"), ("(?=(a+))a*b\\1", "baaabac"), ("(.*?)a(?!(a+)b\\2c)\\2(.*)", "baaabaac"), ("(?<=a)b", "ab"), ("(?<=a)b", "cb"), ("(?<!a)b", "ab"), ("(?<!a)b", "cb"),
             ("(?<=\\d{3})\\d", "1234"), ("(?<=\\d+)\\d", "12"), ("(?<=(\\d+)(\\d+))$", "1053"), ("(?<=([abc])+)d", "abcd"), ("(?<=a|bc)d", "bcd"), ("(?<=a*)b", "aab"),
             ("(?<!abc)\\w\\w\\w", "abcdef"), ("(?<=^a)b", "ab"), ("(?<=a$)b", "ab"), ("(?<=\\b)a", "a"), ("(?<=(?<=a)b)c", "abc"), ("(?<=(?=a)a)b", "ab"), ("(?<![a-c]+)d", "xd"),
             ("(?<=(a))\\1", "aa"), ("(?<=\\1(a))b", "aab"), ("(?<=(a)\\1)b", "aab"), ("(?<=a(?=b))b", "ab"), ("(?<!(a))b", "cb"), ("(?<!(a))\\1b", "cb"), ("(?<=(.))(.)", "ab"),
             ("(?<=a.*)b", "axb"), ("(?<=a.*?)b", "axb"), ("(?<=(a|ab)+)c", "abac"), ("(?<=x{2,3})y", "xxxy"), ("(?<=^|,)a", "x,a"), ("(?<=\\$)\\d+", "cost $42"), ("(?<=a)*b", "ab"), ("(?<=a)+b", "ab"),
             ("(?<!a)*b", "b"), ("(?=a)?b", "b"), ("(?!a){3}b", "b"), ("(?<=😀)a", "😀a"), ("(?<=.)a", "😀a"), ("a(?=\ud83d)", "a😀")]:
    add(p, "", t)
    add(p, "u", t)
# unicode: surrogates, astral
for p in [".", "^.$", "^..$", "[^a]", "\\S", "\\W", "\\u{1F600}", "\\ud83d\\ude00", "\\ud83d", "\\ude00", "\\udf06", "[😀]", "[\\ud83d\\ude00]", "[\\ud83d]", "[\\ude00]", EMO, EMO + "+", "\ud83d+", "[" + EMO + "-" + "\U0001F64F" + "]", "[^" + EMO + "]", "\\p{Emoji}", "\\P{ASCII}", "\\p{Lu}", "[\\u{1F600}-\\u{1F64F}]",
          "(?:" + EMO + ")+", ".{2}", ".{1,2}?", "\\b", "\\B", "(?<=" + EMO + ")", "(?=" + EMO + ")", "(?<=\\ud83d)\\ude00", DSR, DSR_L, "[" + DSR + "]", "\\u{10400}", "[\\u{10400}-\\u{10427}]", "\\udbff\\udfff", "\\u{10ffff}", "\\u{110000}", "\\u{}", "\\u{0000000000041}"]:
    for f in ["", "u", "v", "iu"]:
        for t in [EMO, "a" + EMO + "b", HI, LO + HI, HI + HI + LO, "\U00010400\U00010428"]:
            add(p, f, t)
for f in ["gu", "g", "yu", "y"]:
    for li in [0, 1, 2, 3]:
        for p in ["", ".", "x*", "\\udf06", "𝌆", "\\B"]:
            add(p, f, "𝌆x", li)
# sticky / global / lastIndex
for p, f, t, li in [("a", "g", "aaa", 1), ("a", "g", "aaa", 3), ("a", "g", "aaa", 4), ("a", "y", "baa", 0), ("a", "y", "baa", 1), ("a", "y", "baa", 2), ("a", "gy", "aab", 1), ("^a", "gy", "aa", 1), ("^a", "gm", "b\na", 1),
                    ("a", "", "aaa", 2), ("(?:)", "y", "abc", 3), ("(?:)", "y", "abc", 4), ("\\b", "g", "ab cd", 2), ("$", "g", "ab", 2), ("^", "gm", "a\nb", 2), ("a|b", "gy", "ab", 1), ("(?<=a)b", "y", "ab", 1), ("b", "y", "ab", 1)]:
    add(p, f, t, li)
# modes: flags
addm(["a.b", "a.b$", "^a", "a$"], ["", "s", "m", "ms", "gms", "dgimsuy"], ["a\nb", "a\nb\n", "\na", "a\n"])
# named groups results
for p, t in [("(?<y>\\d{4})-(?<m>\\d{2})-(?<d>\\d{2})", "on 2024-05-06!"), ("(?<a>a)|(?<b>b)", "b"), ("(?<a>a)?(?<b>b)", "b"), ("(?<x>.)(?<y>.)", EMO), ("(?<é>a)", "a"), ("(?<w>\\w+)\\s(?<w2>\\w+)", "hello world"),
             ("(?:(?<a>a)|b)+", "ab"), ("(?<a>a)(?<b>(?<c>c)?)", "ac"), ("(?<n>)", ""), ("(?<A>a)(?<a>b)", "ab")]:
    add(p, "", t)
    add(p, "u", t)
# property escapes
props = ["L", "Lu", "Ll", "Lt", "Lm", "Lo", "LC", "M", "Mn", "Mc", "Me", "N", "Nd", "Nl", "No", "P", "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "S", "Sm", "Sc", "Sk", "So", "Z", "Zs", "Zl", "Zp", "C", "Cc", "Cf", "Cs", "Co", "Cn",
         "Letter", "Uppercase_Letter", "Lowercase_Letter", "Cased_Letter", "Mark", "Combining_Mark", "Number", "Decimal_Number", "digit", "Punctuation", "punct", "Symbol", "Separator", "Other", "Control", "cntrl", "Unassigned", "Private_Use", "Surrogate",
         "General_Category=Lu", "gc=Ll", "General_Category=Letter", "gc=Nd", "Script=Greek", "sc=Grek", "Script=Latin", "sc=Latn", "Script=Han", "sc=Hani", "Script=Cyrillic", "Script=Hiragana", "Script=Katakana", "Script=Arabic", "Script=Hebrew", "Script=Thai",
         "Script=Common", "Script=Inherited", "Script=Unknown", "sc=Zzzz", "Script_Extensions=Greek", "scx=Grek", "scx=Latn", "scx=Hira", "scx=Kana", "scx=Deva", "Script_Extensions=Common", "scx=Zyyy",
         "ASCII", "ASCII_Hex_Digit", "AHex", "Alphabetic", "Alpha", "Any", "Assigned", "Bidi_Control", "Bidi_Mirrored", "Case_Ignorable", "Cased", "Changes_When_Casefolded", "CWCM", "CWL", "CWT", "CWU", "CWKCF", "Dash", "Default_Ignorable_Code_Point", "Deprecated", "Diacritic",
         "Emoji", "Emoji_Component", "Emoji_Modifier", "Emoji_Modifier_Base", "Emoji_Presentation", "Extended_Pictographic", "Extender", "Grapheme_Base", "Grapheme_Extend", "Hex_Digit", "IDS_Binary_Operator", "IDS_Trinary_Operator", "ID_Continue", "ID_Start",
         "Ideographic", "Join_Control", "Logical_Order_Exception", "Lowercase", "Math", "Noncharacter_Code_Point", "Pattern_Syntax", "Pattern_White_Space", "Quotation_Mark", "Radical", "Regional_Indicator", "Sentence_Terminal", "Soft_Dotted", "Terminal_Punctuation",
         "Unified_Ideograph", "Uppercase", "Variation_Selector", "White_Space", "space", "XID_Continue", "XID_Start"]
PTEXT = "aZ9_ éÉǅαΆः⃝Ⅰ²٠-()!?$+^~©  \u0000‍あア中اאกक" + EMO + "\U0001F3FB\ud800\U0001d7d8\U0010ffff﷐"
for pr in props:
    add("\\p{" + pr + "}+", "u", PTEXT)
    add("\\P{" + pr + "}+", "u", PTEXT)
    add("[\\p{" + pr + "}]+", "v", PTEXT)
for pr in ["Lu", "Greek", "Script=Greek", "Alphabetic"]:
    add("\\p{" + pr + "}+", "iu", "aAbαΑ")
    add("\\P{" + pr + "}+", "iu", "aAbαΑ")
    add("\\p{" + pr + "}+", "iv", "aAbαΑ")
    add("\\P{" + pr + "}+", "iv", "aAbαΑ")
for pr in ["Foo", "", "L ", " L", "l", "lu", "Script", "Script=", "=Greek", "Script=Foo", "Script=greek", "gc=", "General_Category", "Greek", "Hrkt", "Script=Hrkt", "Basic_Emoji", "RGI_Emoji", "Emoji_Keycap_Sequence", "Script=Latin=x", "L}", "Lu\u0000", "ASCII_Hex", "IDS", "Block=Basic_Latin", "InGreek", "Is Letter", "Any ", "Line_Break=Alphabetic", "Age=6.0", "Numeric_Type=Decimal", "Bidi_Class=L", "East_Asian_Width=W"]:
    add("\\p{" + pr + "}", "u", "a")
    add("[\\p{" + pr + "}]", "u", "a")
    add("\\P{" + pr + "}", "v", "a")
for p in ["\\p{L", "\\p{", "\\p", "\\p{}", "\\pL", "\\P", "[\\p", "[\\p{L", "\\p{L}", "\\p{L}"]:
    add(p, "", "p{L}")
    add(p, "u", "a")
# v mode
for p, t in [("[\\p{L}--[a-z]]+", "abcDEF"), ("[\\p{L}&&\\p{ASCII}]+", "abcdé"), ("[[a-z]--[aeiou]]+", "hello"), ("[\\w--\\d]+", "ab12cd"), ("[\\w&&\\d]+", "ab12cd"), ("[a-z&&[^aeiou]]", "ae"), ("[^[a-z]--[a-c]]", "abcdef"),
             ("[[a-c][x-z]]+", "abcxyz-"), ("[\\q{a|b|c}]+", "abcd"), ("[\\q{a}]", "a"), ("[\\q{}]", "a"), ("[\\q{ab}]", "ab"), ("[\\q{ab|c}]", "ab"), ("[^\\q{a|b}]", "ac"), ("[^\\q{ab}]", "a"), ("[\\q{ab}--\\q{ab}]", "ab"),
             ("[a&&&b]", "a"), ("[a&&b--c]", "a"), ("[(]", "("), ("[)]", ")"), ("[a-z&&b]", "b"), ("[&&]", "a"), ("[a&&]", "a"), ("[--]", "a"), ("[a--]", "a"), ("[a!!b]", "a"), ("[a||b]", "a"), ("[\\d-a]", "a"), ("[z-a]", "a"),
             ("[a", "a"), ("[\\q{a", "a"), ("[\\q]", "a"), ("[\\&]", "&"), ("[\\a]", "a"), ("[a--b--c]", "a"), ("[[a]--b&&c]", "a"), ("[\\p{RGI_Emoji}]", "a"), ("\\p{RGI_Emoji}", "a"), ("[^\\p{RGI_Emoji}]", "a"), ("\\P{RGI_Emoji}", "a"),
             ("[\\-]", "-"), ("[-]", "-"), ("[a-]", "-"), ("[a-z-]", "-"), ("[a-z-b]", "-"), ("[\\&\\-\\!\\#\\%\\,\\:;\\<\\=\\>\\@\\`\\~]+", "&-!#%,:;<=>@`~"), ("[&]", "&"), ("[!!]", "!"), ("[##]", "#"), ("[a&b]", "&"), ("[a-b-c]", "a"),
             ("[\\u{1F600}-\\u{1F64F}]", EMO), ("[[\\d]--[5]]+", "01256"), ("[\\d--[4-6]]+", "012789"), ("[^[^a]]", "a"), ("[^\\d]", "a"), ("[\\D&&\\w]+", "a1b"), ("[\\S--\\w]+", "ab,;"), ("[\\P{L}--\\d]+", "1,;a"), ("[a-z&&[^b]]+", "abc"),
             ("\\q{a}", "a"), ("[\\b]", "\b"), ("[\\B]", "B"), ("[\\x41-\\x43]+", "ABCD"), ("[\\cJ]", "\n"), ("[\\0]", "\x00"), ("[\\01]", "\x01"), ("[\\1]", "\x01"), ("[/]", "/"), ("[\\/]", "/"), ("[{]", "{"), ("[}]", "}"), ("[|]", "|"), ("[\\|]", "|"), ("[\\{\\}\\(\\)\\[\\]\\/\\\\\\|]+", "{}()[]/\\|")]:
    for f in ["v", "u", "iv"]:
        add(p, f, t)
for p in ["[ß]", "[ẞ]", "[a-z]", "[^a-z]", "[\\w]", "[\\W]", "[^\\W]", "[\\p{Ll}]", "[\\p{Lu}]", "[^\\p{Lu}]", "[\\P{Lu}]", "[^\\P{Lu}]", "[\\P{Ll}]", "[^\\P{Ll}]", "\\P{Lu}", "\\P{Ll}", "[\\p{Lu}--[A-Z]]", "[\\p{Ll}&&[a-z]]", "[a-z--[b-y]]", "[σ]", "[ς]", "[\\u212a]", "[^\\u212a]", "[k]", "[\\q{k}]", "[\\q{K}]", "\\W", "\\S", "\\D", "[^\\D]"]:
    add(p, "iv", "aAbBzZkKKßẞσςΣıİ _")
    add(p, "v", "aAbBzZkKKßẞσςΣıİ _")
    add(p, "iu", "aAbBzZkKKßẞσςΣıİ _")
# syntax errors
for p in ["(", "(a", "a)", ")", "*", "+a", "?", "a**", "a{2,1}", "{1}", "a{1,2}{3}", "[", "[a", "[b-a]", "\\", "(?", "(?x)", "(?<a", "(?<=", "(?<!", "(?=", "(?!", "(?:", "(?i:a)", "(?-i:a)", "(?<a>b)(?<a>c)", "(?<=a)*", "(?<!a)+", "(?=a)*", "^*", "$+", "\\b+", "\\B?", "a|*", "(*)", "(?:*)",
          "a{99999999999}", "a{99999999999,1}", "a{1}?{", "x{1}{2}", "a+*", "a*?+", "a??*", "[a-z", "[\\d-", "]", "}", "{", "\\8", "\\u{110000}", "\\u{", "\\u{41", "\\u{g}", "\\ud83d\\ude0", "\\x4", "\\c", "\\c1", "\\0a", "\\00", "\\01", "\\1", "\\k<a>", "\\k", "\\-", "\\p{L}", "\\z", "\\_", "\\ ", "\\a", "\\e", "\\B+", "(?<a>.)\\k<b>", "(?<a>.)\\k<a", "(?<a>.)\\k<>", "(?<a>.)\\k<1>"]:
    for f in ["", "u", "v"]:
        add(p, f, "a")
for f in ["", "u"]:
    for p in ["(?<😀>a)", "(?<a\\u{110000}>a)", "(?<\\x61>a)", "(?<a\\u12>a)", "(?<\\u0030>a)", "(?<a b>a)", "(?<a‍>a)", "(?<a‌>a)", "(?<ª>a)", "(?<\\ud83d\\ude00>a)", "(?<\\ud801\\udc00>a)", "(?<\\ud801>a)", "(?<\ud801>a)", "(?<℘>a)", "(?<℘·>a)"]:
        add(p, f, "a")
add("a", "uv", "a")
# nested depth and size
add("(" * 100 + "a" + ")" * 100, "", "a")
add("(?:" * 300 + "a" + ")" * 300, "", "a")
add("a" * 200, "", "a" * 200)
add("(?:a|b|c|d|e|f|g|h)+", "", "abcdefghabcdefgh" * 4)
add("(a|b)*c", "", "ab" * 100 + "c")
add("(?:a+)+b", "", "a" * 30 + "b")
add("(?:a+)+b", "", "a" * 40 + "c")
add("(a*)*b", "", "a" * 40 + "c")
add("(a|aa)+$", "", "a" * 40 + "b")
add("(.*)(.*)(.*)(.*)(.*)x", "", "a" * 60)
add("^(\\w+\\s?)*$", "", "word " * 20 + "!")
add("(x+x+)+y", "", "x" * 40)
add("a{1000}", "", "a" * 1000)
add("a{2,1000}", "", "a" * 500)
add("(?:ab){100,200}", "", "ab" * 150)
add("(ab){3}", "", "ababab")
add("(?:a{2}){3}", "", "a" * 7)
add("(?:a{1,3}){2,4}b", "", "aaaaab")
add("[\\s\\S]*?x", "", "ab\ncdx")
add(".*?x", "s", "ab\ncdx")
add("(?:.*?,){3}x", "", "a,b,c,x")
# latin-1 and misc text
for p in ["é+", "[à-ÿ]+", "\\xe9", "\\u00e9", "(?i)é", "ÿ", "Ā", "[ÿ-Ā]", "\\w+", "\\W+", "\\s+", "[^é]+", "µ", "À", "[À-Þ]"]:
    for f in ["", "i", "u", "iu"]:
        add(p, f, "café ÉCOLE ÿŸµΜÀà ")
for t in ["", "a", "\x00", "￿", "a\u0000b"]:
    for p in [".", "^$", "[^]", "\\0", "\\x00", "\\uffff", "[\\u0000-\\uffff]"]:
        add(p, "", t)
        add(p, "u", t)

def cu16(s):
    b = s.encode("utf-16-le", "surrogatepass")
    units = [b[i] | (b[i + 1] << 8) for i in range(0, len(b), 2)]
    out = []
    prev_esc = False
    for u in units:
        ch = chr(u)
        if 0x20 <= u < 0x7F and ch not in '"\\?' and not (prev_esc and ch in "0123456789abcdefABCDEF"):
            out.append(ch)
            prev_esc = False
        else:
            out.append('\\x%X""' % u)
            prev_esc = False
    return 'u"' + "".join(out) + '", %d' % len(units)


def main():
    seen = set()
    cases = []
    for c in CASES:
        if c not in seen:
            seen.add(c)
            cases.append(c)
    results = node_run(cases)
    import json
    ver = subprocess.run(["node", "-p", "process.version + ' V8 ' + process.versions.v8 + ' Unicode ' + process.versions.unicode"],
                         capture_output=True, text=True).stdout.strip()
    lines = ["// Generated by tests/gen_jsregex_cases.py with Node %s. Do not edit." % ver,
             "// {pattern, pattern_len, flags, text, text_len, last_index, expected}"]
    nerr = nmatch = 0
    from jsre_tools import units
    for c, r in zip(cases, results):
        if isinstance(r, dict) and "tc" in r:
            continue
        uni = "u" in c[1] or "v" in c[1]
        tu = units(c[2])
        if "u" in c[1] and "v" in c[1]:
            continue
        if "RGI_Emoji" in c[0] or "\\q{ab" in c[0] or "\\q{}" in c[0] or "\\q{ab|c}" in c[0]:
            continue
        if uni and isinstance(r, dict) and "r" in r and 0 < r["r"][0][0] < len(tu) and 0xD800 <= tu[r["r"][0][0] - 1] <= 0xDBFF and 0xDC00 <= tu[r["r"][0][0]] <= 0xDFFF:
            continue
        if uni and ("g" in c[1] or "y" in c[1]) and 0 < c[3] < len(tu) and 0xD800 <= tu[c[3] - 1] <= 0xDBFF and 0xDC00 <= tu[c[3]] <= 0xDFFF:
            continue
        if "v" in c[1] and "i" in c[1] and ("--" in c[0] or "&&" in c[0] or "\\q{" in c[0] or "ASCII" in c[0]):
            continue
        if isinstance(r, dict) and "e" in r:
            nerr += 1
        elif r is not None:
            nmatch += 1
        exp = json.dumps(r, separators=(",", ":"), ensure_ascii=False)
        lines.append('{%s, "%s", %s, %d, R"JS(%s)JS"},' % (cu16(c[0]), c[1], cu16(c[2]), c[3], exp))
    with open(os.path.join(HERE, "jsregex_cases.inc"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("wrote %d cases (%d matches, %d syntax errors)" % (len(lines) - 2, nmatch, nerr))


main()
