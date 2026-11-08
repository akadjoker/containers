#pragma once

// TLS client.  TlsConfig/TlsLibrary/TlsSocket are backend neutral; everything
// specific to a TLS implementation lives in a backend class in ct::detail that
// provides: load, make_context/free_context, new_session/free_session,
// handshake/read/write/shutdown (returning Status plus a Failure), version, alpn,
// pending.  The only backend today is OpenSSL 3 (1.1.1 fallback) loaded with
// dlopen/dlsym (no vendored code, only ${CMAKE_DL_LIBS} at link time).  Schannel,
// Security.framework, JNI and mbedTLS backends are not implemented; where no
// backend exists TlsLibrary::available() is false.

#include "socket.hpp"
#include "thread.hpp"

#include <chrono>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <pthread.h>
#include <csignal>
#endif

namespace ct
{
    class TlsLibrary;

    struct TlsConfig
    {
        String ca_file;
        String ca_dir;
        bool insecure_skip_verify = false;
        String alpn = "http/1.1";
        TlsLibrary *library = nullptr;
    };

    enum TlsErrorCode
    {
        TlsNoLibrary = -100,
        TlsCertificate = -101,
        TlsHostname = -102,
        TlsHandshakeTimeout = -103,
        TlsProtocol = -104,
        TlsTimeout = -105,
        TlsConfigError = -106
    };

    namespace detail
    {
        inline char *tls_message_buffer() noexcept
        {
            static thread_local char buffer[320];
            return buffer;
        }
        inline void tls_set_error(NetError *out, int code, const char *prefix, const char *detail_text = nullptr) noexcept
        {
            if (!out) return;
            char *buffer = tls_message_buffer();
            if (detail_text) std::snprintf(buffer, 320, "%s%s", prefix, detail_text);
            else std::snprintf(buffer, 320, "%s", prefix);
            out->message = buffer;
            out->code = code;
        }
    }


    namespace detail
    {
        struct TlsFailure { int code; char text[256]; };

        enum class TlsStatus { Ok, WantRead, WantWrite, Closed, Failed };

        class TlsOpenSslBackend
        {
        public:
            TlsOpenSslBackend() { std::memset(&f_, 0, sizeof(f_)); error_[0] = '\0'; }

            void load(const char *ssl_name, const char *crypto_name)
            {
#if defined(_WIN32)
                (void)ssl_name; (void)crypto_name;
                std::snprintf(error_, sizeof(error_), "%s", "no TLS backend for this platform");
#else
                static const char *const ssl_names[] = {"libssl.so.3", "libssl.so", "libssl.so.1.1", "libssl.3.dylib", "libssl.dylib", nullptr};
                static const char *const crypto_names[] = {"libcrypto.so.3", "libcrypto.so", "libcrypto.so.1.1", "libcrypto.3.dylib", "libcrypto.dylib", nullptr};
                ssl_ = open_first(ssl_name, ssl_names);
                if (!ssl_) return;
                crypto_ = open_first(crypto_name, crypto_names);
                error_[0] = '\0';
                const char *missing = nullptr;
#define CT_TLS_SYM(name) if (!missing) { f_.name = reinterpret_cast<decltype(f_.name)>(symbol(#name)); if (!f_.name) missing = #name; }
                CT_TLS_SYM(TLS_client_method) CT_TLS_SYM(SSL_CTX_new) CT_TLS_SYM(SSL_CTX_free) CT_TLS_SYM(SSL_CTX_set_default_verify_paths)
                CT_TLS_SYM(SSL_CTX_load_verify_locations) CT_TLS_SYM(SSL_CTX_set_verify) CT_TLS_SYM(SSL_CTX_ctrl) CT_TLS_SYM(SSL_CTX_set_alpn_protos)
                CT_TLS_SYM(SSL_new) CT_TLS_SYM(SSL_free) CT_TLS_SYM(SSL_set_fd) CT_TLS_SYM(SSL_set_connect_state) CT_TLS_SYM(SSL_ctrl)
                CT_TLS_SYM(SSL_set1_host) CT_TLS_SYM(SSL_get0_param) CT_TLS_SYM(X509_VERIFY_PARAM_set1_ip_asc) CT_TLS_SYM(SSL_do_handshake)
                CT_TLS_SYM(SSL_read_ex) CT_TLS_SYM(SSL_write_ex) CT_TLS_SYM(SSL_shutdown) CT_TLS_SYM(SSL_get_error) CT_TLS_SYM(SSL_get_verify_result)
                CT_TLS_SYM(X509_verify_cert_error_string) CT_TLS_SYM(ERR_get_error) CT_TLS_SYM(ERR_error_string_n) CT_TLS_SYM(ERR_clear_error)
                CT_TLS_SYM(SSL_get0_alpn_selected) CT_TLS_SYM(SSL_get_version) CT_TLS_SYM(SSL_pending)
#undef CT_TLS_SYM
                if (missing) { std::snprintf(error_, sizeof(error_), "missing symbol %s", missing); return; }
                ok_ = true;
#endif
            }
            bool ok() const noexcept { return ok_; }
            const char *error() const noexcept { return error_; }

