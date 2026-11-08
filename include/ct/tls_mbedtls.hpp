#pragma once

// mbedTLS 3.6 backend for ct::TlsSocket (detail::TlsBackend, see tls_backend.hpp).  Included by
// tls.hpp only when CT_TLS_MBEDTLS is defined.  The CMake option CT_TLS_MBEDTLS downloads
// mbedTLS and builds it with MBEDTLS_THREADING_C/PTHREAD, which the shared ctr_drbg and the PSA
// layer used by TLS 1.3 need when contexts are used from several threads.
// A context is one shared mbedtls_ssl_config (client, TLS 1.2 minimum, ALPN, CA chain, entropy and
// ctr_drbg); every session is an mbedtls_ssl_context that holds a reference on it.

#include "tls_backend.hpp"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <atomic>
#include <new>

namespace ct
{
    namespace detail
    {
        class TlsMbedBackend : public TlsBackend
        {
        public:
            bool ok() const noexcept override { return true; }
            const char *error() const noexcept override { return ""; }

            // Trust store: ca_file/ca_dir when given, the system store otherwise (SystemCaStore).
            void *make_context(const TlsConfig &config, char *error, std::size_t error_size) override
            {
                auto fail = [&](Context *ctx, const char *text, int rc = 0) -> void *
                {
                    if (error && error_size)
                    {
                        if (rc) { char detail_text[120]; mbedtls_strerror(rc, detail_text, sizeof(detail_text)); std::snprintf(error, error_size, "%s: %s", text, detail_text); }
                        else std::snprintf(error, error_size, "%s", text);
                    }
                    if (ctx) destroy(ctx);
                    return nullptr;
                };
                static const psa_status_t psa = psa_crypto_init();
                if (psa != PSA_SUCCESS) return fail(nullptr, "psa_crypto_init failed");
                Context *ctx = new (std::nothrow) Context();
                if (!ctx) return fail(nullptr, "out of memory");
                int rc = mbedtls_ctr_drbg_seed(&ctx->drbg, mbedtls_entropy_func, &ctx->entropy, nullptr, 0);
                if (rc) return fail(ctx, "cannot seed the random generator", rc);
                rc = mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
                if (rc) return fail(ctx, "mbedtls_ssl_config_defaults failed", rc);
                mbedtls_ssl_conf_min_tls_version(&ctx->conf, MBEDTLS_SSL_VERSION_TLS1_2);
                mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->drbg);
                if (config.insecure_skip_verify) mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
                else
                {
                    mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
                    char reason[200] = "";
                    auto sink = [&](const char *data, std::size_t size) -> bool
                    {
                        const int before = count(ctx->ca);
                        mbedtls_x509_crt_parse(&ctx->ca, reinterpret_cast<const unsigned char *>(data), size);
                        return count(ctx->ca) > before;
                    };
                    if (!SystemCaStore::load(config, sink, reason, sizeof(reason))) return fail(ctx, "cannot load CA certificates");
                    mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->ca, nullptr);
                }
                if (!config.alpn.empty())
                {
                    if (config.alpn.size() >= sizeof(ctx->alpn_text)) return fail(ctx, "invalid ALPN list");
                    std::memcpy(ctx->alpn_text, config.alpn.c_str(), config.alpn.size() + 1);
                    std::size_t n = 0; char *start = ctx->alpn_text;
                    for (char *p = ctx->alpn_text;; ++p)
                    {
                        if (*p != ',' && *p != '\0') continue;
                        const bool last = *p == '\0';
                        *p = '\0';
                        if (p == start || p - start > 255 || n + 1 >= sizeof(ctx->alpn_list) / sizeof(ctx->alpn_list[0])) return fail(ctx, "invalid ALPN list");
                        ctx->alpn_list[n++] = start;
                        start = p + 1;
                        if (last) break;
                    }
                    ctx->alpn_list[n] = nullptr;
                    rc = mbedtls_ssl_conf_alpn_protocols(&ctx->conf, ctx->alpn_list);
                    if (rc) return fail(ctx, "mbedtls_ssl_conf_alpn_protocols failed", rc);
                }
                return ctx;
            }
            void free_context(void *ctx) noexcept override { release(static_cast<Context *>(ctx)); }

