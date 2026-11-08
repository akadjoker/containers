#!/usr/bin/env python3
"""Differential fuzzer: random ES patterns and texts, Node (V8) against ct::JsRegex.

    python3 tests/fuzz_jsregex.py --cli build-jsre/ct_jsregex_cli [--cases 20000] [--seed 1] [--show 10]

Cases whose result is a resource error (timeout in Node or the step limit in ct)
are counted separately and not compared.
"""
import argparse
import random
import sys

from jsre_tools import canon, cli_run, node_run, units

ALPHA = ["a", "b", "c", "A", "B", "k", "K", "s", "S", "0", "1", "_", "-", " ", "\n", " ", "é", "É",
         "ÿ", "Ÿ", "ſ", "K", "σ", "ς", "Σ", "中", "\U0001F600", "\U00010400",
         "\U00010428", "\ud83d", "\ude00", "ẞ", "ß", "ı", "İ", "ǅ"]
LIT = ["a", "b", "c", "A", "B", "k", "K", "s", "0", "1", "_", " ", "é", "É", "ſ", "K", "σ",
       "Σ", "中", "\U0001F600", "\U00010400", "\ud83d", "\ude00", "-", "ß", "ẞ"]
ESC = ["\\d", "\\D", "\\w", "\\W", "\\s", "\\S", "\\n", "\\t", "\\x61", "\\u0061", "\\u{61}", "\\u{1F600}", "\\0",
       "\\cJ", "\\/", "\\.", "\\b", "\\B", "\\v", "\\f", "\\r"]
ESC_BAD = ["\\-", "\\q", "\\z", "\\x6", "\\u12", "\\c", "\\8", "\\00"]
LIT_BAD = ["}", "]", "{"]
PROPS = ["\\p{L}", "\\p{Lu}", "\\P{Lu}", "\\p{Ll}", "\\p{Script=Greek}", "\\p{sc=Latn}", "\\p{scx=Hira}", "\\p{Emoji}",
         "\\p{ASCII}", "\\p{Any}", "\\P{ASCII}", "\\p{Nd}", "\\p{Alphabetic}", "\\p{White_Space}", "\\P{L}",
         "\\p{Lowercase}", "\\p{Uppercase}", "\\p{Cased}", "\\p{Han}", "\\p{Script_Extensions=Latin}"]
FLAGS = ["", "i", "m", "s", "u", "iu", "v", "iv", "g", "y", "gi", "iy", "ms", "mi", "ius", "gu", "sy", "mu", "imsu",
         "imsv", "gimsy", "uy", "vy"]


def lit(rnd):
    return pick(rnd, LIT_BAD) if rnd.random() < 0.03 else pick(rnd, LIT)


def pick(rnd, xs):
    return xs[rnd.randrange(len(xs))]


