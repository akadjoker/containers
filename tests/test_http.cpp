#include <ct/http.hpp>
#include <ct/http_client.hpp>
#include <ct/http_server.hpp>
#include <gtest/gtest.h>
#include <thread>

TEST(HttpParser, IncrementalRequest)
{
    const char first[] = "POST /users?id=7 HTTP/1.1\r\nHost: example\r\nContent-Length: 5\r\n\r\nhe";
    ct::HttpParser parser; ct::HttpRequest request;
    EXPECT_EQ(parser.feed(first, sizeof(first) - 1, request), ct::HttpParser::NeedMore);
    EXPECT_EQ(parser.feed("llo", 3, request), ct::HttpParser::Done);
    EXPECT_EQ(request.method, "POST"); EXPECT_EQ(request.path, "/users"); EXPECT_EQ(request.query, "id=7");
    EXPECT_EQ(request.header("host"), "example"); EXPECT_EQ(request.body, "hello");
}

TEST(HttpParser, ChunkedResponse)
{
    const char message[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    ct::HttpParser parser; ct::HttpResponse response;
    EXPECT_EQ(parser.feed(message, sizeof(message) - 1, response), ct::HttpParser::Done);
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.reason, "OK"); EXPECT_EQ(response.body, "Wikipedia");
}

TEST(HttpParser, ResponseBodyDelimitedByClose)
{
    const char message[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nhello";
    ct::HttpParser parser; ct::HttpResponse response;
    EXPECT_EQ(parser.feed(message, sizeof(message) - 1, response), ct::HttpParser::NeedMore);
    EXPECT_EQ(parser.finish(response), ct::HttpParser::Done);
    EXPECT_EQ(response.body, "hello");
}

TEST(HttpParser, RejectsMalformedAndOversized)
{
    const char malformed[] = "GET / HTTP/1.1\r\nBroken\r\n\r\n";
    ct::HttpRequest request; ct::HttpParser parser;
    EXPECT_EQ(parser.feed(malformed, sizeof(malformed) - 1, request), ct::HttpParser::Error);
    const char oversized[] = "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\ntest";
    ct::HttpParser small(3);
    EXPECT_EQ(small.feed(oversized, sizeof(oversized) - 1, request), ct::HttpParser::Error);
}

TEST(HttpParser, PreservesPipelinedBytesOnReset)
{
    const char messages[] = "GET /one HTTP/1.1\r\n\r\nGET /two HTTP/1.1\r\n\r\n";
    ct::HttpParser parser; ct::HttpRequest request;
    ASSERT_EQ(parser.feed(messages, sizeof(messages) - 1, request), ct::HttpParser::Done); EXPECT_EQ(request.path, "/one");
    parser.reset(); ASSERT_EQ(parser.feed(nullptr, 0, request), ct::HttpParser::Done); EXPECT_EQ(request.path, "/two");
}

TEST(HttpClient, GetsFromLoopbackServer)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::TcpListener listener; if (!listener.bind(address)) GTEST_SKIP() << "HTTP test port unavailable"; ASSERT_TRUE(listener.listen());
    address = listener.local_address();
    std::thread server([&] {
        ct::TcpStream peer; ASSERT_TRUE(listener.accept(peer));
        char request[1024]; ASSERT_GT(peer.recv(request, sizeof(request)), 0);
        ASSERT_TRUE(peer.send_all("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello"));
    });
    ct::HttpResponse response; ct::NetError error = {"", 0};
    ct::String url("http://127.0.0.1:"); url.append_number(address.port()).append("/test?q=1");
    EXPECT_TRUE(ct::HttpClient::get(url, response, &error));
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.body, "hello"); EXPECT_EQ(response.header("content-type"), "text/plain");
    server.join();
}

TEST(HttpServer, RoutesParamsAndKeepAlive)
{
    ct::Address address; ASSERT_TRUE(ct::Address::parse("127.0.0.1", 0, address));
    ct::HttpServer server;
    server.route("GET", "/users/:id", [](const ct::HttpRequest &request, ct::HttpResponse &response) {
        response.text(request.param("id"));
    });
    ct::NetError error = {"", 0};
    if (!server.listen(address, &error)) GTEST_SKIP() << "HTTP server test port unavailable";
    address = server.local_address();
    std::thread loop([&] { server.run(); });
    ct::HttpResponse response;
    ct::String base("http://127.0.0.1:"); base.append_number(address.port());
    EXPECT_TRUE(ct::HttpClient::get(base + "/users/42", response, &error));
    EXPECT_EQ(response.status, 200); EXPECT_EQ(response.body, "42");
    EXPECT_TRUE(ct::HttpClient::get(base + "/missing", response, &error));
    EXPECT_EQ(response.status, 404);
    server.stop(); loop.join();
}
