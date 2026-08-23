#include <ct/xml.hpp>

#include <gtest/gtest.h>

using ct::String;
using ct::Xml;

namespace
{
    Xml parse_ok(const char *text)
    {
        Xml::Error err;
        Xml x = Xml::parse(text, &err);
        EXPECT_FALSE(static_cast<bool>(err))
            << "input: " << text << " erro: " << (err.message ? err.message : "");
        return x;
    }

    Xml::Error parse_err(const char *text)
    {
        Xml::Error err;
        Xml x = Xml::parse(text, &err);
        EXPECT_TRUE(static_cast<bool>(err)) << "devia falhar: " << text;
        EXPECT_TRUE(x.tag().empty());
        return err;
    }
}

TEST(Xml, DefaultIsEmpty)
{
    Xml x;
    EXPECT_TRUE(x.tag().empty());
    EXPECT_TRUE(x.text().empty());
    EXPECT_TRUE(x.empty());
    EXPECT_EQ(x.size(), 0u);
    EXPECT_EQ(x.attributes().size(), 0u);
}

TEST(Xml, SelfClosing)
{
    Xml x = parse_ok("<root/>");
    EXPECT_EQ(x.tag(), "root");
    EXPECT_TRUE(x.empty());
    EXPECT_TRUE(x.text().empty());
}

TEST(Xml, SelfClosingWithSpace)
{
    Xml x = parse_ok("<root />");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, OpenClose)
{
    Xml x = parse_ok("<root></root>");
    EXPECT_EQ(x.tag(), "root");
    EXPECT_TRUE(x.empty());
}

TEST(Xml, WhitespaceIsTrimmedFromWhitespaceOnlyToken)
{
    Xml x = parse_ok("  \n <root/>\n ");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, AttributesDoubleAndSingleQuotes)
{
    Xml x = parse_ok("<root a=\"1\" b='2'/>");
    ASSERT_TRUE(x.has_attr("a"));
    ASSERT_TRUE(x.has_attr("b"));
    EXPECT_STREQ(x.attr_cstr("a"), "1");
    EXPECT_STREQ(x.attr_cstr("b"), "2");
    EXPECT_FALSE(x.has_attr("c"));
    EXPECT_STREQ(x.attr_cstr("c", "def"), "def");
}

TEST(Xml, AttributeEntitiesAndNumericRefs)
{
    Xml x = parse_ok("<root a=\"x&amp;y&lt;z&gt;w&quot;q&apos;r\" b=\"&#65;&#x42;\"/>");
    EXPECT_STREQ(x.attr_cstr("a"), "x&y<z>w\"q'r");
    EXPECT_STREQ(x.attr_cstr("b"), "AB");
}

TEST(Xml, AttributeWhitespaceNormalization)
{

    Xml x = parse_ok("<root a=\"x\ty\nz\"/>");
    EXPECT_STREQ(x.attr_cstr("a"), "x y z");
}

TEST(Xml, TypedAttributeGetters)
{
    Xml x = parse_ok("<root i=\"-42\" u=\"42\" f=\"3.5\" t=\"true\" tt=\"1\" "
                      "fa=\"false\" ff=\"0\" trunc=\"7.9\"/>");
    EXPECT_EQ(x.attr_int("i"), -42);
    EXPECT_EQ(x.attr_uint("u"), 42u);
    EXPECT_DOUBLE_EQ(x.attr_double("f"), 3.5);
    EXPECT_TRUE(x.attr_bool("t"));
    EXPECT_TRUE(x.attr_bool("tt"));
    EXPECT_FALSE(x.attr_bool("fa"));
    EXPECT_FALSE(x.attr_bool("ff"));
    EXPECT_EQ(x.attr_int("trunc"), 7); 
    EXPECT_EQ(x.attr_int("nope", -1), -1);
    EXPECT_EQ(x.attr_uint("nope", 9u), 9u);
    EXPECT_DOUBLE_EQ(x.attr_double("nope", 1.5), 1.5);
    EXPECT_EQ(x.attr_bool("nope", true), true);
}

TEST(Xml, SetAttrOverwritesAndAdds)
{
    Xml x("root");
    x.set_attr("a", "1");
    x.set_attr("a", "2");
    x.set_attr("b", "3");
    ASSERT_EQ(x.attributes().size(), 2u);
    EXPECT_STREQ(x.attr_cstr("a"), "2");
    EXPECT_STREQ(x.attr_cstr("b"), "3");
    EXPECT_TRUE(x.erase_attr("a"));
    EXPECT_FALSE(x.has_attr("a"));
    EXPECT_FALSE(x.erase_attr("a"));
}

TEST(Xml, TextContent)
{
    Xml x = parse_ok("<root>ola mundo</root>");
    EXPECT_EQ(x.text(), "ola mundo");
}

