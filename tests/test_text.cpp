#include <ct/text.hpp>
#include <gtest/gtest.h>

TEST(Text, LinesBomAndWords)
{
    const char source[] = "\xef\xbb\xbf" "first\r\nsecond\rthird\nlast";
    ct::MemoryStream stream(source, sizeof(source) - 1); ct::TextReader reader(stream); ct::String line;
    ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "first"); ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "second"); ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "third"); ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "last"); EXPECT_FALSE(reader.read_line(line));
    ct::MemoryStream words("  42  3.5 hello", 15); ct::TextReader tokens(words); long long integer; double real;
    EXPECT_TRUE(tokens.read_int(integer)); EXPECT_EQ(integer, 42); EXPECT_TRUE(tokens.read_double(real)); EXPECT_DOUBLE_EQ(real, 3.5); EXPECT_TRUE(tokens.read_word(line)); EXPECT_EQ(line, "hello");
}

TEST(Text, WriterBuffersAndFormats)
{
    ct::MemoryStream stream; { ct::TextWriter writer(stream); writer.line("hello").number(42).write(' ').number(1.25, 2).write('\n').fmt("%s:%d", "x", 7); EXPECT_TRUE(writer.flush()); }
    ct::String text; ASSERT_TRUE(stream.seek(0, ct::Seek::Set)); ASSERT_TRUE(ct::TextReader(stream).read_all(text)); EXPECT_EQ(text, "hello\n42 1.2\nx:7");
}