            void *new_session(void *ctx_ptr, int fd, StringView host) override
            {
                Context *ctx = static_cast<Context *>(ctx_ptr);
                Session *s = new (std::nothrow) Session();
                if (!s) return nullptr;
                s->fd = fd;
                int rc = mbedtls_ssl_setup(&s->ssl, &ctx->conf);
                if (rc == 0)
                {
                    String name(host);
                    rc = mbedtls_ssl_set_hostname(&s->ssl, name.empty() ? nullptr : name.c_str());
                }
                if (rc) { delete s; return nullptr; }
                mbedtls_ssl_set_bio(&s->ssl, &s->fd, bio_send, bio_recv, nullptr);
                ctx->refs.fetch_add(1);
                s->ctx = ctx;
                return s;
            }
            void free_session(void *session) noexcept override
            {
                Session *s = static_cast<Session *>(session);
                Context *ctx = s->ctx;
                delete s;
                release(ctx);
            }

            TlsStatus handshake(void *session, TlsFailure &failure) noexcept override
            {
                Session *s = static_cast<Session *>(session);
                int rc;
                do rc = mbedtls_ssl_handshake(&s->ssl); while (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
                if (rc == 0) return TlsStatus::Ok;
                if (rc == MBEDTLS_ERR_SSL_WANT_READ) return TlsStatus::WantRead;
                if (rc == MBEDTLS_ERR_SSL_WANT_WRITE) return TlsStatus::WantWrite;
                describe(s, rc, failure);
                return TlsStatus::Failed;
            }
            TlsStatus read(void *session, void *data, std::size_t n, std::size_t &got, TlsFailure &failure) noexcept override
            {
                Session *s = static_cast<Session *>(session);
                int rc;
                do rc = mbedtls_ssl_read(&s->ssl, static_cast<unsigned char *>(data), n); while (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
                if (rc > 0) { got = static_cast<std::size_t>(rc); return TlsStatus::Ok; }
                if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_SSL_CONN_EOF) return TlsStatus::Closed;
                return io_status(s, rc, failure);
            }
            TlsStatus write(void *session, const void *data, std::size_t n, std::size_t &written, TlsFailure &failure) noexcept override
            {
                Session *s = static_cast<Session *>(session);
                int rc;
                do rc = mbedtls_ssl_write(&s->ssl, static_cast<const unsigned char *>(data), n); while (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
                if (rc > 0) { written = static_cast<std::size_t>(rc); return TlsStatus::Ok; }
                return io_status(s, rc, failure);
            }
            void shutdown(void *session) noexcept override { mbedtls_ssl_close_notify(&static_cast<Session *>(session)->ssl); }
            std::size_t pending(void *session) const noexcept override { return mbedtls_ssl_get_bytes_avail(&static_cast<Session *>(session)->ssl); }
            StringView version(void *session) const noexcept override
            {
                const char *v = mbedtls_ssl_get_version(&static_cast<Session *>(session)->ssl);
                return v ? StringView(v) : StringView();
            }
            StringView alpn(void *session) const noexcept override
            {
                const char *a = mbedtls_ssl_get_alpn_protocol(&static_cast<Session *>(session)->ssl);
                return a ? StringView(a) : StringView();
            }

        private:
            struct Context
            {
                Context() : refs(1)
                {
                    mbedtls_ssl_config_init(&conf); mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg); mbedtls_x509_crt_init(&ca);
                    alpn_text[0] = '\0'; alpn_list[0] = nullptr;
                }
                ~Context() { mbedtls_ssl_config_free(&conf); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy); mbedtls_x509_crt_free(&ca); }
                mbedtls_ssl_config conf;
                mbedtls_entropy_context entropy;
                mbedtls_ctr_drbg_context drbg;
                mbedtls_x509_crt ca;
                char alpn_text[256];
                const char *alpn_list[17];
                std::atomic<int> refs;
            };

            struct Session
            {
                Session() : fd(-1), ctx(nullptr) { mbedtls_ssl_init(&ssl); }
                ~Session() { mbedtls_ssl_free(&ssl); }
                mbedtls_ssl_context ssl;
                int fd;
                Context *ctx;
            };

