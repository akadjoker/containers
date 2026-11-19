#pragma once

#include "detail/utils.hpp"
#include "numconv.hpp"
#include "vector.hpp"

namespace ct
{

    namespace detail
    {
        template <typename T>
        struct StringNpos
        {
            static constexpr T npos = static_cast<T>(-1);
        };

#if !defined(__cpp_inline_variables)
        template <typename T>
        constexpr T StringNpos<T>::npos;
#endif
    }

    class String : private HeapAlloc, public detail::StringNpos<std::size_t>
    {
        struct HeapRep
        {
            char *data;
            std::size_t size;
            std::size_t capacity; 
        };

        static constexpr std::size_t kSSO = sizeof(HeapRep) - 1;
        static constexpr std::size_t kFlag = std::size_t(1)
                                              << (sizeof(std::size_t) * 8 - 1);

        struct SmallRep
        {
            char data[kSSO + 1]; 
        };

        union Storage
        {
            HeapRep heap;
            SmallRep small;

            Storage() : heap() {}
            ~Storage() {}
        };

        static_assert(sizeof(SmallRep) == sizeof(HeapRep),
                      "ct::String: SmallRep e HeapRep tem de ocupar o mesmo espaco");
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
        static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
                      "ct::String: o SSO empacotado assume little-endian "
                      "(WebAssembly, ARM/x86 Android e desktop cumprem isto; "
                      "recusa-se a compilar em big-endian em vez de corromper "
                      "dados em silencio)");
#endif

    public:
        using size_type = std::size_t;
        using iterator = char *;
        using const_iterator = const char *;
        using detail::StringNpos<std::size_t>::npos;

        String() noexcept : storage_()
        {
            set_small_size(0);
        }

        String(const char *s) : storage_()
        {
            init(s, s ? std::strlen(s) : 0);
        }

        String(const char *s, size_type n) : storage_()
        {
            init(s, n);
        }

        String(size_type n, char c) : storage_()
        {
            if (n <= kSSO)
            {
                if (n)
                    std::memset(storage_.small.data, c, n);
                set_small_size(n);
            }
            else
            {
                heap_alloc(n);
                std::memset(storage_.heap.data, c, n);
                set_heap_size(n);
            }
        }

        template <typename S,
                  typename = typename detail::enable_if<
                      !detail::is_same<typename detail::bare<S>::type, String>::value>::type,
                  typename = decltype(detail::declval<const S &>().data(),
                                      detail::declval<const S &>().size())>
        String(const S &s) : storage_()
        {
            init(s.data(), s.size());
        }

        String(const String &o) : storage_()
        {
            if (o.is_small())
            {
                std::memcpy(&storage_.small, &o.storage_.small, sizeof(storage_.small));
                return;
            }
            init(o.storage_.heap.data, o.storage_.heap.size);
        }

        String(String &&o) noexcept : storage_()
        {
            if (o.is_small())
                std::memcpy(&storage_.small, &o.storage_.small, sizeof(storage_.small));
            else
                storage_.heap = o.storage_.heap;
            o.set_small_size(0);
        }

        ~String()
        {
            release_heap();
        }

        String &operator=(const String &o)
        {
            if (this != &o)
                assign(o.data(), o.size());
            return *this;
        }

        String &operator=(String &&o) noexcept
        {
            if (this != &o)
            {
                release_heap();
                if (o.is_small())
                    std::memcpy(&storage_.small, &o.storage_.small, sizeof(storage_.small));
                else
                    storage_.heap = o.storage_.heap;
                o.set_small_size(0);
            }
            return *this;
        }

        String &operator=(const char *s)
        {
            assign(s, s ? std::strlen(s) : 0);
            return *this;
        }

        void assign(const char *p, size_type n)
        {
            if (n > max_size())
                detail::fatal("ct::String: tamanho invalido");
            if (n && !p)
                detail::fatal("ct::String: origem invalida");

            const char *source = p;
            const size_type source_offset = is_inside(p) ? static_cast<size_type>(p - data()) : npos;
            if (n <= capacity())
            {
                if (n)
                    std::memmove(data(), source, n);
                set_size(n);
                return;
            }

            char *new_data = static_cast<char *>(this->allocate(n + 1, alignof(char)));
            if (source_offset != npos)
                source = data() + source_offset;
            std::memcpy(new_data, source, n);
            new_data[n] = '\0';
            release_heap();
            storage_.heap.data = new_data;
            storage_.heap.size = n;
            set_heap_capacity(n);
        }

