#!/usr/bin/env python3
# Junta os resultados de tools/bench_compare.sh em tabelas markdown.
#   bench_tables.py <pasta>                 tabelas de tempo, instrucoes, memoria, layout e compilacao
#   bench_tables.py <pasta> <pasta-antes>   acrescenta ct antes/depois (instrucoes, alocacoes, bytes)
# Colunas de time.tsv e ir.tsv: familia impl cenario tipo N ns/op ops alocacoes bytes-pedidos pico vivo rss sizeof check [Ir]
import os
import sys
from collections import OrderedDict


def load(path, ir=False):
    rows = OrderedDict()
    if not os.path.exists(path):
        return rows
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if len(f) < 14:
            continue
        fam, impl, scen, typ, n = f[0], f[1], f[2], f[3], int(f[4])
        r = dict(ns=float(f[5]), ops=int(f[6]), calls=int(f[7]), req=int(f[8]), peak=int(f[9]), live=int(f[10]),
                 rss=int(f[11]), sz=int(f[12]), check=f[13])
        if ir and len(f) > 14:
            r["ir"] = int(f[14]) / max(1, r["ops"])
        col = impl + ":" + scen if fam == "sort" else impl
        key = (fam, scen if fam != "sort" else "", typ, n)
        rows.setdefault(key, OrderedDict())[col] = r
    return rows


def fmt(v, digits=2):
    if v is None:
        return "-"
    return ("%." + str(digits) + "f") % v


def table(rows, metric, title, ref="ct", digits=2, fams=None):
    out = []
    byfam = OrderedDict()
    for key, cols in rows.items():
        byfam.setdefault(key[0], []).append((key, cols))
    for fam, items in byfam.items():
        if fams and fam not in fams:
            continue
        impls = []
        for _, cols in items:
            for c in cols:
                if c not in impls:
                    impls.append(c)
        out.append("\n#### %s, %s\n" % (title, fam))
        out.append("| cenario | tipo | N | " + " | ".join(impls) + " | " + ref + " / melhor outro |")
        out.append("|---|---|---:|" + "---:|" * len(impls) + "---:|")
        for key, cols in items:
            vals = [cols[c][metric] if c in cols and metric in cols[c] else None for c in impls]
            refc = [c for c in impls if c == ref or c.startswith(ref + ":")]
            rv = [cols[c][metric] for c in refc if c in cols and metric in cols[c]]
            others = [cols[c][metric] for c in impls if c not in refc and c in cols and metric in cols[c]]
            ratio = ""
            if rv and others and min(others) > 0:
                q = min(rv) / min(others)
                ratio = ("**%.2f**" if q > 1.10 else "%.2f") % q
            out.append("| %s | %s | %d | %s | %s |" % (key[1] or "-", key[2], key[3],
                                                     " | ".join(fmt(v, digits) for v in vals), ratio))
    return "\n".join(out)


def memory(rows):
    out = ["\n#### Memoria na construcao (bytes vivos por elemento, alocacoes por construcao, pico por elemento)\n"]
    out.append("| familia | cenario | tipo | N | impl | vivo B/elem | pico B/elem | alocacoes | RSS KiB |")
    out.append("|---|---|---|---:|---|---:|---:|---:|---:|")
    for key, cols in rows.items():
        if key[1] not in ("build", "push_back", "append_char", "append_piece", "build_reserved"):
            continue
        for c, r in cols.items():
            out.append("| %s | %s | %s | %d | %s | %.2f | %.2f | %d | %d |" % (
                key[0], key[1], key[2], key[3], c, r["live"] / key[3], r["peak"] / key[3], r["calls"], r["rss"]))
    return "\n".join(out)


def before_after(now, before, metric, title, impl="ct"):
    out = ["\n#### %s\n" % title, "| familia | cenario | tipo | N | antes | depois | variacao |", "|---|---|---|---:|---:|---:|---:|"]
    for key, cols in now.items():
        for c, r in cols.items():
            if not (c == impl or c.startswith(impl + ":")):
                continue
            b = before.get(key, {}).get(c)
            if not b or metric not in r or metric not in b or b[metric] == 0:
                continue
            d = (r[metric] - b[metric]) / b[metric] * 100.0
            if abs(d) < 0.5:
                continue
            out.append("| %s | %s | %s | %d | %s | %s | %+.1f%% |" % (
                key[0], key[1] or c, key[2], key[3], fmt(b[metric]), fmt(r[metric]), d))
    return "\n".join(out)


def main():
    d = sys.argv[1]
    before = sys.argv[2] if len(sys.argv) > 2 else None
    t = load(os.path.join(d, "time.tsv"))
    i = load(os.path.join(d, "ir.tsv"), ir=True)
    print("### Instrucoes por operacao (callgrind, N = 1e5; menos e melhor)")
    print(table(i, "ir", "Ir por operacao", digits=1))
    print("\n### Tempo por operacao em ns (melhor de varias repeticoes; a maquina estava carregada)")
    print(table(t, "ns", "ns por operacao"))
    print("\n### Memoria")
    print(memory(t))
    lay = os.path.join(d, "layout.txt")
    if os.path.exists(lay):
        print("\n### Layout (tamanho vazio, fator de carga, bytes por elemento, crescimento)\n")
        print("```")
        print(open(lay).read().rstrip())
        print("```")
    comp = os.path.join(d, "compile.tsv")
    if os.path.exists(comp):
        print("\n### Custo de compilacao (unidade com 10 tipos)\n")
        print("| biblioteca | modo | tempo s | memoria pico KiB | objeto B |\n|---|---|---:|---:|---:|")
        for line in open(comp):
            f = line.rstrip("\n").split("\t")
            if len(f) >= 5:
                print("| %s | %s | %s | %s | %s |" % tuple(f[:5]))
    if before:
        bi = load(os.path.join(before, "ir.tsv"), ir=True)
        bt = load(os.path.join(before, "time.tsv"))
        print(before_after(i, bi, "ir", "ct: instrucoes por operacao, antes e depois das correcoes"))
        print(before_after(i, bi, "calls", "ct: alocacoes por execucao, antes e depois"))
        print(before_after(i, bi, "req", "ct: bytes pedidos ao malloc por execucao, antes e depois"))
        print(before_after(i, bi, "live", "ct: bytes vivos no fim da regiao medida, antes e depois"))
        print(before_after(t, bt, "ns", "ct: ns por operacao, antes e depois (ruidoso)"))


if __name__ == "__main__":
    main()
