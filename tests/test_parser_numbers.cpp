// Json, Ini, Xml e TextReader leem e escrevem numeros com ct/numconv.hpp: o texto mais curto que volta ao mesmo double,
// arredondamento correcto, '.' em qualquer locale. Este ficheiro nao usa a biblioteca standard (so o gtest e cabecalhos C).
#include <ct/ini.hpp>
#include <ct/json.hpp>
#include <ct/text.hpp>
#include <ct/xml.hpp>

#include <gtest/gtest.h>

#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed) {}
        uint64_t next()
        {
            s += 0x9e3779b97f4a7c15ull;
            uint64_t z = s;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }
    };

    double from_bits(uint64_t b)
    {
        double d;
        memcpy(&d, &b, sizeof d);
        return d;
    }
    uint64_t to_bits(double d)
    {
        uint64_t b;
        memcpy(&b, &d, sizeof b);
        return b;
    }

    ct::String dump(double v) { return ct::Json(v).dump(); }

    // um double ao acaso de uma classe: bits ao acaso, subnormais, decimais curtos, potencias de 2 e vizinhas, inteiros grandes
    double random_double(Rng &rng)
    {
        switch (rng.next() % 8)
        {
        case 0: return from_bits(rng.next());
        case 1: return from_bits(rng.next() & 0x000fffffffffffffull);
        case 2: return static_cast<double>(static_cast<int64_t>(rng.next() % 2000000)) / 1000.0;
        case 3: return from_bits((rng.next() % 2046 + 1) << 52);
        case 4: return from_bits(((rng.next() % 2046 + 1) << 52) + rng.next() % 3 - 1);
        case 5:
        {
            double v = static_cast<double>(rng.next() % 10000000ull);
            int k = static_cast<int>(rng.next() % 40) - 20;
            for (; k > 0; --k)
                v *= 10;
            for (; k < 0; ++k)
                v /= 10;
            return v;
        }
        case 6: return static_cast<double>(static_cast<int64_t>(rng.next()));
        default: return static_cast<double>(rng.next() % 100000000ull) * (rng.next() & 1 ? 1e-9 : 1e9);
        }
    }

    bool is_json_number(const char *t)
    {
        const char *p = t;
        if (*p == '-')
            ++p;
        if (*p == '0')
            ++p;
        else if (*p >= '1' && *p <= '9')
            while (*p >= '0' && *p <= '9')
                ++p;
        else
            return false;
        if (*p == '.')
        {
            ++p;
            if (!(*p >= '0' && *p <= '9'))
                return false;
            while (*p >= '0' && *p <= '9')
                ++p;
        }
        if (*p == 'e' || *p == 'E')
        {
            ++p;
            if (*p == '+' || *p == '-')
                ++p;
            if (!(*p >= '0' && *p <= '9'))
                return false;
            while (*p >= '0' && *p <= '9')
                ++p;
        }
        return *p == '\0';
    }
}

// O que o Json escreve: o mais curto que volta ao mesmo valor, positional ate 10^max(15, algarismos)
TEST(JsonNumbers, DumpIsTheShortestRoundTripText)
{
    EXPECT_EQ(dump(0.1), "0.1");
    EXPECT_EQ(dump(-0.1), "-0.1");
    EXPECT_EQ(dump(1.0 / 3.0), "0.3333333333333333");   // 16 algarismos (o %.17g dava 0.33333333333333331)
    EXPECT_EQ(dump(2.0 / 3.0), "0.6666666666666666");
    EXPECT_EQ(dump(1e15), "1e+15");                     // inteiros de 1e15 para cima passam ao expoente, como antes
    EXPECT_EQ(dump(123456789012345.0), "123456789012345.0");
    EXPECT_EQ(dump(1e16), "1e+16");
    EXPECT_EQ(dump(1.2345678901234568e20), "1.2345678901234568e+20");
    EXPECT_EQ(dump(5e-324), "5e-324");                  // o menor subnormal (o %.15g dava 4.94065645841247e-324)
    EXPECT_EQ(dump(2.2250738585072014e-308), "2.2250738585072014e-308");
    EXPECT_EQ(dump(1.7976931348623157e308), "1.7976931348623157e+308");
    EXPECT_EQ(dump(1e-5), "1e-05");
    EXPECT_EQ(dump(0.0001), "0.0001");
    EXPECT_EQ(dump(100.0), "100.0");
    EXPECT_EQ(dump(-0.0), "-0.0");
    EXPECT_EQ(dump(0.0), "0.0");
    EXPECT_EQ(dump(1489.71466064453125), "1489.7146606445312"); // empate exacto: o algarismo par (como o printf)
    EXPECT_EQ(dump(8.8630940000000014522700328222938452073e-13), "8.863094000000001e-13");
    EXPECT_EQ(dump(9007199254740993.0), "9007199254740992.0"); // 2^53 + 1 nao existe: arredonda ao par
    EXPECT_EQ(dump(1.5e300), "1.5e+300");
}