        char *data() noexcept { return is_small() ? storage_.small.data : storage_.heap.data; }
        const char *data() const noexcept
        {
            return is_small() ? storage_.small.data : storage_.heap.data;
        }
        const char *c_str() const noexcept { return data(); }

        size_type size() const noexcept { return is_small() ? small_size() : storage_.heap.size; }
        size_type length() const noexcept { return size(); }
        bool empty() const noexcept { return size() == 0; }
        size_type capacity() const noexcept { return is_small() ? kSSO : heap_capacity(); }
        size_type max_size() const noexcept
        {
            return (std::numeric_limits<size_type>::max)() - 1;
        }

        char &operator[](size_type i) { return data()[i]; }
        char operator[](size_type i) const { return data()[i]; }

        char &at(size_type i)
        {
            if (CT_UNLIKELY(i >= size()))
                detail::fatal("ct::String::at: index fora dos limites");
            return data()[i];
        }

        char front() const { return data()[0]; }
        char back() const { return data()[size() - 1]; }

        iterator begin() noexcept { return data(); }
        iterator end() noexcept { return data() + size(); }
        const_iterator begin() const noexcept { return data(); }
        const_iterator end() const noexcept { return data() + size(); }
        const_iterator cbegin() const noexcept { return data(); }
        const_iterator cend() const noexcept { return data() + size(); }

        void clear() noexcept { set_size(0); }

        void reserve(size_type n)
        {
            if (n > max_size())
                detail::fatal("ct::String: capacidade invalida");
            if (n > capacity())
                grow_to(n);
        }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
        void resize(size_type n, char c = '\0')
        {
            const size_type current = size();
            if (n > current)
            {
                reserve_for_append(n - current);
                std::memset(data() + current, c, n - current);
            }
            set_size(n);
        }

        void push_back(char c)
        {
            const size_type current = size();
            if (current == max_size())
                detail::fatal("ct::String: tamanho invalido");
            if (current == capacity())
                grow_to(current + 1);
            data()[current] = c;
            set_size(current + 1);
        }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

        void pop_back()
        {
            if (empty())
                detail::fatal("ct::String::pop_back: string vazia");
            set_size(size() - 1);
        }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#if __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif
#endif
        String &append(const char *p, size_type n)
        {
            if (n == 0)
                return *this;
            if (!p)
                detail::fatal("ct::String: origem invalida");

            const size_type current = size();
            size_type total = 0;
            if (!detail::checked_add(current, n, total) || total > max_size())
                detail::fatal("ct::String: tamanho invalido");

            const char *source = p;
            const size_type source_offset = is_inside(p) ? static_cast<size_type>(p - data()) : npos;
            if (total > capacity())
            {
                grow_to(total);
                if (source_offset != npos)
                    source = data() + source_offset;
            }
            std::memmove(data() + current, source, n);
            set_size(total);
            return *this;
        }

        String &append(const String &s) { return append(s.data(), s.size()); }
        String &append(const char *s) { return append(s, std::strlen(s)); }
        String &append(size_type n, char c)
        {
            if (n == 0)
                return *this;
            const size_type current = size();
            reserve_for_append(n);
            std::memset(data() + current, c, n);
            set_size(current + n);
            return *this;
        }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

        String &operator+=(const String &s) { return append(s); }
        String &operator+=(const char *s) { return append(s); }
        String &operator+=(char c)
        {
            push_back(c);
            return *this;
        }

        String &insert(size_type pos, const String &s)
        {
            return insert(pos, s.data(), s.size());
        }

        String &insert(size_type pos, const char *s)
        {
            return insert(pos, s, s ? std::strlen(s) : 0);
        }

        String &insert(size_type pos, const char *s, size_type n)
        {
            if (pos > size())
                detail::fatal("ct::String::insert: pos fora dos limites");
            if (n == 0)
                return *this;
            if (!s)
                detail::fatal("ct::String::insert: origem invalida");

            const size_type current = size();
            size_type total = 0;
            if (!detail::checked_add(current, n, total) || total > max_size())
                detail::fatal("ct::String: tamanho invalido");

            const size_type source_offset =
                is_inside(s) ? static_cast<size_type>(s - data()) : npos;
            if (total > capacity())
                grow_to(total);

            const char *source = s;
            if (source_offset != npos)
            {
                source = data() + source_offset;
                if (source_offset >= pos)
                    source += n;
            }

            std::memmove(data() + pos + n, data() + pos, current - pos);
            std::memmove(data() + pos, source, n);
            set_size(total);
            return *this;
        }

