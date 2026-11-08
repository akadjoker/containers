#pragma once

#include "detail/unicode_tables.hpp"
#include "detail/utils.hpp"
#include "sort.hpp"
#include "span.hpp"
#include "string.hpp"
#include "unicode.hpp"
#include "vector.hpp"

namespace ct
{
    namespace detail
    {
        namespace jsre
        {
            constexpr std::size_t npos = static_cast<std::size_t>(-1);
            constexpr std::uint32_t kNone = 0xFFFFFFFFu;
            constexpr std::uint32_t kMaxCp = 0x10FFFF;
            constexpr std::int32_t kSat = 1 << 24;
            constexpr int kMaxDepth = 600;

            typedef uni::Tables<> UT;

            enum Flag : unsigned
            {
                F_Global = 1,
                F_IgnoreCase = 2,
                F_Multiline = 4,
                F_DotAll = 8,
                F_Unicode = 16,
                F_UnicodeSets = 32,
                F_Sticky = 64,
                F_HasIndices = 128
            };

            inline int hex_value(unsigned char c) noexcept
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            }

            inline std::size_t utf8_decode(const char *s, std::size_t n, std::size_t i, std::uint32_t &cp) noexcept
            {
                unsigned char c = static_cast<unsigned char>(s[i]);
                std::size_t len = 1;
                std::uint32_t v = c;
                if (c >= 0xC0 && c < 0xF8)
                {
                    len = c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4);
                    v = c & (0x3Fu >> (len - 1));
                    if (i + len > n)
                        len = 1;
                    else
                        for (std::size_t k = 1; k < len; ++k)
                        {
                            unsigned char d = static_cast<unsigned char>(s[i + k]);
                            if ((d & 0xC0) != 0x80)
                            {
                                len = 1;
                                break;
                            }
                            v = (v << 6) | (d & 0x3F);
                        }
                }
                cp = len == 1 ? c : v;
                return len;
            }

            inline std::size_t utf8_encode(std::uint32_t cp, char out[4]) noexcept
            {
                if (cp < 0x80)
                {
                    out[0] = static_cast<char>(cp);
                    return 1;
                }
                if (cp < 0x800)
                {
                    out[0] = static_cast<char>(0xC0 | (cp >> 6));
                    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
                    return 2;
                }
                if (cp < 0x10000)
                {
                    out[0] = static_cast<char>(0xE0 | (cp >> 12));
                    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
                    return 2 + 1;
                }
                out[0] = static_cast<char>(0xF0 | (cp >> 18));
                out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out[3] = static_cast<char>(0x80 | (cp & 0x3F));
                return 4;
            }

            struct Range
            {
                std::uint32_t lo;
                std::uint32_t hi;
            };

            class CharSet
            {
            public:
                Vector<Range> r;

                void add(std::uint32_t lo, std::uint32_t hi)
                {
                    if (lo <= hi)
                        r.push_back(Range{lo, hi});
                }

                void add(const CharSet &o)
                {
                    for (std::size_t i = 0; i < o.r.size(); ++i)
                        r.push_back(o.r[i]);
                }

                void norm()
                {
                    if (r.size() < 2)
                        return;
                    ct::sort(r.begin(), r.end(), [](const Range &a, const Range &b) { return a.lo < b.lo; });
                    std::size_t w = 0;
                    for (std::size_t i = 1; i < r.size(); ++i)
                    {
                        if (r[i].lo <= r[w].hi + 1u)
                        {
                            if (r[i].hi > r[w].hi)
                                r[w].hi = r[i].hi;
                        }
                        else
                            r[++w] = r[i];
                    }
                    r.resize(w + 1);
                }

                bool contains(std::uint32_t cp) const noexcept
                {
                    std::size_t lo = 0, hi = r.size();
                    while (lo < hi)
                    {
                        std::size_t mid = (lo + hi) >> 1;
                        if (r[mid].lo <= cp)
                            lo = mid + 1;
                        else
                            hi = mid;
                    }
                    return lo > 0 && cp <= r[lo - 1].hi;
                }

                bool empty() const noexcept { return r.empty(); }

                CharSet complement() const
                {
                    CharSet out;
                    std::uint32_t next = 0;
                    for (std::size_t i = 0; i < r.size(); ++i)
                    {
                        if (r[i].lo > next)
                            out.add(next, r[i].lo - 1);
                        next = r[i].hi + 1;
                    }
                    if (next <= kMaxCp)
                        out.add(next, kMaxCp);
                    return out;
                }

                static CharSet intersect(const CharSet &a, const CharSet &b)
                {
                    CharSet out;
                    std::size_t i = 0, j = 0;
                    while (i < a.r.size() && j < b.r.size())
                    {
                        std::uint32_t lo = a.r[i].lo > b.r[j].lo ? a.r[i].lo : b.r[j].lo;
                        std::uint32_t hi = a.r[i].hi < b.r[j].hi ? a.r[i].hi : b.r[j].hi;
                        if (lo <= hi)
                            out.add(lo, hi);
                        if (a.r[i].hi < b.r[j].hi)
                            ++i;
                        else
                            ++j;
                    }
                    return out;
                }

                static CharSet subtract(const CharSet &a, const CharSet &b)
                {
                    return intersect(a, b.complement());
                }

                static CharSet unite(const CharSet &a, const CharSet &b)
                {
                    CharSet out = a;
                    out.add(b);
                    out.norm();
                    return out;
                }
            };

            struct ClassSet
            {
                std::uint32_t bits[8];
                Vector<Range> ranges;

                ClassSet()
                {
                    for (int i = 0; i < 8; ++i)
                        bits[i] = 0;
                }

                void build(const CharSet &cs)
                {
                    for (std::size_t i = 0; i < cs.r.size(); ++i)
                    {
                        std::uint32_t lo = cs.r[i].lo, hi = cs.r[i].hi;
                        for (std::uint32_t c = lo; c <= hi && c < 256; ++c)
                            bits[c >> 5] |= 1u << (c & 31);
                        if (hi >= 256)
                            ranges.push_back(Range{lo < 256 ? 256 : lo, hi});
                    }
                }

                bool contains(std::uint32_t cp) const noexcept
                {
                    if (cp < 256)
                        return (bits[cp >> 5] >> (cp & 31)) & 1u;
                    std::size_t lo = 0, hi = ranges.size();
                    while (lo < hi)
                    {
                        std::size_t mid = (lo + hi) >> 1;
                        if (ranges[mid].lo <= cp)
                            lo = mid + 1;
                        else
                            hi = mid;
                    }
                    return lo > 0 && cp <= ranges[lo - 1].hi;
                }
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

            inline unsigned general_category(std::uint32_t cp) noexcept
            {
                return UT::kGcVal[count_le(UT::kGcStart, uni::kGcRuns, cp) - 1];
            }

            inline CharSet gc_set(std::uint32_t mask)
            {
                CharSet out;
                for (std::size_t i = 0; i < uni::kGcRuns; ++i)
                {
                    if ((mask >> UT::kGcVal[i]) & 1u)
                        out.add(UT::kGcStart[i], i + 1 < uni::kGcRuns ? UT::kGcStart[i + 1] - 1 : kMaxCp);
                }
                out.norm();
                return out;
            }

            inline CharSet script_set(std::uint32_t id)
            {
                CharSet out;
                for (std::size_t i = 0; i < uni::kScRuns; ++i)
                {
                    if (UT::kScVal[i] == id)
                        out.add(UT::kScStart[i], i + 1 < uni::kScRuns ? UT::kScStart[i + 1] - 1 : kMaxCp);
                }
                out.norm();
                return out;
            }

            inline CharSet script_ext_set(std::uint32_t id)
            {
                CharSet base = script_set(id), listed, hit;
                for (std::size_t i = 0; i < uni::kScxRows; ++i)
                {
                    listed.add(UT::kScxLo[i], UT::kScxHi[i]);
                    for (const std::uint8_t *p = UT::kScxPool + UT::kScxOff[i]; *p != 255; ++p)
                        if (*p == id)
                        {
                            hit.add(UT::kScxLo[i], UT::kScxHi[i]);
                            break;
                        }
                }
                listed.norm();
                hit.norm();
                return CharSet::unite(CharSet::subtract(base, listed), hit);
            }

            inline bool bin_contains(std::size_t idx, std::uint32_t cp) noexcept
            {
                const std::uint32_t *pts = UT::kBinPts + UT::kBinOff[idx];
                std::size_t n = UT::kBinOff[idx + 1] - UT::kBinOff[idx];
                return count_le(pts, n, cp) & 1u;
            }

            inline CharSet bin_set(std::size_t idx)
            {
                CharSet out;
                if (idx == uni::kBinCount)
                    out.add(0, kMaxCp);
                else if (idx == uni::kBinCount + 1)
                    out.add(0, 0x7F);
                else if (idx == uni::kBinCount + 2)
                    out = gc_set(1u << 2).complement();
                else
                {
                    const std::uint32_t *pts = UT::kBinPts + UT::kBinOff[idx];
                    std::size_t n = UT::kBinOff[idx + 1] - UT::kBinOff[idx];
                    for (std::size_t i = 0; i + 1 < n; i += 2)
                        out.add(pts[i], pts[i + 1] - 1);
                }
                out.norm();
                return out;
            }

