#pragma once

#include "detail/unicode_tables.hpp"
#include "detail/utils.hpp"
#include "vector.hpp"

namespace ct
{
    namespace unicode
    {
        enum Form
        {
            NFC = 0,
            NFD = 1,
            NFKC = 2,
            NFKD = 3
        };

        namespace impl
        {
            typedef ::ct::detail::uni::Tables<> UT;
            namespace uni = ::ct::detail::uni;

            enum : std::uint32_t
            {
                kCccMask = 0xFF,
                kNfdNo = 1u << 8,
                kNfkdNo = 1u << 9,
                kNfcNo = 1u << 10,
                kNfcMaybe = 1u << 11,
                kNfkcNo = 1u << 12,
                kNfkcMaybe = 1u << 13,
                kCccShift = 21,
                kCpMask = (1u << 21) - 1,
                kSBase = 0xAC00,
                kLBase = 0x1100,
                kVBase = 0x1161,
                kTBase = 0x11A7,
                kLCount = 19,
                kVCount = 21,
                kTCount = 28,
                kNCount = kVCount * kTCount,
                kSCount = kLCount * kNCount
            };

            inline std::size_t count_le(const std::uint32_t *a, std::size_t n, std::uint32_t v) noexcept
            {
                std::size_t lo = 0, hi = n;
                while (lo < hi)
                {
                    std::size_t mid = (lo + hi) >> 1;
                    if (a[mid] <= v)
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                return lo;
            }

            inline bool case_map(const uni::CaseRange *t, std::size_t n, std::uint32_t cp, std::uint32_t &out) noexcept
            {
                std::size_t lo = 0, hi = n;
                while (lo < hi)
                {
                    std::size_t mid = (lo + hi) >> 1;
                    if (t[mid].lo <= cp)
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                if (lo == 0)
                    return false;
                const uni::CaseRange &c = t[lo - 1];
                if (cp > c.hi || (c.step == 2 && ((cp - c.lo) & 1u)))
                    return false;
                out = static_cast<std::uint32_t>(static_cast<std::int64_t>(cp) + c.delta);
                return true;
            }

            inline bool bin_has(std::size_t idx, std::uint32_t cp) noexcept
            {
                const std::uint32_t *pts = UT::kBinPts + UT::kBinOff[idx];
                return count_le(pts, UT::kBinOff[idx + 1] - UT::kBinOff[idx], cp) & 1u;
            }

            inline std::uint32_t nf_prop(std::uint32_t cp) noexcept
            {
                if (cp < 0xA0)
                    return 0;
                return UT::kNfVal[count_le(UT::kNfStart, uni::kNfRuns, cp) - 1];
            }

            inline std::uint32_t nf_no_bits(Form f) noexcept
            {
                return f == NFC ? kNfcNo : f == NFD ? kNfdNo : f == NFKC ? kNfkcNo : kNfkdNo;
            }

            inline std::uint32_t nf_maybe_bits(Form f) noexcept
            {
                return f == NFC ? kNfcMaybe : f == NFKC ? kNfkcMaybe : 0;
            }

            inline std::uint32_t next_cp(const char16_t *s, std::size_t n, std::size_t &i) noexcept
            {
                std::uint32_t c = s[i++];
                if (c >= 0xD800 && c < 0xDC00 && i < n && s[i] >= 0xDC00 && s[i] < 0xE000)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<std::uint32_t>(s[i]) - 0xDC00);
                    ++i;
                }
                return c;
            }

            inline std::uint32_t next_cp(const unsigned char *s, std::size_t, std::size_t &i) noexcept
            {
                return s[i++];
            }

            inline std::uint32_t prev_cp(const char16_t *s, std::size_t &i) noexcept
            {
                std::uint32_t c = s[--i];
                if (c >= 0xDC00 && c < 0xE000 && i > 0 && s[i - 1] >= 0xD800 && s[i - 1] < 0xDC00)
                {
                    c = 0x10000 + ((static_cast<std::uint32_t>(s[i - 1]) - 0xD800) << 10) + (c - 0xDC00);
                    --i;
                }
                return c;
            }

            inline void put_cp(Vector<char16_t> &out, std::uint32_t cp)
            {
                if (cp < 0x10000)
                    out.push_back(static_cast<char16_t>(cp));
                else
                {
                    cp -= 0x10000;
                    out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                    out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
                }
            }

