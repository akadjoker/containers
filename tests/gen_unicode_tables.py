#!/usr/bin/env python3
"""Generates include/ct/detail/unicode_tables.hpp from the UCD.

Downloads once into tests/ucd/ (ignored by git) the files of the Unicode version
that Node reports (`node -p process.versions.unicode`) and emits sorted compact
tables: General_Category and Script runs, Script_Extensions ranges, binary
properties as toggle points, simple uppercase, lowercase and case folding as
delta ranges, the unconditional SpecialCasing sequences, and the normalization
data (combining classes and quick-check bits as runs, decomposition mappings,
composition pairs). Also downloads NormalizationTest.txt for the tests.

    python3 tests/gen_unicode_tables.py [version]
"""
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
UCD = os.path.join(HERE, "ucd")
OUT = os.path.join(HERE, "..", "include", "ct", "detail", "unicode_tables.hpp")
MAXCP = 0x10FFFF

FILES = [
    "UnicodeData.txt", "CaseFolding.txt", "Scripts.txt", "ScriptExtensions.txt",
    "PropList.txt", "DerivedCoreProperties.txt", "emoji/emoji-data.txt",
    "PropertyValueAliases.txt", "DerivedNormalizationProps.txt", "SpecialCasing.txt", "NormalizationTest.txt",
]

GC_ORDER = ["Cc", "Cf", "Cn", "Co", "Cs", "Ll", "Lm", "Lo", "Lt", "Lu", "Mc", "Me", "Mn", "Nd", "Nl", "No",
            "Pc", "Pd", "Pe", "Pf", "Pi", "Po", "Ps", "Sc", "Sk", "Sm", "So", "Zl", "Zp", "Zs"]

BINARY = [
    ("ASCII_Hex_Digit", "AHex", "PropList.txt"),
    ("Alphabetic", "Alpha", "DerivedCoreProperties.txt"),
    ("Bidi_Control", "Bidi_C", "PropList.txt"),
    ("Bidi_Mirrored", "Bidi_M", "UnicodeData.txt"),
    ("Case_Ignorable", "CI", "DerivedCoreProperties.txt"),
    ("Cased", None, "DerivedCoreProperties.txt"),
    ("Changes_When_Casefolded", "CWCF", "DerivedCoreProperties.txt"),
    ("Changes_When_Casemapped", "CWCM", "DerivedCoreProperties.txt"),
    ("Changes_When_Lowercased", "CWL", "DerivedCoreProperties.txt"),
    ("Changes_When_NFKC_Casefolded", "CWKCF", "DerivedNormalizationProps.txt"),
    ("Changes_When_Titlecased", "CWT", "DerivedCoreProperties.txt"),
    ("Changes_When_Uppercased", "CWU", "DerivedCoreProperties.txt"),
    ("Dash", None, "PropList.txt"),
    ("Default_Ignorable_Code_Point", "DI", "DerivedCoreProperties.txt"),
    ("Deprecated", "Dep", "PropList.txt"),
    ("Diacritic", "Dia", "PropList.txt"),
    ("Emoji", None, "emoji/emoji-data.txt"),
    ("Emoji_Component", "EComp", "emoji/emoji-data.txt"),
    ("Emoji_Modifier", "EMod", "emoji/emoji-data.txt"),
    ("Emoji_Modifier_Base", "EBase", "emoji/emoji-data.txt"),
    ("Emoji_Presentation", "EPres", "emoji/emoji-data.txt"),
    ("Extended_Pictographic", "ExtPict", "emoji/emoji-data.txt"),
    ("Extender", "Ext", "PropList.txt"),
    ("Grapheme_Base", "Gr_Base", "DerivedCoreProperties.txt"),
    ("Grapheme_Extend", "Gr_Ext", "DerivedCoreProperties.txt"),
    ("Hex_Digit", "Hex", "PropList.txt"),
    ("IDS_Binary_Operator", "IDSB", "PropList.txt"),
    ("IDS_Trinary_Operator", "IDST", "PropList.txt"),
    ("ID_Continue", "IDC", "DerivedCoreProperties.txt"),
    ("ID_Start", "IDS", "DerivedCoreProperties.txt"),
    ("Ideographic", "Ideo", "PropList.txt"),
    ("Join_Control", "Join_C", "PropList.txt"),
    ("Logical_Order_Exception", "LOE", "PropList.txt"),
    ("Lowercase", "Lower", "DerivedCoreProperties.txt"),
    ("Math", None, "DerivedCoreProperties.txt"),
    ("Noncharacter_Code_Point", "NChar", "PropList.txt"),
    ("Pattern_Syntax", "Pat_Syn", "PropList.txt"),
    ("Pattern_White_Space", "Pat_WS", "PropList.txt"),
    ("Quotation_Mark", "QMark", "PropList.txt"),
    ("Radical", None, "PropList.txt"),
    ("Regional_Indicator", "RI", "PropList.txt"),
    ("Sentence_Terminal", "STerm", "PropList.txt"),
    ("Soft_Dotted", "SD", "PropList.txt"),
    ("Terminal_Punctuation", "Term", "PropList.txt"),
    ("Unified_Ideograph", "UIdeo", "PropList.txt"),
    ("Uppercase", "Upper", "DerivedCoreProperties.txt"),
    ("Variation_Selector", "VS", "PropList.txt"),
    ("White_Space", "space", "PropList.txt"),
    ("XID_Continue", "XIDC", "DerivedCoreProperties.txt"),
    ("XID_Start", "XIDS", "DerivedCoreProperties.txt"),
]
SPECIAL = [("Any", None), ("ASCII", None), ("Assigned", None)]