            void *symbol(const char *name) const noexcept
            {
#if defined(_WIN32)
                (void)name; return nullptr;
#else
                void *p = ssl_ ? dlsym(ssl_, name) : nullptr;
                return p || !crypto_ ? p : dlsym(crypto_, name);
#endif
            }

            // Trust store: ca_file/ca_dir when given, the system default otherwise.
            void *make_context(const TlsConfig &config, char *error, std::size_t error_size)
            {
                auto fail = [&](const char *text) -> void * { if (error && error_size) std::snprintf(error, error_size, "%s", text); return nullptr; };
                void *ctx = f_.SSL_CTX_new(f_.TLS_client_method());
                if (!ctx) return fail("SSL_CTX_new failed");
                f_.SSL_CTX_ctrl(ctx, CtrlSetMinProto, Tls12, nullptr);
                f_.SSL_CTX_ctrl(ctx, CtrlMode, ModePartialWrite | ModeMovingBuffer, nullptr);
                f_.SSL_CTX_ctrl(ctx, CtrlOptions, OptionIgnoreUnexpectedEof, nullptr);
                f_.SSL_CTX_set_verify(ctx, config.insecure_skip_verify ? VerifyNone : VerifyPeer, nullptr);
                bool trust_ok;
                if (!config.ca_file.empty() || !config.ca_dir.empty())
                    trust_ok = f_.SSL_CTX_load_verify_locations(ctx, config.ca_file.empty() ? nullptr : config.ca_file.c_str(),
                                                                config.ca_dir.empty() ? nullptr : config.ca_dir.c_str()) == 1;
                else
                    trust_ok = f_.SSL_CTX_set_default_verify_paths(ctx) == 1;
                if (!trust_ok) { f_.ERR_clear_error(); f_.SSL_CTX_free(ctx); return fail("cannot load CA certificates"); }
                if (!config.alpn.empty())
                {
                    unsigned char wire[256]; std::size_t used = 0, start = 0;
                    for (std::size_t i = 0; i <= config.alpn.size(); ++i)
                    {
                        if (i != config.alpn.size() && config.alpn[i] != ',') continue;
                        std::size_t n = i - start;
                        if (n == 0 || n > 255 || used + n + 1 > sizeof(wire)) { f_.SSL_CTX_free(ctx); return fail("invalid ALPN list"); }
                        wire[used++] = static_cast<unsigned char>(n);
                        std::memcpy(wire + used, config.alpn.data() + start, n); used += n; start = i + 1;
                    }
                    if (f_.SSL_CTX_set_alpn_protos(ctx, wire, static_cast<unsigned>(used)) != 0) { f_.SSL_CTX_free(ctx); return fail("SSL_CTX_set_alpn_protos failed"); }
                }
                return ctx;
            }
            void free_context(void *ctx) noexcept { f_.SSL_CTX_free(ctx); }

            // The session keeps its own reference to the context.
            void *new_session(void *ctx, int fd, StringView host)
            {
                void *ssl = f_.SSL_new(ctx);
                if (!ssl) return nullptr;
                f_.SSL_set_fd(ssl, fd);
                f_.SSL_set_connect_state(ssl);
                String name(host);
                Address literal;
                if (Address::parse(host, 0, literal)) f_.X509_VERIFY_PARAM_set1_ip_asc(f_.SSL_get0_param(ssl), name.c_str());
                else if (!name.empty())
                {
                    f_.SSL_ctrl(ssl, CtrlSetHostName, 0, const_cast<char *>(name.c_str()));
                    f_.SSL_set1_host(ssl, name.c_str());
                }
                return ssl;
            }
            void free_session(void *ssl) noexcept { f_.SSL_free(ssl); }

