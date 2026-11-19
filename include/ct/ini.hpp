#pragma once

#include <cerrno>
#include <cstdlib>

#include "detail/numconv_g.hpp"
#include "stream.hpp"

namespace ct
{
    namespace detail
    {
        // Como o strtod: salta os espacos do principio e tem de consumir o texto todo. ct/numconv.hpp: arredondamento
        // correcto, '.' em qualquer locale.
        inline bool ini_parse_double(const char *text, double &out)
        {
            const char *p = text;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v' || *p == '\f' || *p == '\r')
                ++p;
            const char *last = p;
            while (*last)
                ++last;
            const char *e = parse_double(p, last, out);
            return e != p && e == last;
        }

        // O texto mais curto que volta ao mesmo double; o expoente aparece a partir de 10^max(15, algarismos) (era
        // %.15g, ou %.17g se aquele nao voltava ao mesmo valor)
        inline void ini_format_double(double v, String &out)
        {
            char buf[48];
            const int n = static_cast<int>(write_shortest_at_least(v, buf, 15) - buf);
            out.assign(buf, static_cast<std::size_t>(n));
        }

        inline bool ini_is_space(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        inline bool ini_needs_quotes(StringView v) noexcept
        {
            if (v.empty())
                return false;
            if (ini_is_space(v.front()) || ini_is_space(v.back()) || v.front() == '"')
                return true;
            for (std::size_t i = 0; i < v.size(); ++i)
                if (v[i] == '\n' || v[i] == '\r')
                    return true;
            return false;
        }

        inline void ini_append_quoted(String &out, StringView v)
        {
            out.push_back('"');
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                if (v[i] == '"')
                    out.push_back('"');
                out.push_back(v[i]);
            }
            out.push_back('"');
        }

        inline bool ini_valid_key(StringView key) noexcept
        {
            if (key.empty() || key.front() == ' ' || key.front() == '\t' || key.back() == ' ' || key.back() == '\t')
                return false;
            if (key.front() == '[' || key.front() == ';' || key.front() == '#')
                return false;
            for (std::size_t i = 0; i < key.size(); ++i)
            {
                const char c = key[i];
                if (c == '=' || c == ':' || c == '\n' || c == '\r' || c == '\0')
                    return false;
            }
            return true;
        }