            inline void append_units(Vector<char16_t> &out, const char16_t *s, std::size_t n)
            {
                std::size_t k = out.size();
                out.resize(k + n);
                if (n)
                    std::memcpy(out.data() + k, s, n * sizeof(char16_t));
            }

            inline void append_units(Vector<char16_t> &out, const unsigned char *s, std::size_t n)
            {
                std::size_t k = out.size();
                out.resize(k + n);
                for (std::size_t i = 0; i < n; ++i)
                    out[k + i] = s[i];
            }

            inline bool find_exact(const std::uint32_t *a, std::size_t n, std::uint32_t v, std::size_t &idx) noexcept
            {
                std::size_t c = count_le(a, n, v);
                if (c == 0 || a[c - 1] != v)
                    return false;
                idx = c - 1;
                return true;
            }

            inline void push_decomp(Vector<std::uint32_t> &out, std::uint32_t cp, bool compat)
            {
                if (cp >= kSBase && cp < kSBase + kSCount)
                {
                    std::uint32_t si = cp - kSBase;
                    std::uint32_t l = kLBase + si / kNCount;
                    std::uint32_t v = kVBase + (si % kNCount) / kTCount;
                    std::uint32_t t = kTBase + si % kTCount;
                    out.push_back(l);
                    out.push_back(v);
                    if (si % kTCount)
                        out.push_back(t);
                    return;
                }
                std::size_t idx;
                if (cp >= 0xA0 && find_exact(UT::kDecCp, uni::kDecCount, cp, idx))
                {
                    std::uint32_t val = UT::kDecVal[idx];
                    if (compat || !(val & 1u))
                    {
                        std::size_t off = val >> 1, end = UT::kDecVal[idx + 1] >> 1;
                        for (; off < end; ++off)
                            push_decomp(out, UT::kDecPool[off], compat);
                        return;
                    }
                }
                out.push_back(cp | ((nf_prop(cp) & kCccMask) << kCccShift));
            }

            inline void reorder(std::uint32_t *a, std::size_t n) noexcept
            {
                for (std::size_t i = 1; i < n; ++i)
                {
                    std::uint32_t v = a[i];
                    std::uint32_t cc = v >> kCccShift;
                    if (cc == 0)
                        continue;
                    std::size_t j = i;
                    while (j > 0)
                    {
                        std::uint32_t pc = a[j - 1] >> kCccShift;
                        if (pc <= cc)
                            break;
                        a[j] = a[j - 1];
                        --j;
                    }
                    a[j] = v;
                }
            }

