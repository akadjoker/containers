#pragma once

// Backend neutral part of the TLS client: TlsConfig, error codes, the interface
// every backend implements (detail::TlsBackend) and the CA store loader
// (detail::SystemCaStore).  tls.hpp builds TlsLibrary/TlsSocket on top.

#include "socket.hpp"
#include "vector.hpp"

#include <cstdio>
#include <cstring>

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace ct
{
    class TlsLibrary;

    struct TlsConfig
    {
        enum Backend { Default, System, Mbed };

        String ca_file;
        String ca_dir;
        bool insecure_skip_verify = false;
        String alpn = "http/1.1";
        TlsLibrary *library = nullptr;
        Backend backend = Default;
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

        struct TlsFailure { int code; char text[256]; };

        enum class TlsStatus { Ok, WantRead, WantWrite, Closed, Failed };

        // Contexts and sessions are opaque; a session keeps its own reference to the context.
        class TlsBackend
        {
        public:
            virtual ~TlsBackend() {}
            virtual bool ok() const noexcept = 0;
            virtual const char *error() const noexcept = 0;
            virtual void *make_context(const TlsConfig &config, char *error, std::size_t error_size) = 0;
            virtual void free_context(void *ctx) noexcept = 0;
            virtual void *new_session(void *ctx, int fd, StringView host) = 0;
            virtual void free_session(void *session) noexcept = 0;
            virtual TlsStatus handshake(void *session, TlsFailure &failure) noexcept = 0;
            virtual TlsStatus read(void *session, void *data, std::size_t n, std::size_t &got, TlsFailure &failure) noexcept = 0;
            virtual TlsStatus write(void *session, const void *data, std::size_t n, std::size_t &written, TlsFailure &failure) noexcept = 0;
            virtual void shutdown(void *session) noexcept = 0;
            virtual std::size_t pending(void *session) const noexcept = 0;
            virtual StringView version(void *session) const noexcept = 0;
            virtual StringView alpn(void *session) const noexcept = 0;
        };

        // PEM certificates from ca_file / ca_dir, or from the platform store when neither is set.
        // Linux: the first existing bundle of bundle_candidates(); Android: every file of
        // /system/etc/security/cacerts/.  Windows and macOS are not implemented (load fails).
        struct SystemCaStore
        {
            static const char *const *bundle_candidates() noexcept
            {
                static const char *const names[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                                                    "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem", nullptr};
                return names;
            }
            static const char *android_directory() noexcept { return "/system/etc/security/cacerts/"; }

            // Path of the platform bundle file, or "" (Android and unsupported platforms have none).
            static String find_bundle()
            {
#if defined(__linux__) && !defined(__ANDROID__)
                for (const char *const *p = bundle_candidates(); *p; ++p)
                    if (std::FILE *f = std::fopen(*p, "rb")) { std::fclose(f); return String(*p); }
#endif
                return String();
            }

            static bool read_file(const char *path, Vector<char> &out)
            {
                std::FILE *f = std::fopen(path, "rb");
                if (!f) return false;
                char chunk[8192]; std::size_t n;
                out.clear();
                while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) for (std::size_t i = 0; i < n; ++i) out.push_back(chunk[i]);
                const bool ok = !std::ferror(f);
                std::fclose(f);
                out.push_back('\0');
                return ok;
            }

            // sink(const char *data, size_t size_with_nul) returns true when it accepted at least one certificate.
            template <class Sink>
            static bool load(const TlsConfig &config, Sink &&sink, char *error, std::size_t error_size)
            {
                auto fail = [&](const char *prefix, const char *path) { if (error && error_size) std::snprintf(error, error_size, "%s%s", prefix, path); return false; };
                auto feed_file = [&](const char *path) -> bool
                {
                    Vector<char> data;
                    return read_file(path, data) && sink(data.data(), data.size());
                };
                const bool explicit_store = !config.ca_file.empty() || !config.ca_dir.empty();
                if (!explicit_store)
                {
#if defined(__ANDROID__)
                    return feed_directory(android_directory(), feed_file) ? true : fail("no certificates in ", android_directory());
#elif defined(__linux__)
                    const String bundle = find_bundle();
                    if (bundle.empty()) return fail("no CA bundle found", "");
                    return feed_file(bundle.c_str()) ? true : fail("cannot load ", bundle.c_str());
#else
                    return fail("system CA store not implemented on this platform", "");
#endif
                }
                if (!config.ca_file.empty() && !feed_file(config.ca_file.c_str())) return fail("cannot load ", config.ca_file.c_str());
                if (!config.ca_dir.empty() && !feed_directory(config.ca_dir.c_str(), feed_file)) return fail("no certificates in ", config.ca_dir.c_str());
                return true;
            }

        private:
            template <class Feed>
            static bool feed_directory(const char *dir, Feed &feed)
            {
#if defined(_WIN32)
                (void)dir; (void)feed; return false;
#else
                DIR *d = opendir(dir);
                if (!d) return false;
                String base(dir);
                if (base.empty() || base.back() != '/') base.push_back('/');
                bool any = false;
                while (const dirent *entry = readdir(d))
                {
                    if (entry->d_name[0] == '.') continue;
                    String path = base; path.append(entry->d_name, std::strlen(entry->d_name));
                    struct stat st;
                    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                    if (feed(path.c_str())) any = true;
                }
                closedir(d);
                return any;
#endif
            }
        };
    }
}