            TlsStatus handshake(void *ssl, TlsFailure &failure) noexcept
            {
                f_.ERR_clear_error();
                const int rc = f_.SSL_do_handshake(ssl);
                if (rc == 1) return TlsStatus::Ok;
                const int error = f_.SSL_get_error(ssl, rc);
                if (error == ErrorWantRead) return TlsStatus::WantRead;
                if (error == ErrorWantWrite) return TlsStatus::WantWrite;
                describe(ssl, error, true, failure);
                return TlsStatus::Failed;
            }
            TlsStatus read(void *ssl, void *data, std::size_t n, std::size_t &got, TlsFailure &failure) noexcept
            {
                f_.ERR_clear_error();
                if (f_.SSL_read_ex(ssl, data, n, &got) == 1) return TlsStatus::Ok;
                return io_status(ssl, failure);
            }
            TlsStatus write(void *ssl, const void *data, std::size_t n, std::size_t &written, TlsFailure &failure) noexcept
            {
                f_.ERR_clear_error();
                if (f_.SSL_write_ex(ssl, data, n, &written) == 1) return TlsStatus::Ok;
                return io_status(ssl, failure);
            }
            void shutdown(void *ssl) noexcept { f_.SSL_shutdown(ssl); }
            std::size_t pending(void *ssl) const noexcept { return static_cast<std::size_t>(f_.SSL_pending(ssl)); }
            StringView version(void *ssl) const noexcept { return StringView(f_.SSL_get_version(ssl)); }
            StringView alpn(void *ssl) const noexcept
            {
                const unsigned char *data = nullptr; unsigned size = 0;
                f_.SSL_get0_alpn_selected(ssl, &data, &size);
                return data ? StringView(reinterpret_cast<const char *>(data), size) : StringView();
            }

        private:
            enum
            {
                VerifyNone = 0, VerifyPeer = 1, Tls12 = 0x0303,
                CtrlOptions = 32, CtrlMode = 33, CtrlSetHostName = 55, CtrlSetMinProto = 123,
                ModePartialWrite = 1, ModeMovingBuffer = 2, OptionIgnoreUnexpectedEof = 0x80,
                ErrorWantRead = 2, ErrorWantWrite = 3, ErrorSyscall = 5, ErrorZeroReturn = 6,
                VerifyHostnameMismatch = 62, VerifyIpMismatch = 64
            };

            static void set(TlsFailure &failure, int code, const char *prefix, const char *text = nullptr) noexcept
            {
                failure.code = code;
                if (text) std::snprintf(failure.text, sizeof(failure.text), "%s%s", prefix, text);
                else std::snprintf(failure.text, sizeof(failure.text), "%s", prefix);
            }

            void describe(void *ssl, int error, bool handshake, TlsFailure &failure) noexcept
            {
                const long verify = handshake ? f_.SSL_get_verify_result(ssl) : 0;
                if (verify == VerifyHostnameMismatch || verify == VerifyIpMismatch) { f_.ERR_clear_error(); set(failure, TlsHostname, "hostname mismatch"); }
                else if (verify != 0) { f_.ERR_clear_error(); set(failure, TlsCertificate, "certificate verify failed: ", f_.X509_verify_cert_error_string(verify)); }
                else
                {
                    const unsigned long code = f_.ERR_get_error();
                    if (!code && error == ErrorSyscall) set(failure, TlsProtocol, "protocol error: connection closed during TLS exchange");
                    else
                    {
                        char text[200];
                        if (code) f_.ERR_error_string_n(code, text, sizeof(text)); else std::snprintf(text, sizeof(text), "%s", "unknown TLS error");
                        set(failure, TlsProtocol, "protocol error: ", text);
                    }
                    f_.ERR_clear_error();
                }
            }

            TlsStatus io_status(void *ssl, TlsFailure &failure) noexcept
            {
                const int error = f_.SSL_get_error(ssl, 0);
                if (error == ErrorWantRead) return TlsStatus::WantRead;
                if (error == ErrorWantWrite) return TlsStatus::WantWrite;
                if (error == ErrorZeroReturn) return TlsStatus::Closed;
                describe(ssl, error, false, failure);
                return TlsStatus::Failed;
            }

#if !defined(_WIN32)
            void *open_first(const char *forced, const char *const *names)
            {
                void *handle = nullptr;
                if (forced)
                {
                    handle = dlopen(forced, RTLD_NOW | RTLD_LOCAL);
                    if (!handle) remember_dlerror(forced);
                    return handle;
                }
                for (std::size_t i = 0; names[i] && !handle; ++i)
                {
                    handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
                    if (!handle && i == 0) remember_dlerror(names[i]);
                }
                return handle;
            }
            void remember_dlerror(const char *name)
            {
                const char *text = dlerror();
                std::snprintf(error_, sizeof(error_), "%s", text ? text : name);
            }
#endif

