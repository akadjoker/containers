// Diferencial longo de ct/numconv.hpp contra o printf e o strtod do sistema (nao corre no ctest; ver docs/COMPARACAO.md):
//
//   ct_numconv_diff [iteracoes [semente [locale]]]
//   ct_numconv_diff --float-all [passo [inicio]]      todos os floats (ou de passo em passo) so para o texto mais curto
//
// Cada iteracao experimenta um double ao acaso por todos os caminhos (texto mais curto, %.*g, leitura de varios
// formatos, em double e em float) e de vez em quando os limites de intervalo e os empates. Acaba com codigo 1 se
// alguma coisa diferir (a primeira dezena fica escrita).
#include "numconv_check.hpp"

#include <float.h>
#include <locale.h>

using namespace numconv_check;

static long fails = 0;
static void report(const char *msg)
{
    if (++fails <= 30)
        printf("FALHA: %s\n", msg);
}

static int float_all(unsigned long long step, unsigned long long start)
{
    char msg[512];
    unsigned long long n = 0;
    for (unsigned long long b = start; b < (1ull << 32); b += step, ++n)
    {
        if (!check_shortest(float_of(static_cast<uint32_t>(b)), msg, sizeof msg))
            report(msg);
        if ((n & 0x3FFFFFF) == 0x3FFFFFF)
        {
            printf("float %llu / 4294967296 falhas=%ld\n", b, fails);
            fflush(stdout);
        }
    }
    printf("floats: %llu testados, %ld falhas\n", n, fails);
    return fails != 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--float-all"))
        return float_all(argc > 2 ? strtoull(argv[2], nullptr, 10) : 1, argc > 3 ? strtoull(argv[3], nullptr, 10) : 0);
    const long n = argc > 1 ? atol(argv[1]) : 1000000;
    Rng r(argc > 2 ? strtoull(argv[2], nullptr, 10) : 1);
    if (argc > 3)
    {
        if (!setlocale(LC_ALL, argv[3]))
            printf("locale %s nao existe: fica o \"C\"\n", argv[3]);
        else
            c_locale() = newlocale(LC_ALL_MASK, "C", nullptr);
    }
    char msg[512];
    // potencias de 2 e de 10 e as vizinhas
    for (int e = -1074; e <= 1023; ++e)
    {
        double u = ldexp(1.0, e);
        for (int d = -2; d <= 2; ++d)
        {
            double w = u;
            for (int i = 0; i < (d < 0 ? -d : d); ++i)
                w = nextafter(w, d < 0 ? 0.0 : DBL_MAX);
            if (!check_shortest(w, msg, sizeof msg))
                report(msg);
            for (int p = 1; p <= 17; ++p)
                if (!check_general(w, p, msg, sizeof msg))
                    report(msg);
        }
    }
    for (int e = -323; e <= 308; ++e)
    {
        char b[32];
        os_snprintf(b, sizeof b, "1e%d", e);
        const double u = os_strtod(b, nullptr);
        for (int d = -2; d <= 2; ++d)
        {
            double w = u;
            for (int i = 0; i < (d < 0 ? -d : d); ++i)
                w = nextafter(w, d < 0 ? 0.0 : DBL_MAX);
            if (!check_shortest(w, msg, sizeof msg))
                report(msg);
            for (int p = 1; p <= 17; ++p)
                if (!check_general(w, p, msg, sizeof msg))
                    report(msg);
            os_snprintf(b, sizeof b, "%.17g", w);
            if (!check_parse(b, msg, sizeof msg))
                report(msg);
        }
    }
    for (long i = 0; i < n; ++i)
    {
        if (!differential_step(r, msg, sizeof msg))
            report(msg);
        if ((i & 3) == 0 && !boundary_step(r, msg, sizeof msg))
            report(msg);
        if ((i % 5000000) == 4999999)
        {
            printf("%ld / %ld falhas=%ld\n", i + 1, n, fails);
            fflush(stdout);
        }
    }
    printf("%ld iteracoes, %ld falhas\n", n, fails);
    return fails != 0;
}
