#pragma once

#include "function.hpp"
#include "http.hpp"
#include "socket.hpp"
#include "stream.hpp"
#include "thread.hpp"

namespace ct
{
    class HttpServer
    {
    public:
        using Handler = Function<void(const HttpRequest &, HttpResponse &)>;

        explicit HttpServer(unsigned = 0) noexcept : running_(0) {}
        ~HttpServer() { stop(); }
        HttpServer(const HttpServer &) = delete;
        HttpServer &operator=(const HttpServer &) = delete;

        void route(StringView method, StringView pattern, Handler handler)
        {
            routes_.push_back(Route{String(method), String(pattern), detail::move(handler)});
        }

        void serve_files(StringView url_prefix, StringView directory)
        {
            static_dirs_.push_back(StaticDir{String(url_prefix), String(directory)});
        }

        bool listen(const Address &address, NetError *error = nullptr)
        {
            stop();
            listener_.close();
            connections_.clear();
            if (!listener_.bind(address, error) || !listener_.listen())
            {
                if (error && !*error) *error = listener_.last_error();
                return false;
            }
            if (!listener_.set_nonblocking(true))
            {
                if (error) *error = listener_.last_error();
                listener_.close();
                return false;
            }
            running_.store(1);
            return true;
        }

        void run()
        {
            while (running_.load()) poll(100);
            connections_.clear();
            listener_.close();
        }

        void stop() noexcept
        {
            running_.store(0);
        }

        void poll(unsigned timeout_ms)
        {
            if (!running_.load() || !listener_.valid()) return;
            Poller poller;
            poller.add(listener_, Poller::Readable);
            for (std::size_t i = 0; i < connections_.size(); ++i)
                poller.add(connections_[i].stream, connections_[i].output.empty() ? Poller::Readable : Poller::Writable);
            if (poller.wait(timeout_ms) < 0) return;
            if (poller.readable(0)) accept_ready();
            for (std::size_t i = connections_.size(); i-- > 0;)
            {
                const std::size_t event = i + 1;
                if (event >= poller.size()) continue;
                bool keep = true;
                if (poller.error(event)) keep = false;
                else if (poller.readable(event) && connections_[i].output.empty()) keep = read_ready(connections_[i]);
                else if (poller.writable(event) && !connections_[i].output.empty()) keep = write_ready(connections_[i]);
                if (!keep) connections_.erase(connections_.begin() + i);
            }
        }

        bool running() const noexcept { return running_.load() != 0; }
        Address local_address() const noexcept { return listener_.local_address(); }

    private:
        struct Route { String method, pattern; Handler handler; };
        struct Connection
        {
            TcpStream stream;
            HttpParser parser;
            String output;
            bool close_after = false;
        };

        static StringView segment(StringView path, std::size_t &at)
        {
            while (at < path.size() && path[at] == '/') ++at;
            std::size_t start = at;
            while (at < path.size() && path[at] != '/') ++at;
            return StringView(path.data() + start, at - start);
        }

        static bool match(StringView pattern, StringView path, Vector<HttpParam> &params)
        {
            params.clear();
            std::size_t pi = 0, xi = 0;
            for (;;)
            {
                StringView p = segment(pattern, pi), x = segment(path, xi);
                if (p.size() && p[0] == '*')
                {
                    std::size_t start = xi - x.size();
                    params.push_back(HttpParam{String(p.size() > 1 ? p.substr(1) : StringView("*")), String(path.substr(start))});
                    return true;
                }
                if (p.empty() || x.empty()) return p.empty() && x.empty() && pi == pattern.size() && xi == path.size();
                if (p[0] == ':') params.push_back(HttpParam{String(p.substr(1)), String(x)});
                else if (p != x) return false;
                if (pi == pattern.size() || xi == path.size()) return pi == pattern.size() && xi == path.size();
            }
        }

        void accept_ready()
        {
            for (;;)
            {
                TcpStream stream;
                if (!listener_.accept(stream)) break;
                stream.set_nonblocking(true);
                Connection connection;
                connection.stream = detail::move(stream);
                connections_.push_back(detail::move(connection));
            }
        }

        bool read_ready(Connection &connection)
        {
            char buffer[8192];
            long count = connection.stream.recv(buffer, sizeof(buffer));
            if (count == 0) return false;
            if (count < 0) return connection.stream.would_block();
            HttpRequest request;
            HttpParser::State state = connection.parser.feed(buffer, static_cast<std::size_t>(count), request);
            if (state == HttpParser::Error) return false;
            if (state == HttpParser::Done) prepare_response(connection, request);
            return true;
        }

        bool write_ready(Connection &connection)
        {
            long count = connection.stream.send(connection.output.data(), connection.output.size());
            if (count < 0) return connection.stream.would_block();
            if (!count) return false;
            connection.output.erase(0, static_cast<std::size_t>(count));
            if (!connection.output.empty()) return true;
            if (connection.close_after) return false;
            connection.parser.reset();
            HttpRequest next;
            HttpParser::State state = connection.parser.feed(nullptr, 0, next);
            if (state == HttpParser::Done) prepare_response(connection, next);
            else if (state == HttpParser::Error) return false;
            return true;
        }