TEST(Xml, TextEntities)
{
    Xml x = parse_ok("<root>a&amp;b &lt;tag&gt; &#9731;</root>");
    EXPECT_EQ(x.text(), "a&b <tag> \xE2\x98\x83"); 
}

TEST(Xml, Cdata)
{
    Xml x = parse_ok("<data><![CDATA[1,2,<3>,&4]]></data>");
    EXPECT_EQ(x.text(), "1,2,<3>,&4"); 
}

TEST(Xml, CdataConcatenatesWithSurroundingText)
{
    Xml x = parse_ok("<data>a<![CDATA[b]]>c</data>");
    EXPECT_EQ(x.text(), "abc");
}

TEST(Xml, TextTrimmed)
{
    Xml x = parse_ok("<data>\n  1,2,3  \n</data>");
    EXPECT_EQ(x.text_trimmed(), "1,2,3");
}

TEST(Xml, InsignificantWhitespaceBetweenChildrenIsDropped)
{
    Xml x = parse_ok("<root>\n  <a/>\n  <b/>\n</root>");
    EXPECT_TRUE(x.text().empty()); 
    EXPECT_EQ(x.size(), 2u);
}

TEST(Xml, WhitespaceOnlyLeafKeepsText)
{

    Xml x = parse_ok("<sep>   </sep>");
    EXPECT_EQ(x.text(), "   ");
}

TEST(Xml, ChildrenAndLookup)
{
    Xml x = parse_ok("<map><tileset id=\"1\"/><layer name=\"chao\"/><layer name=\"topo\"/></map>");
    EXPECT_EQ(x.size(), 3u);
    Xml *ts = x.child("tileset");
    ASSERT_NE(ts, nullptr);
    EXPECT_STREQ(ts->attr_cstr("id"), "1");
    Xml *layer = x.child("layer"); 
    ASSERT_NE(layer, nullptr);
    EXPECT_STREQ(layer->attr_cstr("name"), "chao");
    EXPECT_EQ(x.child("nope"), nullptr);
}

TEST(Xml, AddChildAndAccessors)
{
    Xml root("root");
    root.add_child(Xml("a")).set_attr("k", "v");
    root.add_child(Xml("b"));
    EXPECT_EQ(root.size(), 2u);
    EXPECT_STREQ(root.child("a")->attr_cstr("k"), "v");
}

TEST(Xml, NestedTraversal)
{
    Xml x = parse_ok(
        "<map>"
        "<layer name=\"chao\"><data encoding=\"csv\">1,2,3,4</data></layer>"
        "</map>");
    Xml *layer = x.child("layer");
    ASSERT_NE(layer, nullptr);
    Xml *data = layer->child("data");
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->text(), "1,2,3,4");
}

TEST(Xml, PrologAndCommentsAreSkipped)
{
    Xml x = parse_ok("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<!-- comentario antes -->\n"
                      "<root>\n"
                      "  <!-- comentario dentro -->\n"
                      "  <a/>\n"
                      "</root>\n"
                      "<!-- comentario depois -->\n");
    EXPECT_EQ(x.tag(), "root");
    EXPECT_EQ(x.size(), 1u);
}

TEST(Xml, DoctypeIsSkipped)
{
    Xml x = parse_ok("<!DOCTYPE root SYSTEM \"x.dtd\">\n<root/>");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, DoctypeWithInternalSubsetIsSkipped)
{
    Xml x = parse_ok("<!DOCTYPE root [ <!ELEMENT root (#PCDATA)> ]>\n<root/>");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, ProcessingInstructionInsideContentIsSkipped)
{
    Xml x = parse_ok("<root><?some-pi data?><a/></root>");
    EXPECT_EQ(x.size(), 1u);
}

TEST(Xml, Utf8Bom)
{
    const char with_bom[] = "\xEF\xBB\xBF<root/>";
    Xml x = parse_ok(with_bom);
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, ErrorEmptyInput)
{
    Xml::Error e = parse_err("");
    EXPECT_TRUE(static_cast<bool>(e));
}

TEST(Xml, ErrorNoRootElement)
{
    parse_err("   \n  ");
}

TEST(Xml, ErrorUnclosedTag)
{
    parse_err("<a><b>");
}

TEST(Xml, ErrorMismatchedCloseTag)
{
    parse_err("<a><b></c></a>");
}

TEST(Xml, ErrorTrailingGarbage)
{
    parse_err("<a/><b/>");
}

TEST(Xml, ErrorRawLessThanInAttribute)
{
    parse_err("<a b=\"<\"/>");
}

TEST(Xml, ErrorUnknownEntityWithoutDtd)
{
    Xml::Error e = parse_err("<a>&bogus;</a>");
    EXPECT_STREQ(e.message, "entidade desconhecida (sem suporte a DTD)");
}

