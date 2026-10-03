#include <ct/ini.hpp>

#include <gtest/gtest.h>

using ct::Ini;
using ct::String;

TEST(Ini, ParsesSectionsAndKeys)
{
    Ini ini = Ini::parse(
        "; comentario\n"
        "[window]\n"
        "width=640\n"
        "height : 480\n"
        "title = demo app\n"
        "\n"
        "# outro comentario\n"
        "[server]\n"
        "host=127.0.0.1\n"
        "port=8080\n");

    ASSERT_TRUE(ini.has_section("window"));
    ASSERT_TRUE(ini.has_section("server"));
    EXPECT_EQ(ini.get("window", "width"), "640");
    EXPECT_EQ(ini.get("window", "height"), "480");
    EXPECT_EQ(ini.get("window", "title"), "demo app");
    EXPECT_EQ(ini.get("server", "host"), "127.0.0.1");
    EXPECT_EQ(ini.get_int("server", "port"), 8080);
}

TEST(Ini, GlobalEntries)
{
    Ini ini = Ini::parse("name=ct\nversion=1\n");
    EXPECT_EQ(ini.get("", "name"), "ct");
    EXPECT_EQ(ini.get("", "version"), "1");
}

TEST(Ini, TypedGetters)
{
    Ini ini = Ini::parse(
        "[cfg]\n"
        "count=42\n"
        "ratio=1.5\n"
        "enabled=true\n"
        "name=ct\n");
    EXPECT_EQ(ini.get_int("cfg", "count"), 42);
    EXPECT_DOUBLE_EQ(ini.get_double("cfg", "ratio"), 1.5);
    EXPECT_TRUE(ini.get_bool("cfg", "enabled"));
    EXPECT_EQ(ini.get_int("cfg", "missing", -1), -1);
    EXPECT_FALSE(ini.get_bool("cfg", "missing"));
}

TEST(Ini, SettersAndErase)
{
    Ini ini;
    ini.set("a", "x", "1");
    ini.set("a", "y", 2);
    ini.set("a", "z", 3.5);
    ini.set("a", "flag", true);
    EXPECT_EQ(ini.get("a", "x"), "1");
    EXPECT_EQ(ini.get("a", "y"), "2");
    EXPECT_EQ(ini.get("a", "z"), "3.5");
    EXPECT_EQ(ini.get("a", "flag"), "true");

    EXPECT_TRUE(ini.erase("a", "x"));
    EXPECT_FALSE(ini.has("a", "x"));
    EXPECT_TRUE(ini.erase_section("a"));
    EXPECT_FALSE(ini.has_section("a"));
}

TEST(Ini, DumpRoundTrip)
{
    Ini ini;
    ini.set("", "root", "/tmp");
    ini.set("window", "width", 800);
    ini.set("window", "height", 600);
    ini.set("server", "port", 80);

    String text = ini.dump();
    Ini back = Ini::parse(text);
    EXPECT_EQ(back.get("", "root"), "/tmp");
    EXPECT_EQ(back.get_int("window", "width"), 800);
    EXPECT_EQ(back.get_int("window", "height"), 600);
    EXPECT_EQ(back.get_int("server", "port"), 80);
}

TEST(Ini, FileSaveLoad)
{
    const char *path = "ct_ini_tmp.ini";
    Ini ini;
    ini.set("ui", "theme", "dark");
    ini.set("ui", "scale", 1.25);
    ASSERT_TRUE(ini.save(path));

    Ini loaded;
    ASSERT_TRUE(Ini::load(loaded, path));
    EXPECT_EQ(loaded.get("ui", "theme"), "dark");
    EXPECT_DOUBLE_EQ(loaded.get_double("ui", "scale"), 1.25);

    ct::File::remove(path);
}

