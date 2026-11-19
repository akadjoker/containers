#pragma once

// Numeros de virgula flutuante para texto e de volta, sem locale e sem a biblioteca standard:
//
//   write_shortest(double | float, out)   o texto mais curto que volta exactamente ao mesmo valor (Schubfach)
//   parse_double / parse_float            texto para o valor mais proximo, arredondado correctamente (Eisel-Lemire,
//                                         com caminho exacto por decimais longos nos casos duvidosos)
//   detail::write_general(v, p, out)      o mesmo texto que snprintf("%.*g", p, v) no locale "C", para p de 1 a 17
//
// Todas usam sempre '.' como separador decimal, qualquer que seja o locale do programa. As tabelas (detail/
// numconv_tables.hpp) saem de tests/gen_numconv_tables.py.

#include "detail/numconv_tables.hpp"
#include "detail/utils.hpp"

#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__i386__) && !defined(__SSE2_MATH__)
#define CT_NUMCONV_NO_CLINGER 1
#endif

namespace ct
{
    namespace detail
    {
        // ---- aritmetica de 128 bits ------------------------------------------------------------------------------

        CT_FORCEINLINE uint64_t mul64(uint64_t a, uint64_t b, uint64_t &hi) noexcept
        {
#if defined(__SIZEOF_INT128__)
            __extension__ typedef unsigned __int128 u128;
            const u128 p = static_cast<u128>(a) * b;
            hi = static_cast<uint64_t>(p >> 64);
            return static_cast<uint64_t>(p);
#elif defined(_MSC_VER) && defined(_M_X64)
            return _umul128(a, b, &hi);
#elif defined(_MSC_VER) && defined(_M_ARM64)
            hi = __umulh(a, b);
            return a * b;
#else
            const uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
            const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
            const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
            hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
            return (mid << 32) | (p00 & 0xFFFFFFFFu);
#endif
        }

        // x != 0
        CT_FORCEINLINE int clz64(uint64_t x) noexcept
        {
#if defined(__GNUC__) || defined(__clang__)
            return __builtin_clzll(x);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
            unsigned long i;
            _BitScanReverse64(&i, x);
            return 63 - static_cast<int>(i);
#else
            int n = 0;
            while (!(x >> 63))
            {
                x <<= 1;
                ++n;
            }
            return n;
#endif
        }

        struct U192
        {
            uint64_t w0, w1, w2; // baixa, do meio, alta
        };

        // a (64 bits) * T (128 bits, palavra alta em T[0], baixa em T[1])
        CT_FORCEINLINE U192 mul_64x128(uint64_t a, const uint64_t *T) noexcept
        {
            uint64_t h1, h2;
            const uint64_t l1 = mul64(a, T[1], h1);
            const uint64_t l2 = mul64(a, T[0], h2);
            U192 r;
            r.w0 = l1;
            r.w1 = l2 + h1;
            r.w2 = h2 + (r.w1 < l2 ? 1u : 0u);
            return r;
        }

        // ---- tabelas pequenas ------------------------------------------------------------------------------------

        CT_FORCEINLINE const uint64_t *pow5_entry(int q) noexcept
        {
            return NumconvTables<>::kPow5 + 2 * (q - NumconvTables<>::kPow5Min);
        }

        CT_FORCEINLINE uint64_t pow10_u64(int i) noexcept
        {
            static const uint64_t t[20] = {1ull,
                                           10ull,
                                           100ull,
                                           1000ull,
                                           10000ull,
                                           100000ull,
                                           1000000ull,
                                           10000000ull,
                                           100000000ull,
                                           1000000000ull,
                                           10000000000ull,
                                           100000000000ull,
                                           1000000000000ull,
                                           10000000000000ull,
                                           100000000000000ull,
                                           1000000000000000ull,
                                           10000000000000000ull,
                                           100000000000000000ull,
                                           1000000000000000000ull,
                                           10000000000000000000ull};
            return t[i];
        }

        // floor(q * log2(5)), floor(q * log10(2)), floor(log10(3/4 * 2^q)), floor(q * log2(10)): ver o gerador
        CT_FORCEINLINE int fl5(int q) noexcept { return static_cast<int>((static_cast<int64_t>(q) * 152170) >> 16); }
        CT_FORCEINLINE int flog10pow2(int e) noexcept { return static_cast<int>((static_cast<int64_t>(e) * 661971961083ll) >> 41); }
        CT_FORCEINLINE int flog10_three_quarters_pow2(int e) noexcept
        {
            return static_cast<int>((static_cast<int64_t>(e) * 661971961083ll - 274743187321ll) >> 41);
        }
        CT_FORCEINLINE int flog2pow10(int e) noexcept { return static_cast<int>((static_cast<int64_t>(e) * 913124641741ll) >> 38); }

        // ---- double e float para texto ---------------------------------------------------------------------------