        String &insert(size_type pos, size_type n, char c)
        {
            if (pos > size())
                detail::fatal("ct::String::insert: pos fora dos limites");
            if (n == 0)
                return *this;

            const size_type current = size();
            reserve_for_append(n);
            std::memmove(data() + pos + n, data() + pos, current - pos);
            std::memset(data() + pos, c, n);
            set_size(current + n);
            return *this;
        }

        String &erase(size_type pos = 0, size_type n = npos)
        {
            const size_type current = size();
            if (pos > current)
                detail::fatal("ct::String::erase: pos fora dos limites");
            const size_type removed = n < current - pos ? n : current - pos;
            if (removed)
            {
                std::memmove(data() + pos, data() + pos + removed,
                             current - pos - removed);
                set_size(current - removed);
            }
            return *this;
        }

        iterator erase(iterator first, iterator last)
        {
            if (first < begin() || first > end() || last < first || last > end())
                detail::fatal("ct::String::erase: iterador fora dos limites");
            const size_type pos = static_cast<size_type>(first - begin());
            erase(pos, static_cast<size_type>(last - first));
            return begin() + pos;
        }

        String &append_number(long long v)
        {
            char tmp[24];
            char *end_ptr = tmp + sizeof(tmp);
            char *p = end_ptr;
            unsigned long long u = v < 0 ? 0ull - static_cast<unsigned long long>(v)
                                         : static_cast<unsigned long long>(v);
            p = write_digits(u, p);
            if (v < 0)
                *--p = '-';
            return append(p, static_cast<size_type>(end_ptr - p));
        }

        String &append_number(unsigned long long v)
        {
            char tmp[24];
            char *end_ptr = tmp + sizeof(tmp);
            char *p = write_digits(v, end_ptr);
            return append(p, static_cast<size_type>(end_ptr - p));
        }

        template <typename I,
                  typename = typename detail::enable_if<std::is_integral<I>::value>::type>
        String &append_number(I v)
        {
            return append_integral(v, detail::integral_constant<bool, std::is_signed<I>::value>{});
        }

        template <typename E,
                  typename = typename detail::enable_if<std::is_enum<E>::value>::type,
                  typename = void>
        String &append_number(E v)
        {
            return append_number(static_cast<typename std::underlying_type<E>::type>(v));
        }

        // O mesmo texto que snprintf("%.*g", precision, v) no locale "C" (sempre com '.'). De 1 a 17 algarismos
        // resolve-se sem o snprintf; fora disso, ou quando o arredondamento fica a menos de 2^-56 de um empate, e o
        // proprio snprintf que escreve.
        String &append_number(double v, int precision = 6)
        {
            char tmp[64];
            if (char *const e = detail::write_general(v, precision, tmp))
                return append(tmp, static_cast<size_type>(e - tmp));
            const int n = std::snprintf(tmp, sizeof(tmp), "%.*g", precision, v);
            if (n <= 0)
                return *this;
            const size_type written = static_cast<size_type>(n) < sizeof(tmp) - 1
                                          ? static_cast<size_type>(n)
                                          : sizeof(tmp) - 1;
            const char dp = detail::decimal_point();
            if (dp != '.')
                for (size_type i = 0; i < written; ++i)
                    if (tmp[i] == dp)
                        tmp[i] = '.';
            return append(tmp, written);
        }

        // O texto mais curto que volta exactamente ao mesmo valor (ver ct::write_shortest em numconv.hpp).
        String &append_number_shortest(double v)
        {
            char tmp[32];
            return append(tmp, static_cast<size_type>(write_shortest(v, tmp) - tmp));
        }
        String &append_number_shortest(float v)
        {
            char tmp[32];
            return append(tmp, static_cast<size_type>(write_shortest(v, tmp) - tmp));
        }