            struct Functions
            {
                const void *(*TLS_client_method)();
                void *(*SSL_CTX_new)(const void *);
                void (*SSL_CTX_free)(void *);
                int (*SSL_CTX_set_default_verify_paths)(void *);
                int (*SSL_CTX_load_verify_locations)(void *, const char *, const char *);
                void (*SSL_CTX_set_verify)(void *, int, void *);
                long (*SSL_CTX_ctrl)(void *, int, long, void *);
                int (*SSL_CTX_set_alpn_protos)(void *, const unsigned char *, unsigned);
                void *(*SSL_new)(void *);
                void (*SSL_free)(void *);
                int (*SSL_set_fd)(void *, int);
                void (*SSL_set_connect_state)(void *);
                long (*SSL_ctrl)(void *, int, long, void *);
                int (*SSL_set1_host)(void *, const char *);
                void *(*SSL_get0_param)(void *);
                int (*X509_VERIFY_PARAM_set1_ip_asc)(void *, const char *);
                int (*SSL_do_handshake)(void *);
                int (*SSL_read_ex)(void *, void *, std::size_t, std::size_t *);
                int (*SSL_write_ex)(void *, const void *, std::size_t, std::size_t *);
                int (*SSL_shutdown)(void *);
                int (*SSL_get_error)(const void *, int);
                long (*SSL_get_verify_result)(const void *);
                const char *(*X509_verify_cert_error_string)(long);
                unsigned long (*ERR_get_error)();
                void (*ERR_error_string_n)(unsigned long, char *, std::size_t);
                void (*ERR_clear_error)();
                void (*SSL_get0_alpn_selected)(const void *, const unsigned char **, unsigned *);
                const char *(*SSL_get_version)(const void *);
                int (*SSL_pending)(const void *);
            };
            Functions f_;
            void *ssl_ = nullptr, *crypto_ = nullptr;
            bool ok_ = false;
            char error_[256];
        };
    }

    class TlsLibrary
    {
    public:
        // ssl_name/crypto_name override the library file names (tests, odd installs).
        explicit TlsLibrary(const char *ssl_name = nullptr, const char *crypto_name = nullptr) { backend_.load(ssl_name, crypto_name); }
        TlsLibrary(const TlsLibrary &) = delete;
        TlsLibrary &operator=(const TlsLibrary &) = delete;

        static TlsLibrary &instance()
        {
            static TlsLibrary library;
            return library;
        }

        bool available() const noexcept { return backend_.ok(); }
        const char *error() const noexcept { return backend_.error(); }
        detail::TlsOpenSslBackend &backend() noexcept { return backend_; }

        // Replaces the configuration of the shared context; open connections keep the previous one.
        bool set_default_config(const TlsConfig &config)
        {
            if (!available()) return false;
            LockGuard guard(mutex_);
            void *fresh = backend_.make_context(config, nullptr, 0);
            if (!fresh) return false;
            if (default_) backend_.free_context(default_);
            default_ = fresh;
            return true;
        }

        // Shared context for a null/plain config, a private one otherwise; release_context frees only private ones.
        void *acquire_context(const TlsConfig *config, char *error, std::size_t error_size)
        {
            if (!available()) return nullptr;
            if (config && !is_plain(*config)) return backend_.make_context(*config, error, error_size);
            LockGuard guard(mutex_);
            if (!default_) default_ = backend_.make_context(TlsConfig(), error, error_size);
            return default_;
        }
        void release_context(void *ctx, const TlsConfig *config) noexcept
        {
            if (ctx && config && !is_plain(*config)) backend_.free_context(ctx);
        }

        static bool is_plain(const TlsConfig &c) noexcept
        {
            return c.ca_file.empty() && c.ca_dir.empty() && !c.insecure_skip_verify && c.alpn == "http/1.1" && !c.library;
        }

    private:
        detail::TlsOpenSslBackend backend_;
        void *default_ = nullptr;
        Mutex mutex_;
    };

