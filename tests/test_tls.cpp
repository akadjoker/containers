#include <ct/http_client.hpp>
#include <ct/tls.hpp>
#include <gtest/gtest.h>


#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#endif

namespace
{
    using ct::Address;
    using ct::NetError;
    using ct::TcpListener;
    using ct::TcpStream;
    using ct::TlsConfig;
    using ct::TlsSocket;

#if !defined(_WIN32)
    struct ServerApi
    {
        const void *(*TLS_server_method)();
        void *(*SSL_CTX_new)(const void *);
        void (*SSL_CTX_free)(void *);
        int (*SSL_CTX_use_certificate_chain_file)(void *, const char *);
        int (*SSL_CTX_use_PrivateKey_file)(void *, const char *, int);
        void *(*SSL_new)(void *);
        void (*SSL_free)(void *);
        int (*SSL_set_fd)(void *, int);
        int (*SSL_accept)(void *);
        int (*SSL_read)(void *, void *, int);
        int (*SSL_write)(void *, const void *, int);
        int (*SSL_shutdown)(void *);

        bool load()
        {
            ct::TlsLibrary &lib = ct::TlsLibrary::instance(TlsConfig::System);
            if (!lib.available() || !lib.openssl()) return false;
#define SYM(n) n = reinterpret_cast<decltype(n)>(lib.openssl()->symbol(#n)); if (!n) return false;
            SYM(TLS_server_method) SYM(SSL_CTX_new) SYM(SSL_CTX_free) SYM(SSL_CTX_use_certificate_chain_file) SYM(SSL_CTX_use_PrivateKey_file)
            SYM(SSL_new) SYM(SSL_free) SYM(SSL_set_fd) SYM(SSL_accept) SYM(SSL_read) SYM(SSL_write) SYM(SSL_shutdown)
#undef SYM
            return true;
        }
    };

    struct Pki
    {
        std::string dir, ca, other_ca, cert, key;
        bool ok = false;
    };

    bool run(const std::string &command) { return std::system((command + " >/dev/null 2>&1").c_str()) == 0; }

    Pki &pki()
    {
        static Pki p;
        static bool done = false;
        if (done) return p;
        done = true;
        if (!run("openssl version")) return p;
        char templ[] = "/tmp/ct_tls_XXXXXX";
        if (!mkdtemp(templ)) return p;
        p.dir = templ;
        const std::string d = p.dir + "/";
        p.ca = d + "ca.pem"; p.other_ca = d + "other.pem"; p.cert = d + "srv.pem"; p.key = d + "srv.key";
        bool ok = run("openssl req -x509 -newkey rsa:2048 -nodes -keyout " + d + "ca.key -out " + p.ca + " -subj /CN=ctTestCA -days 2");
        ok = ok && run("openssl req -x509 -newkey rsa:2048 -nodes -keyout " + d + "other.key -out " + p.other_ca + " -subj /CN=ctOtherCA -days 2");
        ok = ok && run("openssl req -newkey rsa:2048 -nodes -keyout " + p.key + " -out " + d + "srv.csr -subj /CN=localhost");
        ok = ok && run("(printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\\n' > " + d + "ext.cnf)");
        ok = ok && run("openssl x509 -req -in " + d + "srv.csr -CA " + p.ca + " -CAkey " + d + "ca.key -CAcreateserial -out " + p.cert + " -days 2 -extfile " + d + "ext.cnf");
        p.ok = ok;
        return p;
    }

    struct PkiCleanup : ::testing::Environment
    {
        void TearDown() override { if (!pki().dir.empty()) run("rm -rf " + pki().dir); }
    };
    const ::testing::Environment *const cleanup = ::testing::AddGlobalTestEnvironment(new PkiCleanup);