        inline bool ini_valid_section(StringView name) noexcept
        {
            if (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.back() == ' ' || name.back() == '\t'))
                return false;
            for (std::size_t i = 0; i < name.size(); ++i)
            {
                const char c = name[i];
                if (c == ']' || c == '\n' || c == '\r' || c == '\0')
                    return false;
            }
            return true;
        }
    }

    class Ini
    {
    public:
        struct Entry
        {
            String key;
            String value;
        };

        struct Section
        {
            String name;
            Vector<Entry> entries;
        };

        using size_type = std::size_t;
        static constexpr size_type npos = static_cast<size_type>(-1);

        static Ini parse(StringView text)
        {
            Ini ini;
            ini.parse_into(text);
            return ini;
        }

        static bool load(Ini &out, StringView path)
        {
            String text;
            if (!File::read_all(path, text))
                return false;
            out.clear();
            out.parse_into(text);
            return true;
        }

        bool load(StringView path) { return Ini::load(*this, path); }

        bool save(StringView path) const
        {
            String text = dump();
            return File::write_all(path, text);
        }

        String dump() const
        {
            String out;
            for (const Section &s : sections_)
            {
                if (!s.name.empty())
                {
                    out.append("[");
                    out.append(s.name);
                    out.append("]\n");
                }
                for (const Entry &e : s.entries)
                {
                    out.append(e.key);
                    out.append("=");
                    if (detail::ini_needs_quotes(e.value))
                        detail::ini_append_quoted(out, e.value);
                    else
                        out.append(e.value);
                    out.append("\n");
                }
            }
            return out;
        }

        void clear() { sections_.clear(); }
        bool empty() const noexcept { return sections_.empty(); }
        size_type size() const noexcept { return sections_.size(); }

        Vector<Section> &sections() noexcept { return sections_; }
        const Vector<Section> &sections() const noexcept { return sections_; }

        Section *section(StringView name) noexcept
        {
            size_type i = find_section_index(name);
            return i == npos ? nullptr : &sections_[i];
        }
        const Section *section(StringView name) const noexcept
        {
            return const_cast<Ini *>(this)->section(name);
        }

        bool has_section(StringView name) const noexcept { return section(name) != nullptr; }

        bool has(StringView section, StringView key) const noexcept
        {
            const Section *s = this->section(section);
            return s && find_entry(*s, key) != nullptr;
        }

        String get(StringView section, StringView key, const char *fallback = "") const
        {
            const Section *s = this->section(section);
            if (s)
            {
                const Entry *e = find_entry(*s, key);
                if (e)
                    return e->value;
            }
            return String(fallback);
        }

        long long get_int(StringView section, StringView key, long long fallback = 0) const
        {
            const Section *s = this->section(section);
            if (s)
            {
                const Entry *e = find_entry(*s, key);
                if (e && !e->value.empty())
                {
                    char *end = nullptr;
                    errno = 0;
                    const long long parsed = std::strtoll(e->value.c_str(), &end, 10);
                    if (end && *end == '\0' && errno != ERANGE)
                        return parsed;
                }
            }
            return fallback;
        }

        double get_double(StringView section, StringView key, double fallback = 0.0) const
        {
            const Section *s = this->section(section);
            if (s)
            {
                const Entry *e = find_entry(*s, key);
                double parsed = 0.0;
                if (e && !e->value.empty() && detail::ini_parse_double(e->value.c_str(), parsed))
                    return parsed;
            }
            return fallback;
        }

        bool get_bool(StringView section, StringView key, bool fallback = false) const
        {
            const Section *s = this->section(section);
            if (s)
            {
                const Entry *e = find_entry(*s, key);
                if (e)
                    return parse_bool(e->value, fallback);
            }
            return fallback;
        }

        void set(const char *section, const char *key, const char *value)
        {
            set_value(section, key, String(value));
        }
        void set(const char *section, const char *key, const String &value)
        {
            set_value(section, key, value);
        }
        void set(const char *section, const char *key, int value)
        {
            set_value(section, key, String::number(value));
        }
        void set(const char *section, const char *key, unsigned value)
        {
            set_value(section, key, String::number(static_cast<unsigned long long>(value)));
        }
        void set(const char *section, const char *key, long long value)
        {
            set_value(section, key, String::number(value));
        }
        void set(const char *section, const char *key, double value)
        {
            String text;
            detail::ini_format_double(value, text);
            set_value(section, key, text);
        }
        void set(const char *section, const char *key, bool value)
        {
            set_value(section, key, String(value ? "true" : "false"));
        }

        bool erase(StringView section, StringView key)
        {
            Section *s = this->section(section);
            if (!s)
                return false;
            for (size_type i = 0; i < s->entries.size(); ++i)
            {
                if (StringView(s->entries[i].key) == key)
                {
                    s->entries.erase(s->entries.begin() + i);
                    return true;
                }
            }
            return false;
        }

        bool erase_section(StringView name)
        {
            size_type i = find_section_index(name);
            if (i == npos)
                return false;
            sections_.erase(sections_.begin() + i);
            return true;
        }

    private:
        Vector<Section> sections_;

        static bool parse_bool(StringView v, bool fallback)
        {
            String s(v);
            for (char &c : s)
            {
                if (c >= 'A' && c <= 'Z')
                    c = char(c + ('a' - 'A'));
            }
            if (s == "1" || s == "true" || s == "yes" || s == "on")
                return true;
            if (s == "0" || s == "false" || s == "no" || s == "off")
                return false;
            return fallback;
        }

        size_type find_section_index(StringView name) const noexcept
        {
            for (size_type i = 0; i < sections_.size(); ++i)
                if (StringView(sections_[i].name) == name)
                    return i;
            return npos;
        }

        static const Entry *find_entry(const Section &s, StringView key) noexcept
        {
            for (size_type i = 0; i < s.entries.size(); ++i)
                if (StringView(s.entries[i].key) == key)
                    return &s.entries[i];
            return nullptr;
        }

        static Entry *find_entry(Section &s, StringView key) noexcept
        {
            return const_cast<Entry *>(find_entry(static_cast<const Section &>(s), key));
        }

        Section &ensure_section(StringView name)
        {
            size_type i = find_section_index(name);
            if (i != npos)
                return sections_[i];
            Section s;
            s.name = String(name);
            if (name.empty())
            {
                sections_.insert(sections_.begin(), detail::move(s));
                return sections_.front();
            }
            sections_.push_back(detail::move(s));
            return sections_.back();
        }

        void put_entry(Section &s, StringView key, StringView value)
        {
            Entry *e = find_entry(s, key);
            if (e)
            {
                e->value = String(value);
                return;
            }
            Entry entry;
            entry.key = String(key);
            entry.value = String(value);
            s.entries.push_back(detail::move(entry));
        }

        void set_value(StringView section, StringView key, const String &value)
        {
            if (!detail::ini_valid_key(key))
                detail::fatal("ct::Ini::set: chave invalida (vazia, com '=', ':' ou quebra de linha)");
            if (!detail::ini_valid_section(section))
                detail::fatal("ct::Ini::set: nome de seccao invalido (']' ou quebra de linha)");
            Section &s = ensure_section(section);
            put_entry(s, key, StringView(value));
        }

        static bool unquote(StringView rest, String &out, size_type &consumed)
        {
            out.clear();
            size_type i = 1;
            for (; i < rest.size(); ++i)
            {
                if (rest[i] != '"')
                {
                    out.push_back(rest[i]);
                    continue;
                }
                if (i + 1 < rest.size() && rest[i + 1] == '"')
                {
                    out.push_back('"');
                    ++i;
                    continue;
                }
                break;
            }
            if (i >= rest.size())
                return false;
            ++i;
            while (i < rest.size() && rest[i] != '\n' && detail::ini_is_space(rest[i]))
                ++i;
            if (i < rest.size() && rest[i] != '\n')
                return false;
            consumed = i < rest.size() ? i + 1 : i;
            return true;
        }

        void parse_into(StringView text)
        {
            clear();
            size_type current = npos;
            const char *const text_end = text.data() + text.size();
            while (!text.empty())
            {
                size_type nl = text.find('\n');
                StringView raw = nl == npos ? text : text.substr(0, nl);
                text = nl == npos ? StringView() : text.substr(nl + 1);
                StringView line = raw.trimmed();
                if (line.empty() || line[0] == ';' || line[0] == '#')
                    continue;
                if (line[0] == '[')
                {
                    size_type close = line.rfind(']');
                    StringView name = close == npos ? line.substr(1).trimmed()
                                                    : line.substr(1, close - 1).trimmed();
                    if (name.empty())
                        continue;
                    current = find_section_index(name);
                    if (current == npos)
                    {
                        Section s;
                        s.name = String(name);
                        sections_.push_back(detail::move(s));
                        current = sections_.size() - 1;
                    }
                    continue;
                }
                size_type sep_eq = line.find('=');
                size_type sep_colon = line.find(':');
                size_type sep = sep_eq == npos ? sep_colon
                                               : (sep_colon == npos ? sep_eq
                                                                    : (sep_eq < sep_colon ? sep_eq : sep_colon));
                if (sep == npos)
                    continue;
                StringView key = line.substr(0, sep).trimmed();
                StringView value = line.substr(sep + 1).trimmed();
                if (key.empty())
                    continue;
                if (current == npos)
                {
                    Section s;
                    sections_.insert(sections_.begin(), detail::move(s));
                    current = 0;
                }
                String unquoted;
                size_type consumed = 0;
                if (!value.empty() && value.front() == '"' &&
                    unquote(StringView(value.data(), static_cast<size_type>(text_end - value.data())), unquoted, consumed))
                {
                    put_entry(sections_[current], key, StringView(unquoted));
                    const char *after = value.data() + consumed;
                    text = StringView(after, static_cast<size_type>(text_end - after));
                }
                else
                    put_entry(sections_[current], key, value);
            }
        }
    };

}