TEST(JsonNumbers, ParseIsCorrectlyRoundedAndLocaleFree)
{
    const char *texts[] = {"0.1", "1e23", "9007199254740993", "2.2250738585072011e-308", "4.9e-324", "1.7976931348623157e308",
                           "123456789012345678901234567890", "0.000000000000000000000000000001", "1e-400", "5e-324",
                           "2.2250738585072012e-308", "8.41e21", "7.2057594037927933e16", "1.00000000000000011102230246251565404236316680908203125"};
    for (const char *t : texts)
    {
        ct::Json j = ct::Json::parse(t);
        ASSERT_TRUE(j.is_number()) << t;
        char *end = nullptr;
        const double want = strtod(t, &end);
        EXPECT_EQ(to_bits(j.as_double()), to_bits(want)) << t;
    }
    EXPECT_FALSE(ct::Json::parse("1e999").is_number()); // fora do alcance do double: recusa, como antes
}

// 1e7 valores com o diferencial longo (CT_JSON_ITERS muda o numero; o ctest corre 300000): o texto volta aos mesmos bits,
// e um numero JSON valido, e tem os mesmos algarismos que o write_shortest
TEST(JsonNumbers, RoundTripOfRandomDoublesIsBitExact)
{
    const char *env = getenv("CT_JSON_ITERS");
    const long iters = env ? atol(env) : 300000;
    Rng rng(20241119);
    long checked = 0;
    for (long i = 0; i < iters; ++i)
    {
        const double v = random_double(rng);
        if (v != v || v - v != 0)
            continue;
        const ct::String t = dump(v);
        ASSERT_TRUE(is_json_number(t.c_str())) << t.c_str();
        ct::Json back = ct::Json::parse(t);
        ASSERT_TRUE(back.is_number()) << t.c_str();
        ASSERT_EQ(to_bits(back.as_double()), to_bits(v)) << t.c_str();
        // os mesmos algarismos que o texto mais curto (o Json so muda o ".0" e onde aparece o expoente)
        char ref[48];
        char *e = ct::write_shortest(v, ref);
        *e = '\0';
        char dig_a[24], dig_b[24];
        int na = 0, nb = 0;
        for (const char *p = ref; *p && *p != 'e'; ++p)
            if (*p >= '0' && *p <= '9' && !(na == 0 && *p == '0'))
                dig_a[na++] = *p;
        for (const char *p = t.c_str(); *p && *p != 'e'; ++p)
            if (*p >= '0' && *p <= '9' && !(nb == 0 && *p == '0'))
                dig_b[nb++] = *p;
        while (na && dig_a[na - 1] == '0')
            --na;
        while (nb && dig_b[nb - 1] == '0')
            --nb;
        ASSERT_TRUE(na == nb && memcmp(dig_a, dig_b, static_cast<size_t>(na)) == 0) << t.c_str() << " contra " << ref;
        ++checked;
    }
    EXPECT_GT(checked, iters / 2);
}

TEST(IniNumbers, SetAndGetDoubleRoundTripAndTextIsShortest)
{
    ct::Ini ini;
    const double values[] = {0.1, 1.0 / 3.0, 1e15, 1e-5, 5e-324, 1.7976931348623157e308, -2.5, 3.0, 123456.789, 0.0};
    const char *texts[] = {"0.1", "0.3333333333333333", "1e+15", "1e-05", "5e-324", "1.7976931348623157e+308", "-2.5", "3", "123456.789", "0"};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; ++i)
    {
        ini.set("s", "k", values[i]);
        EXPECT_EQ(ini.get("s", "k"), texts[i]);
        EXPECT_EQ(to_bits(ini.get_double("s", "k", -1)), to_bits(values[i])) << texts[i];
    }
    ct::Ini parsed = ct::Ini::parse("[s]\na = 1.5\nb = \t 2e3\nc = 0x1p4\nd = 1.5x\ne = \nf = inf\ng = .5\nh = 5.\n");
    EXPECT_EQ(parsed.get_double("s", "a"), 1.5);
    EXPECT_EQ(parsed.get_double("s", "b"), 2000.0);
    EXPECT_EQ(parsed.get_double("s", "c"), 16.0);
    EXPECT_EQ(parsed.get_double("s", "d", -7), -7.0); // lixo no fim: usa o valor por omissao
    EXPECT_EQ(parsed.get_double("s", "e", -7), -7.0);
    EXPECT_GT(parsed.get_double("s", "f", -7), 1e308);
    EXPECT_EQ(parsed.get_double("s", "g"), 0.5);
    EXPECT_EQ(parsed.get_double("s", "h"), 5.0);
}