    class TlsServer
    {
    public:
        typedef void (*Handler)(ServerApi &, void *ssl);
        bool start(Handler handler)
        {
            if (!api_.load() || !pki().ok) return false;
            ctx_ = api_.SSL_CTX_new(api_.TLS_server_method());
            if (!ctx_ || api_.SSL_CTX_use_certificate_chain_file(ctx_, pki().cert.c_str()) != 1 ||
                api_.SSL_CTX_use_PrivateKey_file(ctx_, pki().key.c_str(), 1) != 1) return false;
            Address any;
            Address::parse("127.0.0.1", 0, any);
            if (!listener_.bind(any) || !listener_.listen()) return false;
            port_ = listener_.local_address().port();
            handler_ = handler;
            thread_ = std::thread([this] { loop(); });
            return true;
        }
        ~TlsServer()
        {
            stop_ = true;
            if (port_)
            {
                TcpStream wake; Address a; Address::parse("127.0.0.1", port_, a);
                wake.connect(a, 1000);
            }
            if (thread_.joinable()) thread_.join();
            if (ctx_) api_.SSL_CTX_free(ctx_);
        }
        unsigned short port() const { return port_; }
        Address address() const { Address a; Address::parse("127.0.0.1", port_, a); return a; }

    private:
        void loop()
        {
            sigset_t block; sigemptyset(&block); sigaddset(&block, SIGPIPE);
            pthread_sigmask(SIG_BLOCK, &block, nullptr);
            while (!stop_)
            {
                TcpStream client;
                if (!listener_.accept(client)) break;
                if (stop_) break;
                void *ssl = api_.SSL_new(ctx_);
                api_.SSL_set_fd(ssl, static_cast<int>(client.native_handle()));
                if (api_.SSL_accept(ssl) == 1) handler_(api_, ssl);
                api_.SSL_free(ssl);
            }
        }
        ServerApi api_;
        void *ctx_ = nullptr;
        TcpListener listener_;
        std::thread thread_;
        Handler handler_ = nullptr;
        unsigned short port_ = 0;
        std::atomic<bool> stop_{false};
    };

    const std::size_t big = 10u * 1024u * 1024u;

    char pattern(std::size_t i) { return static_cast<char>((i * 131 + (i >> 8)) & 0xff); }

    void upload_handler(ServerApi &api, void *ssl)
    {
        std::size_t total = 0; bool good = true; char buf[16384];
        while (total < big)
        {
            int n = api.SSL_read(ssl, buf, sizeof(buf));
            if (n <= 0) break;
            for (int i = 0; i < n; ++i) if (buf[i] != pattern(total + static_cast<std::size_t>(i))) good = false;
            total += static_cast<std::size_t>(n);
        }
        const char *reply = good && total == big ? "OK" : "BAD";
        api.SSL_write(ssl, reply, static_cast<int>(std::strlen(reply)));
        api.SSL_shutdown(ssl);
    }

    void download_handler(ServerApi &api, void *ssl)
    {
        std::vector<char> data(big);
        for (std::size_t i = 0; i < big; ++i) data[i] = pattern(i);
        std::size_t sent = 0;
        while (sent < big)
        {
            int n = api.SSL_write(ssl, data.data() + sent, static_cast<int>(std::min<std::size_t>(big - sent, 32768)));
            if (n <= 0) return;
            sent += static_cast<std::size_t>(n);
        }
        api.SSL_shutdown(ssl);
    }

    void greeting_handler(ServerApi &api, void *ssl)
    {
        api.SSL_write(ssl, "hello", 5);
        char buf[16];
        api.SSL_read(ssl, buf, sizeof(buf));
    }

    void http_handler(ServerApi &api, void *ssl)
    {
        std::string request; char buf[2048];
        while (request.find("\r\n\r\n") == std::string::npos)
        {
            int n = api.SSL_read(ssl, buf, sizeof(buf));
            if (n <= 0) return;
            request.append(buf, static_cast<std::size_t>(n));
        }
        const std::string body = "secure hello";
        const std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        api.SSL_write(ssl, reply.data(), static_cast<int>(reply.size()));
        api.SSL_shutdown(ssl);
    }

    TlsConfig::Backend g_backend = TlsConfig::System;

    const char *backend_name(TlsConfig::Backend b) { return b == TlsConfig::Mbed ? "Mbed" : "System"; }