TEST(Xml, ErrorUnterminatedComment)
{
    parse_err("<a><!-- sem fecho</a>");
}

TEST(Xml, ErrorUnterminatedCdata)
{
    parse_err("<a><![CDATA[sem fecho</a>");
}

TEST(Xml, ErrorNullInput)
{
    Xml::Error e;
    Xml x = Xml::parse(static_cast<const char *>(nullptr), &e);
    EXPECT_TRUE(static_cast<bool>(e));
    EXPECT_TRUE(x.tag().empty());
}

TEST(Xml, ErrorLineAndColumn)
{
    Xml::Error e = parse_err("<a>\n  <b>\n</a>"); 
    EXPECT_GE(e.line, 1u);
    EXPECT_GE(e.column, 1u);
}

TEST(Xml, ErrorDepthLimit)
{
    ct::String s;
    for (std::size_t i = 0; i < Xml::kMaxDepth + 10; ++i)
        s.append("<a>", 3);
    Xml::Error e;
    Xml::parse(s, &e);
    EXPECT_TRUE(static_cast<bool>(e));
}

TEST(Xml, DumpSelfClosingWhenEmpty)
{
    Xml x("root");
    x.set_attr("a", "1");
    EXPECT_EQ(x.dump(), "<root a=\"1\"/>");
}

TEST(Xml, DumpEscapesAttributesAndText)
{
    Xml x("root");
    x.set_attr("a", "x&y<z>\"q");
    x.set_text("a&b<c>");
    ct::String out = x.dump();

    EXPECT_TRUE(out.find("&amp;") != ct::String::npos);
    EXPECT_TRUE(out.find("&lt;") != ct::String::npos);
    EXPECT_TRUE(out.find("&quot;") != ct::String::npos);
}

TEST(Xml, DumpCompactRoundTrip)
{
    Xml original = parse_ok(
        "<map version=\"1.10\"><tileset firstgid=\"1\" name=\"a &amp; b\"/>"
        "<layer name=\"chao\"><data>1,2,3</data></layer></map>");
    ct::String dumped = original.dump();
    Xml reparsed = parse_ok(dumped.c_str());
    EXPECT_EQ(reparsed.tag(), "map");
    EXPECT_STREQ(reparsed.attr_cstr("version"), "1.10");
    EXPECT_STREQ(reparsed.child("tileset")->attr_cstr("name"), "a & b");
    EXPECT_EQ(reparsed.child("layer")->child("data")->text(), "1,2,3");
}

TEST(Xml, DumpIndentedIsParseable)
{
    Xml original = parse_ok("<a><b/><c><d/></c></a>");
    ct::String dumped = original.dump(2);
    Xml reparsed = parse_ok(dumped.c_str());
    EXPECT_EQ(reparsed.tag(), "a");
    EXPECT_EQ(reparsed.size(), 2u);
    EXPECT_EQ(reparsed.child("c")->size(), 1u);
}

TEST(Xml, DumpDocumentHasPrologAndParses)
{
    Xml x("root");
    x.add_child(Xml("a"));
    ct::String doc = x.dump_document(2);
    EXPECT_EQ(doc.find("<?xml"), 0u);
    Xml::Error err;
    Xml reparsed = Xml::parse(doc, &err);
    EXPECT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(reparsed.tag(), "root");
}

TEST(Xml, DumpAttributeSpecialWhitespaceRoundTrips)
{
    Xml x("root");
    x.set_attr("a", "line1\nline2\ttab");
    ct::String dumped = x.dump();
    Xml reparsed = parse_ok(dumped.c_str());
    EXPECT_STREQ(reparsed.attr_cstr("a"), "line1\nline2\ttab");
}

TEST(Xml, CopyIsDeep)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b = a;
    b.add_child(Xml("child2"));
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 2u);
}

TEST(Xml, CopyAssignIsDeep)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b("outro");
    b = a;
    b.add_child(Xml("child2"));
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 2u);
    EXPECT_EQ(b.tag(), "root");
}

TEST(Xml, MoveLeavesSourceValidAndEmpty)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b(ct::detail::move(a));
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(a.size(), 0u); 
    EXPECT_TRUE(a.empty());
}

TEST(Xml, MoveAssignLeavesSourceValid)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b;
    b = ct::detail::move(a);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_TRUE(a.empty());
    a.add_child(Xml("still works")); 
    EXPECT_EQ(a.size(), 1u);
}

TEST(Xml, ParseFromCtString)
{
    ct::String s = "<root a=\"1\"/>";
    Xml x = parse_ok(s.c_str());
    EXPECT_STREQ(x.attr_cstr("a"), "1");
    Xml::Error err;
    Xml y = Xml::parse(s, &err);
    EXPECT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(y.tag(), "root");
}