        void prepare_response(Connection &connection, HttpRequest &request)
        {
            HttpResponse response;
            bool found = false;
            for (std::size_t i = 0; i < routes_.size(); ++i)
            {
                Route &route = routes_[i];
                if ((route.method == "*" || detail::http_iequal(route.method, request.method)) && match(route.pattern, request.path, request.params))
                {
                    route.handler(request, response);
                    found = true;
                    break;
                }
            }
            if (!found) found = static_response(request, response);
            if (!found) { response.status = 404; response.text("Not Found"); }
            StringView requested_connection = request.header("Connection");
            connection.close_after = request.version == "HTTP/1.0"
                ? !detail::http_has_token(requested_connection, "keep-alive")
                : detail::http_has_token(requested_connection, "close");
            if (response.header("Content-Length").empty()) response.set("Content-Length", String::number(static_cast<unsigned long long>(response.body.size())));
            response.set("Connection", connection.close_after ? StringView("close") : StringView("keep-alive"));
            connection.output.clear();
            connection.output.append("HTTP/1.1 ").append_number(response.status).append(" ").append(reason(response.status)).append("\r\n");
            for (std::size_t i = 0; i < response.headers.size(); ++i)
                connection.output.append(response.headers[i].name).append(": ").append(response.headers[i].value).append("\r\n");
            connection.output.append("\r\n").append(response.body);
        }

        static const char *reason(int status) noexcept
        {
            switch (status)
            {
            case 200: return "OK"; case 201: return "Created"; case 204: return "No Content";
            case 400: return "Bad Request"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
            case 413: return "Payload Too Large"; case 500: return "Internal Server Error";
            default: return "Status";
            }
        }

        bool static_response(const HttpRequest &request, HttpResponse &response)
        {
            if (!detail::http_iequal(request.method, "GET") && !detail::http_iequal(request.method, "HEAD")) return false;
            for (std::size_t i = 0; i < static_dirs_.size(); ++i)
            {
                const StaticDir &entry = static_dirs_[i];
                StringView prefix(entry.prefix), path(request.path);
                if (!path.starts_with(prefix)) continue;
                if (path.size() > prefix.size() && !prefix.empty() && prefix.back() != '/' && path[prefix.size()] != '/') continue;
                StringView relative = path.substr(prefix.size());
                while (!relative.empty() && relative.front() == '/') relative.remove_prefix(1);
                if (relative.empty() || unsafe_path(relative)) return false;
                String filename(entry.directory);
                if (!filename.empty() && filename.back() != '/' && filename.back() != '\\') filename.append("/");
                filename.append(relative.data(), relative.size());
                Vector<std::uint8_t> bytes;
                if (!File::read_all(filename, bytes)) continue;
                response.status = 200;
                response.set("Content-Type", mime(relative));
                response.set("Content-Length", String::number(static_cast<unsigned long long>(bytes.size())));
                if (!detail::http_iequal(request.method, "HEAD") && !bytes.empty())
                    response.body.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                return true;
            }
            return false;
        }

        static bool unsafe_path(StringView path) noexcept
        {
            if (path.find('\\') != StringView::npos) return true;
            std::size_t at = 0;
            while (at <= path.size())
            {
                std::size_t end = at; while (end < path.size() && path[end] != '/') ++end;
                StringView part(path.data() + at, end - at);
                if (part == ".." || part == ".") return true;
                if (end == path.size()) break;
                at = end + 1;
            }
            return false;
        }

        static const char *mime(StringView path) noexcept
        {
            std::size_t dot = path.rfind('.');
            if (dot == StringView::npos) return "application/octet-stream";
            StringView ext = path.substr(dot + 1);
            if (detail::http_iequal(ext, "html") || detail::http_iequal(ext, "htm")) return "text/html; charset=utf-8";
            if (detail::http_iequal(ext, "css")) return "text/css; charset=utf-8";
            if (detail::http_iequal(ext, "js")) return "application/javascript";
            if (detail::http_iequal(ext, "json")) return "application/json";
            if (detail::http_iequal(ext, "png")) return "image/png";
            if (detail::http_iequal(ext, "jpg") || detail::http_iequal(ext, "jpeg")) return "image/jpeg";
            if (detail::http_iequal(ext, "svg")) return "image/svg+xml";
            if (detail::http_iequal(ext, "txt")) return "text/plain; charset=utf-8";
            return "application/octet-stream";
        }

        struct StaticDir { String prefix, directory; };
        TcpListener listener_;
        Vector<Connection> connections_;
        Vector<Route> routes_;
        Vector<StaticDir> static_dirs_;
        Atomic<int> running_;
    };
}