TEST(Ini, GettersTipadosDevolvemFallbackParaValoresNaoNumericos)
{
    Ini ini = Ini::parse(
        "[s]\n"
        "texto=abc\n"
        "hex=0x10\n"
        "real=3.14\n"
        "vazio=\n"
        "grande=99999999999999999999\n"
        "neg=-17\n");
    EXPECT_EQ(ini.get_int("s", "texto", 42), 42);
    EXPECT_EQ(ini.get_int("s", "hex", 42), 42);
    EXPECT_EQ(ini.get_int("s", "real", 42), 42);
    EXPECT_EQ(ini.get_int("s", "vazio", 42), 42);
    EXPECT_EQ(ini.get_int("s", "grande", 42), 42);
    EXPECT_EQ(ini.get_int("s", "neg", 42), -17);
    EXPECT_DOUBLE_EQ(ini.get_double("s", "texto", 2.5), 2.5);
    EXPECT_DOUBLE_EQ(ini.get_double("s", "vazio", 2.5), 2.5);
    EXPECT_DOUBLE_EQ(ini.get_double("s", "real", 2.5), 3.14);
    EXPECT_DOUBLE_EQ(ini.get_double("s", "neg", 2.5), -17.0);
}

TEST(Ini, CabecalhoSemFechoNaoRedirecionaChavesParaASeccaoAnterior)
{
    Ini ini = Ini::parse(
        "[s]\n"
        "a=1\n"
        "[t\n"
        "z=9\n"
        "[\n"
        "w=2\n");
    EXPECT_EQ(ini.get("s", "a"), "1");
    EXPECT_EQ(ini.get("s", "z", "ausente"), "ausente");
    EXPECT_EQ(ini.get("t", "z"), "9");
    EXPECT_EQ(ini.get("t", "w"), "2");
}

TEST(Ini, RoundTripPreservaValoresComEspacosSeparadoresEQuebrasDeLinha)
{
    const char *values[] = {"line1\nline2", "  padded  ", "a;b", "k=v", "x:y", "", "\"quoted\"", "back\\slash", "tab\there", "ends with space ", "#notacomment", "normal value"};
    Ini ini;
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        String key("v");
        key.append_number(i);
        ini.set("s", key.c_str(), values[i]);
    }
    String text = ini.dump();
    Ini back = Ini::parse(text);
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        String key("v");
        key.append_number(i);
        EXPECT_EQ(back.get("s", key), values[i]) << "valor " << i << " em\n" << text.c_str();
    }
    EXPECT_EQ(back.section("s")->entries.size(), sizeof(values) / sizeof(values[0]));
    EXPECT_TRUE(Ini::parse(back.dump()).dump() == text);
}

TEST(Ini, ValoresEntreAspasAceitamComentarioNoFimEEscapes)
{
    Ini ini = Ini::parse(
        "[s]\n"
        "a = \"x ; y\" ; comentario\n"
        "b = \"linha1\\nlinha2\\t\\\"fim\\\"\"\n"
        "c = \"sem fecho\n"
        "d = plain ; fica\n");
    EXPECT_EQ(ini.get("s", "a"), "x ; y");
    EXPECT_EQ(ini.get("s", "b"), "linha1\nlinha2\t\"fim\"");
    EXPECT_EQ(ini.get("s", "c"), "\"sem fecho");
    EXPECT_EQ(ini.get("s", "d"), "plain ; fica");
}

TEST(Ini, DoublesFazemRoundTripExacto)
{
    const double values[] = {3.141592653589793, 123456789.0, 0.1, 1e-7, 1e300, -2.5e-300, 1.0 / 3.0, 100.0, 0.0};
    Ini ini;
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        String key("d");
        key.append_number(i);
        ini.set("n", key.c_str(), values[i]);
    }
    EXPECT_EQ(ini.get("n", "d1"), "123456789");
    EXPECT_EQ(ini.get("n", "d2"), "0.1");
    EXPECT_EQ(ini.get("n", "d7"), "100");
    Ini back = Ini::parse(ini.dump());
    for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        String key("d");
        key.append_number(i);
        EXPECT_EQ(back.get_double("n", key, -1.0), values[i]) << "valor " << i;
    }
}

TEST(Ini, ChavesESeccoesInvalidasSaoFatais)
{
    Ini ini;
    EXPECT_DEATH(ini.set("s", "a=b", "1"), "");
    EXPECT_DEATH(ini.set("s", "a:b", "1"), "");
    EXPECT_DEATH(ini.set("s", "", "1"), "");
    EXPECT_DEATH(ini.set("s", "a\nb", "1"), "");
    EXPECT_DEATH(ini.set("x]y", "a", "1"), "");
    ini.set("ok", "key with spaces", "1");
    EXPECT_EQ(Ini::parse(ini.dump()).get("ok", "key with spaces"), "1");
}
