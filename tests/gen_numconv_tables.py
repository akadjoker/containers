#!/usr/bin/env python3
"""Gera include/ct/detail/numconv_tables.hpp: as potencias de 5 de 128 bits que o numconv.hpp usa.

    python3 tests/gen_numconv_tables.py

Para cada q de -342 a 340 escreve T(q) = floor(5^q * 2^s) com s tal que T fica em [2^127, 2^128), em duas palavras de
64 bits (alta e baixa), e confere com inteiros exactos as formulas inteiras que o numconv.hpp usa para os expoentes
(se alguma falhar o script para e nao escreve nada).
"""
import os
import sys
from decimal import Decimal, getcontext

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "include", "ct", "detail", "numconv_tables.hpp")
QMIN, QMAX = -342, 340

getcontext().prec = 120


def pow5_128(q):
    if q >= 0:
        p = 5 ** q
        bl = p.bit_length()
        t = p << (128 - bl) if bl <= 128 else p >> (bl - 128)
    else:
        d = 5 ** (-q)
        t = (1 << (127 + d.bit_length())) // d
    assert (1 << 127) <= t < (1 << 128), q
    return t


def fl5(q):  # floor(q * log2(5))
    return (5 ** q).bit_length() - 1 if q >= 0 else -((5 ** (-q)).bit_length())


def fl2pow10(e):  # floor(e * log2(10))
    return (10 ** e).bit_length() - 1 if e >= 0 else -((10 ** (-e)).bit_length())


def fl10pow2(e):  # floor(e * log10(2))
    if e >= 0:
        return len(str(2 ** e)) - 1
    return -len(str(2 ** (-e)))


def fl10three_quarters_pow2(e):  # floor(log10(3/4 * 2^e))
    x = Decimal(3) * (Decimal(2) ** (e - 2))
    return int(x.log10().to_integral_value(rounding="ROUND_FLOOR"))


def check_formulas():
    for q in range(-400, 401):
        assert (q * 152170) >> 16 == fl5(q), ("fl5", q)
        assert (q * 217706) >> 16 == fl5(q) + q, ("fl2pow10 por 217706", q)
    for e in range(-1200, 1201):
        assert (e * 661971961083) >> 41 == fl10pow2(e), ("flog10pow2", e)
        assert (e * 661971961083 - 274743187321) >> 41 == fl10three_quarters_pow2(e), ("flog10threeQuartersPow2", e)
    for e in range(-400, 401):
        assert (e * 913124641741) >> 38 == fl2pow10(e), ("flog2pow10", e)
    # h = q + flog2pow10(-k) + 2 fica em [2, 5] para todos os (c, q) de double e de float
    for q in range(-1074, 972):
        k = (q * 661971961083) >> 41
        h = q + ((-k * 913124641741) >> 38) + 2
        assert 2 <= h <= 5, ("h", q, h)
        k = (q * 661971961083 - 274743187321) >> 41
        h = q + ((-k * 913124641741) >> 38) + 2
        assert 2 <= h <= 5, ("h irregular", q, h)


def main():
    check_formulas()
    lines = []
    for q in range(QMIN, QMAX + 1):
        t = pow5_128(q)
        lines.append("0x%016xULL, 0x%016xULL" % (t >> 64, t & ((1 << 64) - 1)))
    out = []
    out.append("#pragma once")
    out.append("")
    out.append("// Gerado por tests/gen_numconv_tables.py. Nao editar.")
    out.append("// kPow5[2 * (q - kPow5Min)] e kPow5[2 * (q - kPow5Min) + 1] sao a palavra alta e a baixa de")
    out.append("// T(q) = floor(5^q * 2^s), com s tal que 2^127 <= T(q) < 2^128, para q de kPow5Min a kPow5Max.")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("namespace ct")
    out.append("{")
    out.append("    namespace detail")
    out.append("    {")
    out.append("        template <int Unused = 0>")
    out.append("        struct NumconvTables")
    out.append("        {")
    out.append("            static constexpr int kPow5Min = %d;" % QMIN)
    out.append("            static constexpr int kPow5Max = %d;" % QMAX)
    out.append("            static constexpr uint64_t kPow5[%d] = {" % (2 * len(lines)))
    for i in range(0, len(lines), 2):
        chunk = lines[i:i + 2]
        out.append("                " + ", ".join(chunk) + ",")
    out.append("            };")
    out.append("        };")
    out.append("        template <int Unused>")
    out.append("        constexpr uint64_t NumconvTables<Unused>::kPow5[%d];" % (2 * len(lines)))
    out.append("    } // namespace detail")
    out.append("} // namespace ct")
    out.append("")
    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print("escrito", os.path.normpath(OUT), len(lines), "entradas")


if __name__ == "__main__":
    sys.exit(main())
