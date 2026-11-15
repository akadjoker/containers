// Funcoes pequenas que nenhum teste chamava (a lista saiu de tools/coverage.sh --functions): acessores, operadores de
// comparacao, construtores de Json, as funcoes de Stream que so existem para os callbacks, can_*, close e skip. Cada uma
// confere o que devolve. Sem biblioteca standard.

#include <ct/array.hpp>
#include <ct/deque.hpp>
#include <ct/json.hpp>
#include <ct/ptr.hpp>
#include <ct/span.hpp>
#include <ct/stream.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>

TEST(Coverage, FuncoesPequenasQueNenhumTesteChamava)
{
    // String: acessores e operadores que ficaram por chamar
    ct::String s("hello world");
    EXPECT_EQ(s.length(), 11u);
    EXPECT_EQ(s.at(4), 'o');
    EXPECT_EQ(s.front(), 'h');
    const ct::String &cs = s;
    EXPECT_EQ(cs.end() - cs.begin(), 11);
    EXPECT_EQ(cs.cend() - cs.cbegin(), 11);
    EXPECT_EQ(*cs.cbegin(), 'h');
    s.at(0) = 'H';
    EXPECT_TRUE(s == "Hello world");
    EXPECT_TRUE(ct::String("ab") <= ct::String("ab"));
    EXPECT_TRUE(ct::String("ab") <= ct::String("b"));
    EXPECT_FALSE(ct::String("b") <= ct::String("ab"));
    EXPECT_TRUE(ct::String("b") >= ct::String("ab"));
    EXPECT_TRUE(ct::String("ab") >= ct::String("ab"));
    EXPECT_FALSE(ct::String("ab") >= ct::String("b"));
    EXPECT_TRUE("abc" != ct::String("abd"));
    EXPECT_FALSE("abc" != ct::String("abc"));
    ct::String ins("0123");
    ins.insert(2, "xy");
    EXPECT_TRUE(ins == "01xy23");
    const char *none = nullptr;
    ins.insert(1, none);
    EXPECT_TRUE(ins == "01xy23");
    ins.insert(ins.size(), "!");
    EXPECT_TRUE(ins == "01xy23!");
    ins.insert(0, "");
    EXPECT_TRUE(ins == "01xy23!");

    // StringView
    ct::StringView v("abcdef");
    EXPECT_EQ(v.length(), 6u);
    EXPECT_EQ(v.end() - v.begin(), 6);
    EXPECT_EQ(v.bytes().size(), 6u);
    EXPECT_TRUE("abc" != ct::StringView("abd"));
    EXPECT_FALSE("abc" != ct::StringView("abc"));
    ct::StringView empty;
    EXPECT_EQ(empty.begin(), empty.end());
    EXPECT_EQ(empty.bytes().size(), 0u);

    // Span e Array e Deque
    int values[4] = {5, 6, 7, 8};
    ct::Span<int> span(values, 4);
    EXPECT_EQ(span.front(), 5);
    EXPECT_EQ(span.back(), 8);
    ct::Array<int, 4> arr = {{1, 2, 3, 4}};
    const ct::Array<int, 4> &carr = arr;
    EXPECT_EQ(carr[2], 3);
    EXPECT_EQ(carr.front(), 1);
    EXPECT_EQ(carr.back(), 4);
    EXPECT_EQ(carr.at(3), 4);
    ct::Deque<int> dq;
    dq.push_back(9);
    dq.push_front(8);
    EXPECT_EQ(dq.at(0), 8);
    EXPECT_EQ(dq.at(1), 9);

    // Rc: operator* de leitura
    ct::Rc<int> rc = ct::make_rc<int>(41);
    EXPECT_EQ(*rc, 41);
    *rc = 42;
    EXPECT_EQ(*rc, 42);

    // Json: construtores por (char*, tamanho) e por String, items() const e Member()
    ct::Json a("abcdef", 3);
    EXPECT_TRUE(a.str() == "abc");
    const ct::String text("um texto");
    ct::Json b(text);
    EXPECT_TRUE(b.str() == "um texto");
    ct::Json arr_json = ct::Json::parse("[1,2,3]");
    const ct::Json &carr_json = arr_json;
    EXPECT_EQ(carr_json.items().size(), 3u);
    EXPECT_EQ(carr_json.items()[2].as_int(), 3);
    ct::Json::Member member;
    EXPECT_TRUE(member.key.empty());
    EXPECT_TRUE(member.value.is_null());
    ct::Json::Object members;
    members.push_back(ct::Json::Member());
    EXPECT_EQ(members.size(), 1u);

    // Stream: skip, can_*, close, StreamCallbacks, File::exists
    ct::MemoryStream ms("0123456789", 10);
    EXPECT_TRUE(ms.can_read());
    EXPECT_FALSE(ms.can_write());
    EXPECT_TRUE(ms.skip(3));
    EXPECT_EQ(ms.tell(), 3);
    EXPECT_FALSE(ms.skip(100));
    EXPECT_TRUE(ms.skip(-2));
    EXPECT_EQ(ms.tell(), 1);
    char buf[8];
    EXPECT_EQ(ct::StreamCallbacks::read(&ms, buf, 4), 4);
    EXPECT_EQ(memcmp(buf, "1234", 4), 0);
    EXPECT_EQ(ct::StreamCallbacks::read(&ms, buf, 0), 0);
    EXPECT_EQ(ct::StreamCallbacks::read(&ms, buf, -3), 0);
    ct::StreamCallbacks::skip(&ms, 2);
    EXPECT_EQ(ms.tell(), 7);
    EXPECT_EQ(ct::StreamCallbacks::eof(&ms), 0);
    ct::StreamCallbacks::skip(&ms, 3);
    EXPECT_EQ(ct::StreamCallbacks::eof(&ms), 1);
    ms.close();
    EXPECT_FALSE(ms.is_open());
    EXPECT_FALSE(ms.can_read());
    EXPECT_FALSE(ms.can_write());
    EXPECT_EQ(ms.tell(), -1);

    ct::MemoryStream own;
    EXPECT_TRUE(own.can_write());
    own.close();
    EXPECT_FALSE(own.can_write());
    EXPECT_EQ(own.write("x", 1), 0u);

    ct::MemoryStream base("abcdefgh", 8);
    ct::SubStream sub(base, 2, 4);
    EXPECT_TRUE(sub.can_read());
    EXPECT_FALSE(sub.can_write());
    EXPECT_TRUE(sub.can_seek());
    sub.close();
    EXPECT_FALSE(sub.is_open());
    EXPECT_FALSE(sub.can_read());
    EXPECT_FALSE(sub.can_seek());
    EXPECT_EQ(sub.read(buf, 2), 0u);
    ASSERT_NE(sub.error(), nullptr);

    EXPECT_FALSE(ct::File::exists("/nonexistent/dir/for/the/test"));
    EXPECT_TRUE(ct::File::exists("/proc/self/exe") || ct::File::exists("/dev/null"));
    ct::FileStream fs;
    EXPECT_FALSE(fs.can_read());
    EXPECT_FALSE(fs.can_write());
    EXPECT_FALSE(fs.open("/nonexistent/dir/for/the/test", ct::FileStream::Read));
    ASSERT_NE(fs.error(), nullptr);
}