def fetch(version):
    os.makedirs(UCD, exist_ok=True)
    for f in FILES:
        path = os.path.join(UCD, os.path.basename(f))
        if os.path.exists(path):
            continue
        url = "https://www.unicode.org/Public/%s/ucd/%s" % (version, f)
        print("downloading", url, file=sys.stderr)
        urllib.request.urlretrieve(url, path)


def lines(name):
    with open(os.path.join(UCD, os.path.basename(name)), encoding="utf-8") as fh:
        for ln in fh:
            ln = ln.split("#", 1)[0].strip()
            if ln:
                yield [x.strip() for x in ln.split(";")]


def rng(s):
    if ".." in s:
        a, b = s.split("..")
        return int(a, 16), int(b, 16)
    v = int(s, 16)
    return v, v


def merge(rs):
    rs = sorted(rs)
    out = []
    for lo, hi in rs:
        if out and lo <= out[-1][1] + 1:
            if hi > out[-1][1]:
                out[-1][1] = hi
        else:
            out.append([lo, hi])
    return out


def toggles(rs):
    pts = []
    for lo, hi in merge(rs):
        pts.append(lo)
        pts.append(hi + 1)
    return pts


def case_ranges(m):
    items = sorted(m.items())
    out = []
    i = 0
    while i < len(items):
        cp, tg = items[i]
        delta = tg - cp
        step = 1
        last = cp
        j = i + 1
        if j < len(items) and items[j][1] - items[j][0] == delta and items[j][0] - cp in (1, 2):
            step = items[j][0] - cp
        while j < len(items) and items[j][1] - items[j][0] == delta and items[j][0] - last == step:
            last = items[j][0]
            j += 1
        out.append((cp, last, delta, step))
        i = j
    return out