        static String number(long long v)
        {
            String s;
            s.append_number(v);
            return s;
        }
        template <typename I,
                  typename = typename detail::enable_if<std::is_integral<I>::value>::type>
        static String number(I v)
        {
            String s;
            s.append_number(v);
            return s;
        }
        template <typename E,
                  typename = typename detail::enable_if<std::is_enum<E>::value>::type,
                  typename = void>
        static String number(E v)
        {
            String s;
            s.append_number(v);
            return s;
        }
        static String number(unsigned long long v)
        {
            String s;
            s.append_number(v);
            return s;
        }
        static String number(double v, int precision = 6)
        {
            String s;
            s.append_number(v, precision);
            return s;
        }
        static String number_shortest(double v)
        {
            String s;
            s.append_number_shortest(v);
            return s;
        }
        static String number_shortest(float v)
        {
            String s;
            s.append_number_shortest(v);
            return s;
        }

        size_type find(char c, size_type pos = 0) const
        {
            if (pos >= size())
                return npos;
            const void *p = std::memchr(data() + pos, c, size() - pos);
            return p ? static_cast<size_type>(static_cast<const char *>(p) - data()) : npos;
        }

        size_type find(const char *needle, size_type pos = 0) const
        {
            return find_raw(needle, std::strlen(needle), pos);
        }
        size_type find(const String &needle, size_type pos = 0) const
        {
            return find_raw(needle.data(), needle.size(), pos);
        }

        size_type rfind(char c) const
        {
            for (size_type i = size(); i > 0; --i)
                if (data()[i - 1] == c)
                    return i - 1;
            return npos;
        }

        size_type find_first_of(const char *set, size_type pos = 0) const
        {
            if (!set)
                detail::fatal("ct::String::find_first_of: conjunto invalido");
            const CharSet chars(set);
            const char *d = data();
            const size_type n = size();
            for (size_type i = pos; i < n; ++i)
                if (chars.has(d[i]))
                    return i;
            return npos;
        }

        size_type find_first_not_of(const char *set, size_type pos = 0) const
        {
            if (!set)
                detail::fatal("ct::String::find_first_not_of: conjunto invalido");
            const CharSet chars(set);
            const char *d = data();
            const size_type n = size();
            for (size_type i = pos; i < n; ++i)
                if (!chars.has(d[i]))
                    return i;
            return npos;
        }

        // pos e a ultima posicao onde se procura (procura em [0, pos]), como o find_last_of da biblioteca standard;
        // npos, ou qualquer pos >= size(), procura em tudo.
        size_type find_last_of(const char *set, size_type pos = npos) const
        {
            if (!set)
                detail::fatal("ct::String::find_last_of: conjunto invalido");
            const CharSet chars(set);
            const char *d = data();
            const size_type count = pos < size() ? pos + 1 : size();
            for (size_type i = count; i > 0; --i)
                if (chars.has(d[i - 1]))
                    return i - 1;
            return npos;
        }

        bool contains(char c) const { return find(c) != npos; }
        bool contains(const char *s) const { return find(s) != npos; }
        bool contains(const String &s) const { return find(s) != npos; }

        bool starts_with(const char *p) const
        {
            const size_type n = std::strlen(p);
            return n <= size() && std::memcmp(data(), p, n) == 0;
        }
        bool starts_with(const String &p) const
        {
            return p.size() <= size() && std::memcmp(data(), p.data(), p.size()) == 0;
        }

        bool ends_with(const char *p) const
        {
            const size_type n = std::strlen(p);
            return n <= size() && std::memcmp(data() + size() - n, p, n) == 0;
        }
        bool ends_with(const String &p) const
        {
            return p.size() <= size() &&
                   std::memcmp(data() + size() - p.size(), p.data(), p.size()) == 0;
        }

        String substr(size_type pos, size_type n = npos) const
        {
            if (pos > size())
                detail::fatal("ct::String::substr: pos fora dos limites");
            const size_type available = size() - pos;
            return String(data() + pos, n < available ? n : available);
        }

        String trimmed() const
        {
            size_type begin_pos = 0;
            size_type end_pos = size();
            const char *d = data();
            while (begin_pos < end_pos && is_space(d[begin_pos]))
                ++begin_pos;
            while (end_pos > begin_pos && is_space(d[end_pos - 1]))
                --end_pos;
            return String(d + begin_pos, end_pos - begin_pos);
        }