TEST(XmlNumbers, AttributeDoublesIntsAndLocale)
{
    ct::Xml x = ct::Xml::parse("<a r=\"  2.5\" s=\"1e3\" t=\"abc\" u=\"7.25xyz\" v=\"0x1p3\" i=\"3.0\" j=\"2.5e1\" k=\"-1.5\"/>");
    EXPECT_EQ(x.attr_double("r", -1), 2.5);
    EXPECT_EQ(x.attr_double("s", -1), 1000.0);
    EXPECT_EQ(x.attr_double("t", -1), -1.0);   // nao e numero: o valor por omissao
    EXPECT_EQ(x.attr_double("u", -1), 7.25);   // o prefixo numerico (como o strtod)
    EXPECT_EQ(x.attr_double("v", -1), 8.0);
    EXPECT_EQ(x.attr_int("i", -1), 3);
    EXPECT_EQ(x.attr_int("j", -1), 25);
    EXPECT_EQ(x.attr_int("k", -1), -1);
    EXPECT_EQ(x.attr_double("nao_existe", 4.5), 4.5);
    const char *names[] = {"pt_BR.utf8", "pt_PT.utf8", "de_DE.utf8"};
    const char *saved = setlocale(LC_ALL, nullptr);
    char keep[128];
    snprintf(keep, sizeof keep, "%s", saved ? saved : "C");
    bool any = false;
    for (const char *n : names)
    {
        if (!setlocale(LC_ALL, n))
            continue;
        any = true;
        EXPECT_EQ(x.attr_double("r", -1), 2.5) << n;
        EXPECT_EQ(x.attr_double("t", -1), -1.0) << n; // com a virgula como separador, o texto invalido devolvia 0 e nao o valor por omissao
        EXPECT_EQ(dump(0.1), "0.1") << n;
        ct::Ini ini;
        ini.set("s", "k", 0.5);
        EXPECT_EQ(ini.get("s", "k"), "0.5") << n;
        EXPECT_EQ(ini.get_double("s", "k", -1), 0.5) << n;
        EXPECT_EQ(ct::Json::parse("1.25").as_double(), 1.25) << n;
    }
    setlocale(LC_ALL, keep);
    if (!any)
        GTEST_SKIP() << "nenhum locale com virgula decimal instalado";
}

TEST(TextNumbers, ReadDoubleTakesTheWholeWordLikeStrtod)
{
    const char words[] = "3.5 1e3 0x1p-2 inf abc 2.5x -7 .5 5. nan";
    ct::MemoryStream m(words, sizeof(words) - 1);
    ct::TextReader r(m);
    double d = 0;
    ASSERT_TRUE(r.read_double(d));
    EXPECT_EQ(d, 3.5);
    ASSERT_TRUE(r.read_double(d));
    EXPECT_EQ(d, 1000.0);
    ASSERT_TRUE(r.read_double(d));
    EXPECT_EQ(d, 0.25);
    ASSERT_TRUE(r.read_double(d));
    EXPECT_GT(d, 1e308);
    EXPECT_FALSE(r.read_double(d)); // abc
    EXPECT_FALSE(r.read_double(d)); // 2.5x
    ASSERT_TRUE(r.read_double(d));
    EXPECT_EQ(d, -7.0);
    ASSERT_TRUE(r.read_double(d));
    EXPECT_EQ(d, 0.5);
    ASSERT_TRUE(r.read_double(d));
    EXPECT_EQ(d, 5.0);
    ASSERT_TRUE(r.read_double(d));
    EXPECT_TRUE(d != d);
}