            inline bool compose_pair(std::uint32_t a, std::uint32_t b, std::uint32_t &out) noexcept
            {
                if (a >= kLBase && a < kLBase + kLCount && b >= kVBase && b < kVBase + kVCount)
                {
                    out = kSBase + ((a - kLBase) * kVCount + (b - kVBase)) * kTCount;
                    return true;
                }
                if (a >= kSBase && a < kSBase + kSCount && (a - kSBase) % kTCount == 0 && b > kTBase && b < kTBase + kTCount)
                {
                    out = a + (b - kTBase);
                    return true;
                }
                std::size_t lo = 0, hi = uni::kCompCount;
                while (lo < hi)
                {
                    std::size_t mid = (lo + hi) >> 1;
                    const uni::CompPair &p = UT::kComp[mid];
                    if (p.a < a || (p.a == a && p.b < b))
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                if (lo < uni::kCompCount && UT::kComp[lo].a == a && UT::kComp[lo].b == b)
                {
                    out = UT::kComp[lo].c;
                    return true;
                }
                return false;
            }

            inline std::size_t compose(std::uint32_t *a, std::size_t n) noexcept
            {
                if (n == 0)
                    return 0;
                std::size_t starter = 0, w = 1;
                std::uint32_t starter_cp = a[0] & kCpMask;
                std::uint32_t last = a[0] >> kCccShift;
                if (last != 0)
                    last = 256;
                for (std::size_t r = 1; r < n; ++r)
                {
                    std::uint32_t cp = a[r] & kCpMask;
                    std::uint32_t cc = a[r] >> kCccShift;
                    std::uint32_t comp;
                    if ((last < cc || last == 0) && compose_pair(starter_cp, cp, comp))
                    {
                        a[starter] = comp;
                        starter_cp = comp;
                    }
                    else
                    {
                        if (cc == 0)
                        {
                            starter = w;
                            starter_cp = cp;
                        }
                        last = cc;
                        a[w++] = a[r];
                    }
                }
                return w;
            }

            template <typename U>
            std::size_t find_bad(Form f, const U *s, std::size_t n, std::size_t i, bool &definite) noexcept
            {
                std::uint32_t no = nf_no_bits(f), maybe = nf_maybe_bits(f);
                std::uint32_t prev = 0;
                while (i < n)
                {
                    if (s[i] < 0x80)
                    {
                        prev = 0;
                        ++i;
                        continue;
                    }
                    std::size_t at = i;
                    std::uint32_t p = nf_prop(next_cp(s, n, i));
                    std::uint32_t cc = p & kCccMask;
                    if ((p & no) || (cc != 0 && prev > cc))
                    {
                        definite = true;
                        return at;
                    }
                    if (p & maybe)
                    {
                        definite = false;
                        return at;
                    }
                    prev = cc;
                }
                return n;
            }

            inline std::size_t seg_start(Form f, const char16_t *s, std::size_t floor, std::size_t bad) noexcept
            {
                std::uint32_t mask = nf_no_bits(f) | nf_maybe_bits(f);
                std::size_t i = bad;
                while (i > floor)
                {
                    std::uint32_t p = nf_prop(prev_cp(s, i));
                    if ((p & kCccMask) == 0 && !(p & mask))
                        break;
                }
                return i;
            }

            inline std::size_t seg_start(Form f, const unsigned char *s, std::size_t floor, std::size_t bad) noexcept
            {
                std::uint32_t mask = nf_no_bits(f) | nf_maybe_bits(f);
                std::size_t i = bad;
                while (i > floor)
                {
                    std::uint32_t p = nf_prop(s[--i]);
                    if ((p & kCccMask) == 0 && !(p & mask))
                        break;
                }
                return i;
            }

            template <typename U>
            std::size_t seg_end(Form f, const U *s, std::size_t n, std::size_t bad) noexcept
            {
                std::uint32_t mask = nf_no_bits(f) | nf_maybe_bits(f);
                std::size_t i = bad;
                next_cp(s, n, i);
                while (i < n)
                {
                    std::size_t at = i;
                    std::uint32_t p = nf_prop(next_cp(s, n, i));
                    if ((p & kCccMask) == 0 && !(p & mask))
                        return at;
                }
                return n;
            }

            template <typename U>
            void norm_segment(Form f, const U *s, std::size_t b, std::size_t e, Vector<std::uint32_t> &tmp, Vector<char16_t> &out)
            {
                tmp.clear();
                bool compat = f == NFKC || f == NFKD;
                std::size_t i = b;
                while (i < e)
                    push_decomp(tmp, next_cp(s, e, i), compat);
                reorder(tmp.data(), tmp.size());
                std::size_t m = tmp.size();
                if (f == NFC || f == NFKC)
                    m = compose(tmp.data(), m);
                for (std::size_t k = 0; k < m; ++k)
                    put_cp(out, tmp[k] & kCpMask);
            }

            template <typename U>
            bool same_units(const U *a, const char16_t *b, std::size_t n) noexcept
            {
                for (std::size_t i = 0; i < n; ++i)
                    if (static_cast<char16_t>(a[i]) != b[i])
                        return false;
                return true;
            }

            template <typename U>
            bool is_normalized_impl(Form f, const U *s, std::size_t n)
            {
                Vector<std::uint32_t> tmp;
                Vector<char16_t> out;
                std::size_t i = 0;
                for (;;)
                {
                    bool definite = false;
                    std::size_t bad = find_bad(f, s, n, i, definite);
                    if (bad == n)
                        return true;
                    if (definite)
                        return false;
                    std::size_t b = seg_start(f, s, i, bad);
                    std::size_t e = seg_end(f, s, n, bad);
                    out.clear();
                    norm_segment(f, s, b, e, tmp, out);
                    if (out.size() != e - b || !same_units(s + b, out.data(), e - b))
                        return false;
                    i = e;
                }
            }

            template <typename U>
            void normalize_impl(Form f, const U *s, std::size_t n, Vector<char16_t> &out)
            {
                out.clear();
                out.reserve(n);
                Vector<std::uint32_t> tmp;
                std::size_t i = 0;
                while (i < n)
                {
                    bool definite = false;
                    std::size_t bad = find_bad(f, s, n, i, definite);
                    if (bad == n)
                    {
                        append_units(out, s + i, n - i);
                        return;
                    }
                    std::size_t b = seg_start(f, s, i, bad);
                    std::size_t e = seg_end(f, s, n, bad);
                    append_units(out, s + i, b - i);
                    norm_segment(f, s, b, e, tmp, out);
                    i = e;
                }
            }

            inline void upper_cp(Vector<char16_t> &out, std::uint32_t cp)
            {
                std::uint32_t m;
                if (cp < 0xB5 || cp > 0x1E943)
                {
                    put_cp(out, cp);
                    return;
                }
                if (case_map(UT::kUpper, uni::kUpperRanges, cp, m))
                {
                    put_cp(out, m);
                    return;
                }
                std::size_t idx;
                if (find_exact(UT::kSpUpCp, uni::kSpecialUpper, cp, idx))
                {
                    for (std::size_t k = UT::kSpUpOff[idx]; k < UT::kSpUpOff[idx + 1]; ++k)
                        put_cp(out, UT::kSpPool[k]);
                    return;
                }
                put_cp(out, cp);
            }

            inline bool is_case_ignorable(std::uint32_t cp) noexcept
            {
                return bin_has(uni::kBinCaseIgnorable, cp);
            }

            inline bool is_cased(std::uint32_t cp) noexcept
            {
                return bin_has(uni::kBinCased, cp);
            }

            inline bool final_sigma(const char16_t *s, std::size_t n, std::size_t before, std::size_t after) noexcept
            {
                bool found = false;
                std::size_t i = before;
                while (i > 0)
                {
                    std::uint32_t c = prev_cp(s, i);
                    if (is_case_ignorable(c))
                        continue;
                    found = is_cased(c);
                    break;
                }
                if (!found)
                    return false;
                i = after;
                while (i < n)
                {
                    std::uint32_t c = next_cp(s, n, i);
                    if (is_case_ignorable(c))
                        continue;
                    return !is_cased(c);
                }
                return true;
            }

            inline void widen(const Vector<unsigned char> &in, Vector<char16_t> &out)
            {
                out.clear();
                out.reserve(in.size() + 8);
                for (std::size_t i = 0; i < in.size(); ++i)
                    out.push_back(in[i]);
            }
        }