    TlsConfig plain()
    {
        TlsConfig c;
        c.backend = g_backend;
        return c;
    }

    TlsConfig trusting()
    {
        TlsConfig c = plain();
        c.ca_file = ct::String(pki().ca.c_str());
        return c;
    }

#define REQUIRE_TLS_SERVER(server, handler) \
    if (!ct::TlsLibrary::instance(g_backend).available()) GTEST_SKIP() << "no TLS library: " << ct::TlsLibrary::instance(g_backend).error(); \
    if (!ct::TlsLibrary::instance(TlsConfig::System).available()) GTEST_SKIP() << "test server needs the system OpenSSL"; \
    if (!pki().ok) GTEST_SKIP() << "openssl CLI unavailable"; \
    TlsServer server; \
    ASSERT_TRUE(server.start(handler))
#else
#define REQUIRE_TLS_SERVER(server, handler) GTEST_SKIP() << "no TLS backend on this platform"
#endif
}

#if !defined(_WIN32)

namespace
{
    class TlsParam : public ::testing::TestWithParam<TlsConfig::Backend>
    {
    protected:
        void SetUp() override { g_backend = GetParam(); }
    };
    class Backends : public TlsParam {};
    class HttpBackends : public TlsParam {};
    class NetworkBackends : public TlsParam {};

    std::string param_name(const ::testing::TestParamInfo<TlsConfig::Backend> &info) { return backend_name(info.param); }

#if defined(CT_TLS_MBEDTLS)
    const TlsConfig::Backend backends[] = {TlsConfig::System, TlsConfig::Mbed};
#else
    const TlsConfig::Backend backends[] = {TlsConfig::System};
#endif
}

TEST_P(Backends, LibraryLoads)
{
    ct::TlsLibrary &lib = ct::TlsLibrary::instance(g_backend);
    if (!lib.available()) GTEST_SKIP() << lib.error();
    EXPECT_STREQ(lib.error(), "");
}

TEST_P(Backends, HandshakeWithCaFile)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = trusting();
    ASSERT_TRUE(socket.connect(server.address(), "localhost", 3000, &error, &config)) << error.message;
    EXPECT_TRUE(socket.peer_version() == "TLSv1.3" || socket.peer_version() == "TLSv1.2");
    EXPECT_EQ(socket.alpn().size(), 0u);
    char buf[8] = {};
    ASSERT_TRUE(socket.recv_exact(buf, 5));
    EXPECT_EQ(std::string(buf, 5), "hello");
}

TEST_P(Backends, IpAddressMatchesSubjectAltName)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = trusting();
    EXPECT_TRUE(socket.connect(server.address(), "127.0.0.1", 3000, &error, &config)) << error.message;
}

TEST_P(Backends, UnknownCaFailsWithDefaultStore)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = plain();
    EXPECT_FALSE(socket.connect(server.address(), "localhost", 3000, &error, &config));
    EXPECT_NE(std::string(error.message).find("certificate verify failed"), std::string::npos) << error.message;
    EXPECT_EQ(error.code, ct::TlsCertificate);
    EXPECT_FALSE(socket.valid());
}

TEST_P(Backends, WrongCaFileFails)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = plain(); config.ca_file = ct::String(pki().other_ca.c_str());
    EXPECT_FALSE(socket.connect(server.address(), "localhost", 3000, &error, &config));
    EXPECT_NE(std::string(error.message).find("certificate verify failed"), std::string::npos) << error.message;
}

TEST_P(Backends, HostnameMismatchFails)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = trusting();
    EXPECT_FALSE(socket.connect(server.address(), "wrong.example", 3000, &error, &config));
    EXPECT_STREQ(error.message, "hostname mismatch");
    EXPECT_EQ(error.code, ct::TlsHostname);
}

TEST_P(Backends, InsecureSkipVerifyPasses)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = plain(); config.insecure_skip_verify = true;
    ASSERT_TRUE(socket.connect(server.address(), "wrong.example", 3000, &error, &config)) << error.message;
    char buf[8] = {};
    EXPECT_TRUE(socket.recv_exact(buf, 5));
}

