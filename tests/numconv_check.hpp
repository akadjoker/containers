#pragma once

// Verificadores de ct/numconv.hpp contra o printf e o strtod do sistema, usados por tests/test_numconv.cpp (poucas
// amostras) e por tests/numconv_diff.cpp (centenas de milhoes). Cada um devolve false e escreve a razao em msg.
// So usam funcoes de C e a ct (a biblioteca standard de C++ nao entra).

#include <ct/numconv.hpp>

#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace numconv_check
{
    // O oracle (printf e strtod do sistema) corre sempre no locale "C", mesmo quando o programa pos outro (para ver que a
    // ct nao depende dele): g_c_locale e o locale "C" criado por quem quiser esse teste, e cada chamada ao oracle passa por
    // aqui.
    inline locale_t &c_locale()
    {
        static locale_t l = nullptr;
        return l;
    }
    struct OracleScope
    {
        locale_t prev;
        OracleScope() : prev(c_locale() ? uselocale(c_locale()) : nullptr) {}
        ~OracleScope()
        {
            if (c_locale())
                uselocale(prev);
        }
    };
    inline int os_snprintf(char *buf, size_t n, const char *fmt, ...)
    {
        OracleScope scope;
        va_list args;
        va_start(args, fmt);
        const int r = vsnprintf(buf, n, fmt, args);
        va_end(args);
        return r;
    }
    inline double os_strtod(const char *s, char **end)
    {
        OracleScope scope;
        return strtod(s, end);
    }
    inline float os_strtof(const char *s, char **end)
    {
        OracleScope scope;
        return strtof(s, end);
    }
}