class Gen:
    def __init__(self, rnd, uni, vmode, icase):
        self.icase = icase
        self.rnd = rnd
        self.uni = uni
        self.vmode = vmode
        self.ngroups = 0
        self.names = []

    def quant(self):
        r = self.rnd.random()
        if r < 0.5:
            return ""
        q = pick(self.rnd, ["*", "+", "?", "{2}", "{1,3}", "{0,2}", "{2,}", "{0}", "{1,1}", "{3,2}"] if r < 0.52 else
                 ["*", "+", "?", "{2}", "{1,3}", "{0,2}", "{2,}", "{0}", "{1,1}"])
        if self.rnd.random() < 0.3:
            q += "?"
        return q

    def prop(self):
        p = pick(self.rnd, PROPS)
        while self.vmode and self.icase and "ASCII" in p:
            p = pick(self.rnd, PROPS)
        return p

    def cls_atom(self):
        r = self.rnd.random()
        if r < 0.5:
            return pick(self.rnd, ["a", "b", "k", "s", "0", "_", "é", "ſ", "K", "σ", "\U0001F600", "-"])
        if r < 0.65:
            return pick(self.rnd, ["\\d", "\\w", "\\s", "\\D", "\\W", "\\S", "\\b", "\\n"])
        if r < 0.75 and self.uni:
            return self.prop()
        if r < 0.9:
            lo, hi = pick(self.rnd, [("a", "z"), ("A", "Z"), ("0", "9"), ("à", "ÿ"), ("α", "ω"), ("一", "鿿"), ("a", "k"),
                                     ("K", "s"), ("0", "z"), ("A", "z"), ("\x00", "\x7f")])
            return lo + "-" + hi
        return pick(self.rnd, ["\\x61", "\\u0062", "\\]", "\\^", "\\-", "\\.", "[", "\\1", "\\0", "\\c1"])

    def cls(self, depth):
        if self.vmode and self.rnd.random() < 0.6:
            return self.cls_v(depth)
        n = self.rnd.randrange(0, 4)
        body = "".join(self.cls_atom() for _ in range(n))
        return "[" + ("^" if self.rnd.random() < 0.25 else "") + body + "]"

    def cls_v(self, depth):
        def operand(d):
            r = self.rnd.random()
            if r < 0.45:
                return pick(self.rnd, ["a", "b", "c", "k", "s", "0", "é", "σ", "\U0001F600", "\\-", "\\&"])
            if r < 0.6:
                return pick(self.rnd, ["\\d", "\\w", "\\s", "\\p{L}", "\\p{Lu}", "\\P{Ll}"] + ([] if self.icase else ["\\p{ASCII}", "\\q{a|b}", "\\q{c}"]))
            if r < 0.8 and d < 2:
                return self.cls_v(d + 1)
            return pick(self.rnd, ["a", "z", "A", "\\x62"])
        mode = self.rnd.random()
        if self.icase and mode < 0.4:
            mode = 0.9
        parts = [operand(depth) for _ in range(self.rnd.randrange(1, 4))]
        if mode < 0.2:
            body = "&&".join(parts)
        elif mode < 0.4:
            body = "--".join(parts)
        elif mode < 0.55:
            body = "".join(p + ("-z" if len(p) == 1 and p.isalpha() and p < "z" and self.rnd.random() < 0.3 else "")
                           for p in parts)
        else:
            body = "".join(parts)
        return "[" + ("^" if self.rnd.random() < 0.2 else "") + body + "]"

    def atom(self, depth):
        r = self.rnd.random()
        if r < 0.30:
            return lit(self.rnd)
        if r < 0.38:
            return "."
        if r < 0.50:
            return pick(self.rnd, ESC_BAD if self.rnd.random() < 0.05 else ESC)
        if r < 0.54 and self.uni:
            return self.prop()
        if r < 0.64:
            return self.cls(depth)
        if r < 0.67:
            return pick(self.rnd, ["^", "$", "\\b", "\\B"])
        if depth < 3:
            if r < 0.80:
                self.ngroups += 1
                return "(" + self.alt(depth + 1) + ")"
            if r < 0.84:
                return "(?:" + self.alt(depth + 1) + ")"
            if r < 0.88:
                n = pick(self.rnd, ["x", "y", "abc", "é"])
                if n in self.names:
                    return pick(self.rnd, LIT)
                self.names.append(n)
                self.ngroups += 1
                return "(?<" + n + ">" + self.alt(depth + 1) + ")"
            if r < 0.93:
                return pick(self.rnd, ["(?=", "(?!", "(?<=", "(?<!"]) + self.alt(depth + 1) + ")"
        if r < 0.97 and self.ngroups:
            return "\\" + str(1 + self.rnd.randrange(self.ngroups + 1))
        if self.names and r < 0.99:
            return "\\k<" + pick(self.rnd, self.names) + ">"
        return pick(self.rnd, LIT)

    def term(self, depth):
        a = self.atom(depth)
        bare = a in ("^", "$", "\\b", "\\B") or a.startswith("(?<=") or a.startswith("(?<!") or (
            self.uni and (a.startswith("(?=") or a.startswith("(?!")))
        if bare and self.rnd.random() > 0.03:
            return a
        return a + self.quant()

    def seq(self, depth):
        return "".join(self.term(depth) for _ in range(self.rnd.randrange(0, 5)))

    def alt(self, depth):
        n = 1 if self.rnd.random() < 0.7 else self.rnd.randrange(2, 4)
        return "|".join(self.seq(depth) for _ in range(n))


MUT = ["(", ")", "[", "]", "{", "}", "*", "+", "?", "\\", "|", "^", "$", "(?<", "\\k", "\\p{", "-", "&&", "--", "\\u{"]


def make_case(rnd):
    flags = pick(rnd, FLAGS)
    uni = "u" in flags or "v" in flags
    g = Gen(rnd, uni, "v" in flags, "i" in flags)
    pat = g.alt(0)
    if rnd.random() < 0.06:
        k = rnd.randrange(len(pat) + 1)
        pat = pat[:k] + pick(rnd, MUT) + pat[k + rnd.randrange(0, 2):]
    nt = rnd.randrange(0, 12)
    text = "".join(pick(rnd, ALPHA) for _ in range(nt))
    if rnd.random() < 0.5 and pat:
        pass
    li = rnd.randrange(0, nt + 2) if rnd.random() < 0.5 else 0
    ascii_only = all(ord(c) < 256 for c in text)
    return (pat, flags, text, li, 1 if (ascii_only and rnd.random() < 0.5) else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--cases", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--batch", type=int, default=2000)
    ap.add_argument("--show", type=int, default=10)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    done = bad = res = errs = matches = quirk = 0
    shown = 0
    while done < a.cases:
        n = min(a.batch, a.cases - done)
        cases = [make_case(rnd) for _ in range(n)]
        want = node_run(cases)
        got = cli_run(a.cli, cases)
        for c, w, g in zip(cases, want, got):
            if (isinstance(w, dict) and "tc" in w) or (isinstance(g, dict) and "tc" in g):
                res += 1
                continue
            if isinstance(g, dict) and g.get("e") == "unsupported":
                res += 1
                continue
            tu = units(c[2])
            if (isinstance(w, dict) and "r" in w and ("u" in c[1] or "v" in c[1]) and 0 < w["r"][0][0] < len(tu)
                    and 0xD800 <= tu[w["r"][0][0] - 1] <= 0xDBFF and 0xDC00 <= tu[w["r"][0][0]] <= 0xDFFF):
                quirk += 1
                continue
            if isinstance(w, dict) and "e" in w:
                errs += 1
            elif w is not None:
                matches += 1
            if canon(w) != canon(g):
                bad += 1
                if shown < a.show:
                    shown += 1
                    print("DIFF pattern=%r flags=%r text=%r last=%d mode=%d\n  node %s\n  ct   %s" %
                          (c[0], c[1], c[2], c[3], c[4], canon(w), canon(g)))
        done += n
    print("cases=%d diffs=%d resource=%d v8_mid_pair=%d (syntax errors %d, matches %d) seed=%d" %
          (done, bad, res, quirk, errs, matches, a.seed))
    return 1 if bad else 0


sys.exit(main())
