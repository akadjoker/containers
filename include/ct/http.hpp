#pragma once

#include "span.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{
    namespace detail
    {
        inline char http_lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }
        inline bool http_iequal(StringView a, StringView b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) if (http_lower(a[i]) != http_lower(b[i])) return false;
            return true;
        }
        inline StringView http_trim(StringView s) noexcept
        {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
            return StringView(s.data() + a, b - a);
        }
        inline bool http_has_token(StringView value, StringView token) noexcept
        {
            for (std::size_t a = 0; a <= value.size();)
            {
                std::size_t b = a; while (b < value.size() && value[b] != ',') ++b;
                if (http_iequal(http_trim(StringView(value.data() + a, b - a)), token)) return true;
                if (b == value.size()) break;
                a = b + 1;
            }
            return false;
        }
    }

    struct HttpHeader { String name; String value; };
    struct HttpParam { String name; String value; };

    struct HttpRequest
    {
        String method, path, query, version;
        Vector<HttpHeader> headers;
        Vector<HttpParam> params;
        String body;
        StringView header(StringView name) const noexcept
        {
            for (std::size_t i = 0; i < headers.size(); ++i) if (detail::http_iequal(headers[i].name, name)) return headers[i].value;
            return StringView();
        }
        StringView param(StringView name) const noexcept
        {
            for (std::size_t i = 0; i < params.size(); ++i) if (StringView(params[i].name) == name) return params[i].value;
            return StringView();
        }
    };

    struct HttpResponse
    {
        int status = 200;
        String version = "HTTP/1.1", reason;
        Vector<HttpHeader> headers;
        String body;
        StringView header(StringView name) const noexcept
        {
            for (std::size_t i = 0; i < headers.size(); ++i) if (detail::http_iequal(headers[i].name, name)) return headers[i].value;
            return StringView();
        }
        void set(StringView name, StringView value)
        {
            for (std::size_t i = 0; i < headers.size(); ++i) if (detail::http_iequal(headers[i].name, name)) { headers[i].value = String(value); return; }
            headers.push_back(HttpHeader{String(name), String(value)});
        }
        void text(StringView value) { set("Content-Type", "text/plain; charset=utf-8"); body = String(value); }
        void json(StringView value) { set("Content-Type", "application/json"); body = String(value); }
    };

    class HttpParser
    {
    public:
        enum State { NeedMore, Done, Error };
        static constexpr std::size_t kMaxHeaders = 16 * 1024;
        static constexpr std::size_t kMaxBody = 8 * 1024 * 1024;
        explicit HttpParser(std::size_t max_body = kMaxBody, std::size_t max_headers = kMaxHeaders)
            : max_body_(max_body), max_headers_(max_headers), state_(NeedMore), consumed_(0), error_(nullptr) {}
        State feed(const char *p, std::size_t n, HttpRequest &out) { if (!append(p, n)) return state_; return request(out); }
        State feed(const char *p, std::size_t n, HttpResponse &out) { if (!append(p, n)) return state_; return response(out, false); }
        State finish(HttpResponse &out) { return state_ == NeedMore ? response(out, true) : state_; }
        void reset() { if (consumed_ <= data_.size()) data_.erase(0, consumed_); else data_.clear(); state_ = NeedMore; consumed_ = 0; error_ = nullptr; }
        State state() const noexcept { return state_; }
        const char *error() const noexcept { return error_; }
        std::size_t buffered() const noexcept { return data_.size(); }
        std::size_t consumed() const noexcept { return consumed_; }
    private:
        struct Head { std::size_t body, length; bool chunked, has_length; };
        bool fail(const char *s) { state_ = Error; error_ = s; return false; }
        bool append(const char *p, std::size_t n)
        {
            if (state_ != NeedMore) return state_ != Error;
            if (n && !p) return fail("null HTTP input");
            if (n > max_headers_ + max_body_ || data_.size() > max_headers_ + max_body_ - n) return fail("HTTP message exceeds limit");
            data_.append(p, n); return true;
        }
        std::size_t crlf(std::size_t at) const noexcept
        {
            for (std::size_t i = at; i + 1 < data_.size(); ++i) if (data_[i] == '\r' && data_[i + 1] == '\n') return i;
            return String::npos;
        }
        bool headers(std::size_t at, Vector<HttpHeader> &out, Head &head)
        {
            out.clear(); head.length = 0; head.chunked = false; head.has_length = false;
            for (;;)
            {
                std::size_t end = crlf(at);
                if (end == String::npos) { if (data_.size() > max_headers_) fail("HTTP headers exceed limit"); return false; }
                if (end + 2 > max_headers_) return fail("HTTP headers exceed limit");
                if (end == at) { head.body = end + 2; return true; }
                std::size_t colon = at; while (colon < end && data_[colon] != ':') ++colon;
                if (colon == at || colon == end) return fail("malformed HTTP header");
                StringView name(data_.data() + at, colon - at), value = detail::http_trim(StringView(data_.data() + colon + 1, end - colon - 1));
                out.push_back(HttpHeader{String(name), String(value)});
                if (detail::http_iequal(name, "Content-Length"))
                {
                    if (head.has_length || value.empty()) return fail("invalid Content-Length");
                    head.has_length = true;
                    for (std::size_t i = 0; i < value.size(); ++i)
                    {
                        if (value[i] < '0' || value[i] > '9') return fail("invalid Content-Length");
                        std::size_t d = static_cast<std::size_t>(value[i] - '0');
                        if (d > max_body_ || head.length > (max_body_ - d) / 10) return fail("HTTP body exceeds limit");
                        head.length = head.length * 10 + d;
                    }
                }
                if (detail::http_iequal(name, "Transfer-Encoding") && detail::http_has_token(value, "chunked")) head.chunked = true;
                at = end + 2;
            }
        }
        State body(const Head &head, String &out)
        {
            if (!head.chunked)
            {
                if (data_.size() < head.body + head.length) return NeedMore;
                out.assign(data_.data() + head.body, head.length); consumed_ = head.body + head.length; state_ = Done; return Done;
            }
            String decoded; std::size_t at = head.body;
            for (;;)
            {
                std::size_t end = crlf(at); if (end == String::npos) return NeedMore;
                std::size_t size = 0, digits = 0;
                for (std::size_t i = at; i < end && data_[i] != ';'; ++i)
                {
                    char c = data_[i]; unsigned d;
                    if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10; else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else return fail("invalid chunk size"), Error;
                    if (d > max_body_ || size > (max_body_ - d) / 16) return fail("HTTP body exceeds limit"), Error;
                    size = size * 16 + d;
                    ++digits;
                }
                if (!digits) return fail("invalid chunk size"), Error;
                at = end + 2;
                if (!size)
                {
                    if (data_.size() < at + 2) return NeedMore;
                    if (data_[at] != '\r' || data_[at + 1] != '\n') return fail("chunk trailers are not supported"), Error;
                    out = detail::move(decoded); consumed_ = at + 2; state_ = Done; return Done;
                }
                if (decoded.size() > max_body_ - size) return fail("HTTP body exceeds limit"), Error;
                if (data_.size() < at + size + 2) return NeedMore;
                if (data_[at + size] != '\r' || data_[at + size + 1] != '\n') return fail("malformed HTTP chunk"), Error;
                decoded.append(data_.data() + at, size); at += size + 2;
            }
        }
        State request(HttpRequest &out)
        {
            std::size_t end = crlf(0); if (end == String::npos) { if (data_.size() > max_headers_) fail("request line too long"); return state_; }
            std::size_t a = 0; while (a < end && data_[a] != ' ') ++a; std::size_t b = a + 1; while (b < end && data_[b] != ' ') ++b;
            if (!a || a >= end || b >= end || b == a + 1) return fail("malformed request line"), Error;
            HttpRequest value; value.method.assign(data_.data(), a); StringView target(data_.data() + a + 1, b - a - 1); std::size_t q = target.find('?');
            if (q == StringView::npos) value.path = String(target); else { value.path.assign(target.data(), q); value.query.assign(target.data() + q + 1, target.size() - q - 1); }
            value.version.assign(data_.data() + b + 1, end - b - 1); if (value.version != "HTTP/1.0" && value.version != "HTTP/1.1") return fail("unsupported HTTP version"), Error;
            Head head; if (!headers(end + 2, value.headers, head)) return state_; State result = body(head, value.body); if (result == Done) out = detail::move(value); return result;
        }
        State response(HttpResponse &out, bool eof)
        {
            std::size_t end = crlf(0); if (end == String::npos) { if (data_.size() > max_headers_) fail("status line too long"); return state_; }
            std::size_t a = 0; while (a < end && data_[a] != ' ') ++a;
            if (a + 4 > end) return fail("malformed status line"), Error;
            HttpResponse value; value.version.assign(data_.data(), a); if (value.version != "HTTP/1.0" && value.version != "HTTP/1.1") return fail("unsupported HTTP version"), Error;
            for (std::size_t i = a + 1; i < a + 4; ++i) if (data_[i] < '0' || data_[i] > '9') return fail("invalid HTTP status"), Error;
            value.status = (data_[a + 1] - '0') * 100 + (data_[a + 2] - '0') * 10 + data_[a + 3] - '0';
            if (a + 4 < end) { if (data_[a + 4] != ' ') return fail("malformed status line"), Error; value.reason.assign(data_.data() + a + 5, end - a - 5); }
            Head head; if (!headers(end + 2, value.headers, head)) return state_;
            if (!head.has_length && !head.chunked && !((value.status >= 100 && value.status < 200) || value.status == 204 || value.status == 304))
            {
                if (!eof) return NeedMore;
                const std::size_t length = data_.size() - head.body;
                if (length > max_body_) return fail("HTTP body exceeds limit"), Error;
                value.body.assign(data_.data() + head.body, length); consumed_ = data_.size(); state_ = Done; out = detail::move(value); return Done;
            }
            State result = body(head, value.body); if (result == Done) out = detail::move(value); return result;
        }
        String data_; std::size_t max_body_, max_headers_; State state_; std::size_t consumed_; const char *error_;
    };
}