        inline std::uint32_t simple_upper(std::uint32_t cp) noexcept
        {
            if (cp < 128)
                return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
            std::uint32_t m;
            return impl::case_map(impl::UT::kUpper, impl::uni::kUpperRanges, cp, m) ? m : cp;
        }

        inline std::uint32_t simple_lower(std::uint32_t cp) noexcept
        {
            if (cp < 128)
                return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
            std::uint32_t m;
            return impl::case_map(impl::UT::kLower, impl::uni::kLowerRanges, cp, m) ? m : cp;
        }

        inline std::uint32_t simple_fold(std::uint32_t cp) noexcept
        {
            if (cp < 128)
                return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
            std::uint32_t m;
            return impl::case_map(impl::UT::kFold, impl::uni::kFoldRanges, cp, m) ? m : cp;
        }

        inline unsigned combining_class(std::uint32_t cp) noexcept
        {
            return impl::nf_prop(cp) & impl::kCccMask;
        }

        inline bool is_cased(std::uint32_t cp) noexcept { return impl::is_cased(cp); }

        inline bool is_case_ignorable(std::uint32_t cp) noexcept { return impl::is_case_ignorable(cp); }

        inline void to_upper(const char16_t *s, std::size_t n, Vector<char16_t> &out)
        {
            out.clear();
            out.reserve(n);
            std::size_t i = 0;
            while (i < n)
            {
                char16_t c = s[i];
                if (c < 0x80)
                {
                    out.push_back((c >= 'a' && c <= 'z') ? static_cast<char16_t>(c - 32) : c);
                    ++i;
                    continue;
                }
                impl::upper_cp(out, impl::next_cp(s, n, i));
            }
        }