namespace numconv_check
{
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed ? seed : 88172645463325252ull) {}
        uint64_t next()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return s;
        }
        uint64_t below(uint64_t n) { return next() % n; }
    };

    inline uint64_t bits_of(double v)
    {
        uint64_t b;
        memcpy(&b, &v, sizeof(b));
        return b;
    }
    inline uint32_t bits_of(float v)
    {
        uint32_t b;
        memcpy(&b, &v, sizeof(b));
        return b;
    }
    inline double double_of(uint64_t b)
    {
        double v;
        memcpy(&v, &b, sizeof(v));
        return v;
    }
    inline float float_of(uint32_t b)
    {
        float v;
        memcpy(&v, &b, sizeof(v));
        return v;
    }

    // numero de algarismos significativos do texto (do primeiro ao ultimo diferentes de zero, sem o expoente)
    inline int significant_digits(const char *s)
    {
        const char *e = s;
        while (*e && *e != 'e')
            ++e;
        const char *first = nullptr, *last = nullptr;
        for (const char *p = s; p < e; ++p)
            if (*p >= '1' && *p <= '9')
            {
                if (!first)
                    first = p;
                last = p;
            }
        if (!first)
            return 1;
        int n = 0;
        for (const char *p = first; p <= last; ++p)
            if (*p >= '0' && *p <= '9')
                ++n;
        return n;
    }

    // Os algarismos de "%.*e" como inteiro e o expoente
    inline void split_e(const char *t, bool &neg, unsigned long long &D, int &E)
    {
        neg = false;
        D = 0;
        const char *p = t;
        if (*p == '-')
        {
            neg = true;
            ++p;
        }
        for (; *p && *p != 'e'; ++p)
            if (*p >= '0' && *p <= '9')
                D = D * 10 + static_cast<unsigned>(*p - '0');
        E = atoi(p + 1);
    }

    // double para texto mais curto: volta ao mesmo valor, nenhum decimal mais curto volta, e entre os da mesma largura
    // e o mais proximo (comparado com o "%.*e" do printf, que arredonda bem)
    inline bool check_shortest(double v, char *msg, size_t msg_size)
    {
        char buf[64];
        *ct::write_shortest(v, buf) = 0;
        if (v != v)
        {
            if (strcmp(buf, "nan"))
            {
                os_snprintf(msg, msg_size, "nan escreveu %s", buf);
                return false;
            }
            return true;
        }
        if (v - v != 0)
        {
            if (strcmp(buf, v > 0 ? "inf" : "-inf"))
            {
                os_snprintf(msg, msg_size, "inf escreveu %s", buf);
                return false;
            }
            return true;
        }
        char *end;
        const double back = os_strtod(buf, &end);
        if (*end || bits_of(back) != bits_of(v))
        {
            os_snprintf(msg, msg_size, "%016llx escreveu %s que le %016llx", (unsigned long long)bits_of(v), buf,
                     (unsigned long long)bits_of(back));
            return false;
        }
        const int n = significant_digits(buf);
        char t[64];
        if (n > 1)
        {
            os_snprintf(t, sizeof t, "%.*e", n - 2, v);
            bool neg;
            unsigned long long D;
            int E;
            split_e(t, neg, D, E);
            for (int d = -1; d <= 1; ++d)
            {
                if (D + static_cast<unsigned long long>(d) == 0)
                    continue;
                char u[64];
                os_snprintf(u, sizeof u, "%s%llue%d", neg ? "-" : "", D + static_cast<unsigned long long>(d), E - (n - 2));
                if (bits_of(os_strtod(u, nullptr)) == bits_of(v))
                {
                    os_snprintf(msg, msg_size, "%016llx escreveu %s (%d algarismos) mas %s tambem volta", (unsigned long long)bits_of(v), buf, n, u);
                    return false;
                }
            }
        }
        os_snprintf(t, sizeof t, "%.*e", n - 1, v);
        if (bits_of(os_strtod(t, nullptr)) == bits_of(v))
        {
            char a[64], b[64];
            int na = 0, nb = 0;
            for (const char *p = buf; *p && *p != 'e'; ++p)
                if (*p >= '0' && *p <= '9')
                    a[na++] = *p;
            for (const char *p = t; *p && *p != 'e'; ++p)
                if (*p >= '0' && *p <= '9')
                    b[nb++] = *p;
            int sa = 0;
            while (sa < na - 1 && a[sa] == '0')
                ++sa;
            int ea = na;
            while (ea > sa + 1 && a[ea - 1] == '0')
                --ea;
            int eb = nb;
            while (eb > 1 && b[eb - 1] == '0')
                --eb;
            if (ea - sa != eb || memcmp(a + sa, b, static_cast<size_t>(eb)))
            {
                os_snprintf(msg, msg_size, "%016llx escreveu %s mas o mais proximo e %s", (unsigned long long)bits_of(v), buf, t);
                return false;
            }
        }
        return true;
    }

    inline bool check_shortest(float v, char *msg, size_t msg_size)
    {
        char buf[64];
        *ct::write_shortest(v, buf) = 0;
        if (v != v || v - v != 0)
            return true;
        char *end;
        const float back = os_strtof(buf, &end);
        if (*end || bits_of(back) != bits_of(v))
        {
            os_snprintf(msg, msg_size, "float %08x escreveu %s que le %08x", bits_of(v), buf, bits_of(back));
            return false;
        }
        const int n = significant_digits(buf);
        if (n > 1)
        {
            char t[64];
            os_snprintf(t, sizeof t, "%.*e", n - 2, static_cast<double>(v));
            bool neg;
            unsigned long long D;
            int E;
            split_e(t, neg, D, E);
            for (int d = -1; d <= 1; ++d)
            {
                if (D + static_cast<unsigned long long>(d) == 0)
                    continue;
                char u[64];
                os_snprintf(u, sizeof u, "%s%llue%d", neg ? "-" : "", D + static_cast<unsigned long long>(d), E - (n - 2));
                if (bits_of(os_strtof(u, nullptr)) == bits_of(v))
                {
                    os_snprintf(msg, msg_size, "float %08x escreveu %s (%d algarismos) mas %s tambem volta", bits_of(v), buf, n, u);
                    return false;
                }
            }
        }
        return true;
    }

    // o texto de write_general e o do snprintf (ou a funcao diz que nao trata o caso)
    inline bool check_general(double v, int p, char *msg, size_t msg_size, bool *handled = nullptr)
    {
        char a[64], b[64];
        char *e = ct::detail::write_general(v, p, a);
        if (handled)
            *handled = e != nullptr;
        if (!e)
            return true;
        *e = 0;
        os_snprintf(b, sizeof b, "%.*g", p, v);
        if (strcmp(a, b))
        {
            os_snprintf(msg, msg_size, "%%.%dg de %016llx: escreveu %s, o printf %s", p, (unsigned long long)bits_of(v), a, b);
            return false;
        }
        return true;
    }

    // um numero em texto: mesmos bits e mesmo fim que o strtod e o strtof (e o caminho exacto sozinho tambem)
    inline bool check_parse(const char *s, char *msg, size_t msg_size)
    {
        const size_t len = strlen(s);
        char *end1;
        const double d1 = os_strtod(s, &end1);
        double d2;
        const char *e2 = ct::parse_double(s, s + len, d2);
        char *end3;
        float f1 = os_strtof(s, &end3);
        // O strtod e o strtof da glibc arredondam mal os hexadecimais com mais de 53 bits de mantissa quando o resultado
        // e subnormal (0x1.8b9ff5p-128 em float da ...fe e o certo e ...ff; 0X98A382a242e7eCp-1078 em double da ...7e e o
        // certo e ...7f, confirmado com aritmetica exacta). Nesses casos so se compara o fim do texto; com ate 13
        // algarismos hexadecimais o strtod e exacto e o corte para float e um so arredondamento.
        bool skip_double_value = false, skip_float_value = false;
        {
            const char *h = s;
            if (*h == '-' || *h == '+')
                ++h;
            if (h[0] == '0' && (h[1] == 'x' || h[1] == 'X'))
            {
                int hex_digits = 0;
                bool leading = true;
                for (const char *q = h + 2; (*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F') || *q == '.'; ++q)
                {
                    if (*q == '.')
                        continue;
                    if (leading && *q == '0')
                        continue;
                    leading = false;
                    ++hex_digits;
                }
                const double a = d1 < 0 ? -d1 : d1;
                if (hex_digits > 13)
                {
                    if (a < 2.2250738585072014e-308)
                        skip_double_value = true;
                    const float af = f1 < 0 ? -f1 : f1;
                    if (af < 1.17549435e-38f)
                        skip_float_value = true;
                }
                else if (d1 - d1 == 0)
                {
                    if (a >= 3.4028235677973366e38)
                        f1 = d1 < 0 ? -float_of(0x7F800000u) : float_of(0x7F800000u);
                    else
                        f1 = static_cast<float>(d1);
                }
            }
        }
        float f2;
        const char *e3 = ct::parse_float(s, s + len, f2);
        if (!skip_double_value && bits_of(d1) != bits_of(d2) && !(d1 != d1 && d2 != d2))
        {
            os_snprintf(msg, msg_size, "double '%.200s': strtod %016llx, ct %016llx", s, (unsigned long long)bits_of(d1), (unsigned long long)bits_of(d2));
            return false;
        }
        if (end1 - s != e2 - s)
        {
            os_snprintf(msg, msg_size, "double '%.200s': strtod parou em %ld, ct em %ld", s, (long)(end1 - s), (long)(e2 - s));
            return false;
        }
        if (!skip_float_value && bits_of(f1) != bits_of(f2) && !(f1 != f1 && f2 != f2))
        {
            os_snprintf(msg, msg_size, "float '%.200s': strtof %08x, ct %08x", s, bits_of(f1), bits_of(f2));
            return false;
        }
        if (end3 - s != e3 - s)
        {
            os_snprintf(msg, msg_size, "float '%.200s': strtof parou em %ld, ct em %ld", s, (long)(end3 - s), (long)(e3 - s));
            return false;
        }
        // o caminho exacto sozinho (so para texto decimal)
        const char *p = s;
        bool neg = false;
        if (*p == '-' || *p == '+')
        {
            neg = *p == '-';
            ++p;
        }
        const char *mb = p;
        int nd = 0;
        while (*p >= '0' && *p <= '9')
        {
            ++p;
            ++nd;
        }
        if (*p == '.')
        {
            ++p;
            while (*p >= '0' && *p <= '9')
            {
                ++p;
                ++nd;
            }
        }
        const bool hex = (mb[0] == '0' && (mb[1] == 'x' || mb[1] == 'X'));
        if (nd > 0 && !hex)
        {
            const char *me = p;
            int ex = 0;
            if (*p == 'e' || *p == 'E')
            {
                const char *q = p + 1;
                bool en = false;
                if (*q == '+' || *q == '-')
                {
                    en = *q == '-';
                    ++q;
                }
                if (*q >= '0' && *q <= '9')
                {
                    int v = 0;
                    for (; *q >= '0' && *q <= '9'; ++q)
                        if (v < 100000)
                            v = v * 10 + (*q - '0');
                    ex = en ? -v : v;
                }
            }
            const uint64_t bd = ct::detail::slow_real<ct::detail::DoubleTraits>(mb, me, ex, neg);
            if (bd != bits_of(d1))
            {
                os_snprintf(msg, msg_size, "caminho exacto double '%.200s': strtod %016llx, ct %016llx", s, (unsigned long long)bits_of(d1), (unsigned long long)bd);
                return false;
            }
            const uint64_t bf = ct::detail::slow_real<ct::detail::FloatTraits>(mb, me, ex, neg);
            if (static_cast<uint32_t>(bf) != bits_of(f1))
            {
                os_snprintf(msg, msg_size, "caminho exacto float '%.200s': strtof %08x, ct %08x", s, bits_of(f1), static_cast<unsigned>(bf));
                return false;
            }
        }
        return true;
    }

    // doubles de varias famílias: expoente uniforme, subnormais, quocientes, decimais curtos e inteiros grandes
    inline double random_double(Rng &r)
    {
        uint64_t b = r.next();
        switch (r.below(8))
        {
        case 0:
            b = (b & 0x800FFFFFFFFFFFFFull) | (r.below(0x7FF) << 52);
            break;
        case 1:
            b &= 0x800FFFFFFFFFFFFFull;
            break;
        case 2:
            return static_cast<double>(static_cast<int64_t>(r.next() >> 12)) / static_cast<double>(1 + r.below(1000000));
        case 3:
            return static_cast<double>(r.below(1000000)) / 1000.0;
        case 4:
            return static_cast<double>(static_cast<int64_t>(r.below(100000000000000ull))) * (r.next() & 1 ? 1.0 : 0.001);
        default:
            break;
        }
        return double_of(b);
    }

    // texto decimal ao acaso: sinal, 1 a 40 algarismos com ponto, expoente
    inline void random_decimal(Rng &r, char *buf)
    {
        const int nd = 1 + static_cast<int>(r.below(40));
        char *o = buf;
        if (r.next() & 1)
            *o++ = '-';
        const int dot = static_cast<int>(r.below(static_cast<uint64_t>(nd) + 1));
        for (int k = 0; k < nd; ++k)
        {
            if (k == dot && (r.next() & 1))
                *o++ = '.';
            *o++ = static_cast<char>('0' + r.below(10));
        }
        if (r.next() & 1)
            sprintf(o, "e%d", static_cast<int>(r.below(700)) - 350);
        else
            *o = 0;
    }

    // texto hexadecimal ao acaso
    inline void random_hex(Rng &r, char *buf)
    {
        const int nd = 1 + static_cast<int>(r.below(30));
        char *o = buf;
        if (r.next() & 1)
            *o++ = '-';
        *o++ = '0';
        *o++ = (r.next() & 1) ? 'x' : 'X';
        const int dot = static_cast<int>(r.below(static_cast<uint64_t>(nd) + 1));
        for (int k = 0; k < nd; ++k)
        {
            if (k == dot && (r.next() & 1))
                *o++ = '.';
            *o++ = "0123456789abcdefABCDEF"[r.below(22)];
        }
        if (r.next() & 1)
            sprintf(o, "p%d", static_cast<int>(r.below(2400)) - 1200);
        else
            *o = 0;
    }

    // Um passo do diferencial: um double ao acaso por todos os caminhos. Devolve false com a razao em msg.
    inline bool differential_step(Rng &r, char *msg, size_t msg_size)
    {
        const double v = random_double(r);
        if (!check_shortest(v, msg, msg_size))
            return false;
        const int p = 1 + static_cast<int>(r.below(17));
        if (!check_general(v, p, msg, msg_size) || !check_general(v, 6, msg, msg_size))
            return false;
        const uint32_t fb = static_cast<uint32_t>(r.next());
        if (!check_shortest(float_of(fb), msg, msg_size))
            return false;
        char buf[1400];
        if (v - v == 0)
        {
            os_snprintf(buf, sizeof buf, "%.17g", v);
            if (!check_parse(buf, msg, msg_size))
                return false;
            os_snprintf(buf, sizeof buf, "%.*g", p, v);
            if (!check_parse(buf, msg, msg_size))
                return false;
            os_snprintf(buf, sizeof buf, "%.*e", static_cast<int>(r.below(25)), v);
            if (!check_parse(buf, msg, msg_size))
                return false;
            os_snprintf(buf, sizeof buf, "%a", v);
            if (!check_parse(buf, msg, msg_size))
                return false;
            os_snprintf(buf, sizeof buf, "%.*a", static_cast<int>(r.below(20)), v);
            if (!check_parse(buf, msg, msg_size))
                return false;
        }
        if ((r.next() & 7) == 0)
        {
            random_decimal(r, buf);
            if (!check_parse(buf, msg, msg_size))
                return false;
        }
        if ((r.next() & 15) == 0)
        {
            random_hex(r, buf);
            if (!check_parse(buf, msg, msg_size))
                return false;
        }
        return true;
    }

    // limites de intervalo que caem em decimais curtos (o ponto medio entre dois doubles vizinhos e 5^k * r * 2^(q-1)),
    // e pontos medios exactos como texto (empates e a vizinhanca)
    inline bool boundary_step(Rng &r, char *msg, size_t msg_size)
    {
        const int k = 1 + static_cast<int>(r.below(22));
        unsigned long long p5 = 1;
        for (int j = 0; j < k; ++j)
            p5 *= 5;
        const unsigned long long lo = (1ull << 53) / p5 + 1, hi = ((1ull << 54) - 1) / p5;
        if (hi > lo)
        {
            unsigned long long m = lo + r.below(hi - lo);
            m |= 1;
            if (m > hi)
                m -= 2;
            m *= p5;
            if (m >= (1ull << 53) && m < (1ull << 54))
            {
                const unsigned long long c = (m - 1) / 2;
                const int q = 1 + static_cast<int>(r.below(90));
                if (!check_shortest(ldexp(static_cast<double>(c), q), msg, msg_size) ||
                    !check_shortest(ldexp(static_cast<double>(c + 1), q), msg, msg_size))
                    return false;
                const float f1 = ldexpf(static_cast<float>(c >> 30), q % 60);
                if (!check_shortest(f1, msg, msg_size) || !check_shortest(nextafterf(f1, 1e30f), msg, msg_size))
                    return false;
            }
        }
        const double v = random_double(r);
        if (v - v != 0 || v <= 0)
            return true;
        const double w = nextafter(v, 1e308);
        if (w - w != 0)
            return true;
        char buf[1400];
        const long double mid = (static_cast<long double>(v) + static_cast<long double>(w)) / 2;
        os_snprintf(buf, sizeof buf, "%.*Le", 780, mid);
        if (!check_parse(buf, msg, msg_size))
            return false;
        char *e = strchr(buf, 'e');
        if (e)
        {
            char tail[16];
            strcpy(tail, e);
            *e = 0;
            strcat(buf, "1");
            strcat(buf, tail);
            if (!check_parse(buf, msg, msg_size))
                return false;
        }
        const float fv = static_cast<float>(v);
        if (fv - fv != 0 || fv <= 0)
            return true;
        const float fw = nextafterf(fv, 1e30f);
        if (fw - fw != 0)
            return true;
        const long double fm = (static_cast<long double>(fv) + static_cast<long double>(fw)) / 2;
        os_snprintf(buf, sizeof buf, "%.*Le", 200, fm);
        return check_parse(buf, msg, msg_size);
    }
} // namespace numconv_check
