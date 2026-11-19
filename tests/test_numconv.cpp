// ct/numconv.hpp (texto mais curto, %.*g, leitura correcta) e a sua ligacao ao String, contra o printf e o strtod do
// sistema. O diferencial longo (centenas de milhoes) esta em tests/numconv_diff.cpp.
#include <ct/numconv.hpp>
#include <ct/string.hpp>

#include <gtest/gtest.h>

#include "numconv_check.hpp"

#include <float.h>
#include <limits.h>
#include <locale.h>

using namespace numconv_check;

namespace
{
    long iterations(long fallback)
    {
        const char *e = getenv("CT_NUMCONV_ITERS");
        if (e && *e)
        {
            const long v = atol(e);
            if (v > 0)
                return v;
        }
        return fallback;
    }

    ct::String shortest(double v)
    {
        char buf[40];
        return ct::String(buf, static_cast<size_t>(ct::write_shortest(v, buf) - buf));
    }
    ct::String shortest(float v)
    {
        char buf[40];
        return ct::String(buf, static_cast<size_t>(ct::write_shortest(v, buf) - buf));
    }

    double parse(const char *s, size_t *used = nullptr)
    {
        double d;
        const char *e = ct::parse_double(s, s + strlen(s), d);
        if (used)
            *used = static_cast<size_t>(e - s);
        return d;
    }
    float parsef(const char *s, size_t *used = nullptr)
    {
        float f;
        const char *e = ct::parse_float(s, s + strlen(s), f);
        if (used)
            *used = static_cast<size_t>(e - s);
        return f;
    }
} // namespace