        inline void to_lower(const char16_t *s, std::size_t n, Vector<char16_t> &out)
        {
            out.clear();
            out.reserve(n);
            std::size_t i = 0;
            while (i < n)
            {
                char16_t c = s[i];
                if (c < 0x80)
                {
                    out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char16_t>(c + 32) : c);
                    ++i;
                    continue;
                }
                std::size_t at = i;
                std::uint32_t cp = impl::next_cp(s, n, i);
                if (cp < 0xC0 || cp > 0x1E921)
                {
                    impl::put_cp(out, cp);
                    continue;
                }
                if (cp == 0x3A3)
                {
                    out.push_back(impl::final_sigma(s, n, at, i) ? 0x3C2 : 0x3C3);
                    continue;
                }
                std::uint32_t m;
                if (impl::case_map(impl::UT::kLower, impl::uni::kLowerRanges, cp, m))
                {
                    impl::put_cp(out, m);
                    continue;
                }
                std::size_t idx;
                if (impl::find_exact(impl::UT::kSpLoCp, impl::uni::kSpecialLower, cp, idx))
                {
                    for (std::size_t k = impl::UT::kSpLoOff[idx]; k < impl::UT::kSpLoOff[idx + 1]; ++k)
                        impl::put_cp(out, impl::UT::kSpPool[k]);
                    continue;
                }
                impl::put_cp(out, cp);
            }
        }

        inline bool to_upper(const unsigned char *s, std::size_t n, Vector<unsigned char> &out8, Vector<char16_t> &out16)
        {
            out8.clear();
            out8.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                unsigned char c = s[i];
                if (c < 0x80)
                    out8.push_back((c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 32) : c);
                else if (c == 0xDF)
                {
                    out8.push_back('S');
                    out8.push_back('S');
                }
                else if (c == 0xB5 || c == 0xFF)
                {
                    impl::widen(out8, out16);
                    for (; i < n; ++i)
                    {
                        c = s[i];
                        if (c < 0x80)
                            out16.push_back((c >= 'a' && c <= 'z') ? static_cast<char16_t>(c - 32) : c);
                        else if (c == 0xDF)
                        {
                            out16.push_back('S');
                            out16.push_back('S');
                        }
                        else if (c == 0xB5)
                            out16.push_back(0x39C);
                        else if (c == 0xFF)
                            out16.push_back(0x178);
                        else if (c >= 0xE0 && c != 0xF7)
                            out16.push_back(static_cast<char16_t>(c - 32));
                        else
                            out16.push_back(c);
                    }
                    return true;
                }
                else if (c >= 0xE0 && c != 0xF7)
                    out8.push_back(static_cast<unsigned char>(c - 32));
                else
                    out8.push_back(c);
            }
            return false;
        }

        inline void to_lower(const unsigned char *s, std::size_t n, Vector<unsigned char> &out8)
        {
            out8.clear();
            out8.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                unsigned char c = s[i];
                if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
                    c = static_cast<unsigned char>(c + 32);
                out8.push_back(c);
            }
        }

        inline bool is_normalized(Form f, const char16_t *s, std::size_t n)
        {
            return impl::is_normalized_impl(f, s, n);
        }

        inline bool is_normalized(Form f, const unsigned char *s, std::size_t n)
        {
            if (f == NFC)
                return true;
            return impl::is_normalized_impl(f, s, n);
        }

        inline void normalize(Form f, const char16_t *s, std::size_t n, Vector<char16_t> &out)
        {
            impl::normalize_impl(f, s, n, out);
        }

        inline bool normalize(Form f, const unsigned char *s, std::size_t n, Vector<unsigned char> &out8, Vector<char16_t> &out16)
        {
            out8.clear();
            if (impl::is_normalized_impl(f, s, n))
            {
                out8.resize(n);
                if (n)
                    std::memcpy(out8.data(), s, n);
                return false;
            }
            impl::normalize_impl(f, s, n, out16);
            for (std::size_t i = 0; i < out16.size(); ++i)
                if (out16[i] > 0xFF)
                    return true;
            out8.resize(out16.size());
            for (std::size_t i = 0; i < out16.size(); ++i)
                out8[i] = static_cast<unsigned char>(out16[i]);
            out16.clear();
            return false;
        }
    }
}