    class TlsSocket
    {
    public:
        TlsSocket() noexcept : lib_(nullptr), session_(nullptr), nonblocking_(false), established_(false), io_timeout_ms_(0), error_{"", 0} {}
        ~TlsSocket() { close(); }
        TlsSocket(const TlsSocket &) = delete;
        TlsSocket &operator=(const TlsSocket &) = delete;
        TlsSocket(TlsSocket &&o) noexcept : tcp_(static_cast<TcpStream &&>(o.tcp_)), lib_(o.lib_), session_(o.session_), nonblocking_(o.nonblocking_),
            established_(o.established_), io_timeout_ms_(o.io_timeout_ms_), error_(o.error_) { o.session_ = nullptr; o.established_ = false; }
        TlsSocket &operator=(TlsSocket &&o) noexcept
        {
            if (this != &o)
            {
                close();
                tcp_ = static_cast<TcpStream &&>(o.tcp_); lib_ = o.lib_; session_ = o.session_; nonblocking_ = o.nonblocking_;
                established_ = o.established_; io_timeout_ms_ = o.io_timeout_ms_; error_ = o.error_;
                o.session_ = nullptr; o.established_ = false;
            }
            return *this;
        }

        bool valid() const noexcept { return session_ != nullptr && tcp_.valid(); }
        NetError last_error() const noexcept { return error_; }
        bool would_block() const noexcept { return detail::would_block(error_.code); }
        bool set_nonblocking(bool enabled) noexcept { nonblocking_ = enabled; return true; }
        Address peer() const noexcept { return tcp_.peer(); }
        Address local_address() const noexcept { return tcp_.local_address(); }
        const Socket &socket() const noexcept { return tcp_; }
        std::size_t pending() const noexcept { return session_ ? lib_->backend().pending(session_) : 0; }
        StringView peer_version() const noexcept { return session_ ? lib_->backend().version(session_) : StringView(); }
        StringView alpn() const noexcept { return session_ ? lib_->backend().alpn(session_) : StringView(); }

        bool connect(const Address &address, StringView host, unsigned timeout_ms = 0, NetError *err = nullptr, const TlsConfig *config = nullptr)
        {
            close();
            lib_ = config && config->library ? config->library : &TlsLibrary::instance();
            if (!lib_->available())
            {
                char reason[260];
                std::snprintf(reason, sizeof(reason), "%s)", lib_->error());
                detail::tls_set_error(err, TlsNoLibrary, "TLS library not available (", reason);
                if (err) error_ = *err;
                lib_ = nullptr;
                return false;
            }
            char text[200] = {};
            void *ctx = lib_->acquire_context(config, text, sizeof(text));
            if (!ctx) { detail::tls_set_error(err, TlsConfigError, "TLS configuration error: ", text); return false; }
            if (!tcp_.connect(address, timeout_ms, err)) { lib_->release_context(ctx, config); return false; }
            tcp_.set_nonblocking(true);
            tcp_.set_nodelay();
            io_timeout_ms_ = timeout_ms;
            session_ = lib_->backend().new_session(ctx, static_cast<int>(tcp_.native_handle()), host);
            lib_->release_context(ctx, config);
            if (!session_) { detail::tls_set_error(err, TlsProtocol, "protocol error: cannot create TLS session"); tcp_.close(); return false; }
            if (!handshake(timeout_ms, err)) { drop(); return false; }
            established_ = true;
            return true;
        }

        long send(const void *data, std::size_t n) noexcept
        {
            if (!session_ || n == 0) return 0;
            for (;;)
            {
                std::size_t written = 0; detail::TlsFailure failure = {};
                detail::TlsStatus status;
                { SigpipeGuard guard; status = lib_->backend().write(session_, data, n, written, failure); }
                if (status == detail::TlsStatus::Ok) return static_cast<long>(written);
                if (!retry(status, failure)) return error_.code == 0 ? 0 : -1;
            }
        }

        long recv(void *data, std::size_t n) noexcept
        {
            if (!session_ || n == 0) return 0;
            for (;;)
            {
                std::size_t got = 0; detail::TlsFailure failure = {};
                detail::TlsStatus status = lib_->backend().read(session_, data, n, got, failure);
                if (status == detail::TlsStatus::Ok) return static_cast<long>(got);
                if (!retry(status, failure)) return error_.code == 0 ? 0 : -1;
            }
        }

