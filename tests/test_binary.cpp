#include <ct/binary.hpp>
#include <gtest/gtest.h>

TEST(Binary, RoundTripAndLittleEndian)
{
    ct::MemoryStream stream;
    ct::BinaryWriter writer(stream);
    writer.u32(0x01020304); writer.i64(-42); writer.f32(1.5f); writer.varint(300); writer.string("ola");
    ASSERT_TRUE(writer.ok());
    const ct::Span<const std::uint8_t> raw = stream.data();
    ASSERT_GE(raw.size(), 4u);
    EXPECT_EQ(raw[0], 4); EXPECT_EQ(raw[1], 3); EXPECT_EQ(raw[2], 2); EXPECT_EQ(raw[3], 1);
    ASSERT_TRUE(stream.seek(0, ct::Seek::Set));
    ct::BinaryReader reader(stream);
    EXPECT_EQ(reader.u32(), 0x01020304u); EXPECT_EQ(reader.i64(), -42); EXPECT_FLOAT_EQ(reader.f32(), 1.5f); EXPECT_EQ(reader.varint(), 300u);
    ct::String text; EXPECT_TRUE(reader.string(text)); EXPECT_EQ(text, "ola"); EXPECT_TRUE(reader.ok());
}

TEST(Binary, TruncatedReadFails)
{
    const std::uint8_t raw[] = {1, 2}; ct::MemoryStream stream(raw, sizeof(raw)); ct::BinaryReader reader(stream);
    EXPECT_EQ(reader.u32(), 0u); EXPECT_FALSE(reader.ok()); EXPECT_EQ(reader.u8(), 0);
}