        Vector<String> split(char sep, bool keep_empty = false) const
        {
            Vector<String> out;
            size_type start = 0;
            for (size_type i = 0; i <= size(); ++i)
            {
                if (i == size() || data()[i] == sep)
                {
                    if (i > start || keep_empty)
                        out.emplace_back(data() + start, i - start);
                    start = i + 1;
                }
            }
            return out;
        }

        int compare(const String &o) const
        {
            const size_type n = size() < o.size() ? size() : o.size();
            const int result = n ? std::memcmp(data(), o.data(), n) : 0;
            if (result)
                return result;
            return size() < o.size() ? -1 : (size() > o.size() ? 1 : 0);
        }

        int compare(size_type pos, size_type n, const String &o) const
        {
            if (pos > size())
                detail::fatal("ct::String::compare: pos fora dos limites");
            const size_type available = size() - pos;
            const size_type compared = n < available ? n : available;
            const size_type common = compared < o.size() ? compared : o.size();
            const int result = common ? std::memcmp(data() + pos, o.data(), common) : 0;
            if (result)
                return result;
            return compared < o.size() ? -1 : (compared > o.size() ? 1 : 0);
        }

        int compare(size_type pos, size_type n, const char *s) const
        {
            if (!s)
                detail::fatal("ct::String::compare: origem invalida");
            return compare(pos, n, String(s));
        }

        // Espacos iniciais, sinal e algarismos; o resto e ignorado. Um valor fora do intervalo do int fica no limite
        // (INT_MIN ou INT_MAX), e sem algarismos da 0. to_int(out) diz se o texto era mesmo um inteiro.
        int to_int() const
        {
            const char *p = data();
            const char *const end = p + size();
            while (p != end && is_space(*p))
                ++p;
            bool negative = false;
            if (p != end && (*p == '-' || *p == '+'))
                negative = *p++ == '-';
            std::uint64_t value;
            detail::scan_uint(p, end, value);
            return clamp_int(negative, value);
        }

        // Espacos, sinal, um ou mais algarismos e espacos: nada mais. Devolve false (e out fica como estava) se o texto
        // nao for isso ou o valor nao couber num int.
        bool to_int(int &out) const
        {
            const char *p = data();
            const char *const end = p + size();
            while (p != end && is_space(*p))
                ++p;
            bool negative = false;
            if (p != end && (*p == '-' || *p == '+'))
                negative = *p++ == '-';
            std::uint64_t value;
            const char *const digits_end = detail::scan_uint(p, end, value);
            if (digits_end == p)
                return false;
            p = digits_end;
            while (p != end && is_space(*p))
                ++p;
            if (p != end)
                return false;
            const std::uint64_t limit = negative ? std::uint64_t(2147483648u) : std::uint64_t(2147483647u);
            if (value > limit)
                return false;
            out = clamp_int(negative, value);
            return true;
        }

        // Como o strtof no locale "C" (espacos iniciais, sinal, digitos com ponto e expoente, inf, nan, e
        // hexadecimal), mas arredondado correctamente e sem depender do locale: o separador e sempre '.'.
        float to_float() const
        {
            const char *p = data();
            const char *const end = p + size();
            while (p != end && is_space(*p))
                ++p;
            float f;
            parse_float(p, end, f);
            return f;
        }

        double to_double() const
        {
            const char *p = data();
            const char *const end = p + size();
            while (p != end && is_space(*p))
                ++p;
            double d;
            parse_double(p, end, d);
            return d;
        }

        template <typename S>
        S to() const
        {
            return S(data(), size());
        }

        std::uint64_t hash() const noexcept { return detail::hash_bytes(data(), size()); }

        void swap(String &o) noexcept
        {
            if (this == &o)
                return;
            String temporary(detail::move(o));
            o = detail::move(*this);
            *this = detail::move(temporary);
        }

        bool is_small() const noexcept
        {
            return (static_cast<unsigned char>(storage_.small.data[kSSO]) & 0x80) == 0;
        }

    private:
        Storage storage_;

        // Escreve v em decimal, acabando em end, e devolve o primeiro caracter: dois algarismos por passo.
        static char *write_digits(unsigned long long v, char *end) noexcept
        {
            static const char pairs[] =
                "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
                "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
                "8081828384858687888990919293949596979899";
            char *p = end;
            while (v >= 100)
            {
                const unsigned r = static_cast<unsigned>(v % 100);
                v /= 100;
                p -= 2;
                p[0] = pairs[2 * r];
                p[1] = pairs[2 * r + 1];
            }
            if (v >= 10)
            {
                p -= 2;
                p[0] = pairs[2 * v];
                p[1] = pairs[2 * v + 1];
            }
            else
                *--p = static_cast<char>('0' + v);
            return p;
        }