TEST(NumconvShortest, KnownDoubles)
{
    EXPECT_EQ(shortest(0.0), "0");
    EXPECT_EQ(shortest(-0.0), "-0");
    EXPECT_EQ(shortest(1.0), "1");
    EXPECT_EQ(shortest(-1.5), "-1.5");
    EXPECT_EQ(shortest(0.1), "0.1");
    EXPECT_EQ(shortest(0.3), "0.3");
    EXPECT_EQ(shortest(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(shortest(1.0 / 3.0), "0.3333333333333333");
    EXPECT_EQ(shortest(100.0), "100");
    EXPECT_EQ(shortest(123456789.0), "123456789");
    EXPECT_EQ(shortest(1e15), "1000000000000000");
    EXPECT_EQ(shortest(1e16), "1e+16");
    EXPECT_EQ(shortest(1e22), "1e+22");
    EXPECT_EQ(shortest(1e23), "1e+23");
    EXPECT_EQ(shortest(0.0001), "0.0001");
    EXPECT_EQ(shortest(0.00001), "1e-05");
    EXPECT_EQ(shortest(1.5e-7), "1.5e-07");
    EXPECT_EQ(shortest(5e-324), "5e-324");
    EXPECT_EQ(shortest(1.5e-323), "1.5e-323");
    EXPECT_EQ(shortest(2.2250738585072014e-308), "2.2250738585072014e-308");
    EXPECT_EQ(shortest(1.7976931348623157e308), "1.7976931348623157e+308");
    EXPECT_EQ(shortest(9007199254740992.0), "9007199254740992");
    EXPECT_EQ(shortest(9007199254740993.0), "9007199254740992");
    EXPECT_EQ(shortest(123456789012345680.0), "1.2345678901234568e+17");
    EXPECT_EQ(shortest(4.35), "4.35");
    EXPECT_EQ(shortest(2.5), "2.5");
    EXPECT_EQ(shortest(ldexp(1.0, -1074)), "5e-324");
    EXPECT_EQ(shortest(ldexp(1.0, 1023)), "8.98846567431158e+307");
    EXPECT_EQ(shortest(1.0 / 0.0), "inf");
    EXPECT_EQ(shortest(-1.0 / 0.0), "-inf");
    EXPECT_EQ(shortest(double_of(0x7FF8000000000000ull)), "nan");
}

TEST(NumconvShortest, KnownFloats)
{
    EXPECT_EQ(shortest(0.0f), "0");
    EXPECT_EQ(shortest(-0.0f), "-0");
    EXPECT_EQ(shortest(0.1f), "0.1");
    EXPECT_EQ(shortest(1.0f / 3.0f), "0.33333334");
    EXPECT_EQ(shortest(16777216.0f), "16777216");
    EXPECT_EQ(shortest(3.4028235e38f), "3.4028235e+38");
    EXPECT_EQ(shortest(1.17549435e-38f), "1.1754944e-38");
    EXPECT_EQ(shortest(1e-45f), "1e-45");
    EXPECT_EQ(shortest(float_of(1u)), "1e-45");
    EXPECT_EQ(shortest(float_of(2u)), "3e-45");
    EXPECT_EQ(shortest(1e10f), "10000000000");
    EXPECT_EQ(shortest(1e16f), "1e+16");
    EXPECT_EQ(shortest(float_of(0x7F800000u)), "inf");
    EXPECT_EQ(shortest(float_of(0xFF800000u)), "-inf");
    EXPECT_EQ(shortest(float_of(0x7FC00000u)), "nan");
}

// Cada potencia de 2 e de 10, com as duas vizinhas de cada lado, volta ao mesmo valor, e nenhum decimal mais curto volta
TEST(NumconvShortest, PowersAndNeighbours)
{
    char msg[512];
    for (int e = -1074; e <= 1023; ++e)
        for (int d = -2; d <= 2; ++d)
        {
            double w = ldexp(1.0, e);
            for (int i = 0; i < (d < 0 ? -d : d); ++i)
                w = nextafter(w, d < 0 ? 0.0 : DBL_MAX);
            ASSERT_TRUE(check_shortest(w, msg, sizeof msg)) << msg;
        }
    for (int e = -323; e <= 308; ++e)
    {
        char b[32];
        snprintf(b, sizeof b, "1e%d", e);
        const double u = strtod(b, nullptr);
        for (int d = -2; d <= 2; ++d)
        {
            double w = u;
            for (int i = 0; i < (d < 0 ? -d : d); ++i)
                w = nextafter(w, d < 0 ? 0.0 : DBL_MAX);
            ASSERT_TRUE(check_shortest(w, msg, sizeof msg)) << msg;
        }
    }
    for (int e = -149; e <= 127; ++e)
        for (int d = -2; d <= 2; ++d)
        {
            float w = ldexpf(1.0f, e);
            for (int i = 0; i < (d < 0 ? -d : d); ++i)
                w = nextafterf(w, d < 0 ? 0.0f : FLT_MAX);
            ASSERT_TRUE(check_shortest(w, msg, sizeof msg)) << msg;
        }
}

// os limites do intervalo de arredondamento que caem num decimal curto (o intervalo e fechado se a mantissa e par):
// o defeito que um arredondamento "para cima" do produto de 126 bits teria
TEST(NumconvShortest, IntervalBoundariesOnShortDecimals)
{
    Rng r(11);
    char msg[512];
    for (long i = 0; i < iterations(20000) / 4; ++i)
        ASSERT_TRUE(boundary_step(r, msg, sizeof msg)) << msg;
    // casos concretos: 2^54 + 4 (limite superior em 18014398509481990, mantissa impar: fora) e vizinhos de float
    EXPECT_EQ(shortest(double_of(0x4350000000000001ull)), "1.8014398509481988e+16");
    EXPECT_EQ(shortest(float_of(0xce4cb72du)), "-858639170");
    EXPECT_EQ(shortest(float_of(0x4e2dd871u)), "729160770");
}

// todos os floats com um passo grande (o diferencial completo, de 2^32 floats, esta em numconv_diff --float-all)
TEST(NumconvShortest, FloatSample)
{
    char msg[512];
    for (uint64_t b = 0; b < (1ull << 32); b += 40009)
        ASSERT_TRUE(check_shortest(float_of(static_cast<uint32_t>(b)), msg, sizeof msg)) << msg;
}

TEST(NumconvShortest, RandomDoubles)
{
    Rng r(2024);
    char msg[512];
    for (long i = 0; i < iterations(200000); ++i)
        ASSERT_TRUE(check_shortest(random_double(r), msg, sizeof msg)) << msg;
}

TEST(NumconvGeneral, MatchesPrintfOnRandomDoubles)
{
    Rng r(77);
    char msg[512];
    long handled = 0, total = 0;
    for (long i = 0; i < iterations(200000); ++i)
    {
        const double v = random_double(r);
        const int p = 1 + static_cast<int>(r.below(17));
        bool h;
        ASSERT_TRUE(check_general(v, p, msg, sizeof msg, &h)) << msg;
        handled += h;
        ++total;
        ASSERT_TRUE(check_general(v, 6, msg, sizeof msg, &h)) << msg;
        handled += h;
        ++total;
    }
    // a funcao so se recusa nos empates (menos de 1 em mil) e nos nao finitos
    EXPECT_GT(handled * 100, total * 98);
}

TEST(NumconvGeneral, KnownValuesAndTies)
{
    char msg[512];
    const double values[] = {0.0, -0.0, 1.0, 0.5, 2.5, 0.125, 0.375, 1e15, 1e16, 1e17, 123456.5, 1234565.0, 0.0001234565, 9.9999995, 99999.95,
                             999999.5, 0.00001, 1e-5, 5e-324, 1.7976931348623157e308, 1e100, 1e-100, 0.1, 0.2, 0.3, 100.0, 1e21, 1e22, 1e23};
    for (double v : values)
        for (int p = 1; p <= 17; ++p)
            ASSERT_TRUE(check_general(v, p, msg, sizeof msg)) << msg;
    // com String::number: precisoes fora de 1 a 17 e nao finitos passam pelo snprintf e dao o mesmo
    const int precisions[] = {-3, -1, 0, 1, 6, 17, 18, 20, 40};
    for (double v : values)
        for (int p : precisions)
        {
            char want[128];
            snprintf(want, sizeof want, "%.*g", p, v);
            EXPECT_EQ(ct::String::number(v, p), want) << v << " " << p;
        }
    EXPECT_EQ(ct::String::number(1.0 / 0.0), "inf");
    EXPECT_EQ(ct::String::number(-1.0 / 0.0), "-inf");
    char want[32];
    snprintf(want, sizeof want, "%g", double_of(0x7FF8000000000000ull));
    EXPECT_EQ(ct::String::number(double_of(0x7FF8000000000000ull)), want);
    snprintf(want, sizeof want, "%g", double_of(0xFFF8000000000000ull));
    EXPECT_EQ(ct::String::number(double_of(0xFFF8000000000000ull)), want);
}

TEST(NumconvParse, KnownValues)
{
    size_t used;
    EXPECT_EQ(parse("0", &used), 0.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(bits_of(parse("-0")), 0x8000000000000000ull);
    EXPECT_EQ(parse("1.5"), 1.5);
    EXPECT_EQ(parse("  1", &used), 0.0); // espacos nao se saltam aqui; o String::to_double salta
    EXPECT_EQ(used, 0u);
    EXPECT_EQ(parse("+.5", &used), 0.5);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(parse("5.", &used), 5.0);
    EXPECT_EQ(used, 2u);
    EXPECT_EQ(parse(".", &used), 0.0);
    EXPECT_EQ(used, 0u);
    EXPECT_EQ(parse("-.", &used), 0.0);
    EXPECT_EQ(used, 0u);
    EXPECT_EQ(parse("e5", &used), 0.0);
    EXPECT_EQ(used, 0u);
    EXPECT_EQ(parse("1e", &used), 1.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(parse("1e+", &used), 1.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(parse("1e5e5", &used), 1e5);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(parse("1.2.3", &used), 1.2);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(parse("1,5", &used), 1.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(parse("--5", &used), 0.0);
    EXPECT_EQ(used, 0u);
    EXPECT_EQ(bits_of(parse("5e-324")), 1ull);
    EXPECT_EQ(bits_of(parse("2.4703282292062327e-324")), 0ull);
    EXPECT_EQ(bits_of(parse("2.4703282292062328e-324")), 1ull);
    EXPECT_EQ(bits_of(parse("1.7976931348623157e308")), 0x7FEFFFFFFFFFFFFFull);
    EXPECT_EQ(bits_of(parse("1.7976931348623158e308")), 0x7FEFFFFFFFFFFFFFull);
    EXPECT_EQ(bits_of(parse("1.7976931348623159e308")), 0x7FF0000000000000ull);
    EXPECT_EQ(bits_of(parse("-1e309")), 0xFFF0000000000000ull);
    EXPECT_EQ(bits_of(parse("1e99999999999999999999")), 0x7FF0000000000000ull);
    EXPECT_EQ(bits_of(parse("1e-99999999999999999999")), 0ull);
    EXPECT_EQ(bits_of(parse("0e99999999999999999999")), 0ull);
    EXPECT_EQ(bits_of(parse("9007199254740993")), bits_of(9007199254740992.0));
    EXPECT_EQ(bits_of(parse("9007199254740993.000000000000000000001")), bits_of(9007199254740994.0));
    EXPECT_EQ(bits_of(parse("9007199254740995")), bits_of(9007199254740996.0));
    EXPECT_EQ(bits_of(parse("2.2250738585072011e-308")), 0x000FFFFFFFFFFFFFull);
    EXPECT_EQ(bits_of(parse("2.2250738585072012e-308")), 0x0010000000000000ull);
    // 340282356779733661637539395458142568448 = (2^128 + FLT_MAX) / 2: o empate vai para o par (infinito)
    EXPECT_EQ(bits_of(parsef("340282356779733661637539395458142568448")), 0x7F800000u);
    EXPECT_EQ(bits_of(parsef("340282356779733661637539395458142568447")), 0x7F7FFFFFu);
    EXPECT_EQ(bits_of(parsef("3.4028235677973366e38")), 0x7F7FFFFFu);
    EXPECT_EQ(bits_of(parsef("1e-45")), 1u);
    EXPECT_EQ(bits_of(parsef("7e-46")), 0u);
    EXPECT_EQ(bits_of(parsef("7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015625e-46")), 0u);
    EXPECT_EQ(bits_of(parsef("7.00649232162408535461864791644958065640130970938257885878534141944895541342930300743319094181060791015626e-46")), 1u);
}

TEST(NumconvParse, LongInputs)
{
    char buf[3000];
    // zeros do principio e muitos algarismos
    EXPECT_EQ(parse("000000000000000000000000000000000000001.5"), 1.5);
    EXPECT_EQ(parse("0.00000000000000000000000000000000000000000000000000000000000000000000001"), 1e-71);
    strcpy(buf, "1");
    for (int i = 0; i < 400; ++i)
        strcat(buf, "0");
    strcat(buf, "e-400");
    EXPECT_EQ(parse(buf), 1.0);
    strcpy(buf, "0.");
    for (int i = 0; i < 400; ++i)
        strcat(buf, "0");
    strcat(buf, "1e400");
    EXPECT_EQ(parse(buf), 0.1);
    // 2^53 + 1 e um empate; com um 1 setecentas casas depois do ponto passa a ser mais que o empate
    strcpy(buf, "9007199254740993.");
    for (int i = 0; i < 700; ++i)
        strcat(buf, "0");
    EXPECT_EQ(bits_of(parse(buf)), bits_of(9007199254740992.0));
    strcat(buf, "1");
    EXPECT_EQ(bits_of(parse(buf)), bits_of(9007199254740994.0));
    char msg[512];
    Rng r(5);
    for (int i = 0; i < 2000; ++i)
    {
        char *o = buf;
        const int n = 20 + static_cast<int>(r.below(900));
        for (int k = 0; k < n; ++k)
            *o++ = static_cast<char>('0' + r.below(10));
        if (r.next() & 1)
            sprintf(o, "e%d", static_cast<int>(r.below(2000)) - 1000);
        else
            *o = 0;
        ASSERT_TRUE(check_parse(buf, msg, sizeof msg)) << msg;
    }
}

TEST(NumconvParse, SpecialSpellings)
{
    size_t used;
    EXPECT_EQ(bits_of(parse("inf", &used)), 0x7FF0000000000000ull);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(bits_of(parse("-INF", &used)), 0xFFF0000000000000ull);
    EXPECT_EQ(used, 4u);
    EXPECT_EQ(bits_of(parse("Infinity", &used)), 0x7FF0000000000000ull);
    EXPECT_EQ(used, 8u);
    EXPECT_EQ(bits_of(parse("infinit", &used)), 0x7FF0000000000000ull);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(bits_of(parse("+iNfInItY!", &used)), 0x7FF0000000000000ull);
    EXPECT_EQ(used, 9u);
    EXPECT_EQ(bits_of(parse("in", &used)), 0ull);
    EXPECT_EQ(used, 0u);
    const double n1 = parse("nan", &used);
    EXPECT_TRUE(n1 != n1);
    EXPECT_EQ(used, 3u);
    parse("NAN(abc_1)", &used);
    EXPECT_EQ(used, 10u);
    parse("nan(abc", &used);
    EXPECT_EQ(used, 3u);
    parse("nan()", &used);
    EXPECT_EQ(used, 5u);
    parse("nan(a b)", &used);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(bits_of(parsef("-inf")), 0xFF800000u);
    const float nf = parsef("-nan");
    EXPECT_TRUE(nf != nf);
    char msg[512];
    const char *cases[] = {"inf", "-inf", "+infinity", "infinit", "nan", "-nan", "nan(1)", "NaN(", "iNF", "n", "na", "i", "infx", "nanx"};
    for (const char *c : cases)
        ASSERT_TRUE(check_parse(c, msg, sizeof msg)) << msg;
}

TEST(NumconvParse, Hexadecimal)
{
    size_t used;
    EXPECT_EQ(parse("0x10", &used), 16.0);
    EXPECT_EQ(used, 4u);
    EXPECT_EQ(parse("0x1.8p1", &used), 3.0);
    EXPECT_EQ(used, 7u);
    EXPECT_EQ(parse("-0X.8P-1", &used), -0.25);
    EXPECT_EQ(parse("0x", &used), 0.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(parse("0xg", &used), 0.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(parse("0x.", &used), 0.0);
    EXPECT_EQ(used, 1u);
    EXPECT_EQ(parse("0x1p", &used), 1.0);
    EXPECT_EQ(used, 3u);
    EXPECT_EQ(bits_of(parse("0x1p-1074")), 1ull);
    EXPECT_EQ(bits_of(parse("0x1p-1075")), 0ull);
    EXPECT_EQ(bits_of(parse("0x1.8p-1075")), 1ull);
    EXPECT_EQ(bits_of(parse("0x1.fffffffffffffp1023")), 0x7FEFFFFFFFFFFFFFull);
    EXPECT_EQ(bits_of(parse("0x1p1024")), 0x7FF0000000000000ull);
    EXPECT_EQ(bits_of(parsef("0x1p-149")), 1u);
    EXPECT_EQ(bits_of(parsef("0x1.fffffep127")), 0x7F7FFFFFu);
    char msg[512];
    const char *cases[] = {"0x1.fffffffffffff8p0", "0x1.fffffffffffff80000000001p0", "0x1.00000000000008p0", "0x1.00000000000008000001p0",
                           "0x0.0000000000000000000000000000001p0", "0x123456789abcdef123456789p-100", "0x.8p-1073", "0x.fffffffffffff8p-1022",
                           "0x1.0000001p0", "0x1.000001p0", "0x1.0000010000000000001p0", "0xffffffffffffffffffffp0", "-0x0p5", "0x00000000000000000001p0"};
    for (const char *c : cases)
        ASSERT_TRUE(check_parse(c, msg, sizeof msg)) << msg;
}

TEST(NumconvParse, RandomFormats)
{
    Rng r(31337);
    char msg[512];
    for (long i = 0; i < iterations(200000) / 4; ++i)
        ASSERT_TRUE(differential_step(r, msg, sizeof msg)) << msg;
}

// o separador e sempre '.', qualquer que seja o locale do programa
TEST(NumconvLocale, DecimalPointDoesNotDependOnLocale)
{
    const char *saved = setlocale(LC_ALL, nullptr);
    char keep[128];
    snprintf(keep, sizeof keep, "%s", saved ? saved : "C");
    const char *names[] = {"pt_BR.utf8", "pt_PT.utf8", "de_DE.utf8", "fr_FR.utf8"};
    bool any = false;
    for (const char *name : names)
    {
        if (!setlocale(LC_ALL, name))
            continue;
        any = true;
        char probe[16];
        snprintf(probe, sizeof probe, "%.1f", 1.5);
        EXPECT_EQ(shortest(0.1), "0.1") << name;
        EXPECT_EQ(shortest(-2.5e-7f), "-2.5e-07") << name;
        EXPECT_EQ(ct::String::number(1.5), "1.5") << name;
        EXPECT_EQ(ct::String::number(1234.5678, 6), "1234.57") << name;
        EXPECT_EQ(ct::String::number(1234.5678, 20), "1234.5678000000000338") << name << " (precisao 20, pelo snprintf)";
        EXPECT_EQ(ct::String::number_shortest(0.1), "0.1") << name;
        EXPECT_EQ(ct::String("1.5").to_double(), 1.5) << name;
        EXPECT_EQ(ct::String("1.5").to_float(), 1.5f) << name;
        EXPECT_EQ(ct::String("1,5").to_double(), 1.0) << name;
        EXPECT_EQ(ct::String("0x1.8p0").to_double(), 1.5) << name;
        EXPECT_EQ(parse("2.75"), 2.75) << name;
        EXPECT_EQ(parsef("1e-3"), 1e-3f) << name;
    }
    setlocale(LC_ALL, keep);
    if (!any)
        GTEST_SKIP() << "nenhum locale com virgula decimal instalado";
}

TEST(NumconvString, ToIntSaturates)
{
    EXPECT_EQ(ct::String("2147483647").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("2147483648").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("3000000000").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("4294967296").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("4294967297").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("-2147483648").to_int(), INT_MIN);
    EXPECT_EQ(ct::String("-2147483649").to_int(), INT_MIN);
    EXPECT_EQ(ct::String("-3000000000").to_int(), INT_MIN);
    EXPECT_EQ(ct::String("-4294967296").to_int(), INT_MIN);
    EXPECT_EQ(ct::String("9223372036854775807").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("9223372036854775808").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("18446744073709551616").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("99999999999999999999999999").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("-99999999999999999999999999").to_int(), INT_MIN);
    EXPECT_EQ(ct::String("  +2147483648abc").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("0000000000000000000000000002147483647").to_int(), INT_MAX);
    EXPECT_EQ(ct::String("00000000000000000000000000000000000012").to_int(), 12);
    EXPECT_EQ(ct::String("-0000000000000000000000000000000000012").to_int(), -12);
}

TEST(NumconvString, ToIntChecked)
{
    struct Case
    {
        const char *text;
        bool ok;
        int value;
    };
    const Case cases[] = {{"0", true, 0},
                          {"42", true, 42},
                          {"-42", true, -42},
                          {"+7", true, 7},
                          {"  \t -17 \n", true, -17},
                          {"2147483647", true, INT_MAX},
                          {"-2147483648", true, INT_MIN},
                          {"2147483648", false, 0},
                          {"-2147483649", false, 0},
                          {"3000000000", false, 0},
                          {"99999999999999999999999", false, 0},
                          {"-99999999999999999999999", false, 0},
                          {"0000000000000000000000000000000000000000000002147483647", true, INT_MAX},
                          {"", false, 0},
                          {"   ", false, 0},
                          {"-", false, 0},
                          {"+", false, 0},
                          {"abc", false, 0},
                          {"12abc", false, 0},
                          {"12 34", false, 0},
                          {"1e3", false, 0},
                          {"0x10", false, 0},
                          {"- 5", false, 0},
                          {"--5", false, 0},
                          {"1.5", false, 0}};
    for (const Case &c : cases)
    {
        int out = 12345;
        const bool ok = ct::String(c.text).to_int(out);
        EXPECT_EQ(ok, c.ok) << "'" << c.text << "'";
        if (c.ok)
            EXPECT_EQ(out, c.value) << "'" << c.text << "'";
        else
            EXPECT_EQ(out, 12345) << "'" << c.text << "' (out nao se mexe quando falha)";
    }
    int out = 0;
    EXPECT_FALSE(ct::String("12\0" "3", 4).to_int(out)); // um NUL no meio e lixo
    EXPECT_TRUE(ct::String("12\0", 3).to_int(out) == false);
}

// contra o strtol com o resultado ao limite do int
TEST(NumconvString, ToIntMatchesSaturatedStrtolOnRandomText)
{
    Rng r(99);
    const char alphabet[] = "0123456789  +-\t\n\v\f\rabcxX.e";
    for (long i = 0; i < iterations(200000) * 2; ++i)
    {
        char buf[48];
        const size_t n = r.below(40);
        const uint32_t digits_bias = static_cast<uint32_t>(r.below(4));
        for (size_t k = 0; k < n; ++k)
        {
            const uint64_t v = r.next();
            buf[k] = (digits_bias && (v & 3)) ? static_cast<char>('0' + (v >> 4) % 10) : alphabet[(v >> 4) % (sizeof(alphabet) - 1)];
        }
        buf[n] = 0;
        const long long wide = strtoll(buf, nullptr, 10);
        const int want = wide > INT_MAX ? INT_MAX : (wide < INT_MIN ? INT_MIN : static_cast<int>(wide));
        ASSERT_EQ(ct::String(buf).to_int(), want) << buf;
        int out;
        const bool ok = ct::String(buf).to_int(out);
        if (ok)
            ASSERT_EQ(out, want) << buf;
    }
}

TEST(NumconvString, ToFloatAndToDouble)
{
    EXPECT_EQ(ct::String("3.5").to_double(), 3.5);
    EXPECT_EQ(ct::String("  -2.5e3xyz").to_double(), -2500.0);
    EXPECT_EQ(ct::String("").to_double(), 0.0);
    EXPECT_EQ(ct::String("abc").to_double(), 0.0);
    EXPECT_EQ(ct::String("1e999").to_double(), 1.0 / 0.0);
    EXPECT_EQ(ct::String("0.1").to_double(), 0.1);
    EXPECT_EQ(ct::String("0.1").to_float(), 0.1f);
    EXPECT_EQ(ct::String("16777217").to_float(), 16777216.0f);
    EXPECT_EQ(ct::String("\t\n 12.5").to_float(), 12.5f);
    EXPECT_EQ(ct::String("1.5\0" "9", 4).to_float(), 1.5f); // o NUL acaba o numero
    EXPECT_EQ(bits_of(ct::String("-0").to_double()), 0x8000000000000000ull);
    // o mesmo que o strtod nos textos que o strtod le inteiros
    Rng r(7);
    char buf[64];
    for (long i = 0; i < iterations(200000); ++i)
    {
        const double v = random_double(r);
        if (v - v != 0)
            continue;
        snprintf(buf, sizeof buf, "%.17g", v);
        ASSERT_EQ(bits_of(ct::String(buf).to_double()), bits_of(strtod(buf, nullptr))) << buf;
        ASSERT_EQ(bits_of(ct::String(buf).to_float()), bits_of(strtof(buf, nullptr))) << buf;
    }
}

TEST(NumconvString, ShortestRoundTripsThroughString)
{
    Rng r(3);
    for (long i = 0; i < iterations(200000) / 2; ++i)
    {
        const double v = random_double(r);
        const ct::String s = ct::String::number_shortest(v);
        if (v != v)
        {
            EXPECT_EQ(s, "nan");
            continue;
        }
        if (v - v != 0)
            continue;
        ASSERT_EQ(bits_of(s.to_double()), bits_of(v)) << s.c_str();
        const float f = float_of(static_cast<uint32_t>(r.next()));
        if (f - f != 0)
            continue;
        const ct::String sf = ct::String::number_shortest(f);
        ASSERT_EQ(bits_of(sf.to_float()), bits_of(f)) << sf.c_str();
    }
    ct::String a("x=");
    a.append_number_shortest(0.1).append(" ").append_number_shortest(0.5f).append(" ").append_number(0.1, 17);
    EXPECT_EQ(a, "x=0.1 0.5 0.10000000000000001");
}