def arr(vals, per=16):
    out = []
    for k in range(0, len(vals), per):
        out.append("        " + ",".join(str(v) for v in vals[k:k + per]))
    return ",\n".join(out)


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else subprocess.check_output(
        ["node", "-p", "process.versions.unicode"]).decode().strip()
    if version.count(".") == 1:
        version += ".0"
    fetch(version)

    gc = [2] * (MAXCP + 1)
    mirrored = []
    upper = {}
    lower = {}
    ccc = {}
    decomp = {}
    first = None
    for f in lines("UnicodeData.txt"):
        cp = int(f[0], 16)
        name = f[1]
        cat = GC_ORDER.index(f[2])
        if name.endswith(", First>"):
            first = cp
            continue
        lo = first if (name.endswith(", Last>") and first is not None) else cp
        first = None
        for c in range(lo, cp + 1):
            gc[c] = cat
        if f[9] == "Y":
            mirrored.append((cp, cp))
        if f[12]:
            upper[cp] = int(f[12], 16)
        if f[13]:
            lower[cp] = int(f[13], 16)
        if f[3] != "0":
            ccc[cp] = int(f[3])
        if f[5]:
            compat = f[5].startswith("<")
            decomp[cp] = (compat, [int(x, 16) for x in f[5].split() if not x.startswith("<")])
    multi = set()
    sp_upper = {}
    sp_lower = {}
    for f in lines("SpecialCasing.txt"):
        if len(f) > 4 and f[4] == "" or len(f) == 4:
            cp = int(f[0], 16)
            if len(f[3].split()) != 1:
                multi.add(cp)
                sp_upper[cp] = [int(x, 16) for x in f[3].split()]
            if len(f[1].split()) != 1:
                sp_lower[cp] = [int(x, 16) for x in f[1].split()]
    upper = {k: v for k, v in upper.items() if k not in multi}
    lower = {k: v for k, v in lower.items() if k not in sp_lower}

    fold = {}
    for f in lines("CaseFolding.txt"):
        if f[1] in ("C", "S"):
            fold[int(f[0], 16)] = int(f[2], 16)

    qc_bits = {("NFD_QC", "N"): 8, ("NFKD_QC", "N"): 9, ("NFC_QC", "N"): 10, ("NFC_QC", "M"): 11,
               ("NFKC_QC", "N"): 12, ("NFKC_QC", "M"): 13}
    nfp = [0] * (MAXCP + 1)
    for c, v in ccc.items():
        nfp[c] = v
    fce = []
    for f in lines("DerivedNormalizationProps.txt"):
        if len(f) > 2 and (f[1], f[2]) in qc_bits:
            lo, hi = rng(f[0])
            b = 1 << qc_bits[(f[1], f[2])]
            for c in range(lo, hi + 1):
                nfp[c] |= b
        elif len(f) > 1 and f[1] == "Full_Composition_Exclusion":
            fce.append(rng(f[0]))
    fce_set = set()
    for lo, hi in fce:
        fce_set.update(range(lo, hi + 1))
    nf_start, nf_val = [], []
    for c in range(MAXCP + 1):
        if not nf_val or nf_val[-1] != nfp[c]:
            nf_start.append(c)
            nf_val.append(nfp[c])
    dec_cp = sorted(decomp)
    dec_val = []
    dec_pool = []
    for c in dec_cp:
        compat, seq = decomp[c]
        dec_val.append(len(dec_pool) * 2 + (1 if compat else 0))
        dec_pool.extend(seq)
    dec_val.append(len(dec_pool) * 2)
    comp = []
    for c in dec_cp:
        compat, seq = decomp[c]
        if not compat and len(seq) == 2 and c not in fce_set:
            comp.append((seq[0], seq[1], c))
    comp.sort()

    sp_cps = sorted(sp_upper)
    sp_off = []
    sp_pool = []
    for c in sp_cps:
        sp_off.append(len(sp_pool))
        sp_pool.extend(sp_upper[c])
    sp_off.append(len(sp_pool))
    sl_cps = sorted(sp_lower)
    sl_off = []
    for c in sl_cps:
        sl_off.append(len(sp_pool))
        sp_pool.extend(sp_lower[c])
    sl_off.append(len(sp_pool))

    gc_start, gc_val = [], []
    for c in range(MAXCP + 1):
        if not gc_val or gc_val[-1] != gc[c]:
            gc_start.append(c)
            gc_val.append(gc[c])

    sc_names = []
    sc_alias = []
    for f in lines("PropertyValueAliases.txt"):
        if f[0] == "sc" and f[1] != "Hrkt":
            sid = len(sc_names)
            sc_names.append(f[1])
            for a in f[1:]:
                if a:
                    sc_alias.append((a, sid))
    zz = sc_names.index("Zzzz")
    short2id = {n: i for i, n in enumerate(sc_names)}
    alias2id = dict(sc_alias)
    sc = [zz] * (MAXCP + 1)
    for f in lines("Scripts.txt"):
        lo, hi = rng(f[0])
        sid = alias2id[f[1]]
        for c in range(lo, hi + 1):
            sc[c] = sid
    sc_start, sc_val = [], []
    for c in range(MAXCP + 1):
        if not sc_val or sc_val[-1] != sc[c]:
            sc_start.append(c)
            sc_val.append(sc[c])

    scx_rows = []
    for f in lines("ScriptExtensions.txt"):
        lo, hi = rng(f[0])
        ids = sorted(alias2id[s] for s in f[1].split())
        scx_rows.append((lo, hi, tuple(ids)))
    scx_rows.sort()
    pool = []
    pool_idx = {}
    scx_lo, scx_hi, scx_off = [], [], []
    for lo, hi, ids in scx_rows:
        if ids not in pool_idx:
            pool_idx[ids] = len(pool)
            pool.extend(ids)
            pool.append(255)
        scx_lo.append(lo)
        scx_hi.append(hi)
        scx_off.append(pool_idx[ids])

    gc_groups = []
    with open(os.path.join(UCD, "PropertyValueAliases.txt"), encoding="utf-8") as fh:
        for ln in fh:
            if not ln.startswith("gc ;"):
                continue
            body, _, comment = ln.partition("#")
            names = [a.strip() for a in body.split(";")[1:] if a.strip()]
            if comment.strip():
                members = [m.strip() for m in comment.split("|")]
                mask = 0
                for m in members:
                    mask |= 1 << GC_ORDER.index(m)
            else:
                mask = 1 << GC_ORDER.index(names[0])
            gc_groups.append((names, mask))

    props = {}
    for long_, short, src in BINARY:
        if src == "UnicodeData.txt":
            props[long_] = mirrored
            continue
        rs = []
        for f in lines(src):
            if len(f) > 1 and f[1] == long_:
                rs.append(rng(f[0]))
        if not rs:
            sys.exit("no data for " + long_)
        props[long_] = rs

    out = []
    w = out.append
    w("#pragma once\n")
    w("// Generated by tests/gen_unicode_tables.py from the UCD. Do not edit.")
    w("#include <cstddef>\n#include <cstdint>\n")
    w("namespace ct\n{\n    namespace detail\n    {\n        namespace uni\n        {")
    w('            constexpr const char *kVersion = "%s";\n' % version)
    w("            struct NameId\n            {\n                const char *name;\n                std::uint32_t id;\n            };\n")
    w("            struct CaseRange\n            {\n                std::uint32_t lo;\n                std::uint32_t hi;\n                std::int32_t delta;\n                std::uint32_t step;\n            };\n")
    upr = case_ranges(upper)
    fdr = case_ranges(fold)
    lwr = case_ranges(lower)
    nbin = len(BINARY)
    w("            struct CompPair\n            {\n                std::uint32_t a;\n                std::uint32_t b;\n                std::uint32_t c;\n            };\n")
    w("            enum : std::size_t\n            {")
    w("                kGcRuns = %d,\n                kScRuns = %d,\n                kScxRows = %d,\n                kUpperRanges = %d,\n                kFoldRanges = %d,\n                kBinCount = %d,\n                kScriptCount = %d,\n                kLowerRanges = %d,\n                kSpecialUpper = %d,\n                kSpecialLower = %d,\n                kNfRuns = %d,\n                kDecCount = %d,\n                kCompCount = %d,\n                kBinCased = %d,\n                kBinCaseIgnorable = %d"
      % (len(gc_start), len(sc_start), len(scx_lo), len(upr), len(fdr), nbin, len(sc_names), len(lwr),
         len(sp_cps), len(sl_cps), len(nf_start), len(dec_cp), len(comp),
         [b[0] for b in BINARY].index("Cased"), [b[0] for b in BINARY].index("Case_Ignorable")))
    w("            };\n")
    w("            template <typename T = void>\n            struct Tables\n            {")
    for decl in ["std::uint32_t kGcStart[]", "std::uint8_t kGcVal[]", "std::uint32_t kScStart[]", "std::uint8_t kScVal[]",
                 "std::uint32_t kScxLo[]", "std::uint32_t kScxHi[]", "std::uint32_t kScxOff[]", "std::uint8_t kScxPool[]",
                 "std::uint32_t kBinOff[]", "std::uint32_t kBinPts[]", "CaseRange kUpper[]", "CaseRange kFold[]",
                 "CaseRange kLower[]", "std::uint32_t kSpUpCp[]", "std::uint32_t kSpUpOff[]", "std::uint32_t kSpLoCp[]",
                 "std::uint32_t kSpLoOff[]", "std::uint32_t kSpPool[]", "std::uint32_t kNfStart[]", "std::uint16_t kNfVal[]",
                 "std::uint32_t kDecCp[]", "std::uint32_t kDecVal[]", "std::uint32_t kDecPool[]", "CompPair kComp[]",
                 "NameId kGcNames[]", "NameId kScNames[]", "NameId kBinNames[]"]:
        w("                static const %s;" % decl)
    w("            };\n")

    def define(decl, name, body):
        w("            template <typename T>\n            const %s Tables<T>::%s[] = {" % (decl, name))
        w(body)
        w("            };\n")

    define("std::uint32_t", "kGcStart", arr(gc_start))
    define("std::uint8_t", "kGcVal", arr(gc_val, 32))
    define("std::uint32_t", "kScStart", arr(sc_start))
    define("std::uint8_t", "kScVal", arr(sc_val, 32))
    define("std::uint32_t", "kScxLo", arr(scx_lo))
    define("std::uint32_t", "kScxHi", arr(scx_hi))
    define("std::uint32_t", "kScxOff", arr(scx_off))
    define("std::uint8_t", "kScxPool", arr(pool, 32))
    offs = [0]
    pts = []
    for long_, short, src in BINARY:
        pts.extend(toggles(props[long_]))
        offs.append(len(pts))
    define("std::uint32_t", "kBinOff", arr(offs))
    define("std::uint32_t", "kBinPts", arr(pts, 12))

    def cr(rows):
        return ",\n".join("                {%d,%d,%d,%d}" % r for r in rows)
    define("CaseRange", "kUpper", cr(upr))
    define("CaseRange", "kFold", cr(fdr))
    define("CaseRange", "kLower", cr(lwr))
    define("std::uint32_t", "kSpUpCp", arr(sp_cps))
    define("std::uint32_t", "kSpUpOff", arr(sp_off))
    define("std::uint32_t", "kSpLoCp", arr(sl_cps))
    define("std::uint32_t", "kSpLoOff", arr(sl_off))
    define("std::uint32_t", "kSpPool", arr(sp_pool))
    define("std::uint32_t", "kNfStart", arr(nf_start))
    define("std::uint16_t", "kNfVal", arr(nf_val, 24))
    define("std::uint32_t", "kDecCp", arr(dec_cp))
    define("std::uint32_t", "kDecVal", arr(dec_val))
    define("std::uint32_t", "kDecPool", arr(dec_pool))
    define("CompPair", "kComp", ",\n".join("                {%d,%d,%d}" % r for r in comp))

    names = []
    for ns, mask in gc_groups:
        for n in ns:
            names.append('                {"%s",%d}' % (n, mask))
    define("NameId", "kGcNames", ",\n".join(names))
    names = ['                {"%s",%d}' % (a, i) for a, i in sc_alias]
    define("NameId", "kScNames", ",\n".join(names))
    names = []
    for i, (long_, short, src) in enumerate(BINARY):
        names.append('                {"%s",%d}' % (long_, i))
        if short:
            names.append('                {"%s",%d}' % (short, i))
    for k, (n, _) in enumerate(SPECIAL):
        names.append('                {"%s",%d}' % (n, nbin + k))
    define("NameId", "kBinNames", ",\n".join(names))
    w("            enum : std::size_t\n            {")
    w("                kGcNameCount = %d,\n                kScNameCount = %d,\n                kBinNameCount = %d"
      % (sum(len(n) for n, _ in gc_groups), len(sc_alias), len(names)))
    w("            };\n")
    w("            constexpr const char *kScriptNames[] = {" + ",".join('"%s"' % n for n in sc_names) + "};\n")
    w("            constexpr const char *kGcOrder[] = {" + ",".join('"%s"' % n for n in GC_ORDER) + "};")
    w("        }\n    }\n}")
    text = "\n".join(out) + "\n"
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("wrote %s (%d bytes), Unicode %s" % (os.path.normpath(OUT), len(text), version))


main()