        static bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        static int clamp_int(bool negative, std::uint64_t value) noexcept
        {
            if (negative)
                return value >= std::uint64_t(2147483648u) ? (std::numeric_limits<int>::min)()
                                                           : static_cast<int>(0 - static_cast<long long>(value));
            return value >= std::uint64_t(2147483647u) ? (std::numeric_limits<int>::max)() : static_cast<int>(value);
        }

        // O conjunto como 256 bits: um teste por caracter em vez de um strchr. O NUL nunca esta (o conjunto acaba nele).
        struct CharSet
        {
            std::uint64_t bits[4];

            explicit CharSet(const char *set)
            {
                bits[0] = bits[1] = bits[2] = bits[3] = 0;
                for (; *set; ++set)
                {
                    const unsigned char c = static_cast<unsigned char>(*set);
                    bits[c >> 6] |= std::uint64_t(1) << (c & 63);
                }
            }
            bool has(char ch) const
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                return ((bits[c >> 6] >> (c & 63)) & 1) != 0;
            }
        };

        template <typename I>
        String &append_integral(I v, detail::true_type)
        {
            return append_number(static_cast<long long>(v));
        }

        template <typename I>
        String &append_integral(I v, detail::false_type)
        {
            return append_number(static_cast<unsigned long long>(v));
        }

        size_type small_size() const noexcept
        {
            return kSSO - static_cast<unsigned char>(storage_.small.data[kSSO]);
        }

        size_type heap_capacity() const noexcept { return storage_.heap.capacity & ~kFlag; }
        void set_heap_capacity(size_type n) noexcept { storage_.heap.capacity = n | kFlag; }

        bool is_inside(const char *p) const noexcept
        {
            if (!p)
                return false;
            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(data());
            const std::uintptr_t end = begin + size();
            const std::uintptr_t source = reinterpret_cast<std::uintptr_t>(p);
            return source >= begin && source <= end;
        }

        void init(const char *p, size_type n)
        {
            if (n > max_size())
                detail::fatal("ct::String: tamanho invalido");
            if (n && !p)
                detail::fatal("ct::String: origem invalida");
            if (n <= kSSO)
            {
                if (n)
                    std::memcpy(storage_.small.data, p, n);
                set_small_size(n);
            }
            else
            {
                heap_alloc(n);
                std::memcpy(storage_.heap.data, p, n);
                set_heap_size(n);
            }
        }

        void heap_alloc(size_type capacity)
        {
            size_type bytes = 0;
            if (!detail::checked_add(capacity, 1, bytes))
                detail::fatal("ct::String: tamanho invalido");
            storage_.heap.data = static_cast<char *>(this->allocate(bytes, alignof(char)));
            set_heap_capacity(capacity);
        }

        void release_heap() noexcept
        {
            if (!is_small())
                this->deallocate(storage_.heap.data, heap_capacity() + 1);
        }

        void set_small_size(size_type n) noexcept
        {
            if (n > kSSO)
                detail::fatal("ct::String: tamanho SSO invalido");
            storage_.small.data[n] = '\0';
            storage_.small.data[kSSO] = static_cast<char>(kSSO - n);
        }

        void set_heap_size(size_type n) noexcept
        {
            storage_.heap.size = n;
            storage_.heap.data[n] = '\0';
        }

        void set_size(size_type n) noexcept
        {
            if (is_small())
                set_small_size(n);
            else
                set_heap_size(n);
        }

        void reserve_for_append(size_type extra)
        {
            size_type required = 0;
            if (!detail::checked_add(size(), extra, required) || required > max_size())
                detail::fatal("ct::String: tamanho invalido");
            if (required > capacity())
                grow_to(required);
        }

        CT_NOINLINE void grow_to(size_type required)
        {
            if (required <= capacity())
                return;
            size_type target = capacity();
            if (target < kSSO)
                target = kSSO;
            if (target > max_size() / 2)
                target = required;
            else
                target *= 2;
            if (target < required)
                target = required;
            if (target > max_size())
                detail::fatal("ct::String: capacidade invalida");

            const size_type current = size();
            if (is_small())
            {
                size_type bytes = 0;
                if (!detail::checked_add(target, 1, bytes))
                    detail::fatal("ct::String: tamanho invalido");
                char *new_data = static_cast<char *>(this->allocate(bytes, alignof(char)));
                std::memcpy(new_data, storage_.small.data, current);
                new_data[current] = '\0';
                storage_.heap.data = new_data;
                storage_.heap.size = current;
                set_heap_capacity(target);
            }
            else
            {
                size_type old_bytes = 0;
                size_type new_bytes = 0;
                if (!detail::checked_add(heap_capacity(), 1, old_bytes) ||
                    !detail::checked_add(target, 1, new_bytes))
                    detail::fatal("ct::String: tamanho invalido");
                storage_.heap.data = static_cast<char *>(
                    this->reallocate(storage_.heap.data, old_bytes, new_bytes, alignof(char)));
                set_heap_capacity(target);
            }
        }

        size_type find_raw(const char *needle, size_type n, size_type pos) const
        {
            if (n == 0)
                return pos <= size() ? pos : npos;
            if (pos > size() || n > size() - pos)
                return npos;
            const char *d = data();
            const char *last = d + size() - n;
            for (const char *p = d + pos; p <= last; ++p)
            {
                p = static_cast<const char *>(
                    std::memchr(p, needle[0], static_cast<size_type>(last - p) + 1));
                if (!p)
                    return npos;
                if (std::memcmp(p, needle, n) == 0)
                    return static_cast<size_type>(p - d);
            }
            return npos;
        }
    };

    inline bool operator==(const String &a, const String &b)
    {
        return a.size() == b.size() &&
               (a.size() == 0 || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }
    inline bool operator==(const String &a, const char *b)
    {
        const std::size_t n = std::strlen(b);
        return a.size() == n && (n == 0 || std::memcmp(a.data(), b, n) == 0);
    }
    inline bool operator==(const char *a, const String &b) { return b == a; }
    inline bool operator!=(const String &a, const String &b) { return !(a == b); }
    inline bool operator!=(const String &a, const char *b) { return !(a == b); }
    inline bool operator!=(const char *a, const String &b) { return !(b == a); }
    inline bool operator<(const String &a, const String &b) { return a.compare(b) < 0; }
    inline bool operator>(const String &a, const String &b) { return a.compare(b) > 0; }
    inline bool operator<=(const String &a, const String &b) { return a.compare(b) <= 0; }
    inline bool operator>=(const String &a, const String &b) { return a.compare(b) >= 0; }

    inline String operator+(const String &a, const String &b)
    {
        String result;
        std::size_t total = 0;
        if (!detail::checked_add(a.size(), b.size(), total))
            detail::fatal("ct::String: tamanho invalido");
        result.reserve(total);
        result.append(a);
        result.append(b);
        return result;
    }
    inline String operator+(const String &a, const char *b)
    {
        String result;
        const std::size_t n = std::strlen(b);
        std::size_t total = 0;
        if (!detail::checked_add(a.size(), n, total))
            detail::fatal("ct::String: tamanho invalido");
        result.reserve(total);
        result.append(a);
        result.append(b, n);
        return result;
    }
    inline String operator+(const char *a, const String &b)
    {
        String result;
        const std::size_t n = std::strlen(a);
        std::size_t total = 0;
        if (!detail::checked_add(n, b.size(), total))
            detail::fatal("ct::String: tamanho invalido");
        result.reserve(total);
        result.append(a, n);
        result.append(b);
        return result;
    }
    inline String operator+(const String &a, char c)
    {
        String result(a);
        result.push_back(c);
        return result;
    }

    inline void swap(String &a, String &b) noexcept { a.swap(b); }

    namespace detail
    {
        // So e dona de bytes (char), que nao guardam objectos: nao pode ser dona de um Variant.
        template <>
        struct variant_leaf<String> : true_type
        {
        };
        // O operator=(String &&) solta o bloco que tinha e fica com o da origem: e destruir e construir por move.
        template <>
        struct variant_move_rebuilds<String> : true_type
        {
        };
    }

    template <typename OS>
    OS &operator<<(OS &os, const String &s)
    {
        os.write(s.data(), static_cast<std::ptrdiff_t>(s.size()));
        return os;
    }

} 