TEST_P(Backends, UploadTenMegabytes)
{
    REQUIRE_TLS_SERVER(server, upload_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = trusting();
    ASSERT_TRUE(socket.connect(server.address(), "localhost", 5000, &error, &config)) << error.message;
    std::vector<char> data(big);
    for (std::size_t i = 0; i < big; ++i) data[i] = pattern(i);
    ASSERT_TRUE(socket.send_all(ct::StringView(data.data(), data.size())));
    char reply[4] = {};
    long n = socket.recv(reply, sizeof(reply));
    ASSERT_EQ(n, 2);
    EXPECT_EQ(std::string(reply, 2), "OK");
}

TEST_P(Backends, DownloadTenMegabytesAndCleanClose)
{
    REQUIRE_TLS_SERVER(server, download_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = trusting();
    ASSERT_TRUE(socket.connect(server.address(), "localhost", 5000, &error, &config)) << error.message;
    std::vector<char> data(big);
    ASSERT_TRUE(socket.recv_exact(data.data(), big));
    for (std::size_t i = 0; i < big; ++i) ASSERT_EQ(data[i], pattern(i)) << i;
    char extra[1];
    EXPECT_EQ(socket.recv(extra, 1), 0);
    socket.close();
    EXPECT_FALSE(socket.valid());
}

TEST_P(Backends, NonBlockingRecvReportsWouldBlock)
{
    REQUIRE_TLS_SERVER(server, greeting_handler);
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = trusting();
    ASSERT_TRUE(socket.connect(server.address(), "localhost", 3000, &error, &config)) << error.message;
    char buf[8];
    ASSERT_TRUE(socket.recv_exact(buf, 5));
    socket.set_nonblocking(true);
    EXPECT_EQ(socket.recv(buf, sizeof(buf)), -1);
    EXPECT_TRUE(socket.would_block());
}

TEST_P(Backends, HandshakeTimeoutAgainstSilentListener)
{
    if (!ct::TlsLibrary::instance(g_backend).available()) GTEST_SKIP() << ct::TlsLibrary::instance(g_backend).error();
    TcpListener listener; Address any; Address::parse("127.0.0.1", 0, any);
    if (!listener.bind(any) || !listener.listen()) GTEST_SKIP() << "TCP port unavailable";
    Address target; Address::parse("127.0.0.1", listener.local_address().port(), target);
    std::atomic<bool> release{false};
    std::thread holder([&] { TcpStream c; if (listener.accept(c)) while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    TlsSocket socket; NetError error = {"", 0}; TlsConfig config = plain(); config.insecure_skip_verify = true;
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(socket.connect(target, "localhost", 300, &error, &config));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_STREQ(error.message, "handshake timeout");
    EXPECT_EQ(error.code, ct::TlsHandshakeTimeout);
    EXPECT_GE(ms, 250);
    EXPECT_LT(ms, 3000);
    release = true;
    holder.join();
}

TEST(TlsOpenSsl, MissingLibraryGivesClearError)
{
    ct::TlsLibrary missing("libssl_ct_missing.so.99", "libcrypto_ct_missing.so.99", TlsConfig::System);
    EXPECT_FALSE(missing.available());
    EXPECT_STRNE(missing.error(), "");
    TlsConfig config; config.library = &missing;
    Address target; Address::parse("127.0.0.1", 9, target);
    TlsSocket socket; NetError error = {"", 0};
    EXPECT_FALSE(socket.connect(target, "localhost", 500, &error, &config));
    EXPECT_NE(std::string(error.message).find("TLS library not available ("), std::string::npos) << error.message;
    EXPECT_NE(std::string(error.message).find("libssl_ct_missing"), std::string::npos) << error.message;
    ct::HttpResponse response; NetError http_error = {"", 0};
    EXPECT_FALSE(ct::HttpClient::get("https://localhost:9/", config, response, &http_error, 500));
    EXPECT_NE(std::string(http_error.message).find("TLS library not available"), std::string::npos) << http_error.message;
}

TEST_P(HttpBackends, GetOverHttpsLocalServer)
{
    REQUIRE_TLS_SERVER(server, http_handler);
    TlsConfig config = trusting();
    ct::HttpResponse response; NetError error = {"", 0};
    std::string url = "https://localhost:" + std::to_string(server.port()) + "/hello";
    ASSERT_TRUE(ct::HttpClient::get(ct::StringView(url.c_str()), config, response, &error, 3000)) << error.message;
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "secure hello");
}

TEST_P(HttpBackends, RequestWithExplicitHost)
{
    REQUIRE_TLS_SERVER(server, http_handler);
    TlsConfig config = trusting();
    ct::HttpRequest request; request.method = "GET"; request.path = "/";
    ct::HttpResponse response; NetError error = {"", 0};
    ASSERT_TRUE(ct::HttpClient::request_tls(request, server.address(), "localhost", response, &error, 3000, &config)) << error.message;
    EXPECT_EQ(response.status, 200);
    EXPECT_FALSE(ct::HttpClient::request_tls(request, server.address(), "wrong.example", response, &error, 3000, &config));
    EXPECT_STREQ(error.message, "hostname mismatch");
}

TEST_P(HttpBackends, UntrustedServerFailsWithoutConfig)
{
    REQUIRE_TLS_SERVER(server, http_handler);
    ct::HttpResponse response; NetError error = {"", 0}; TlsConfig config = plain();
    std::string url = "https://localhost:" + std::to_string(server.port()) + "/";
    EXPECT_FALSE(ct::HttpClient::get(ct::StringView(url.c_str()), config, response, &error, 3000));
    EXPECT_NE(std::string(error.message).find("certificate verify failed"), std::string::npos) << error.message;
}

namespace
{
    void check_real(const char *url, int expected_status)
    {
        if (!ct::TlsLibrary::instance(g_backend).available()) GTEST_SKIP() << ct::TlsLibrary::instance(g_backend).error();
        TlsConfig config = plain();
        ct::HttpResponse response; NetError error = {"", 0};
        if (!ct::HttpClient::get(url, config, response, &error, 8000))
        {
            if (error.code == ct::TlsCertificate || error.code == ct::TlsHostname) FAIL() << error.message;
            GTEST_SKIP() << "network unavailable: " << error.message;
        }
        EXPECT_EQ(response.status, expected_status);
        EXPECT_FALSE(response.body.empty());
    }

    void check_rejected(const char *url, int code, const char *needle)
    {
        if (!ct::TlsLibrary::instance(g_backend).available()) GTEST_SKIP() << ct::TlsLibrary::instance(g_backend).error();
        TlsConfig config = plain();
        ct::HttpResponse response; NetError error = {"", 0};
        if (ct::HttpClient::get(url, config, response, &error, 8000)) FAIL() << "connection to " << url << " was accepted";
        if (error.code != ct::TlsCertificate && error.code != ct::TlsHostname) GTEST_SKIP() << "network unavailable: " << error.message;
        EXPECT_EQ(error.code, code) << error.message;
        EXPECT_NE(std::string(error.message).find(needle), std::string::npos) << error.message;
    }
}

TEST_P(NetworkBackends, ExampleCom) { check_real("https://example.com/", 200); }
TEST_P(NetworkBackends, UnicodeRobots) { check_real("https://www.unicode.org/robots.txt", 200); }
TEST_P(NetworkBackends, ExpiredCertificateRejected) { check_rejected("https://expired.badssl.com/", ct::TlsCertificate, "expired"); }
TEST_P(NetworkBackends, WrongHostRejected) { check_rejected("https://wrong.host.badssl.com/", ct::TlsHostname, "hostname mismatch"); }
TEST_P(NetworkBackends, SelfSignedRejected) { check_rejected("https://self-signed.badssl.com/", ct::TlsCertificate, "certificate verify failed"); }

INSTANTIATE_TEST_SUITE_P(Tls, Backends, ::testing::ValuesIn(backends), param_name);
INSTANTIATE_TEST_SUITE_P(TlsHttp, HttpBackends, ::testing::ValuesIn(backends), param_name);
INSTANTIATE_TEST_SUITE_P(TlsNetwork, NetworkBackends, ::testing::ValuesIn(backends), param_name);

TEST(TlsBackendChoice, DefaultFollowsBuild)
{
#if defined(CT_TLS_DEFAULT_MBEDTLS) && defined(CT_TLS_MBEDTLS)
    EXPECT_EQ(ct::TlsLibrary::instance().choice(), TlsConfig::Mbed);
#else
    EXPECT_EQ(ct::TlsLibrary::instance().choice(), TlsConfig::System);
#endif
}

TEST(TlsCaStore, FindsBundleOnThisMachine)
{
#if defined(__linux__) && !defined(__ANDROID__)
    EXPECT_FALSE(ct::detail::SystemCaStore::find_bundle().empty());
#else
    GTEST_SKIP() << "no bundle candidates on this platform";
#endif
}

TEST(TlsCaStore, ExplicitFileAndMissingFile)
{
    if (!pki().ok) GTEST_SKIP() << "openssl CLI unavailable";
    int accepted = 0; char error[100] = "";
    auto sink = [&](const char *data, std::size_t size) { ++accepted; return size > 1 && std::strstr(data, "BEGIN CERTIFICATE") != nullptr; };
    TlsConfig config; config.ca_file = ct::String(pki().ca.c_str());
    EXPECT_TRUE(ct::detail::SystemCaStore::load(config, sink, error, sizeof(error)));
    EXPECT_EQ(accepted, 1);
    config.ca_file = ct::String("/nonexistent/ct_ca.pem");
    EXPECT_FALSE(ct::detail::SystemCaStore::load(config, sink, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("/nonexistent/ct_ca.pem"), std::string::npos) << error;
    TlsConfig dir; dir.ca_dir = ct::String(pki().dir.c_str());
    accepted = 0;
    EXPECT_TRUE(ct::detail::SystemCaStore::load(dir, sink, error, sizeof(error)));
    EXPECT_GE(accepted, 2);
}

#if defined(CT_TLS_MBEDTLS)
TEST(TlsMbed, SystemCaStoreLoadsManyCertificates)
{
    mbedtls_x509_crt chain; mbedtls_x509_crt_init(&chain);
    char error[100] = "";
    auto sink = [&](const char *data, std::size_t size)
    {
        return mbedtls_x509_crt_parse(&chain, reinterpret_cast<const unsigned char *>(data), size) >= 0;
    };
    if (!ct::detail::SystemCaStore::load(TlsConfig(), sink, error, sizeof(error))) { mbedtls_x509_crt_free(&chain); GTEST_SKIP() << error; }
    int count = 0;
    for (const mbedtls_x509_crt *c = &chain; c && c->version != 0; c = c->next) ++count;
    mbedtls_x509_crt_free(&chain);
    EXPECT_GE(count, 50);
}

TEST(TlsMbed, SeparateFromSystemBackend)
{
    ct::TlsLibrary &mbed = ct::TlsLibrary::instance(TlsConfig::Mbed);
    EXPECT_TRUE(mbed.available());
    EXPECT_EQ(mbed.openssl(), nullptr);
    EXPECT_EQ(mbed.choice(), TlsConfig::Mbed);
    EXPECT_TRUE(ct::TlsLibrary::mbedtls_compiled_in());
}
#else
TEST(TlsMbed, NotCompiledInGivesClearError)
{
    ct::TlsLibrary &mbed = ct::TlsLibrary::instance(TlsConfig::Mbed);
    EXPECT_FALSE(mbed.available());
    EXPECT_NE(std::string(mbed.error()).find("CT_TLS_MBEDTLS"), std::string::npos) << mbed.error();
    TlsConfig config; config.backend = TlsConfig::Mbed;
    Address target; Address::parse("127.0.0.1", 9, target);
    TlsSocket socket; NetError error = {"", 0};
    EXPECT_FALSE(socket.connect(target, "localhost", 500, &error, &config));
    EXPECT_EQ(error.code, ct::TlsNoLibrary);
}
#endif

#endif
