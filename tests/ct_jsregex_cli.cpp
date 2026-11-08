#include <ct/jsregex.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

    bool read_line(std::string &out)
    {
        out.clear();
        int c;
        while ((c = std::getchar()) != EOF && c != '\n')
            out.push_back(static_cast<char>(c));
        return c != EOF || !out.empty();
    }

    std::vector<char16_t> from_hex(const std::string &s)
    {
        std::vector<char16_t> out;
        if (s == "-")
            return out;
        for (std::size_t i = 0; i + 4 <= s.size(); i += 4)
            out.push_back(static_cast<char16_t>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16)));
        return out;
    }

    void json_string(const ct::StringView &v)
    {
        std::putchar('"');
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(v[i]);
            if (c == '"' || c == '\\')
                std::printf("\\%c", c);
            else if (c < 0x20)
                std::printf("\\u%04x", c);
            else
                std::putchar(c);
        }
        std::putchar('"');
    }

    void span(const ct::JsCaptures &c, std::size_t g)
    {
        if (!c.matched(g))
            std::printf("null");
        else
            std::printf("[%zu,%zu]", c.start(g), c.end(g));
    }

} // namespace

int main(int argc, char **argv)
{
    std::size_t steps = 0;
    if (argc > 1)
        steps = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    std::string line;
    while (read_line(line))
    {
        char flags[32], pat[1 << 16], text[1 << 20];
        unsigned long last = 0;
        int mode = 0;
        if (std::sscanf(line.c_str(), "%31s %65535s %1048575s %lu %d", flags, pat, text, &last, &mode) != 5)
            break;
        unsigned f = 0;
        bool sticky = false, gy = false;
        for (const char *p = flags; *p; ++p)
        {
            switch (*p)
            {
            case 'g': f |= ct::JsRegex::Global; gy = true; break;
            case 'i': f |= ct::JsRegex::IgnoreCase; break;
            case 'm': f |= ct::JsRegex::Multiline; break;
            case 's': f |= ct::JsRegex::DotAll; break;
            case 'u': f |= ct::JsRegex::Unicode; break;
            case 'v': f |= ct::JsRegex::UnicodeSets; break;
            case 'y': f |= ct::JsRegex::Sticky; sticky = true; gy = true; break;
            case 'd': f |= ct::JsRegex::HasIndices; break;
            default: break;
            }
        }
        std::vector<char16_t> p16 = from_hex(pat), t16 = from_hex(text);
        ct::JsRegex::Error err;
        ct::JsRegex re = ct::JsRegex::compile(p16.data(), p16.size(), f, &err);
        if (!re)
        {
            std::printf("{\"e\":");
            json_string(ct::StringView(err.message));
            std::printf("}\n");
            std::fflush(stdout);
            continue;
        }
        if (steps)
        {
            ct::JsRegex::Limits l;
            l.max_steps = steps;
            re.set_limits(l);
        }
        ct::JsCaptures caps;
        std::size_t start = gy ? last : 0;
        ct::JsRegex::ExecResult r;
        if (mode == 1)
        {
            std::vector<unsigned char> t8;
            for (std::size_t i = 0; i < t16.size(); ++i)
                t8.push_back(static_cast<unsigned char>(t16[i]));
            r = re.exec(t8.data(), t8.size(), start, sticky, caps);
        }
        else
            r = re.exec(t16.data(), t16.size(), start, sticky, caps);
        if (r == ct::JsRegex::TooComplex)
            std::printf("{\"tc\":1}\n");
        else if (r == ct::JsRegex::NoMatch)
            std::printf("null\n");
        else
        {
            std::printf("{\"r\":[");
            for (std::size_t g = 0; g <= re.group_count(); ++g)
            {
                if (g)
                    std::putchar(',');
                span(caps, g);
            }
            std::putchar(']');
            bool named = false;
            for (std::size_t g = 1; g <= re.group_count(); ++g)
            {
                if (re.group_name(g).size() == 0)
                    continue;
                std::printf(named ? "," : ",\"g\":{");
                named = true;
                json_string(re.group_name(g));
                std::putchar(':');
                span(caps, g);
            }
            if (named)
                std::putchar('}');
            std::printf("}\n");
        }
        std::fflush(stdout);
    }
    return 0;
}
