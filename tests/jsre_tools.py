"""Shared helpers for gen_jsregex_cases.py and fuzz_jsregex.py: run a case table
through Node (the oracle) and through the ct_jsregex_cli binary."""
import json
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def units(s):
    b = s.encode("utf-16-le", "surrogatepass")
    return [b[i] | (b[i + 1] << 8) for i in range(0, len(b), 2)]


def hexu(s):
    u = units(s)
    return "-" if not u else "".join("%04x" % x for x in u)


def node_run(cases):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
        json.dump([list(c) for c in cases], fh)
        path = fh.name
    try:
        out = subprocess.run(["node", os.path.join(HERE, "jsregex_oracle.js"), path], check=True,
                             capture_output=True, text=True, encoding="utf-8").stdout
    finally:
        os.unlink(path)
    lines = out.split("\n")
    return [json.loads(x) for x in lines[:len(cases)]]


def cli_run(cli, cases, steps=0):
    lines = []
    for p, f, t, li, mode in cases:
        lines.append("%s %s %s %d %d" % (f or "-", hexu(p), hexu(t), li, mode))
    args = [cli] + ([str(steps)] if steps else [])
    out = subprocess.run(args, input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout
    res = out.split("\n")
    return [json.loads(x) for x in res[:len(cases)]]


def canon(v):
    return json.dumps(v, separators=(",", ":"), ensure_ascii=True)