        // arredondamento para impar de cp * g / 2^127 com g = g1 * 2^63 + g0 (126 bits): floor, com a ultima casa a 1 se
        // ha resto. So olha para os 63 bits de cima do resto de proposito: g e um teto, e o excesso que isso mete (menos
        // de 2^-67) tem de desaparecer quando o produto exacto e um inteiro (limite de intervalo num decimal curto).
        CT_FORCEINLINE uint64_t schubfach_rop(uint64_t g1, uint64_t g0, uint64_t cp) noexcept
        {
            uint64_t x1, y1;
            mul64(g0, cp, x1);
            const uint64_t y0 = mul64(g1, cp, y1);
            const uint64_t z = (y0 >> 1) + x1;
            const uint64_t vbp = y1 + (z >> 63);
            return vbp | ((z & 0x7FFFFFFFFFFFFFFFull) != 0 ? 1u : 0u);
        }

        // v = c * 2^q (c > 0). Devolve f e e tais que o decimal f * 10^e e o mais curto dentro do intervalo de
        // arredondamento de v e, entre os da mesma largura, o mais proximo de v (empate: f par).
        inline void schubfach(uint64_t c, int q, bool irregular, uint64_t &f, int &e) noexcept
        {
            const uint64_t cb = c << 2;
            const uint64_t cbr = cb + 2;
            uint64_t cbl;
            int k;
            if (!irregular)
            {
                cbl = cb - 2;
                k = flog10pow2(q);
            }
            else
            {
                cbl = cb - 1;
                k = flog10_three_quarters_pow2(q);
            }
            const int h = q + flog2pow10(-k) + 2;
            // g = floor(10^-k * 2^-r) + 1 com 126 bits (r = flog2pow10(-k) - 125), partido em g1 (63 bits de cima) e g0;
            // T(-k) e quatro vezes isso, arredondado para baixo
            const uint64_t *T = pow5_entry(-k);
            uint64_t ghi = T[0] >> 2;
            uint64_t glo = (T[0] << 62) | (T[1] >> 2);
            ++glo;
            if (glo == 0)
                ++ghi;
            const uint64_t g1 = (ghi << 1) | (glo >> 63);
            const uint64_t g0 = glo & 0x7FFFFFFFFFFFFFFFull;
            const uint64_t vb = schubfach_rop(g1, g0, cb << h);
            const uint64_t vbl = schubfach_rop(g1, g0, cbl << h);
            const uint64_t vbr = schubfach_rop(g1, g0, cbr << h);
            const uint64_t out = c & 1;
            const uint64_t s = vb >> 2;
            e = k;
            if (s >= 10)
            {
                const uint64_t sp10 = 10 * (s / 10);
                const uint64_t tp10 = sp10 + 10;
                const bool upin = vbl + out <= (sp10 << 2);
                const bool wpin = (tp10 << 2) + out <= vbr;
                if (upin != wpin)
                {
                    f = upin ? sp10 : tp10;
                    return;
                }
            }
            const uint64_t t = s + 1;
            const bool uin = vbl + out <= (s << 2);
            const bool win = (t << 2) + out <= vbr;
            if (uin != win)
            {
                f = uin ? s : t;
                return;
            }
            const int64_t cmp = static_cast<int64_t>(vb - ((s + t) << 1));
            f = (cmp < 0 || (cmp == 0 && (s & 1) == 0)) ? s : t;
        }

        CT_FORCEINLINE const char *digit_pairs() noexcept
        {
            static const char t[201] =
                "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
                "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
                "8081828384858687888990919293949596979899";
            return t;
        }

        // escreve v (v > 0) em decimal acabando em end; devolve o principio
        CT_FORCEINLINE char *put_digits(char *end, uint64_t v) noexcept
        {
            const char *pairs = digit_pairs();
            while (v >= 100)
            {
                const uint64_t r = v % 100;
                v /= 100;
                end -= 2;
                end[0] = pairs[2 * r];
                end[1] = pairs[2 * r + 1];
            }
            if (v >= 10)
            {
                end -= 2;
                end[0] = pairs[2 * v];
                end[1] = pairs[2 * v + 1];
            }
            else
                *--end = static_cast<char>('0' + v);
            return end;
        }