        bool send_all(StringView data) noexcept
        {
            std::size_t sent = 0;
            while (sent < data.size()) { long n = send(data.data() + sent, data.size() - sent); if (n <= 0) return false; sent += static_cast<std::size_t>(n); }
            return true;
        }
        bool recv_exact(void *data, std::size_t n) noexcept
        {
            std::size_t got = 0;
            while (got < n) { long r = recv(static_cast<char *>(data) + got, n - got); if (r <= 0) return false; got += static_cast<std::size_t>(r); }
            return true;
        }

        void close() noexcept
        {
            if (session_)
            {
                if (established_) { SigpipeGuard guard; lib_->backend().shutdown(session_); }
                drop();
            }
            tcp_.close();
        }

    private:
        struct SigpipeGuard
        {
#if defined(__linux__)
            sigset_t block, old; bool pending_before;
            SigpipeGuard() noexcept
            {
                sigemptyset(&block); sigaddset(&block, SIGPIPE);
                sigset_t pending; sigpending(&pending);
                pending_before = sigismember(&pending, SIGPIPE) == 1;
                pthread_sigmask(SIG_BLOCK, &block, &old);
            }
            ~SigpipeGuard()
            {
                if (!pending_before)
                {
                    sigset_t pending; sigpending(&pending);
                    if (sigismember(&pending, SIGPIPE) == 1) { timespec zero = {0, 0}; sigtimedwait(&block, nullptr, &zero); }
                }
                pthread_sigmask(SIG_SETMASK, &old, nullptr);
            }
#endif
        };

        void drop() noexcept
        {
            if (session_) { lib_->backend().free_session(session_); session_ = nullptr; }
            established_ = false;
            tcp_.close();
        }

        // 1 ready, 0 timeout, -1 error
        int wait_socket(bool for_write, unsigned timeout_ms) noexcept
        {
            Poller poller;
            poller.add(tcp_, for_write ? Poller::Writable : Poller::Readable);
            int r = poller.wait(timeout_ms);
            return r < 0 ? -1 : (r == 0 ? 0 : 1);
        }

        void fail(int code, const char *text) noexcept
        {
            NetError e = {"", 0};
            detail::tls_set_error(&e, code, text);
            error_ = e;
        }

        bool handshake(unsigned timeout_ms, NetError *err) noexcept
        {
            typedef std::chrono::steady_clock Clock;
            const Clock::time_point start = Clock::now();
            for (;;)
            {
                detail::TlsFailure failure = {};
                detail::TlsStatus status;
                { SigpipeGuard guard; status = lib_->backend().handshake(session_, failure); }
                if (status == detail::TlsStatus::Ok) return true;
                if (status != detail::TlsStatus::WantRead && status != detail::TlsStatus::WantWrite)
                {
                    fail(failure.code ? failure.code : TlsProtocol, failure.text);
                    if (err) *err = error_;
                    return false;
                }
                unsigned remaining = 0xFFFFFFFFu;
                if (timeout_ms)
                {
                    const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
                    remaining = elapsed >= static_cast<long long>(timeout_ms) ? 0 : static_cast<unsigned>(static_cast<long long>(timeout_ms) - elapsed);
                }
                const int ready = remaining == 0 ? 0 : wait_socket(status == detail::TlsStatus::WantWrite, remaining);
                if (ready == 0) { fail(TlsHandshakeTimeout, "handshake timeout"); if (err) *err = error_; return false; }
                if (ready < 0) { error_ = tcp_.last_error(); if (err) *err = error_; return false; }
            }
        }

        // True when the operation must be retried; false when it ended (error_ describes it, code 0 = closed).
        bool retry(detail::TlsStatus status, const detail::TlsFailure &failure) noexcept
        {
            if (status == detail::TlsStatus::WantRead || status == detail::TlsStatus::WantWrite)
            {
                if (nonblocking_)
                {
                    NetError e = {"", 0};
                    detail::set_error(&e, EAGAIN);
                    error_ = e;
                    return false;
                }
                const int ready = wait_socket(status == detail::TlsStatus::WantWrite, io_timeout_ms_ ? io_timeout_ms_ : 0xFFFFFFFFu);
                if (ready > 0) return true;
                if (ready == 0) fail(TlsTimeout, "TLS I/O timeout");
                else error_ = tcp_.last_error();
                return false;
            }
            if (status == detail::TlsStatus::Closed) { error_ = NetError{"", 0}; return false; }
            fail(failure.code ? failure.code : TlsProtocol, failure.text);
            return false;
        }

        TcpStream tcp_;
        TlsLibrary *lib_;
        void *session_;
        bool nonblocking_, established_;
        unsigned io_timeout_ms_;
        NetError error_;
    };
}
