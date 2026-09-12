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