        // dig[0..nd) sao os algarismos (sem zeros no fim), X o expoente decimal do primeiro. Estilo %g: expoente
        // ('e', sinal, pelo menos dois algarismos) se X < -4 ou X >= limit, senao posicional.
        inline char *emit_g(char *o, const char *dig, int nd, int X, int limit) noexcept
        {
            if (X < -4 || X >= limit)
            {
                *o++ = dig[0];
                if (nd > 1)
                {
                    *o++ = '.';
                    for (int i = 1; i < nd; ++i)
                        *o++ = dig[i];
                }
                *o++ = 'e';
                int ex = X;
                if (ex < 0)
                {
                    *o++ = '-';
                    ex = -ex;
                }
                else
                    *o++ = '+';
                if (ex >= 100)
                {
                    *o++ = static_cast<char>('0' + ex / 100);
                    ex %= 100;
                }
                *o++ = static_cast<char>('0' + ex / 10);
                *o++ = static_cast<char>('0' + ex % 10);
                return o;
            }
            if (X >= 0)
            {
                const int ip = X + 1;
                for (int i = 0; i < ip; ++i)
                    *o++ = i < nd ? dig[i] : '0';
                if (nd > ip)
                {
                    *o++ = '.';
                    for (int i = ip; i < nd; ++i)
                        *o++ = dig[i];
                }
                return o;
            }
            *o++ = '0';
            *o++ = '.';
            for (int i = 0; i < -X - 1; ++i)
                *o++ = '0';
            for (int i = 0; i < nd; ++i)
                *o++ = dig[i];
            return o;
        }

        // f > 0, valor f * 10^e; tira os zeros do fim e escreve
        inline char *emit_decimal(char *o, uint64_t f, int e, int limit) noexcept
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
            return emit_g(o, start, nd, e + nd - 1, limit);
        }

