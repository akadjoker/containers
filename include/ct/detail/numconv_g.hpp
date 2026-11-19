#pragma once

// write_shortest com o limite do expoente posicional dado por quem chama (o que o Json e o Ini escreviam com %.{15,16,17}g).
// Cabecalho a parte para nao mexer no numconv.hpp, que o resto da biblioteca inclui. Sem `std::` de proposito: ficheiro novo.

#include "../numconv.hpp"

namespace ct
{
    namespace detail
    {
        // f > 0, valor f * 10^e; tira os zeros do fim e escreve em estilo %g com o expoente a partir de 10^max(min_limit, algarismos)
        inline char *emit_decimal_at_least(char *o, uint64_t f, int e, int min_limit) noexcept
        {
            while (f % 10 == 0)
            {
                f /= 10;
                ++e;
            }
            char dig[24];
            char *end = dig + sizeof(dig);
            char *start = put_digits(end, f);
            const int nd = static_cast<int>(end - start);
            return emit_g(o, start, nd, e + nd - 1, nd > min_limit ? nd : min_limit);
        }
    } // namespace detail

    // Como write_shortest (os mesmos algarismos), mas o expoente so aparece a partir de 10^max(min_limit, numero de algarismos),
    // como num %.{p}g com p de min_limit a 17: write_shortest_at_least(v, out, 15) e o que o Json e o Ini escrevem. Nao depende do locale.
    inline char *write_shortest_at_least(double v, char *out, int min_limit) noexcept
    {
        uint64_t bits;
        memcpy(&bits, &v, sizeof(bits));
        const bool neg = (bits >> 63) != 0;
        const int be = static_cast<int>((bits >> 52) & 0x7FF);
        const uint64_t frac = bits & ((1ull << 52) - 1);
        if (be == 0x7FF)
            return detail::write_special(neg, frac != 0, out);
        char *o = out;
        if (neg)
            *o++ = '-';
        uint64_t c, f;
        int q, e;
        if (be == 0)
        {
            if (frac == 0)
            {
                *o++ = '0';
                return o;
            }
            c = frac;
            q = -1074;
        }
        else
        {
            c = frac | (1ull << 52);
            q = be - 1075;
            const int mq = -q;
            if (mq > 0 && mq < 53)
            {
                f = c >> mq;
                if ((f << mq) == c)
                    return detail::emit_decimal_at_least(o, f, 0, min_limit);
            }
        }
        detail::schubfach(c, q, c == (1ull << 52) && be > 1, f, e);
        return detail::emit_decimal_at_least(o, f, e, min_limit);
    }
} // namespace ct
