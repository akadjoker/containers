#include <ct/stream.hpp>

#include <gtest/gtest.h>

TEST(Stream, MemoryReadWriteSeekAndTake)
{
    ct::MemoryStream stream;
    EXPECT_TRUE(stream.write_all("hello", 5));
    EXPECT_EQ(stream.size(), 5);
    EXPECT_TRUE(stream.seek(1, ct::Seek::Set));
    char text[4] = {};
    EXPECT_TRUE(stream.read_exact(text, 3));
    EXPECT_EQ(std::memcmp(text, "ell", 3), 0);
    EXPECT_TRUE(stream.seek(0, ct::Seek::End));
    EXPECT_TRUE(stream.eof());
    ct::Vector<std::uint8_t> data = stream.take();
    ASSERT_EQ(data.size(), 5u);
    EXPECT_EQ(std::memcmp(data.data(), "hello", 5), 0);
}

TEST(Stream, ReadOnlyMemoryAndSubstream)
{
    const char text[] = "0123456789";
    ct::MemoryStream memory(text, 10);
    ct::SubStream sub(memory, 3, 4);
    char out[5] = {};
    EXPECT_TRUE(sub.read_exact(out, 4));
    EXPECT_EQ(std::memcmp(out, "3456", 4), 0);
    EXPECT_TRUE(sub.eof());
    EXPECT_FALSE(sub.write("x", 1));
    EXPECT_NE(sub.error(), nullptr);
}

TEST(Stream, FileHelpers)
{
    const char *path = "/tmp/ct_stream_test.bin";
    ASSERT_TRUE(ct::File::write_all(path, "abc", 3));
    ct::String text;
    ASSERT_TRUE(ct::File::read_all(path, text));
    EXPECT_EQ(text, "abc");
    EXPECT_EQ(ct::File::size(path), 3);
    EXPECT_TRUE(ct::File::remove(path));
}