            static void destroy(Context *ctx) noexcept { delete ctx; }
            static void release(Context *ctx) noexcept { if (ctx && ctx->refs.fetch_sub(1) == 1) destroy(ctx); }

            static int count(const mbedtls_x509_crt &chain) noexcept
            {
                int n = 0;
                if (chain.version != 0) for (const mbedtls_x509_crt *c = &chain; c; c = c->next) ++n;
                return n;
            }

            static int bio_send(void *ctx, const unsigned char *buf, std::size_t n)
            {
                const SocketHandle fd = static_cast<SocketHandle>(*static_cast<int *>(ctx));
#if defined(MSG_NOSIGNAL)
                const int flags = MSG_NOSIGNAL;
#else
                const int flags = 0;
#endif
                const long long r = ::send(fd, reinterpret_cast<const char *>(buf), io_chunk(n), flags);
                if (r >= 0) return static_cast<int>(r);
                const int e = socket_error();
                if (would_block(e) || interrupted(e)) return MBEDTLS_ERR_SSL_WANT_WRITE;
                return MBEDTLS_ERR_NET_SEND_FAILED;
            }
            static int bio_recv(void *ctx, unsigned char *buf, std::size_t n)
            {
                const SocketHandle fd = static_cast<SocketHandle>(*static_cast<int *>(ctx));
                const long long r = ::recv(fd, reinterpret_cast<char *>(buf), io_chunk(n), 0);
                if (r >= 0) return static_cast<int>(r);
                const int e = socket_error();
                if (would_block(e) || interrupted(e)) return MBEDTLS_ERR_SSL_WANT_READ;
                return MBEDTLS_ERR_NET_RECV_FAILED;
            }

            static void set(TlsFailure &failure, int code, const char *prefix, const char *text = nullptr) noexcept
            {
                failure.code = code;
                if (text) std::snprintf(failure.text, sizeof(failure.text), "%s%s", prefix, text);
                else std::snprintf(failure.text, sizeof(failure.text), "%s", prefix);
            }

            // Verification errors other than the host name win, as in the OpenSSL backend.
            static void describe(Session *s, int rc, TlsFailure &failure) noexcept
            {
                if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                {
                    const std::uint32_t flags = mbedtls_ssl_get_verify_result(&s->ssl);
                    const std::uint32_t others = flags & ~static_cast<std::uint32_t>(MBEDTLS_X509_BADCERT_CN_MISMATCH);
                    if ((flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) && others == 0) { set(failure, TlsHostname, "hostname mismatch"); return; }
                    char info[200];
                    const int len = mbedtls_x509_crt_verify_info(info, sizeof(info), "", others ? others : flags);
                    std::size_t n = len > 0 ? static_cast<std::size_t>(len) : 0;
                    while (n && (info[n - 1] == '\n' || info[n - 1] == ' ')) --n;
                    info[n] = '\0';
                    for (std::size_t i = 0; i < n; ++i) if (info[i] == '\n') info[i] = ';';
                    set(failure, TlsCertificate, "certificate verify failed: ", n ? info : "unknown verification error");
                    return;
                }
                if (rc == MBEDTLS_ERR_SSL_CONN_EOF || rc == MBEDTLS_ERR_NET_RECV_FAILED || rc == MBEDTLS_ERR_NET_CONN_RESET)
                {
                    set(failure, TlsProtocol, "protocol error: connection closed during TLS exchange");
                    return;
                }
                char text[200];
                mbedtls_strerror(rc, text, sizeof(text));
                set(failure, TlsProtocol, "protocol error: ", text);
            }

            static TlsStatus io_status(Session *s, int rc, TlsFailure &failure) noexcept
            {
                if (rc == MBEDTLS_ERR_SSL_WANT_READ) return TlsStatus::WantRead;
                if (rc == MBEDTLS_ERR_SSL_WANT_WRITE) return TlsStatus::WantWrite;
                describe(s, rc, failure);
                return TlsStatus::Failed;
            }
        };
    }
}