            inline bool find_name(const uni::NameId *t, std::size_t n, const char *s, std::size_t len, std::uint32_t &id)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (std::strlen(t[i].name) == len && std::memcmp(t[i].name, s, len) == 0)
                    {
                        id = t[i].id;
                        return true;
                    }
                }
                return false;
            }

            inline std::size_t bin_index(const char *name)
            {
                std::uint32_t id = 0;
                find_name(UT::kBinNames, uni::kBinNameCount, name, std::strlen(name), id);
                return id;
            }

            enum PropResult
            {
                P_Ok,
                P_Invalid,
                P_Strings
            };

            inline PropResult property_set(const char *name, std::size_t nlen, const char *value, std::size_t vlen, bool has_value, CharSet &out)
            {
                std::uint32_t id;
                if (has_value)
                {
                    auto is = [&](const char *a) { return std::strlen(a) == nlen && std::memcmp(name, a, nlen) == 0; };
                    if (is("General_Category") || is("gc"))
                    {
                        if (!find_name(UT::kGcNames, uni::kGcNameCount, value, vlen, id))
                            return P_Invalid;
                        out = gc_set(id);
                        return P_Ok;
                    }
                    if (is("Script") || is("sc") || is("Script_Extensions") || is("scx"))
                    {
                        if (!find_name(UT::kScNames, uni::kScNameCount, value, vlen, id))
                            return P_Invalid;
                        out = (is("Script") || is("sc")) ? script_set(id) : script_ext_set(id);
                        return P_Ok;
                    }
                    return P_Invalid;
                }
                if (find_name(UT::kGcNames, uni::kGcNameCount, name, nlen, id))
                {
                    out = gc_set(id);
                    return P_Ok;
                }
                if (find_name(UT::kBinNames, uni::kBinNameCount, name, nlen, id))
                {
                    out = bin_set(id);
                    return P_Ok;
                }
                static const char *const strs[] = {"Basic_Emoji", "Emoji_Keycap_Sequence", "RGI_Emoji_Modifier_Sequence",
                                                   "RGI_Emoji_Flag_Sequence", "RGI_Emoji_Tag_Sequence",
                                                   "RGI_Emoji_ZWJ_Sequence", "RGI_Emoji"};
                for (std::size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); ++i)
                    if (std::strlen(strs[i]) == nlen && std::memcmp(strs[i], name, nlen) == 0)
                        return P_Strings;
                return P_Invalid;
            }

            inline std::uint32_t canon_upper(std::uint32_t cp) noexcept
            {
                if (cp < 128)
                    return (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
                std::uint32_t m = unicode::simple_upper(cp);
                if (m == cp || m < 128 || m > 0xFFFF)
                    return cp;
                return m;
            }

            inline std::uint32_t canon_fold(std::uint32_t cp) noexcept
            {
                if (cp < 128)
                    return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
                return unicode::simple_fold(cp);
            }

            inline std::uint32_t canon(std::uint32_t cp, bool unicode) noexcept
            {
                return unicode ? canon_fold(cp) : canon_upper(cp);
            }

            class Equiv
            {
            public:
                explicit Equiv(bool unicode) : unicode_(unicode)
                {
                    const uni::CaseRange *t = unicode ? UT::kFold : UT::kUpper;
                    std::size_t n = unicode ? uni::kFoldRanges : uni::kUpperRanges;
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        for (std::uint32_t cp = t[i].lo; cp <= t[i].hi; cp += t[i].step)
                        {
                            std::uint32_t y = canon(cp, unicode);
                            if (y != cp)
                            {
                                pairs_.push_back((static_cast<std::uint64_t>(y) << 32) | cp);
                                domain_.add(cp, cp);
                            }
                        }
                    }
                    ct::sort(pairs_.begin(), pairs_.end());
                    domain_.norm();
                }

                const CharSet &domain() const noexcept { return domain_; }

                void members(std::uint32_t cp, Vector<std::uint32_t> &out) const
                {
                    std::uint32_t y = canon(cp, unicode_);
                    out.push_back(y);
                    std::size_t lo = 0, hi = pairs_.size();
                    while (lo < hi)
                    {
                        std::size_t mid = (lo + hi) >> 1;
                        if ((pairs_[mid] >> 32) < y)
                            lo = mid + 1;
                        else
                            hi = mid;
                    }
                    for (; lo < pairs_.size() && (pairs_[lo] >> 32) == y; ++lo)
                        out.push_back(static_cast<std::uint32_t>(pairs_[lo]));
                }

                void closure(CharSet &s) const
                {
                    CharSet add;
                    std::size_t i = 0, n = pairs_.size();
                    while (i < n)
                    {
                        std::uint32_t y = static_cast<std::uint32_t>(pairs_[i] >> 32);
                        std::size_t j = i;
                        bool any = s.contains(y);
                        for (; j < n && (pairs_[j] >> 32) == y; ++j)
                            if (!any && s.contains(static_cast<std::uint32_t>(pairs_[j])))
                                any = true;
                        if (any)
                        {
                            add.add(y, y);
                            for (std::size_t k = i; k < j; ++k)
                                add.add(static_cast<std::uint32_t>(pairs_[k]), static_cast<std::uint32_t>(pairs_[k]));
                        }
                        i = j;
                    }
                    s.add(add);
                    s.norm();
                }

                CharSet fold(const CharSet &s) const
                {
                    CharSet rem, add;
                    for (std::size_t i = 0; i < pairs_.size(); ++i)
                    {
                        std::uint32_t x = static_cast<std::uint32_t>(pairs_[i]);
                        if (s.contains(x))
                        {
                            rem.add(x, x);
                            add.add(static_cast<std::uint32_t>(pairs_[i] >> 32), static_cast<std::uint32_t>(pairs_[i] >> 32));
                        }
                    }
                    rem.norm();
                    add.norm();
                    return CharSet::unite(CharSet::subtract(s, rem), add);
                }

            private:
                bool unicode_;
                Vector<std::uint64_t> pairs_;
                CharSet domain_;
            };

            inline const Equiv &equiv(bool unicode)
            {
                static const Equiv plain(false);
                static const Equiv uni_(true);
                return unicode ? uni_ : plain;
            }

            inline bool is_line_term(std::uint32_t c) noexcept
            {
                return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
            }

            inline bool is_ascii_word(std::uint32_t c) noexcept
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            }

            inline bool is_id_start(std::uint32_t cp)
            {
                static const std::size_t idx = bin_index("ID_Start");
                return cp == '$' || cp == '_' || bin_contains(idx, cp);
            }

            inline bool is_id_continue(std::uint32_t cp)
            {
                static const std::size_t idx = bin_index("ID_Continue");
                return cp == '$' || cp == 0x200C || cp == 0x200D || bin_contains(idx, cp);
            }

            inline void append_utf8(String &out, std::uint32_t cp)
            {
                char b[4];
                std::size_t n = utf8_encode(cp, b);
                out.append(b, n);
            }

            enum NodeKind : std::uint8_t
            {
                N_Empty,
                N_Char,
                N_Any,
                N_Class,
                N_Concat,
                N_Alt,
                N_Repeat,
                N_Group,
                N_Backref,
                N_Assert,
                N_Look
            };

            enum AssertKind : std::uint8_t
            {
                As_Bol,
                As_BolMulti,
                As_Eol,
                As_EolMulti,
                As_WordB,
                As_NotWordB
            };

            enum LookKind : std::uint8_t
            {
                L_Ahead = 0,
                L_NegAhead = 1,
                L_Behind = 2,
                L_NegBehind = 3
            };

            struct Node
            {
                std::uint8_t kind;
                std::uint8_t flag;
                std::int32_t a;
                std::int32_t b;
                std::int32_t child;
                std::int32_t next;
                std::int32_t minw;
                std::int32_t sz;
                std::int32_t glo;
                std::int32_t ghi;
            };

            enum Op : std::uint8_t
            {
                Op_Char,
                Op_Char2,
                Op_Class,
                Op_Any,
                Op_AnyNoNL,
                Op_Split,
                Op_Jmp,
                Op_Save,
                Op_Match,
                Op_Assert,
                Op_Backref,
                Op_Reset,
                Op_SimpleRep,
                Op_RepInit,
                Op_RepEnter,
                Op_RepBegin,
                Op_RepNext,
                Op_LookStart,
                Op_LookEnd
            };

            struct Inst
            {
                std::uint8_t op;
                std::uint8_t arg;
                std::uint16_t split;
                std::int32_t x;
                std::int32_t y;
            };

            struct RepInfo
            {
                std::int32_t min;
                std::int32_t max;
                std::int32_t glo;
                std::int32_t ghi;
                bool lazy;
            };

            struct Program
            {
                Vector<Inst> code;
                Vector<ClassSet> classes;
                Vector<RepInfo> reps;
                Vector<String> names;
                std::size_t ngroups;
                std::size_t nsplits;
                bool memo_ok;
                bool anchored;
                bool unicode;
                bool icase;
                bool use_first;
                bool first_high;
                std::size_t min_len;
                std::uint32_t first[8];

                Program()
                    : ngroups(0), nsplits(0), memo_ok(false), anchored(false), unicode(false), icase(false),
                      use_first(false), first_high(false), min_len(0)
                {
                    for (int i = 0; i < 8; ++i)
                        first[i] = 0;
                }

                bool first_has(std::uint32_t c) const noexcept
                {
                    return c < 256 ? ((first[c >> 5] >> (c & 31)) & 1u) != 0 : first_high;
                }
            };

            inline std::int32_t sat_add(std::int32_t a, std::int32_t b) noexcept
            {
                std::int64_t v = static_cast<std::int64_t>(a) + b;
                return v > kSat ? kSat : static_cast<std::int32_t>(v);
            }

            inline std::int32_t sat_mul(std::int64_t a, std::int64_t b) noexcept
            {
                std::int64_t v = a * b;
                return v > kSat ? kSat : static_cast<std::int32_t>(v);
            }

            class Parser
            {
            public:
                Parser(const char16_t *p, std::size_t n, unsigned flags, Program &prog, Vector<Node> &nodes)
                    : p_(p), n_(n), i_(0), icase_((flags & F_IgnoreCase) != 0), multi_((flags & F_Multiline) != 0),
                      dotall_((flags & F_DotAll) != 0), vmode_((flags & F_UnicodeSets) != 0),
                      uni_((flags & (F_Unicode | F_UnicodeSets)) != 0), prog_(prog), nodes_(nodes), err_(nullptr),
                      err_off_(0), depth_(0), ngroups_(0), total_groups_(0), has_named_(false), root_(-1)
                {
                    prog_.names.push_back(String());
                }

                bool parse()
                {
                    prescan();
                    root_ = parse_alt();
                    if (err_)
                        return false;
                    if (i_ < n_)
                    {
                        fail("Unmatched ')'");
                        return false;
                    }
                    for (std::size_t k = 0; k < refs_.size(); ++k)
                    {
                        int found = -1;
                        for (std::size_t g = 1; g < prog_.names.size(); ++g)
                            if (StringView(prog_.names[g]) == StringView(refs_[k].name))
                                found = static_cast<int>(g);
                        if (found < 0)
                        {
                            fail("Invalid named capture referenced");
                            return false;
                        }
                        nodes_[static_cast<std::size_t>(refs_[k].node)].a = found;
                    }
                    prog_.ngroups = static_cast<std::size_t>(ngroups_);
                    return true;
                }

                int root() const noexcept { return root_; }
                const char *error() const noexcept { return err_; }
                std::size_t error_offset() const noexcept { return err_off_; }

            private:
                struct NamedRef
                {
                    int node;
                    String name;
                };

                struct Atom
                {
                    bool is_set;
                    std::uint32_t cp;
                    CharSet set;
                    Atom() : is_set(false), cp(0) {}
                };

                struct Opnd
                {
                    CharSet set;
                    Vector<Vector<std::uint32_t>> strs;
                    bool mcs;
                    bool unsup;
                    Opnd() : mcs(false), unsup(false) {}
                };

                const char16_t *p_;
                std::size_t n_;
                std::size_t i_;
                bool icase_;
                bool multi_;
                bool dotall_;
                bool vmode_;
                bool uni_;
                Program &prog_;
                Vector<Node> &nodes_;
                const char *err_;
                std::size_t err_off_;
                int depth_;
                int ngroups_;
                int total_groups_;
                bool has_named_;
                int root_;
                Vector<NamedRef> refs_;

                void fail(const char *msg)
                {
                    if (!err_)
                    {
                        err_ = msg;
                        err_off_ = i_ < n_ ? i_ : n_;
                    }
                }

                static bool is_high(std::uint32_t c) noexcept { return c >= 0xD800 && c <= 0xDBFF; }
                static bool is_low(std::uint32_t c) noexcept { return c >= 0xDC00 && c <= 0xDFFF; }

                std::uint32_t read_cp()
                {
                    std::uint32_t c = p_[i_++];
                    if (uni_ && is_high(c) && i_ < n_ && is_low(p_[i_]))
                    {
                        c = 0x10000 + ((c - 0xD800) << 10) + (p_[i_] - 0xDC00);
                        ++i_;
                    }
                    return c;
                }

                bool at(std::size_t k, char c) const noexcept
                {
                    return k < n_ && p_[k] == static_cast<char16_t>(c);
                }

                static bool is_digit(std::uint32_t c) noexcept { return c >= '0' && c <= '9'; }

                static bool in_set(const char *set, std::uint32_t c) noexcept
                {
                    return c != 0 && c < 128 && std::strchr(set, static_cast<int>(c)) != nullptr;
                }

                void prescan()
                {
                    int depth = 0;
                    for (std::size_t k = 0; k < n_; ++k)
                    {
                        char16_t c = p_[k];
                        if (c == '\\')
                            ++k;
                        else if (c == '[')
                        {
                            ++depth;
                            while (++k < n_)
                            {
                                if (p_[k] == '\\')
                                    ++k;
                                else if (p_[k] == '[' && vmode_)
                                    ++depth;
                                else if (p_[k] == ']' && --depth == 0)
                                    break;
                            }
                            depth = 0;
                        }
                        else if (c == '(')
                        {
                            if (!at(k + 1, '?'))
                                ++total_groups_;
                            else if (at(k + 2, '<') && !at(k + 3, '=') && !at(k + 3, '!'))
                            {
                                ++total_groups_;
                                has_named_ = true;
                            }
                        }
                    }
                }

                int new_node(std::uint8_t kind, std::uint8_t flag, std::int32_t a, std::int32_t b, std::int32_t child)
                {
                    Node nd;
                    nd.kind = kind;
                    nd.flag = flag;
                    nd.a = a;
                    nd.b = b;
                    nd.child = child;
                    nd.next = -1;
                    nd.minw = 0;
                    nd.sz = 1;
                    nd.glo = 0x7FFFFFFF;
                    nd.ghi = -1;
                    nodes_.push_back(nd);
                    return static_cast<int>(nodes_.size()) - 1;
                }

                int leaf(std::uint8_t kind, std::int32_t a)
                {
                    int n = new_node(kind, 0, a, 0, -1);
                    if (kind == N_Char || kind == N_Any || kind == N_Class)
                        nodes_[static_cast<std::size_t>(n)].minw = 1;
                    return n;
                }

                int make_class(const CharSet &set)
                {
                    if (set.r.size() == 1 && set.r[0].lo == set.r[0].hi)
                        return leaf(N_Char, static_cast<std::int32_t>(set.r[0].lo));
                    ClassSet cs;
                    cs.build(set);
                    prog_.classes.push_back(static_cast<ClassSet &&>(cs));
                    return leaf(N_Class, static_cast<std::int32_t>(prog_.classes.size()) - 1);
                }

                void merge_groups(Node &dst, const Node &src) const
                {
                    if (src.glo < dst.glo)
                        dst.glo = src.glo;
                    if (src.ghi > dst.ghi)
                        dst.ghi = src.ghi;
                }

                int make_list(std::uint8_t kind, const Vector<int> &items)
                {
                    int n = new_node(kind, 0, 0, 0, items[0]);
                    std::int32_t minw = kind == N_Alt ? kSat : 0, sz = kind == N_Alt ? 2 : 0;
                    Node acc = nodes_[static_cast<std::size_t>(n)];
                    for (std::size_t k = 0; k < items.size(); ++k)
                    {
                        Node &c = nodes_[static_cast<std::size_t>(items[k])];
                        c.next = k + 1 < items.size() ? items[k + 1] : -1;
                        if (kind == N_Alt)
                        {
                            if (c.minw < minw)
                                minw = c.minw;
                            sz = sat_add(sz, sat_add(c.sz, 2));
                        }
                        else
                        {
                            minw = sat_add(minw, c.minw);
                            sz = sat_add(sz, c.sz);
                        }
                        merge_groups(acc, c);
                    }
                    Node &nd = nodes_[static_cast<std::size_t>(n)];
                    nd.minw = minw;
                    nd.sz = sz;
                    nd.glo = acc.glo;
                    nd.ghi = acc.ghi;
                    return n;
                }

                int make_group(int idx, int body)
                {
                    int n = new_node(N_Group, 0, idx, 0, body);
                    Node &nd = nodes_[static_cast<std::size_t>(n)];
                    const Node &c = nodes_[static_cast<std::size_t>(body)];
                    nd.minw = c.minw;
                    nd.sz = sat_add(c.sz, 2);
                    nd.glo = idx;
                    nd.ghi = c.ghi > idx ? c.ghi : idx;
                    return n;
                }

                int make_look(int kind, int body)
                {
                    int n = new_node(N_Look, 0, kind, 0, body);
                    Node &nd = nodes_[static_cast<std::size_t>(n)];
                    const Node &c = nodes_[static_cast<std::size_t>(body)];
                    nd.sz = sat_add(c.sz, 2);
                    nd.glo = c.glo;
                    nd.ghi = c.ghi;
                    return n;
                }

                int make_repeat(int min, int max, bool lazy, int body)
                {
                    int n = new_node(N_Repeat, lazy ? 1 : 0, min, max, body);
                    Node &nd = nodes_[static_cast<std::size_t>(n)];
                    const Node &c = nodes_[static_cast<std::size_t>(body)];
                    nd.minw = sat_mul(min, c.minw);
                    std::int64_t copies = static_cast<std::int64_t>(min) + (max < 0 ? 1 : max - min);
                    nd.sz = sat_add(sat_mul(copies, c.sz), 6);
                    nd.glo = c.glo;
                    nd.ghi = c.ghi;
                    return n;
                }

                int parse_alt()
                {
                    if (++depth_ > kMaxDepth)
                    {
                        fail("Regular expression too large");
                        return -1;
                    }
                    Vector<int> alts;
                    for (;;)
                    {
                        int c = parse_concat();
                        if (err_)
                            return -1;
                        alts.push_back(c);
                        if (i_ < n_ && p_[i_] == '|')
                        {
                            ++i_;
                            continue;
                        }
                        break;
                    }
                    --depth_;
                    if (alts.size() == 1)
                        return alts[0];
                    return make_list(N_Alt, alts);
                }

                int parse_concat()
                {
                    Vector<int> items;
                    while (i_ < n_ && p_[i_] != '|' && p_[i_] != ')')
                    {
                        int t = parse_term();
                        if (err_)
                            return -1;
                        items.push_back(t);
                    }
                    if (items.empty())
                        return leaf(N_Empty, 0);
                    if (items.size() == 1)
                        return items[0];
                    return make_list(N_Concat, items);
                }

                bool interval_at(std::size_t k, std::int64_t &mn, std::int64_t &mx, std::size_t &end) const
                {
                    if (!at(k, '{'))
                        return false;
                    ++k;
                    std::size_t d0 = k;
                    std::int64_t a = 0;
                    while (k < n_ && is_digit(p_[k]))
                    {
                        a = a * 10 + (p_[k] - '0');
                        if (a > 0x7FFFFFFF)
                            a = 0x7FFFFFFF;
                        ++k;
                    }
                    if (k == d0)
                        return false;
                    std::int64_t b = a;
                    if (at(k, ','))
                    {
                        ++k;
                        std::size_t d1 = k;
                        b = 0;
                        while (k < n_ && is_digit(p_[k]))
                        {
                            b = b * 10 + (p_[k] - '0');
                            if (b > 0x7FFFFFFF)
                                b = 0x7FFFFFFF;
                            ++k;
                        }
                        if (k == d1)
                            b = -1;
                    }
                    if (!at(k, '}'))
                        return false;
                    mn = a;
                    mx = b;
                    end = k + 1;
                    return true;
                }

                void bad_quantifier()
                {
                    if (i_ >= n_)
                        return;
                    char16_t c = p_[i_];
                    if (c == '*' || c == '+' || c == '?')
                    {
                        fail("Invalid quantifier");
                        return;
                    }
                    if (c != '{')
                        return;
                    std::int64_t a, b;
                    std::size_t e;
                    if (interval_at(i_, a, b, e))
                        fail(b >= 0 && a > b ? "numbers out of order in {} quantifier" : "Invalid quantifier");
                    else if (uni_)
                        fail("Incomplete quantifier");
                }

                int parse_quantifier(int atom)
                {
                    char16_t c = p_[i_];
                    std::int64_t mn, mx;
                    if (c == '*')
                    {
                        mn = 0;
                        mx = -1;
                        ++i_;
                    }
                    else if (c == '+')
                    {
                        mn = 1;
                        mx = -1;
                        ++i_;
                    }
                    else if (c == '?')
                    {
                        mn = 0;
                        mx = 1;
                        ++i_;
                    }
                    else
                    {
                        std::size_t e;
                        if (!interval_at(i_, mn, mx, e))
                        {
                            if (uni_)
                                fail("Incomplete quantifier");
                            return atom;
                        }
                        if (mx >= 0 && mn > mx)
                        {
                            fail("numbers out of order in {} quantifier");
                            return atom;
                        }
                        i_ = e;
                    }
                    bool lazy = false;
                    if (i_ < n_ && p_[i_] == '?')
                    {
                        lazy = true;
                        ++i_;
                    }
                    return make_repeat(static_cast<int>(mn), static_cast<int>(mx), lazy, atom);
                }

                int parse_term()
                {
                    std::uint32_t c = p_[i_];
                    int atom = -1;
                    bool quant = true, look = false;
                    switch (c)
                    {
                    case '^':
                        ++i_;
                        atom = leaf(N_Assert, multi_ ? As_BolMulti : As_Bol);
                        quant = false;
                        break;
                    case '$':
                        ++i_;
                        atom = leaf(N_Assert, multi_ ? As_EolMulti : As_Eol);
                        quant = false;
                        break;
                    case '(':
                        atom = parse_group(quant, look);
                        break;
                    case '.':
                        ++i_;
                        atom = leaf(N_Any, 0);
                        break;
                    case '\\':
                        atom = parse_atom_escape();
                        if (atom >= 0 && nodes_[static_cast<std::size_t>(atom)].kind == N_Assert)
                            quant = false;
                        break;
                    case '[':
                        atom = vmode_ ? parse_class_v() : parse_class();
                        break;
                    case '*':
                    case '+':
                    case '?':
                        fail("Nothing to repeat");
                        return -1;
                    case '{':
                    {
                        std::int64_t a, b;
                        std::size_t e;
                        if (interval_at(i_, a, b, e))
                            fail("Nothing to repeat");
                        else if (uni_)
                            fail("Lone quantifier brackets");
                        else
                        {
                            ++i_;
                            atom = leaf(N_Char, '{');
                        }
                        break;
                    }
                    case '}':
                    case ']':
                        if (uni_)
                        {
                            fail("Lone quantifier brackets");
                            return -1;
                        }
                        ++i_;
                        atom = leaf(N_Char, static_cast<std::int32_t>(c));
                        break;
                    default:
                        atom = leaf(N_Char, static_cast<std::int32_t>(read_cp()));
                        break;
                    }
                    if (err_ || atom < 0)
                        return -1;
                    if (quant)
                    {
                        if (i_ < n_ && (p_[i_] == '*' || p_[i_] == '+' || p_[i_] == '?' || p_[i_] == '{'))
                            atom = parse_quantifier(atom);
                    }
                    else if (look)
                        bad_quantifier();
                    return err_ ? -1 : atom;
                }

                int parse_group(bool &quant, bool &look)
                {
                    ++i_;
                    int idx = 0, lk = -1;
                    if (i_ < n_ && p_[i_] == '?')
                    {
                        char16_t c = i_ + 1 < n_ ? p_[i_ + 1] : 0;
                        if (c == ':')
                            i_ += 2;
                        else if (c == '=' || c == '!')
                        {
                            lk = c == '=' ? L_Ahead : L_NegAhead;
                            i_ += 2;
                        }
                        else if (c == '<' && (at(i_ + 2, '=') || at(i_ + 2, '!')))
                        {
                            lk = at(i_ + 2, '=') ? L_Behind : L_NegBehind;
                            i_ += 3;
                        }
                        else if (c == '<')
                        {
                            i_ += 2;
                            String name;
                            if (!parse_name(name))
                                return -1;
                            for (std::size_t g = 1; g < prog_.names.size(); ++g)
                                if (StringView(prog_.names[g]) == StringView(name))
                                {
                                    fail("Duplicate capture group name");
                                    return -1;
                                }
                            idx = ++ngroups_;
                            prog_.names.push_back(static_cast<String &&>(name));
                        }
                        else
                        {
                            fail("Invalid group");
                            return -1;
                        }
                    }
                    else
                    {
                        idx = ++ngroups_;
                        prog_.names.push_back(String());
                    }
                    int body = parse_alt();
                    if (err_)
                        return -1;
                    if (i_ >= n_ || p_[i_] != ')')
                    {
                        fail("Unterminated group");
                        return -1;
                    }
                    ++i_;
                    if (lk >= 0)
                    {
                        quant = (lk == L_Ahead || lk == L_NegAhead) && !uni_;
                        look = !quant;
                        return make_look(lk, body);
                    }
                    return idx ? make_group(idx, body) : body;
                }

                bool parse_name(String &out)
                {
                    for (;;)
                    {
                        if (i_ >= n_)
                        {
                            fail("Invalid capture group name");
                            return false;
                        }
                        if (p_[i_] == '>')
                        {
                            ++i_;
                            break;
                        }
                        std::uint32_t cp;
                        if (p_[i_] == '\\')
                        {
                            if (!at(i_ + 1, 'u'))
                            {
                                fail("Invalid capture group name");
                                return false;
                            }
                            i_ += 2;
                            if (!parse_u_escape(true, cp))
                            {
                                fail("Invalid Unicode escape");
                                return false;
                            }
                        }
                        else
                        {
                            cp = p_[i_++];
                            if (is_high(cp) && i_ < n_ && is_low(p_[i_]))
                            {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (p_[i_] - 0xDC00);
                                ++i_;
                            }
                        }
                        if (!(out.size() == 0 ? is_id_start(cp) : is_id_continue(cp)))
                        {
                            fail("Invalid capture group name");
                            return false;
                        }
                        append_utf8(out, cp);
                    }
                    if (out.size() == 0)
                    {
                        fail("Invalid capture group name");
                        return false;
                    }
                    return true;
                }

                bool hex_at(std::size_t k, int count, std::uint32_t &v) const
                {
                    v = 0;
                    for (int d = 0; d < count; ++d)
                    {
                        if (k + d >= n_)
                            return false;
                        int h = p_[k + d] < 128 ? hex_value(static_cast<unsigned char>(p_[k + d])) : -1;
                        if (h < 0)
                            return false;
                        v = v * 16 + static_cast<std::uint32_t>(h);
                    }
                    return true;
                }

                bool parse_u_escape(bool force, std::uint32_t &cp)
                {
                    bool u = force || uni_;
                    if (u && at(i_, '{'))
                    {
                        std::size_t k = i_ + 1;
                        std::uint64_t v = 0;
                        std::size_t d0 = k;
                        while (k < n_ && p_[k] < 128 && hex_value(static_cast<unsigned char>(p_[k])) >= 0)
                        {
                            v = v * 16 + static_cast<std::uint64_t>(hex_value(static_cast<unsigned char>(p_[k])));
                            if (v > kMaxCp)
                                return false;
                            ++k;
                        }
                        if (k == d0 || !at(k, '}'))
                            return false;
                        cp = static_cast<std::uint32_t>(v);
                        i_ = k + 1;
                        return true;
                    }
                    std::uint32_t v;
                    if (!hex_at(i_, 4, v))
                        return false;
                    i_ += 4;
                    if (u && is_high(v) && at(i_, '\\') && at(i_ + 1, 'u'))
                    {
                        std::uint32_t lo;
                        if (hex_at(i_ + 2, 4, lo) && is_low(lo))
                        {
                            v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                            i_ += 6;
                        }
                    }
                    cp = v;
                    return true;
                }

                void octal_escape(std::uint32_t &cp)
                {
                    std::uint32_t v = p_[i_++] - '0';
                    if (i_ < n_ && p_[i_] >= '0' && p_[i_] <= '7')
                    {
                        v = v * 8 + (p_[i_++] - '0');
                        if (v < 32 && i_ < n_ && p_[i_] >= '0' && p_[i_] <= '7')
                            v = v * 8 + (p_[i_++] - '0');
                    }
                    cp = v;
                }

                CharSet space_set() const
                {
                    CharSet s;
                    s.add(9, 13);
                    s.add(32, 32);
                    s.add(0xA0, 0xA0);
                    s.add(0x1680, 0x1680);
                    s.add(0x2000, 0x200A);
                    s.add(0x2028, 0x2029);
                    s.add(0x202F, 0x202F);
                    s.add(0x205F, 0x205F);
                    s.add(0x3000, 0x3000);
                    s.add(0xFEFF, 0xFEFF);
                    s.norm();
                    return s;
                }

                CharSet finish_set(CharSet base, bool neg) const
                {
                    base.norm();
                    if (vmode_ && icase_)
                    {
                        const Equiv &e = equiv(true);
                        base = e.fold(base);
                        if (neg)
                            base = CharSet::subtract(base.complement(), e.domain());
                        return base;
                    }
                    return neg ? base.complement() : base;
                }

                CharSet escape_set(char16_t c) const
                {
                    CharSet s;
                    char16_t l = static_cast<char16_t>(c | 32);
                    if (l == 'd')
                        s.add('0', '9');
                    else if (l == 's')
                        s = space_set();
                    else
                    {
                        s.add('a', 'z');
                        s.add('A', 'Z');
                        s.add('0', '9');
                        s.add('_', '_');
                        if (uni_ && icase_)
                        {
                            s.add(0x17F, 0x17F);
                            s.add(0x212A, 0x212A);
                        }
                    }
                    return finish_set(s, c != l);
                }

                bool parse_property(bool neg, bool in_class, CharSet &set, bool &strings)
                {
                    strings = false;
                    std::size_t j = i_;
                    bool ok = at(j, '{');
                    char nbuf[64], vbuf[64];
                    std::size_t nl = 0, vl = 0;
                    bool hasv = false;
                    if (ok)
                    {
                        ++j;
                        while (j < n_ && p_[j] != '}')
                        {
                            char16_t c = p_[j];
                            if (c == '=' && !hasv)
                                hasv = true;
                            else if (c < 128 && (is_digit(c) || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                            {
                                if ((hasv ? vl : nl) >= 63)
                                {
                                    ok = false;
                                    break;
                                }
                                (hasv ? vbuf[vl++] : nbuf[nl++]) = static_cast<char>(c);
                            }
                            else
                            {
                                ok = false;
                                break;
                            }
                            ++j;
                        }
                        if (ok && (j >= n_ || nl == 0 || (hasv && vl == 0)))
                            ok = false;
                    }
                    PropResult r = P_Invalid;
                    CharSet raw;
                    if (ok)
                        r = property_set(nbuf, nl, vbuf, vl, hasv, raw);
                    if (r == P_Strings && (!vmode_ || neg))
                        r = P_Invalid;
                    if (r == P_Invalid)
                    {
                        fail(in_class ? "Invalid property name in character class" : "Invalid property name");
                        return false;
                    }
                    i_ = j + 1;
                    if (r == P_Strings)
                    {
                        strings = true;
                        return true;
                    }
                    set = finish_set(raw, neg);
                    return true;
                }

                bool escape(bool in_class, bool &is_set, std::uint32_t &cp, CharSet &set)
                {
                    is_set = false;
                    if (i_ + 1 >= n_)
                    {
                        fail("\\ at end of pattern");
                        return false;
                    }
                    std::uint32_t c = p_[i_ + 1];
                    switch (c)
                    {
                    case 'd':
                    case 'D':
                    case 's':
                    case 'S':
                    case 'w':
                    case 'W':
                        i_ += 2;
                        is_set = true;
                        set = escape_set(static_cast<char16_t>(c));
                        return true;
                    case 'p':
                    case 'P':
                        if (uni_)
                        {
                            i_ += 2;
                            bool strings;
                            if (!parse_property(c == 'P', in_class, set, strings))
                                return false;
                            is_set = true;
                            if (strings)
                                fail("unsupported");
                            return !err_;
                        }
                        break;
                    case 't':
                        cp = 9;
                        i_ += 2;
                        return true;
                    case 'n':
                        cp = 10;
                        i_ += 2;
                        return true;
                    case 'v':
                        cp = 11;
                        i_ += 2;
                        return true;
                    case 'f':
                        cp = 12;
                        i_ += 2;
                        return true;
                    case 'r':
                        cp = 13;
                        i_ += 2;
                        return true;
                    case 'c':
                    {
                        std::uint32_t l = i_ + 2 < n_ ? p_[i_ + 2] : 0;
                        if ((l >= 'a' && l <= 'z') || (l >= 'A' && l <= 'Z'))
                        {
                            cp = l % 32;
                            i_ += 3;
                            return true;
                        }
                        if (in_class && !uni_ && (is_digit(l) || l == '_'))
                        {
                            cp = l % 32;
                            i_ += 3;
                            return true;
                        }
                        if (uni_)
                        {
                            fail("Invalid Unicode escape");
                            return false;
                        }
                        cp = '\\';
                        ++i_;
                        return true;
                    }
                    case '0':
                        if (!(i_ + 2 < n_ && is_digit(p_[i_ + 2])))
                        {
                            cp = 0;
                            i_ += 2;
                            return true;
                        }
                        if (uni_)
                        {
                            fail(in_class ? "Invalid class escape" : "Invalid decimal escape");
                            return false;
                        }
                        ++i_;
                        octal_escape(cp);
                        return true;
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        if (uni_)
                        {
                            fail(in_class && c < '8' ? "Invalid class escape" : "Invalid escape");
                            return false;
                        }
                        if (c >= '8')
                        {
                            cp = c;
                            i_ += 2;
                            return true;
                        }
                        ++i_;
                        octal_escape(cp);
                        return true;
                    case 'x':
                    {
                        std::uint32_t v;
                        if (hex_at(i_ + 2, 2, v))
                        {
                            cp = v;
                            i_ += 4;
                            return true;
                        }
                        if (uni_)
                        {
                            fail("Invalid escape");
                            return false;
                        }
                        cp = 'x';
                        i_ += 2;
                        return true;
                    }
                    case 'u':
                    {
                        i_ += 2;
                        if (parse_u_escape(false, cp))
                            return true;
                        if (uni_)
                        {
                            fail("Invalid Unicode escape");
                            return false;
                        }
                        cp = 'u';
                        return true;
                    }
                    case 'b':
                        if (in_class)
                        {
                            cp = 8;
                            i_ += 2;
                            return true;
                        }
                        break;
                    case '-':
                        if (in_class && uni_)
                        {
                            cp = '-';
                            i_ += 2;
                            return true;
                        }
                        break;
                    default:
                        break;
                    }
                    if (uni_)
                    {
                        if ((in_set("^$\\.*+?()[]{}|/", c)))
                        {
                            cp = c;
                            i_ += 2;
                            return true;
                        }
                        fail("Invalid escape");
                        return false;
                    }
                    if (c == 'k' && has_named_)
                    {
                        fail("Invalid escape");
                        return false;
                    }
                    cp = c;
                    i_ += 2;
                    return true;
                }

                int parse_atom_escape()
                {
                    if (i_ + 1 >= n_)
                    {
                        fail("\\ at end of pattern");
                        return -1;
                    }
                    std::uint32_t c = p_[i_ + 1];
                    if (c == 'b' || c == 'B')
                    {
                        i_ += 2;
                        return leaf(N_Assert, c == 'b' ? As_WordB : As_NotWordB);
                    }
                    if (c >= '1' && c <= '9')
                    {
                        std::size_t j = i_ + 1;
                        std::int64_t v = 0;
                        while (j < n_ && is_digit(p_[j]))
                        {
                            v = v * 10 + (p_[j] - '0');
                            if (v > 0x7FFFFFFF)
                                v = 0x7FFFFFFF;
                            ++j;
                        }
                        if (v <= total_groups_)
                        {
                            i_ = j;
                            return leaf(N_Backref, static_cast<std::int32_t>(v));
                        }
                    }
                    if (c == 'k' && (uni_ || has_named_))
                    {
                        if (!at(i_ + 2, '<'))
                        {
                            fail("Invalid named reference");
                            return -1;
                        }
                        i_ += 3;
                        NamedRef ref;
                        if (!parse_name(ref.name))
                            return -1;
                        ref.node = leaf(N_Backref, 0);
                        refs_.push_back(static_cast<NamedRef &&>(ref));
                        return refs_[refs_.size() - 1].node;
                    }
                    bool is_set;
                    std::uint32_t cp = 0;
                    CharSet set;
                    if (!escape(false, is_set, cp, set))
                        return -1;
                    if (is_set)
                    {
                        if (icase_)
                            equiv(uni_).closure(set);
                        return make_class(set);
                    }
                    return leaf(N_Char, static_cast<std::int32_t>(cp));
                }

                bool class_atom(Atom &a)
                {
                    if (p_[i_] == '\\')
                        return escape(true, a.is_set, a.cp, a.set);
                    a.cp = read_cp();
                    return true;
                }

                int finish_class(CharSet &set, bool neg)
                {
                    set.norm();
                    if (vmode_ && icase_)
                    {
                        if (neg)
                            set = CharSet::subtract(set.complement(), equiv(true).domain());
                        equiv(true).closure(set);
                    }
                    else
                    {
                        if (icase_)
                            equiv(uni_).closure(set);
                        if (neg)
                            set = set.complement();
                    }
                    return make_class(set);
                }

                int parse_class()
                {
                    ++i_;
                    bool neg = false;
                    if (i_ < n_ && p_[i_] == '^')
                    {
                        neg = true;
                        ++i_;
                    }
                    CharSet set;
                    for (;;)
                    {
                        if (i_ >= n_)
                        {
                            fail("Unterminated character class");
                            return -1;
                        }
                        if (p_[i_] == ']')
                        {
                            ++i_;
                            break;
                        }
                        Atom a;
                        if (!class_atom(a))
                            return -1;
                        if (i_ + 1 < n_ && p_[i_] == '-' && p_[i_ + 1] != ']')
                        {
                            ++i_;
                            Atom b;
                            if (!class_atom(b))
                                return -1;
                            if (a.is_set || b.is_set)
                            {
                                if (uni_)
                                {
                                    fail("Invalid character class");
                                    return -1;
                                }
                                if (a.is_set)
                                    set.add(a.set);
                                else
                                    set.add(a.cp, a.cp);
                                set.add('-', '-');
                                if (b.is_set)
                                    set.add(b.set);
                                else
                                    set.add(b.cp, b.cp);
                            }
                            else if (a.cp > b.cp)
                            {
                                fail("Range out of order in character class");
                                return -1;
                            }
                            else
                                set.add(a.cp, b.cp);
                        }
                        else if (a.is_set)
                            set.add(a.set);
                        else
                            set.add(a.cp, a.cp);
                    }
                    return finish_class(set, neg);
                }

                std::uint32_t fold_cp(std::uint32_t cp) const noexcept { return icase_ ? canon_fold(cp) : cp; }

                static bool strings_equal(const Vector<std::uint32_t> &a, const Vector<std::uint32_t> &b)
                {
                    if (a.size() != b.size())
                        return false;
                    for (std::size_t i = 0; i < a.size(); ++i)
                        if (a[i] != b[i])
                            return false;
                    return true;
                }

                static bool strings_has(const Vector<Vector<std::uint32_t>> &l, const Vector<std::uint32_t> &s)
                {
                    for (std::size_t i = 0; i < l.size(); ++i)
                        if (strings_equal(l[i], s))
                            return true;
                    return false;
                }

                bool class_set_char(std::uint32_t &cp)
                {
                    if (i_ >= n_)
                    {
                        fail("Unterminated character class");
                        return false;
                    }
                    std::uint32_t c = p_[i_];
                    if (c == '\\')
                    {
                        std::uint32_t d = i_ + 1 < n_ ? p_[i_ + 1] : 0;
                        if (in_set("&-!#%,:;<=>@`~", d))
                        {
                            cp = d;
                            i_ += 2;
                            return true;
                        }
                        bool is_set;
                        CharSet set;
                        if (!escape(true, is_set, cp, set))
                            return false;
                        if (is_set)
                        {
                            fail("Invalid escape");
                            return false;
                        }
                        return true;
                    }
                    if (in_set("()[]{}/-|", c))
                    {
                        fail("Invalid character in character class");
                        return false;
                    }
                    if (in_set("&!#$%*+,.:;<=>?@^`~", c) && at(i_ + 1, static_cast<char>(c)))
                    {
                        fail("Invalid set operation in character class");
                        return false;
                    }
                    cp = read_cp();
                    return true;
                }

                bool parse_q(Opnd &o)
                {
                    i_ += 3;
                    Vector<std::uint32_t> cur;
                    for (;;)
                    {
                        if (i_ >= n_)
                        {
                            fail("Unterminated character class");
                            return false;
                        }
                        if (p_[i_] == '|' || p_[i_] == '}')
                        {
                            bool last = p_[i_] == '}';
                            ++i_;
                            if (cur.size() == 1)
                                o.set.add(fold_cp(cur[0]), fold_cp(cur[0]));
                            else
                            {
                                o.mcs = true;
                                if (!strings_has(o.strs, cur))
                                    o.strs.push_back(cur);
                            }
                            cur.clear();
                            if (last)
                                break;
                            continue;
                        }
                        std::uint32_t cp;
                        if (!class_set_char(cp))
                            return false;
                        cur.push_back(cp);
                    }
                    o.set.norm();
                    return true;
                }

                bool class_operand(Opnd &o, bool &single, std::uint32_t &cp)
                {
                    single = false;
                    if (i_ >= n_)
                    {
                        fail("Unterminated character class");
                        return false;
                    }
                    std::uint32_t c = p_[i_];
                    if (c == '[')
                    {
                        if (!class_v(o))
                            return false;
                        return true;
                    }
                    if (c == '\\' && i_ + 1 < n_)
                    {
                        std::uint32_t d = p_[i_ + 1];
                        if (d == 'q')
                        {
                            if (!at(i_ + 2, '{'))
                            {
                                fail("Invalid escape");
                                return false;
                            }
                            return parse_q(o);
                        }
                        if (d == 'd' || d == 'D' || d == 's' || d == 'S' || d == 'w' || d == 'W' || d == 'p' || d == 'P')
                        {
                            bool is_set;
                            std::uint32_t dummy;
                            if (d == 'p' || d == 'P')
                            {
                                bool strings;
                                i_ += 2;
                                if (!parse_property(d == 'P', true, o.set, strings))
                                    return false;
                                if (strings)
                                {
                                    o.mcs = true;
                                    o.unsup = true;
                                }
                                return true;
                            }
                            return escape(true, is_set, dummy, o.set);
                        }
                    }
                    if (!class_set_char(cp))
                        return false;
                    single = true;
                    return true;
                }

                bool class_v(Opnd &out)
                {
                    ++i_;
                    bool neg = false;
                    if (i_ < n_ && p_[i_] == '^')
                    {
                        neg = true;
                        ++i_;
                    }
                    Opnd acc;
                    int mode = 0, count = 0;
                    bool range_seen = false;
                    for (;;)
                    {
                        if (i_ >= n_)
                        {
                            fail("Unterminated character class");
                            return false;
                        }
                        if (p_[i_] == ']')
                        {
                            ++i_;
                            break;
                        }
                        char16_t c = p_[i_];
                        bool op = (c == '&' || c == '-') && at(i_ + 1, static_cast<char>(c));
                        if (op)
                        {
                            if (count == 0)
                            {
                                fail(c == '&' ? "Invalid set operation in character class" : "Invalid character in character class");
                                return false;
                            }
                            if (mode == 0 && count == 1 && !range_seen)
                                mode = c;
                            else if (mode != c)
                            {
                                fail("Invalid set operation in character class");
                                return false;
                            }
                            i_ += 2;
                            if (c == '&' && at(i_, '&'))
                            {
                                fail("Invalid character in character class");
                                return false;
                            }
                            Opnd o;
                            bool single;
                            std::uint32_t cp = 0;
                            if (!class_operand(o, single, cp))
                                return false;
                            if (single)
                                o.set.add(fold_cp(cp), fold_cp(cp));
                            o.set.norm();
                            if (c == '&')
                            {
                                acc.set = CharSet::intersect(acc.set, o.set);
                                Vector<Vector<std::uint32_t>> keep;
                                for (std::size_t k = 0; k < acc.strs.size(); ++k)
                                    if (strings_has(o.strs, acc.strs[k]))
                                        keep.push_back(acc.strs[k]);
                                acc.strs = static_cast<Vector<Vector<std::uint32_t>> &&>(keep);
                                acc.mcs = acc.mcs && o.mcs;
                            }
                            else
                            {
                                acc.set = CharSet::subtract(acc.set, o.set);
                                Vector<Vector<std::uint32_t>> keep;
                                for (std::size_t k = 0; k < acc.strs.size(); ++k)
                                    if (!strings_has(o.strs, acc.strs[k]))
                                        keep.push_back(acc.strs[k]);
                                acc.strs = static_cast<Vector<Vector<std::uint32_t>> &&>(keep);
                            }
                            acc.unsup = acc.unsup || o.unsup;
                            ++count;
                            continue;
                        }
                        if (mode != 0)
                        {
                            fail("Invalid set operation in character class");
                            return false;
                        }
                        Opnd o;
                        bool single;
                        std::uint32_t cp = 0;
                        if (!class_operand(o, single, cp))
                            return false;
                        if (i_ < n_ && p_[i_] == '-' && !at(i_ + 1, '-'))
                        {
                            if (!single)
                            {
                                fail(i_ + 1 >= n_ ? "Unterminated character class" : "Invalid character class");
                                return false;
                            }
                            ++i_;
                            Opnd o2;
                            bool single2;
                            std::uint32_t cp2 = 0;
                            if (!class_operand(o2, single2, cp2))
                                return false;
                            if (!single2)
                            {
                                fail("Invalid character class");
                                return false;
                            }
                            if (cp > cp2)
                            {
                                fail("Range out of order in character class");
                                return false;
                            }
                            CharSet rs;
                            rs.add(cp, cp2);
                            o.set = icase_ ? equiv(true).fold(rs) : rs;
                            range_seen = true;
                            if (i_ < n_ && p_[i_] == '-' && !at(i_ + 1, '-'))
                            {
                                fail("Invalid character class");
                                return false;
                            }
                        }
                        else if (single)
                            o.set.add(fold_cp(cp), fold_cp(cp));
                        o.set.norm();
                        acc.set.add(o.set);
                        acc.set.norm();
                        for (std::size_t k = 0; k < o.strs.size(); ++k)
                            if (!strings_has(acc.strs, o.strs[k]))
                                acc.strs.push_back(o.strs[k]);
                        acc.mcs = acc.mcs || o.mcs;
                        acc.unsup = acc.unsup || o.unsup;
                        ++count;
                    }
                    acc.set.norm();
                    if (neg)
                    {
                        if (acc.mcs)
                        {
                            fail("Negated character class may contain strings");
                            return false;
                        }
                        acc.set = icase_ ? CharSet::subtract(acc.set.complement(), equiv(true).domain()) : acc.set.complement();
                    }
                    out = static_cast<Opnd &&>(acc);
                    return true;
                }

                int parse_class_v()
                {
                    Opnd o;
                    if (!class_v(o))
                        return -1;
                    if (o.unsup || !o.strs.empty())
                    {
                        fail("unsupported");
                        return -1;
                    }
                    CharSet set = o.set;
                    if (icase_)
                        equiv(true).closure(set);
                    return make_class(set);
                }
            };

            class Compiler
            {
            public:
                Compiler(Program &prog, const Vector<Node> &nodes, unsigned flags)
                    : prog_(prog), nodes_(nodes), icase_((flags & F_IgnoreCase) != 0), dotall_((flags & F_DotAll) != 0),
                      uni_((flags & (F_Unicode | F_UnicodeSets)) != 0), nsplits_(0)
                {
                }

                void compile(int root)
                {
                    prog_.unicode = uni_;
                    prog_.icase = icase_;
                    prog_.memo_ok = true;
                    gen(root, false);
                    emit(Op_Match, 0, 0, 0);
                    if (nsplits_ > 65535)
                        prog_.memo_ok = false;
                    prog_.nsplits = nsplits_;
                    prog_.min_len = static_cast<std::size_t>(nodes_[static_cast<std::size_t>(root)].minw);
                    prog_.anchored = anchored(root);
                    First f;
                    bool nullable = first_of(root, f);
                    if (!nullable && !f.any)
                    {
                        prog_.use_first = true;
                        prog_.first_high = f.high;
                        for (int i = 0; i < 8; ++i)
                            prog_.first[i] = f.bits[i];
                    }
                }

            private:
                struct First
                {
                    std::uint32_t bits[8];
                    bool high;
                    bool any;
                    First() : high(false), any(false)
                    {
                        for (int i = 0; i < 8; ++i)
                            bits[i] = 0;
                    }
                    void set(std::uint32_t c)
                    {
                        if (c < 256)
                            bits[c >> 5] |= 1u << (c & 31);
                        else
                            high = true;
                    }
                };

                Program &prog_;
                const Vector<Node> &nodes_;
                bool icase_;
                bool dotall_;
                bool uni_;
                std::size_t nsplits_;

                const Node &node(int n) const { return nodes_[static_cast<std::size_t>(n)]; }
                int pc() const { return static_cast<int>(prog_.code.size()); }

                int emit(std::uint8_t op, std::uint8_t arg, std::int32_t x, std::int32_t y)
                {
                    Inst in;
                    in.op = op;
                    in.arg = arg;
                    in.split = 0;
                    in.x = x;
                    in.y = y;
                    prog_.code.push_back(in);
                    return pc() - 1;
                }

                int emit_split(std::int32_t x, std::int32_t y)
                {
                    int at = emit(Op_Split, 0, x, y);
                    prog_.code[static_cast<std::size_t>(at)].split = static_cast<std::uint16_t>(nsplits_ < 65535 ? nsplits_ : 0);
                    ++nsplits_;
                    return at;
                }

                Inst &code(int at) { return prog_.code[static_cast<std::size_t>(at)]; }

                void set_split(int at, bool lazy, int body, int exit)
                {
                    code(at).x = lazy ? exit : body;
                    code(at).y = lazy ? body : exit;
                }

                bool anchored(int n) const
                {
                    const Node &nd = node(n);
                    if (nd.kind == N_Assert)
                        return nd.a == As_Bol;
                    if (nd.kind == N_Concat || nd.kind == N_Group)
                        return anchored(nd.child);
                    if (nd.kind == N_Alt)
                    {
                        for (int c = nd.child; c >= 0; c = node(c).next)
                            if (!anchored(c))
                                return false;
                        return true;
                    }
                    return false;
                }

                void gen_char(std::uint32_t cp, bool back)
                {
                    std::uint8_t arg = back ? 1 : 0;
                    if (icase_)
                    {
                        Vector<std::uint32_t> m;
                        equiv(uni_).members(cp, m);
                        if (m.size() == 2)
                        {
                            emit(Op_Char2, arg, static_cast<std::int32_t>(m[0]), static_cast<std::int32_t>(m[1]));
                            return;
                        }
                        if (m.size() > 2)
                        {
                            CharSet cs;
                            for (std::size_t i = 0; i < m.size(); ++i)
                                cs.add(m[i], m[i]);
                            cs.norm();
                            ClassSet c;
                            c.build(cs);
                            prog_.classes.push_back(static_cast<ClassSet &&>(c));
                            emit(Op_Class, arg, static_cast<std::int32_t>(prog_.classes.size()) - 1, 0);
                            return;
                        }
                    }
                    emit(Op_Char, arg, static_cast<std::int32_t>(cp), 0);
                }

                void reset_caps(const Node &child)
                {
                    if (child.ghi >= child.glo)
                        emit(Op_Reset, 0, child.glo, child.ghi);
                }

                void gen(int n, bool back)
                {
                    const Node &nd = node(n);
                    switch (nd.kind)
                    {
                    case N_Empty:
                        break;
                    case N_Char:
                        gen_char(static_cast<std::uint32_t>(nd.a), back);
                        break;
                    case N_Any:
                        emit(dotall_ ? Op_Any : Op_AnyNoNL, back ? 1 : 0, 0, 0);
                        break;
                    case N_Class:
                        emit(Op_Class, back ? 1 : 0, nd.a, 0);
                        break;
                    case N_Concat:
                    {
                        Vector<int> items;
                        for (int c = nd.child; c >= 0; c = node(c).next)
                            items.push_back(c);
                        if (back)
                            for (std::size_t i = items.size(); i-- > 0;)
                                gen(items[i], true);
                        else
                            for (std::size_t i = 0; i < items.size(); ++i)
                                gen(items[i], false);
                        break;
                    }
                    case N_Alt:
                    {
                        Vector<int> jumps;
                        for (int c = nd.child; c >= 0; c = node(c).next)
                        {
                            if (node(c).next >= 0)
                            {
                                int sp = emit_split(0, 0);
                                code(sp).x = sp + 1;
                                gen(c, back);
                                jumps.push_back(emit(Op_Jmp, 0, 0, 0));
                                code(sp).y = pc();
                            }
                            else
                                gen(c, back);
                        }
                        for (std::size_t i = 0; i < jumps.size(); ++i)
                            code(jumps[i]).x = pc();
                        break;
                    }
                    case N_Group:
                        emit(Op_Save, 0, back ? nd.a * 2 + 1 : nd.a * 2, 0);
                        gen(nd.child, back);
                        emit(Op_Save, 0, back ? nd.a * 2 : nd.a * 2 + 1, 0);
                        break;
                    case N_Backref:
                        prog_.memo_ok = false;
                        emit(Op_Backref, static_cast<std::uint8_t>((back ? 1 : 0) | (icase_ ? 2 : 0)), nd.a, 0);
                        break;
                    case N_Assert:
                        emit(Op_Assert, 0, nd.a, 0);
                        break;
                    case N_Look:
                    {
                        prog_.memo_ok = false;
                        std::uint8_t neg = nd.a & 1;
                        int ls = emit(Op_LookStart, neg, 0, 0);
                        gen(nd.child, nd.a >= 2);
                        emit(Op_LookEnd, neg, 0, 0);
                        code(ls).x = pc();
                        break;
                    }
                    case N_Repeat:
                        gen_repeat(nd, back);
                        break;
                    }
                }

                void gen_repeat(const Node &r, bool back)
                {
                    std::int32_t min = r.a, max = r.b;
                    bool lazy = (r.flag & 1) != 0;
                    if (max == 0)
                        return;
                    const Node &ch = node(r.child);
                    if (min == 1 && max == 1)
                    {
                        gen(r.child, back);
                        return;
                    }
                    if (!back && (ch.kind == N_Char || ch.kind == N_Any || ch.kind == N_Class))
                    {
                        RepInfo ri;
                        ri.min = min;
                        ri.max = max;
                        ri.glo = 0;
                        ri.ghi = -1;
                        ri.lazy = lazy;
                        prog_.reps.push_back(ri);
                        emit(Op_SimpleRep, lazy ? 1 : 0, static_cast<std::int32_t>(prog_.reps.size()) - 1, 0);
                        gen(r.child, false);
                        return;
                    }
                    std::int64_t copies = static_cast<std::int64_t>(min) + (max < 0 ? 1 : max - min);
                    if (ch.minw >= 1 && copies * ch.sz <= 3000)
                    {
                        if (min == 1 && max < 0)
                        {
                            int top = pc();
                            reset_caps(ch);
                            gen(r.child, back);
                            int sp = emit_split(0, 0);
                            set_split(sp, lazy, top, sp + 1);
                            return;
                        }
                        for (std::int32_t i = 0; i < min; ++i)
                        {
                            reset_caps(ch);
                            gen(r.child, back);
                        }
                        if (max < 0)
                        {
                            int top = pc();
                            int sp = emit_split(0, 0);
                            reset_caps(ch);
                            gen(r.child, back);
                            emit(Op_Jmp, 0, top, 0);
                            set_split(sp, lazy, sp + 1, pc());
                            return;
                        }
                        Vector<int> splits;
                        for (std::int32_t i = min; i < max; ++i)
                        {
                            int sp = emit_split(0, 0);
                            splits.push_back(sp);
                            reset_caps(ch);
                            gen(r.child, back);
                        }
                        for (std::size_t i = 0; i < splits.size(); ++i)
                            set_split(splits[i], lazy, splits[i] + 1, pc());
                        return;
                    }
                    prog_.memo_ok = false;
                    RepInfo ri;
                    ri.min = min;
                    ri.max = max;
                    ri.glo = ch.glo;
                    ri.ghi = ch.ghi;
                    ri.lazy = lazy;
                    prog_.reps.push_back(ri);
                    std::int32_t idx = static_cast<std::int32_t>(prog_.reps.size()) - 1;
                    emit(Op_RepInit, 0, idx, 0);
                    int top = pc();
                    int enter = emit(Op_RepEnter, 0, idx, 0);
                    emit(Op_RepBegin, 0, idx, 0);
                    gen(r.child, back);
                    emit(Op_RepNext, 0, idx, top);
                    code(enter).y = pc();
                }

                bool first_of(int n, First &f) const
                {
                    const Node &nd = node(n);
                    switch (nd.kind)
                    {
                    case N_Char:
                    {
                        if (icase_)
                        {
                            Vector<std::uint32_t> m;
                            equiv(uni_).members(static_cast<std::uint32_t>(nd.a), m);
                            for (std::size_t i = 0; i < m.size(); ++i)
                                f.set(m[i]);
                        }
                        f.set(static_cast<std::uint32_t>(nd.a));
                        return false;
                    }
                    case N_Any:
                        f.any = true;
                        return false;
                    case N_Class:
                    {
                        const ClassSet &c = prog_.classes[static_cast<std::size_t>(nd.a)];
                        for (int i = 0; i < 8; ++i)
                            f.bits[i] |= c.bits[i];
                        if (!c.ranges.empty())
                            f.high = true;
                        return false;
                    }
                    case N_Concat:
                        for (int c = nd.child; c >= 0; c = node(c).next)
                            if (!first_of(c, f))
                                return false;
                        return true;
                    case N_Alt:
                    {
                        bool nullable = false;
                        for (int c = nd.child; c >= 0; c = node(c).next)
                            if (first_of(c, f))
                                nullable = true;
                        return nullable;
                    }
                    case N_Repeat:
                    {
                        if (nd.b == 0)
                            return true;
                        bool n0 = first_of(nd.child, f);
                        return n0 || nd.a == 0;
                    }
                    case N_Group:
                        return first_of(nd.child, f);
                    case N_Backref:
                        f.any = true;
                        return true;
                    default:
                        return true;
                    }
                }
            };

            enum FrameKind : std::uint32_t
            {
                Fr_Alt,
                Fr_UndoCap,
                Fr_UndoCnt,
                Fr_UndoStart,
                Fr_LookPos,
                Fr_LookNeg,
                Fr_Back,
                Fr_Lazy
            };

            struct Frame
            {
                std::uint32_t kind;
                std::int32_t pc;
                std::uint32_t pos;
                std::uint32_t val;
            };

            template <typename Unit>
            class Runner
            {
            public:
                static constexpr std::size_t kMaxMemoBits = std::size_t(1) << 28;
                static constexpr bool kWide = sizeof(Unit) > 1;

                Runner(const Program &prog, const Unit *s, std::size_t n, std::size_t max_steps, std::size_t max_stack)
                    : prog_(prog), s_(s), n_(static_cast<std::uint32_t>(n)), unicode_(prog.unicode && kWide),
                      wordx_(prog.unicode && prog.icase), steps_(0), max_steps_(max_steps), max_stack_(max_stack),
                      memo_ready_(false), memo_on_(false), stride_(n + 1), threshold_(0)
                {
                    caps_.resize((prog.ngroups + 1) * 2, kNone);
                    cnt_.resize(prog.reps.size(), 0);
                    start_.resize(prog.reps.size(), kNone);
                    stack_.reserve(64);
                    if (prog.memo_ok && prog.nsplits > 0)
                    {
                        std::size_t bits = prog.nsplits * stride_;
                        memo_ready_ = bits / stride_ == prog.nsplits && bits <= kMaxMemoBits;
                        threshold_ = 64 + bits / 512;
                    }
                }

                const Vector<std::uint32_t> &caps() const noexcept { return caps_; }

                int run(std::size_t start)
                {
                    stack_.clear();
                    for (std::size_t k = 0; k < caps_.size(); ++k)
                        caps_[k] = kNone;
                    const Inst *code = prog_.code.data();
                    std::int32_t pc = 0;
                    std::uint32_t pos = static_cast<std::uint32_t>(start);
                    caps_[0] = pos;
                    for (;;)
                    {
                        const Inst &in = code[pc];
                        switch (in.op)
                        {
                        case Op_Char:
                        case Op_Char2:
                        case Op_Class:
                        case Op_Any:
                        case Op_AnyNoNL:
                            if (take(in, pos))
                            {
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_Split:
                            if (++steps_ > max_steps_ || stack_.size() >= max_stack_)
                                return 2;
                            if (!memo_on_ && memo_ready_ && steps_ > threshold_)
                                enable_memo();
                            if (memo_on_)
                            {
                                std::size_t bit = static_cast<std::size_t>(in.split) * stride_ + pos;
                                std::size_t w = bit >> 6;
                                std::uint64_t mask = std::uint64_t(1) << (bit & 63);
                                if (memo_[w] & mask)
                                    break;
                                memo_[w] |= mask;
                            }
                            push(Fr_Alt, in.y, pos, 0);
                            pc = in.x;
                            continue;
                        case Op_Jmp:
                            pc = in.x;
                            continue;
                        case Op_Save:
                            push(Fr_UndoCap, in.x, 0, caps_[static_cast<std::size_t>(in.x)]);
                            caps_[static_cast<std::size_t>(in.x)] = pos;
                            ++pc;
                            continue;
                        case Op_Match:
                            caps_[1] = pos;
                            return 1;
                        case Op_Assert:
                            if (check_assert(in.x, pos))
                            {
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_Backref:
                            if (backref(in, pos))
                            {
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_Reset:
                            reset_caps(in.x, in.y);
                            ++pc;
                            continue;
                        case Op_SimpleRep:
                        {
                            const RepInfo &ri = prog_.reps[static_cast<std::size_t>(in.x)];
                            const Inst &body = code[pc + 1];
                            std::uint32_t p = pos, c = 0;
                            bool ok = true;
                            for (; c < static_cast<std::uint32_t>(ri.min); ++c)
                                if (!take(body, p))
                                {
                                    ok = false;
                                    break;
                                }
                            if (!ok)
                                break;
                            if (in.arg)
                            {
                                if (ri.max < 0 || c < static_cast<std::uint32_t>(ri.max))
                                {
                                    if (stack_.size() >= max_stack_)
                                        return 2;
                                    push(Fr_Lazy, pc, p, c);
                                }
                            }
                            else
                            {
                                std::uint32_t minpos = p;
                                while ((ri.max < 0 || c < static_cast<std::uint32_t>(ri.max)) && take(body, p))
                                    ++c;
                                steps_ += c;
                                if (steps_ > max_steps_)
                                    return 2;
                                if (p > minpos)
                                {
                                    if (stack_.size() >= max_stack_)
                                        return 2;
                                    push(Fr_Back, pc + 2, p, minpos);
                                }
                            }
                            pos = p;
                            pc += 2;
                            continue;
                        }
                        case Op_RepInit:
                            push(Fr_UndoCnt, in.x, 0, cnt_[static_cast<std::size_t>(in.x)]);
                            cnt_[static_cast<std::size_t>(in.x)] = 0;
                            ++pc;
                            continue;
                        case Op_RepEnter:
                        {
                            const RepInfo &ri = prog_.reps[static_cast<std::size_t>(in.x)];
                            std::uint32_t c = cnt_[static_cast<std::size_t>(in.x)];
                            if (++steps_ > max_steps_ || stack_.size() >= max_stack_)
                                return 2;
                            if (ri.max >= 0 && c >= static_cast<std::uint32_t>(ri.max))
                            {
                                pc = in.y;
                                continue;
                            }
                            if (c < static_cast<std::uint32_t>(ri.min))
                            {
                                ++pc;
                                continue;
                            }
                            if (ri.lazy)
                            {
                                push(Fr_Alt, pc + 1, pos, 0);
                                pc = in.y;
                            }
                            else
                            {
                                push(Fr_Alt, in.y, pos, 0);
                                ++pc;
                            }
                            continue;
                        }
                        case Op_RepBegin:
                        {
                            const RepInfo &ri = prog_.reps[static_cast<std::size_t>(in.x)];
                            push(Fr_UndoStart, in.x, 0, start_[static_cast<std::size_t>(in.x)]);
                            start_[static_cast<std::size_t>(in.x)] = pos;
                            if (ri.ghi >= ri.glo)
                                reset_caps(ri.glo, ri.ghi);
                            ++pc;
                            continue;
                        }
                        case Op_RepNext:
                        {
                            const RepInfo &ri = prog_.reps[static_cast<std::size_t>(in.x)];
                            std::uint32_t c = cnt_[static_cast<std::size_t>(in.x)];
                            if (c >= static_cast<std::uint32_t>(ri.min) && pos == start_[static_cast<std::size_t>(in.x)])
                                break;
                            push(Fr_UndoCnt, in.x, 0, c);
                            cnt_[static_cast<std::size_t>(in.x)] = c + 1;
                            pc = in.y;
                            continue;
                        }
                        case Op_LookStart:
                            if (stack_.size() >= max_stack_)
                                return 2;
                            push(in.arg ? Fr_LookNeg : Fr_LookPos, in.x, pos, 0);
                            ++pc;
                            continue;
                        case Op_LookEnd:
                            if (in.arg)
                            {
                                unwind_neg();
                                break;
                            }
                            pos = commit_look();
                            ++pc;
                            continue;
                        }
                        int r = backtrack(pc, pos);
                        if (r != 1)
                            return r == 0 ? 0 : 2;
                    }
                }

            private:
                const Program &prog_;
                const Unit *s_;
                std::uint32_t n_;
                bool unicode_;
                bool wordx_;
                std::size_t steps_;
                std::size_t max_steps_;
                std::size_t max_stack_;
                bool memo_ready_;
                bool memo_on_;
                std::size_t stride_;
                std::size_t threshold_;
                Vector<std::uint32_t> caps_;
                Vector<std::uint32_t> cnt_;
                Vector<std::uint32_t> start_;
                Vector<Frame> stack_;
                Vector<std::uint64_t> memo_;

                void enable_memo()
                {
                    memo_.resize(((prog_.nsplits * stride_) + 63) / 64, 0);
                    memo_on_ = true;
                }

                void push(std::uint32_t kind, std::int32_t pc, std::uint32_t pos, std::uint32_t val)
                {
                    Frame f;
                    f.kind = kind;
                    f.pc = pc;
                    f.pos = pos;
                    f.val = val;
                    stack_.push_back(f);
                }

                void reset_caps(std::int32_t glo, std::int32_t ghi)
                {
                    for (std::size_t k = static_cast<std::size_t>(glo) * 2; k < (static_cast<std::size_t>(ghi) + 1) * 2; ++k)
                        if (caps_[k] != kNone)
                        {
                            push(Fr_UndoCap, static_cast<std::int32_t>(k), 0, caps_[k]);
                            caps_[k] = kNone;
                        }
                }

                static bool is_high(std::uint32_t c) noexcept { return c >= 0xD800 && c <= 0xDBFF; }
                static bool is_low(std::uint32_t c) noexcept { return c >= 0xDC00 && c <= 0xDFFF; }

                std::uint32_t rd_fwd(std::uint32_t pos, std::uint32_t &cp) const noexcept
                {
                    if (pos >= n_)
                        return 0;
                    std::uint32_t c = s_[pos];
                    if (kWide && unicode_ && is_high(c) && pos + 1 < n_ && is_low(s_[pos + 1]))
                    {
                        cp = 0x10000 + ((c - 0xD800) << 10) + (s_[pos + 1] - 0xDC00);
                        return 2;
                    }
                    cp = c;
                    return 1;
                }

                std::uint32_t rd_bwd(std::uint32_t pos, std::uint32_t &cp) const noexcept
                {
                    if (pos == 0)
                        return 0;
                    std::uint32_t c = s_[pos - 1];
                    if (kWide && unicode_ && is_low(c) && pos >= 2 && is_high(s_[pos - 2]))
                    {
                        cp = 0x10000 + ((static_cast<std::uint32_t>(s_[pos - 2]) - 0xD800) << 10) + (c - 0xDC00);
                        return 2;
                    }
                    cp = c;
                    return 1;
                }

                bool test(const Inst &in, std::uint32_t cp) const noexcept
                {
                    switch (in.op)
                    {
                    case Op_Char:
                        return cp == static_cast<std::uint32_t>(in.x);
                    case Op_Char2:
                        return cp == static_cast<std::uint32_t>(in.x) || cp == static_cast<std::uint32_t>(in.y);
                    case Op_Class:
                        return prog_.classes[static_cast<std::size_t>(in.x)].contains(cp);
                    case Op_Any:
                        return true;
                    default:
                        return !is_line_term(cp);
                    }
                }

                bool take(const Inst &in, std::uint32_t &pos) const noexcept
                {
                    std::uint32_t cp;
                    if (in.arg & 1)
                    {
                        std::uint32_t len = rd_bwd(pos, cp);
                        if (!len || !test(in, cp))
                            return false;
                        pos -= len;
                        return true;
                    }
                    std::uint32_t len = rd_fwd(pos, cp);
                    if (!len || !test(in, cp))
                        return false;
                    pos += len;
                    return true;
                }

                bool word(std::uint32_t c) const noexcept
                {
                    return c < 128 ? is_ascii_word(c) : (wordx_ && (c == 0x17F || c == 0x212A));
                }

                bool check_assert(std::int32_t kind, std::uint32_t pos) const noexcept
                {
                    switch (kind)
                    {
                    case As_Bol:
                        return pos == 0;
                    case As_BolMulti:
                        return pos == 0 || is_line_term(s_[pos - 1]);
                    case As_Eol:
                        return pos == n_;
                    case As_EolMulti:
                        return pos == n_ || is_line_term(s_[pos]);
                    default:
                    {
                        bool a = pos > 0 && word(s_[pos - 1]);
                        bool b = pos < n_ && word(s_[pos]);
                        return (a != b) == (kind == As_WordB);
                    }
                    }
                }

                bool backref(const Inst &in, std::uint32_t &pos) const noexcept
                {
                    std::uint32_t a = caps_[static_cast<std::size_t>(in.x) * 2];
                    std::uint32_t b = caps_[static_cast<std::size_t>(in.x) * 2 + 1];
                    if (a == kNone || b == kNone)
                        return true;
                    std::uint32_t len = b - a;
                    bool back = (in.arg & 1) != 0;
                    if (back ? pos < len : static_cast<std::uint64_t>(pos) + len > n_)
                        return false;
                    std::uint32_t from = back ? pos - len : pos;
                    if (in.arg & 2)
                    {
                        std::uint32_t i = 0;
                        while (i < len)
                        {
                            std::uint32_t c1 = 0, c2 = 0;
                            std::uint32_t l1 = rd_fwd(a + i, c1);
                            std::uint32_t l2 = rd_fwd(from + i, c2);
                            if (l1 != l2 || canon(c1, prog_.unicode) != canon(c2, prog_.unicode))
                                return false;
                            i += l1;
                        }
                    }
                    else
                        for (std::uint32_t i = 0; i < len; ++i)
                            if (s_[a + i] != s_[from + i])
                                return false;
                    pos = back ? from : pos + len;
                    return true;
                }

                void unwind_neg()
                {
                    while (!stack_.empty())
                    {
                        Frame &f = stack_.back();
                        switch (f.kind)
                        {
                        case Fr_UndoCap:
                            caps_[static_cast<std::size_t>(f.pc)] = f.val;
                            break;
                        case Fr_UndoCnt:
                            cnt_[static_cast<std::size_t>(f.pc)] = f.val;
                            break;
                        case Fr_UndoStart:
                            start_[static_cast<std::size_t>(f.pc)] = f.val;
                            break;
                        case Fr_LookNeg:
                            stack_.pop_back();
                            return;
                        default:
                            break;
                        }
                        stack_.pop_back();
                    }
                }

                std::uint32_t commit_look()
                {
                    std::size_t m = stack_.size();
                    while (m > 0 && stack_[m - 1].kind != Fr_LookPos)
                        --m;
                    std::size_t mark = m - 1;
                    std::uint32_t pos = stack_[mark].pos;
                    std::size_t w = mark;
                    for (std::size_t i = mark + 1; i < stack_.size(); ++i)
                    {
                        std::uint32_t k = stack_[i].kind;
                        if (k == Fr_UndoCap || k == Fr_UndoCnt || k == Fr_UndoStart)
                            stack_[w++] = stack_[i];
                    }
                    stack_.resize(w);
                    return pos;
                }

                int backtrack(std::int32_t &pc, std::uint32_t &pos)
                {
                    const Inst *code = prog_.code.data();
                    for (;;)
                    {
                        if (stack_.empty())
                            return 0;
                        Frame &f = stack_.back();
                        switch (f.kind)
                        {
                        case Fr_UndoCap:
                            caps_[static_cast<std::size_t>(f.pc)] = f.val;
                            stack_.pop_back();
                            continue;
                        case Fr_UndoCnt:
                            cnt_[static_cast<std::size_t>(f.pc)] = f.val;
                            stack_.pop_back();
                            continue;
                        case Fr_UndoStart:
                            start_[static_cast<std::size_t>(f.pc)] = f.val;
                            stack_.pop_back();
                            continue;
                        case Fr_LookPos:
                            stack_.pop_back();
                            continue;
                        case Fr_Alt:
                        case Fr_LookNeg:
                            if (++steps_ > max_steps_)
                                return 2;
                            pc = f.pc;
                            pos = f.pos;
                            stack_.pop_back();
                            return 1;
                        case Fr_Back:
                        {
                            if (++steps_ > max_steps_)
                                return 2;
                            std::uint32_t cp;
                            std::uint32_t p = f.pos - rd_bwd(f.pos, cp);
                            pc = f.pc;
                            pos = p;
                            if (p > f.val)
                                f.pos = p;
                            else
                                stack_.pop_back();
                            return 1;
                        }
                        default:
                        {
                            if (++steps_ > max_steps_)
                                return 2;
                            const Inst &rep = code[f.pc];
                            const RepInfo &ri = prog_.reps[static_cast<std::size_t>(rep.x)];
                            std::uint32_t p = f.pos;
                            if (!take(code[f.pc + 1], p))
                            {
                                stack_.pop_back();
                                continue;
                            }
                            pc = f.pc + 2;
                            pos = p;
                            f.pos = p;
                            ++f.val;
                            if (ri.max >= 0 && f.val >= static_cast<std::uint32_t>(ri.max))
                                stack_.pop_back();
                            return 1;
                        }
                        }
                    }
                }
            };
        }
    }

    struct JsCaptures
    {
        enum : std::size_t
        {
            npos = static_cast<std::size_t>(-1)
        };

        Vector<std::size_t> spans;

        std::size_t groups() const noexcept { return spans.empty() ? 0 : spans.size() / 2 - 1; }
        bool matched(std::size_t g = 0) const { return spans[2 * g] != npos; }
        std::size_t start(std::size_t g = 0) const { return spans[2 * g]; }
        std::size_t end(std::size_t g = 0) const { return spans[2 * g + 1]; }
    };

    class JsRegex
    {
    public:
        enum : unsigned
        {
            Global = detail::jsre::F_Global,
            IgnoreCase = detail::jsre::F_IgnoreCase,
            Multiline = detail::jsre::F_Multiline,
            DotAll = detail::jsre::F_DotAll,
            Unicode = detail::jsre::F_Unicode,
            UnicodeSets = detail::jsre::F_UnicodeSets,
            Sticky = detail::jsre::F_Sticky,
            HasIndices = detail::jsre::F_HasIndices
        };

        enum ExecResult
        {
            NoMatch = 0,
            Matched = 1,
            TooComplex = 2
        };

        struct Error
        {
            const char *message;
            std::size_t offset;

            Error() noexcept : message(nullptr), offset(0) {}
            explicit operator bool() const noexcept { return message != nullptr; }
        };

        struct Limits
        {
            std::size_t max_steps;
            std::size_t max_stack;

            Limits() noexcept : max_steps(std::size_t(1) << 30), max_stack(std::size_t(1) << 24) {}
        };

        JsRegex() noexcept : flags_(0), valid_(false) {}

        static JsRegex compile(const char16_t *pat, std::size_t n, unsigned flags, Error *err = nullptr);
        static JsRegex compile(StringView pattern, unsigned flags, Error *err = nullptr);

        bool valid() const noexcept { return valid_; }
        explicit operator bool() const noexcept { return valid_; }
        unsigned flags() const noexcept { return flags_; }
        const Vector<char16_t> &source() const noexcept { return source_; }
        std::size_t group_count() const noexcept { return prog_.ngroups; }

        StringView group_name(std::size_t g) const
        {
            if (g >= prog_.names.size())
                detail::fatal("ct::JsRegex::group_name: grupo invalido");
            return StringView(prog_.names[g]);
        }

        int group_index(StringView name) const noexcept
        {
            for (std::size_t g = 1; g < prog_.names.size(); ++g)
                if (StringView(prog_.names[g]) == name)
                    return static_cast<int>(g);
            return -1;
        }

        bool has_named_groups() const noexcept
        {
            for (std::size_t g = 1; g < prog_.names.size(); ++g)
                if (prog_.names[g].size() != 0)
                    return true;
            return false;
        }

        void set_limits(const Limits &l) noexcept { limits_ = l; }
        const Limits &limits() const noexcept { return limits_; }

        ExecResult exec(const char16_t *s, std::size_t n, std::size_t start, bool sticky, JsCaptures &out) const
        {
            return run<char16_t>(s, n, start, sticky, out);
        }

        ExecResult exec(const unsigned char *s, std::size_t n, std::size_t start, bool sticky, JsCaptures &out) const
        {
            return run<unsigned char>(s, n, start, sticky, out);
        }

    private:
        detail::jsre::Program prog_;
        Vector<char16_t> source_;
        unsigned flags_;
        bool valid_;
        Limits limits_;

        template <typename Unit>
        ExecResult run(const Unit *s, std::size_t n, std::size_t start, bool sticky, JsCaptures &out) const;
    };

    inline JsRegex JsRegex::compile(const char16_t *pat, std::size_t n, unsigned flags, Error *err)
    {
        JsRegex re;
        re.flags_ = flags;
        for (std::size_t i = 0; i < n; ++i)
            re.source_.push_back(pat[i]);
        if (err)
            *err = Error();
        if ((flags & UnicodeSets) && (flags & Unicode))
        {
            if (err)
                err->message = "Invalid flags";
            return re;
        }
        Vector<detail::jsre::Node> nodes;
        nodes.reserve(n + 4);
        detail::jsre::Parser parser(pat, n, flags, re.prog_, nodes);
        if (!parser.parse())
        {
            if (err)
            {
                err->message = parser.error();
                err->offset = parser.error_offset();
            }
            re.prog_ = detail::jsre::Program();
            return re;
        }
        detail::jsre::Compiler compiler(re.prog_, nodes, flags);
        compiler.compile(parser.root());
        re.valid_ = true;
        return re;
    }

    inline JsRegex JsRegex::compile(StringView pattern, unsigned flags, Error *err)
    {
        Vector<char16_t> u;
        u.reserve(pattern.size());
        for (std::size_t i = 0; i < pattern.size();)
        {
            std::uint32_t cp;
            i += detail::jsre::utf8_decode(pattern.data(), pattern.size(), i, cp);
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                u.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                u.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
            else
                u.push_back(static_cast<char16_t>(cp));
        }
        return compile(u.data(), u.size(), flags, err);
    }

    template <typename Unit>
    inline JsRegex::ExecResult JsRegex::run(const Unit *s, std::size_t n, std::size_t start, bool sticky, JsCaptures &out) const
    {
        using namespace detail::jsre;
        if (!valid_)
            detail::fatal("ct::JsRegex::exec: regex invalido");
        out.spans.clear();
        out.spans.resize((prog_.ngroups + 1) * 2, JsCaptures::npos);
        if (start > n)
            return NoMatch;
        if (n >= 0xFFFFFFFFu)
            return TooComplex;
        const bool wide = sizeof(Unit) > 1;
        Runner<Unit> runner(prog_, s, n, limits_.max_steps, limits_.max_stack);
        std::size_t pos = start;
        if (wide && prog_.unicode && pos > 0 && pos < n && s[pos] >= 0xDC00 && s[pos] <= 0xDFFF &&
            s[pos - 1] >= 0xD800 && s[pos - 1] <= 0xDBFF)
            --pos;
        if (prog_.anchored && !sticky)
            sticky = true, pos = pos == 0 ? 0 : n + 1;
        for (;;)
        {
            if (pos > n || n - pos < prog_.min_len)
                return NoMatch;
            if (!prog_.use_first || prog_.first_has(s[pos]))
            {
                int st = runner.run(pos);
                if (st == 1)
                {
                    const Vector<std::uint32_t> &c = runner.caps();
                    for (std::size_t k = 0; k < c.size(); ++k)
                        out.spans[k] = c[k] == kNone ? static_cast<std::size_t>(JsCaptures::npos) : static_cast<std::size_t>(c[k]);
                    return Matched;
                }
                if (st == 2)
                    return TooComplex;
            }
            if (sticky || pos >= n)
                return NoMatch;
            if (wide && prog_.unicode && s[pos] >= 0xD800 && s[pos] <= 0xDBFF && pos + 1 < n && s[pos + 1] >= 0xDC00 &&
                s[pos + 1] <= 0xDFFF)
                pos += 2;
            else
                ++pos;
        }
    }
}