        // %.{p}g de um double finito, p de 1 a 17; nullptr se nao for caso tratado (nao finito, p fora do intervalo, ou
        // resto de arredondamento a menos de 2^-56 de um empate: ai o chamador usa o snprintf)
        inline char *write_general(double v, int p, char *out) noexcept
        {
            uint64_t bits;
            memcpy(&bits, &v, sizeof(bits));
            const int be = static_cast<int>((bits >> 52) & 0x7FF);
            const uint64_t frac = bits & ((1ull << 52) - 1);
            if (be == 0x7FF || p < 1 || p > 17)
                return nullptr;
            char *o = out;
            if (bits >> 63)
                *o++ = '-';
            uint64_t c;
            int q;
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
            }
            const int lz = clz64(c);
            const int E = q + (63 - lz);
            int X = flog10pow2(E);
            const int s = p - 1 - X;
            const uint64_t cn = c << lz;
            const U192 P = mul_64x128(cn, pow5_entry(s));
            const int sh = -(fl5(s) - 127 + s + q - lz);
            const int u = sh - 128;
            const uint64_t N0 = P.w2 >> u;
            const uint64_t f = (P.w2 << (64 - u)) | (P.w1 >> u);
            const uint64_t kMargin = 1ull << 8;
            const uint64_t lim = pow10_u64(p);
            uint64_t N;
            if (N0 < lim)
            {
                if (f > (1ull << 63) + kMargin)
                    N = N0 + 1;
                else if (f < (1ull << 63) - kMargin)
                    N = N0;
                else
                    return nullptr;
            }
            else
            {
                ++X;
                const uint64_t qq = N0 / 10;
                const unsigned r = static_cast<unsigned>(N0 % 10);
                bool up;
                if (r >= 6)
                    up = true;
                else if (r <= 3)
                    up = false;
                else if (r == 5)
                {
                    if (f < kMargin)
                        return nullptr;
                    up = true;
                }
                else
                {
                    if (f > ~0ull - kMargin)
                        return nullptr;
                    up = false;
                }
                N = qq + (up ? 1u : 0u);
            }
            if (N == lim)
            {
                N = pow10_u64(p - 1);
                ++X;
            }
            char dig[24];
            char *end = dig + sizeof(dig);
            char *start = put_digits(end, N);
            int nd = static_cast<int>(end - start);
            while (nd > 1 && start[nd - 1] == '0')
                --nd;
            return emit_g(o, start, nd, X, p);
        }

        inline char *write_special(bool neg, bool is_nan, char *o) noexcept
        {
            if (is_nan)
            {
                *o++ = 'n';
                *o++ = 'a';
                *o++ = 'n';
                return o;
            }
            if (neg)
                *o++ = '-';
            *o++ = 'i';
            *o++ = 'n';
            *o++ = 'f';
            return o;
        }
    } // namespace detail

    // Escreve em out (pelo menos 32 bytes) o texto mais curto de v que volta exactamente ao mesmo valor, com o
    // decimal mais proximo de v se houver varios da mesma largura: 0.1, 1e-05, 123456789, 1e+16, 5e-324. Posicional de
    // 1e-4 a 1e16 (exclusive), senao com expoente ('e', sinal, dois algarismos pelo menos). "nan", "inf", "-inf", "0",
    // "-0". Devolve o fim do texto (nao escreve o NUL). Nao depende do locale.
    inline char *write_shortest(double v, char *out) noexcept
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
                    return detail::emit_decimal(o, f, 0, 16);
            }
        }
        detail::schubfach(c, q, c == (1ull << 52) && be > 1, f, e);
        return detail::emit_decimal(o, f, e, 16);
    }

    inline char *write_shortest(float v, char *out) noexcept
    {
        uint32_t bits;
        memcpy(&bits, &v, sizeof(bits));
        const bool neg = (bits >> 31) != 0;
        const int be = static_cast<int>((bits >> 23) & 0xFF);
        const uint32_t frac = bits & ((1u << 23) - 1);
        if (be == 0xFF)
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
            q = -149;
        }
        else
        {
            c = frac | (1u << 23);
            q = be - 150;
            const int mq = -q;
            if (mq > 0 && mq < 24)
            {
                f = c >> mq;
                if ((f << mq) == c)
                    return detail::emit_decimal(o, f, 0, 16);
            }
        }
        detail::schubfach(c, q, c == (1u << 23) && be > 1, f, e);
        return detail::emit_decimal(o, f, e, 16);
    }

    namespace detail
    {
        // ---- texto para double e float ---------------------------------------------------------------------------

        struct DoubleTraits
        {
            typedef double value_t;
            enum
            {
                mant_bits = 52,
                bias = 1023,
                inf_exp = 2047,
                max_exact_e = 22
            };
            static uint64_t max_exact_w() noexcept { return 1ull << 53; }
            static value_t exact_pow10(int e) noexcept
            {
                static const double t[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                             1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
                return t[e];
            }
            static value_t from_bits(uint64_t b) noexcept
            {
                value_t v;
                memcpy(&v, &b, sizeof(v));
                return v;
            }
            static uint64_t make_bits(bool neg, uint64_t power2, uint64_t mant) noexcept
            {
                return (neg ? (1ull << 63) : 0) | (power2 << 52) | mant;
            }
        };

        struct FloatTraits
        {
            typedef float value_t;
            enum
            {
                mant_bits = 23,
                bias = 127,
                inf_exp = 255,
                max_exact_e = 10
            };
            static uint64_t max_exact_w() noexcept { return 1ull << 24; }
            static value_t exact_pow10(int e) noexcept
            {
                static const float t[11] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f};
                return t[e];
            }
            static value_t from_bits(uint64_t b) noexcept
            {
                const uint32_t b32 = static_cast<uint32_t>(b);
                value_t v;
                memcpy(&v, &b32, sizeof(v));
                return v;
            }
            static uint64_t make_bits(bool neg, uint64_t power2, uint64_t mant) noexcept
            {
                return (neg ? (1ull << 31) : 0) | (power2 << 23) | mant;
            }
        };

        // w * 10^q com w != 0 e q de -342 a 308. Devolve false se o arredondamento nao for certo com esta precisao.
        // power2 sai com o expoente ja com a polarizacao (0 para subnormal ou zero, inf_exp para infinito).
        template <typename Tr>
        inline bool eisel_lemire(uint64_t w, int q, uint64_t &mant, int &power2) noexcept
        {
            const int lz = clz64(w);
            w <<= lz;
            const uint64_t *T = pow5_entry(q);
            uint64_t hi, lo;
            lo = mul64(w, T[0], hi);
            const uint64_t mask = ~0ull >> (Tr::mant_bits + 3);
            if ((hi & mask) == mask)
            {
                uint64_t hi2;
                mul64(w, T[1], hi2);
                lo += hi2;
                if (lo < hi2)
                    ++hi;
                if ((hi & mask) == mask && lo >= ~0ull - 1)
                    return false;
            }
            const int upperbit = static_cast<int>(hi >> 63);
            const int shift = upperbit + 64 - Tr::mant_bits - 3;
            uint64_t m = hi >> shift;
            int p2 = fl5(q) + q + 63 + upperbit - lz + static_cast<int>(Tr::bias);
            if (p2 <= 0)
            {
                if (-p2 + 1 >= 64)
                {
                    mant = 0;
                    power2 = 0;
                    return true;
                }
                m >>= -p2 + 1;
                m += m & 1;
                m >>= 1;
                power2 = m < (1ull << Tr::mant_bits) ? 0 : 1;
                mant = m & ((1ull << Tr::mant_bits) - 1);
                return true;
            }
            if ((lo == 0 && (hi & ((1ull << shift) - 1)) == 0) && (m & 3) == 1)
            {
                // o produto de 128 bits e um empate: so e um empate verdadeiro se 5^q cabe nas duas palavras sem perda e
                // a palavra baixa de T nao acrescenta nada (q de 0 a 27); se nao for, o valor e maior e arredonda-se para cima
                if (q >= 0 && q <= 27)
                    m &= ~1ull;
            }
            m += m & 1;
            m >>= 1;
            if (m >= (2ull << Tr::mant_bits))
            {
                m = 1ull << Tr::mant_bits;
                ++p2;
            }
            m &= ~(1ull << Tr::mant_bits);
            if (p2 >= static_cast<int>(Tr::inf_exp))
            {
                p2 = static_cast<int>(Tr::inf_exp);
                m = 0;
            }
            mant = m;
            power2 = p2;
            return true;
        }

        // Decimal de precisao arbitraria (ate kMaxDigits algarismos) para o caminho exacto: o valor e 0.d[0]d[1]... * 10^dp
        struct Decimal
        {
            enum
            {
                kMaxDigits = 800
            };
            uint8_t d[kMaxDigits];
            int nd;
            int dp;
            bool trunc;

            void trim() noexcept
            {
                while (nd > 0 && d[nd - 1] == 0)
                    --nd;
                if (nd == 0)
                    dp = 0;
            }

            void right_shift(unsigned k) noexcept
            {
                int r = 0, w = 0;
                uint64_t n = 0;
                for (; (n >> k) == 0; ++r)
                {
                    if (r >= nd)
                    {
                        if (n == 0)
                        {
                            nd = 0;
                            return;
                        }
                        while ((n >> k) == 0)
                        {
                            n *= 10;
                            ++r;
                        }
                        break;
                    }
                    n = n * 10 + d[r];
                }
                dp -= r - 1;
                const uint64_t mask = (1ull << k) - 1;
                for (; r < nd; ++r)
                {
                    const uint64_t c = d[r];
                    d[w++] = static_cast<uint8_t>(n >> k);
                    n &= mask;
                    n = n * 10 + c;
                }
                while (n > 0)
                {
                    const uint8_t dig = static_cast<uint8_t>(n >> k);
                    n &= mask;
                    if (w < kMaxDigits)
                        d[w++] = dig;
                    else if (dig > 0)
                        trunc = true;
                    n *= 10;
                }
                nd = w;
                trim();
            }

            void left_shift(unsigned k) noexcept
            {
                uint8_t tmp[kMaxDigits + 24];
                int cnt = 0;
                uint64_t n = 0;
                for (int r = nd - 1; r >= 0; --r)
                {
                    n += static_cast<uint64_t>(d[r]) << k;
                    const uint64_t quo = n / 10;
                    tmp[cnt++] = static_cast<uint8_t>(n - 10 * quo);
                    n = quo;
                }
                while (n > 0)
                {
                    const uint64_t quo = n / 10;
                    tmp[cnt++] = static_cast<uint8_t>(n - 10 * quo);
                    n = quo;
                }
                const int delta = cnt - nd;
                int w = 0;
                for (int i = cnt - 1; i >= 0; --i)
                {
                    if (w < kMaxDigits)
                        d[w++] = tmp[i];
                }
                for (int i = cnt - kMaxDigits - 1; i >= 0; --i)
                    if (tmp[i] != 0)
                        trunc = true;
                nd = w;
                dp += delta;
                trim();
            }

            void shift(int k) noexcept
            {
                if (nd == 0)
                    return;
                if (k > 0)
                {
                    while (k > 60)
                    {
                        left_shift(60);
                        k -= 60;
                    }
                    left_shift(static_cast<unsigned>(k));
                }
                else if (k < 0)
                {
                    while (k < -60)
                    {
                        right_shift(60);
                        k += 60;
                    }
                    right_shift(static_cast<unsigned>(-k));
                }
            }

            bool should_round_up(int n) const noexcept
            {
                if (n < 0 || n >= nd)
                    return false;
                if (d[n] == 5 && n + 1 == nd)
                {
                    if (trunc)
                        return true;
                    return n > 0 && (d[n - 1] & 1) != 0;
                }
                return d[n] >= 5;
            }

            uint64_t rounded_integer() const noexcept
            {
                if (dp > 20)
                    return ~0ull;
                int i;
                uint64_t n = 0;
                for (i = 0; i < dp && i < nd; ++i)
                    n = n * 10 + d[i];
                for (; i < dp; ++i)
                    n *= 10;
                if (should_round_up(dp))
                    ++n;
                return n;
            }
        };

        // os algarismos (e o ponto) de [mb, me) e o expoente explicito; devolve os bits do valor mais proximo
        template <typename Tr>
        inline uint64_t slow_real(const char *mb, const char *me, int exp_explicit, bool neg) noexcept
        {
            static const int kPowTab[9] = {1, 3, 6, 9, 13, 16, 19, 23, 26};
            Decimal dec;
            dec.nd = 0;
            dec.dp = 0;
            dec.trunc = false;
            bool sawdot = false;
            int seen = 0; // algarismos significativos vistos, guardados ou nao: a posicao do ponto conta todos
            for (const char *p = mb; p != me; ++p)
            {
                const char ch = *p;
                if (ch == '.')
                {
                    sawdot = true;
                    dec.dp = seen;
                    continue;
                }
                if (ch == '0' && seen == 0)
                {
                    --dec.dp;
                    continue;
                }
                if (seen < 1000000000)
                    ++seen;
                if (dec.nd < Decimal::kMaxDigits)
                    dec.d[dec.nd++] = static_cast<uint8_t>(ch - '0');
                else if (ch != '0')
                    dec.trunc = true;
            }
            if (!sawdot)
                dec.dp = seen;
            dec.dp += exp_explicit;
            dec.trim();

            const int bias = -static_cast<int>(Tr::bias);
            const int expbits_max = static_cast<int>(Tr::inf_exp);
            int exp = 0;
            uint64_t mant = 0;
            bool overflow = false;
            if (dec.nd == 0)
                return Tr::make_bits(neg, 0, 0);
            if (dec.dp > 310)
                overflow = true;
            else if (dec.dp < -330)
                return Tr::make_bits(neg, 0, 0);
            else
            {
                exp = 0;
                while (dec.dp > 0)
                {
                    const int n = dec.dp >= 9 ? 27 : kPowTab[dec.dp];
                    dec.shift(-n);
                    exp += n;
                }
                while (dec.dp < 0 || (dec.dp == 0 && dec.d[0] < 5))
                {
                    const int n = -dec.dp >= 9 ? 27 : kPowTab[-dec.dp];
                    dec.shift(n);
                    exp -= n;
                }
                --exp;
                if (exp < bias + 1)
                {
                    const int n = bias + 1 - exp;
                    dec.shift(-n);
                    exp += n;
                }
                if (exp - bias >= expbits_max)
                    overflow = true;
                else
                {
                    dec.shift(1 + static_cast<int>(Tr::mant_bits));
                    mant = dec.rounded_integer();
                    if (mant == (2ull << Tr::mant_bits))
                    {
                        mant >>= 1;
                        ++exp;
                        if (exp - bias >= expbits_max)
                            overflow = true;
                    }
                    if (!overflow && (mant & (1ull << Tr::mant_bits)) == 0)
                        exp = bias;
                }
            }
            if (overflow)
                return Tr::make_bits(neg, static_cast<uint64_t>(expbits_max), 0);
            return Tr::make_bits(neg, static_cast<uint64_t>(exp - bias), mant & ((1ull << Tr::mant_bits) - 1));
        }

        CT_FORCEINLINE bool is_digit_char(char c) noexcept { return static_cast<unsigned>(c - '0') < 10u; }
        CT_FORCEINLINE char lower_char(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

        // nome sem distinguir maiusculas de minusculas; devolve o fim se p comeca por name, senao p
        inline const char *match_word(const char *p, const char *last, const char *name) noexcept
        {
            const char *s = p;
            while (*name)
            {
                if (s == last || lower_char(*s) != *name)
                    return p;
                ++s;
                ++name;
            }
            return s;
        }

        // oito algarismos decimais seguidos em p (little endian): false se algum nao for algarismo
        CT_FORCEINLINE bool eight_digits(const char *p, uint64_t &value) noexcept
        {
            uint64_t chunk;
            memcpy(&chunk, p, sizeof(chunk));
            if ((((chunk & 0xF0F0F0F0F0F0F0F0ull) | (((chunk + 0x0606060606060606ull) & 0xF0F0F0F0F0F0F0F0ull) >> 4))) !=
                0x3333333333333333ull)
                return false;
            chunk = ((chunk & 0x0F0F0F0F0F0F0F0Full) * 2561) >> 8;
            chunk = ((chunk & 0x00FF00FF00FF00FFull) * 6553601) >> 16;
            chunk = ((chunk & 0x0000FFFF0000FFFFull) * 42949672960001ull) >> 32;
            value = chunk;
            return true;
        }

        // Acumula em w (sem limite: com mais de 19 algarismos da a volta e o chamador refaz a conta) os algarismos
        // seguidos de p; devolve o fim.
        CT_FORCEINLINE const char *accumulate_digits(const char *p, const char *last, uint64_t &w) noexcept
        {
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(_MSC_VER)
            uint64_t chunk;
            while (last - p >= 8 && eight_digits(p, chunk))
            {
                w = w * 100000000ull + chunk;
                p += 8;
            }
#endif
            while (p != last && is_digit_char(*p))
            {
                w = w * 10 + static_cast<unsigned>(*p - '0');
                ++p;
            }
            return p;
        }

        // Algarismos decimais de p a end: devolve o fim, ou p se nao houver nenhum. Zeros do principio nao contam;
        // out = valor, ou 2^64 - 1 se tiver mais de 19 algarismos significativos (nunca cabe num int). A soma nao
        // testa o limite a cada algarismo: com mais de 19 da a volta, e o numero deles (a diferenca de ponteiros)
        // decide no fim. Com terminated = true *end e '\0' (o NUL de um String) e os ciclos nao testam end.
        template <bool Terminated>
        CT_FORCEINLINE const char *scan_uint(const char *p, const char *end, uint64_t &out) noexcept
        {
            const char *const begin = p;
            while ((Terminated || p != end) && *p == '0')
                ++p;
            const char *const first = p;
            uint64_t v = 0;
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(_MSC_VER)
            uint64_t chunk;
            while (end - p >= 8 && eight_digits(p, chunk))
            {
                v = v * 100000000ull + chunk;
                p += 8;
            }
#endif
            while ((Terminated || p != end) && is_digit_char(*p))
            {
                v = v * 10 + static_cast<unsigned>(*p - '0');
                ++p;
            }
            out = (p - first > 19) ? ~0ull : v;
            return p == begin ? begin : p;
        }

        inline int hex_value(char c) noexcept
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        // p aponta para o '0' de "0x". Devolve o fim, ou nullptr se a seguir ao 0x nao houver algarismo hexadecimal
        // (o chamador le so o "0").
        template <typename Tr>
        inline const char *parse_hex(const char *p, const char *last, bool neg, typename Tr::value_t &out) noexcept
        {
            const char *s = p + 2;
            uint64_t m = 0;
            int taken = 0;
            int e2 = 0;
            bool sticky = false, any = false;
            for (; s != last && hex_value(*s) >= 0; ++s)
            {
                const int d = hex_value(*s);
                any = true;
                if (taken < 16)
                {
                    if (m != 0 || d != 0)
                    {
                        m = (m << 4) | static_cast<unsigned>(d);
                        ++taken;
                    }
                }
                else
                {
                    sticky = sticky || d != 0;
                    e2 += 4;
                }
            }
            if (s != last && *s == '.' && (any || (s + 1 != last && hex_value(s[1]) >= 0)))
            {
                ++s;
                for (; s != last && hex_value(*s) >= 0; ++s)
                {
                    const int d = hex_value(*s);
                    any = true;
                    if (taken < 16)
                    {
                        e2 -= 4;
                        if (m != 0 || d != 0)
                        {
                            m = (m << 4) | static_cast<unsigned>(d);
                            ++taken;
                        }
                    }
                    else
                        sticky = sticky || d != 0;
                }
            }
            if (!any)
                return nullptr;
            if (s != last && (*s == 'p' || *s == 'P'))
            {
                const char *ep = s + 1;
                bool eneg = false;
                if (ep != last && (*ep == '+' || *ep == '-'))
                {
                    eneg = *ep == '-';
                    ++ep;
                }
                if (ep != last && is_digit_char(*ep))
                {
                    int ev = 0;
                    for (; ep != last && is_digit_char(*ep); ++ep)
                        if (ev < 100000)
                            ev = ev * 10 + (*ep - '0');
                    e2 += eneg ? -ev : ev;
                    s = ep;
                }
            }
            if (m == 0)
            {
                out = Tr::from_bits(Tr::make_bits(neg, 0, 0));
                return s;
            }
            const int lz = clz64(m);
            m <<= lz;
            e2 -= lz;
            int biased = e2 + 63 + static_cast<int>(Tr::bias);
            int shift = 64 - (static_cast<int>(Tr::mant_bits) + 1);
            if (biased < 1)
            {
                shift += 1 - biased;
                biased = 1;
            }
            uint64_t mant, rem, half;
            if (shift >= 65)
            {
                mant = 0;
                rem = 0;
                half = 1;
            }
            else if (shift == 64)
            {
                mant = 0;
                rem = m;
                half = 1ull << 63;
            }
            else
            {
                mant = m >> shift;
                rem = m & ((1ull << shift) - 1);
                half = 1ull << (shift - 1);
            }
            if (rem > half || (rem == half && (sticky || (mant & 1))))
                ++mant;
            uint64_t bits = (static_cast<uint64_t>(biased - 1) << Tr::mant_bits) + mant;
            if (bits >= (static_cast<uint64_t>(Tr::inf_exp) << Tr::mant_bits))
                bits = static_cast<uint64_t>(Tr::inf_exp) << Tr::mant_bits;
            out = Tr::from_bits(bits | (neg ? (Tr::make_bits(true, 0, 0)) : 0));
            return s;
        }

        // Analisa o prefixo numerico mais longo que comeca em first (sem saltar espacos): sinal, digitos com ponto e
        // expoente, ou inf, infinity, nan, nan(...). Devolve o fim do que usou, ou first se nao havia numero (out
        // fica 0). Fora do intervalo da o infinito (com sinal) ou 0, como o strtod.
        template <typename Tr>
        inline const char *parse_real(const char *first, const char *last, typename Tr::value_t &out) noexcept
        {
            const char *p = first;
            bool neg = false;
            if (p != last && (*p == '-' || *p == '+'))
            {
                neg = *p == '-';
                ++p;
            }
            if (last - p >= 3 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            {
                const char *const r = parse_hex<Tr>(p, last, neg, out);
                if (r)
                    return r;
            }
            const char *const mant_begin = p;
            uint64_t w = 0;
            p = accumulate_digits(p, last, w);
            const int n_int = static_cast<int>(p - mant_begin);
            int n_frac = 0;
            if (p != last && *p == '.')
            {
                ++p;
                const char *const fs = p;
                p = accumulate_digits(p, last, w);
                n_frac = static_cast<int>(p - fs);
            }
            if (n_int + n_frac == 0)
            {
                const char *s = match_word(mant_begin, last, "inf");
                if (s != mant_begin)
                {
                    const char *s2 = match_word(s, last, "inity");
                    if (s2 != s)
                        s = s2;
                    out = Tr::from_bits(Tr::make_bits(neg, static_cast<uint64_t>(Tr::inf_exp), 0));
                    return s;
                }
                s = match_word(mant_begin, last, "nan");
                if (s != mant_begin)
                {
                    if (s != last && *s == '(')
                    {
                        const char *t = s + 1;
                        while (t != last && (is_digit_char(*t) || (lower_char(*t) >= 'a' && lower_char(*t) <= 'z') || *t == '_'))
                            ++t;
                        if (t != last && *t == ')')
                            s = t + 1;
                    }
                    out = Tr::from_bits(Tr::make_bits(neg, static_cast<uint64_t>(Tr::inf_exp), 1ull << (Tr::mant_bits - 1)));
                    return s;
                }
                out = 0;
                return first;
            }
            const char *const mant_end = p;
            int exp_explicit = 0;
            if (p != last && (*p == 'e' || *p == 'E'))
            {
                const char *ep = p + 1;
                bool eneg = false;
                if (ep != last && (*ep == '+' || *ep == '-'))
                {
                    eneg = *ep == '-';
                    ++ep;
                }
                if (ep != last && is_digit_char(*ep))
                {
                    int ev = 0;
                    for (; ep != last && is_digit_char(*ep); ++ep)
                        if (ev < 100000)
                            ev = ev * 10 + (*ep - '0');
                    exp_explicit = eneg ? -ev : ev;
                    p = ep;
                }
            }
            const char *const end_ptr = p;
            int q = exp_explicit - n_frac;
            bool truncated = false;
            if (n_int + n_frac > 19)
            {
                // mais de 19 algarismos (contando zeros do principio): w deu a volta; ficam os 19 primeiros significativos
                w = 0;
                int sig = 0;
                bool after_dot = false;
                q = exp_explicit;
                for (const char *s = mant_begin; s != mant_end; ++s)
                {
                    const char ch = *s;
                    if (ch == '.')
                    {
                        after_dot = true;
                        continue;
                    }
                    const unsigned dgt = static_cast<unsigned>(ch - '0');
                    if (sig == 0 && dgt == 0)
                    {
                        if (after_dot)
                            --q;
                        continue;
                    }
                    if (sig < 19)
                    {
                        w = w * 10 + dgt;
                        ++sig;
                        if (after_dot)
                            --q;
                    }
                    else
                    {
                        if (dgt != 0)
                            truncated = true;
                        if (!after_dot)
                            ++q;
                    }
                }
            }
            if (w == 0)
            {
                out = Tr::from_bits(Tr::make_bits(neg, 0, 0));
                return end_ptr;
            }
#if !defined(CT_NUMCONV_NO_CLINGER)
            if (!truncated && w <= Tr::max_exact_w() && q >= -static_cast<int>(Tr::max_exact_e) && q <= static_cast<int>(Tr::max_exact_e))
            {
                typename Tr::value_t d = static_cast<typename Tr::value_t>(w);
                d = q >= 0 ? d * Tr::exact_pow10(q) : d / Tr::exact_pow10(-q);
                out = neg ? -d : d;
                return end_ptr;
            }
#endif
            if (q < -342)
            {
                out = Tr::from_bits(Tr::make_bits(neg, 0, 0));
                return end_ptr;
            }
            if (q > 308)
            {
                out = Tr::from_bits(Tr::make_bits(neg, static_cast<uint64_t>(Tr::inf_exp), 0));
                return end_ptr;
            }
            uint64_t mant;
            int power2;
            bool ok = eisel_lemire<Tr>(w, q, mant, power2);
            if (ok && truncated)
            {
                uint64_t mant2;
                int power22;
                ok = eisel_lemire<Tr>(w + 1, q, mant2, power22) && mant2 == mant && power22 == power2;
            }
            if (ok)
            {
                out = Tr::from_bits(Tr::make_bits(neg, static_cast<uint64_t>(power2), mant));
                return end_ptr;
            }
            out = Tr::from_bits(slow_real<Tr>(mant_begin, mant_end, exp_explicit, neg));
            return end_ptr;
        }
    } // namespace detail

    // Texto para double: analisa o prefixo numerico mais longo a partir de first (sem saltar espacos). Devolve o fim do
    // que usou, ou first se nao havia numero (out = 0). Arredondamento correcto (o mais proximo, empate ao par),
    // '.' como separador decimal em qualquer locale; aceita inf, infinity, nan e hexadecimal (0x1.8p3), sem
    // distinguir maiusculas.
    inline const char *parse_double(const char *first, const char *last, double &out) noexcept
    {
        return detail::parse_real<detail::DoubleTraits>(first, last, out);
    }

    inline const char *parse_float(const char *first, const char *last, float &out) noexcept
    {
        return detail::parse_real<detail::FloatTraits>(first, last, out);
    }
} // namespace ct
