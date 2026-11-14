// Fuzz diferencial: sequencias aleatorias de operacoes em ct::Vector, String, HashMap, HashSet, FlatMap e TreeMap
// comparadas com um modelo de referencia escrito em arrays C simples (sem biblioteca standard). Inclui os casos que
// costumam partir: argumento que e uma referencia para dentro do proprio contentor (push_back(v[0]) quando cresce,
// insert(pos, v[i]), append/insert/assign de uma fatia da propria string), hashers fracos (muitas colisoes),
// fronteira do SSO e NULs no meio.
//
// CT_FUZZ_ITERS (variavel de ambiente) muda o numero de operacoes por semente; por omissao fica rapido.

#include <ct/deque.hpp>
#include <ct/flatmap.hpp>
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
        uint32_t next()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return static_cast<uint32_t>(s >> 16);
        }
        uint32_t below(uint32_t n) { return n ? next() % n : 0; }
    };

    int fuzz_iters(int fallback)
    {
        const char *e = getenv("CT_FUZZ_ITERS");
        if (!e || !*e)
            return fallback;
        const int v = atoi(e);
        return v > 0 ? v : fallback;
    }

    // Conta os vivos e confirma que ninguem usa um objeto ja destruido ou nunca construido.
    struct Counted
    {
        static int live;
        static int bad;
        int id;
        uint32_t magic;
        Counted() : id(0), magic(0xC0FFEEu) { ++live; }
        explicit Counted(int v) : id(v), magic(0xC0FFEEu) { ++live; }
        Counted(const Counted &o) : id(o.id), magic(0xC0FFEEu)
        {
            if (o.magic != 0xC0FFEEu)
                ++bad;
            ++live;
        }
        Counted(Counted &&o) noexcept : id(o.id), magic(0xC0FFEEu)
        {
            if (o.magic != 0xC0FFEEu)
                ++bad;
            ++live;
        }
        Counted &operator=(const Counted &o)
        {
            if (magic != 0xC0FFEEu || o.magic != 0xC0FFEEu)
                ++bad;
            id = o.id;
            return *this;
        }
        Counted &operator=(Counted &&o) noexcept
        {
            if (magic != 0xC0FFEEu || o.magic != 0xC0FFEEu)
                ++bad;
            id = o.id;
            return *this;
        }
        ~Counted()
        {
            if (magic != 0xC0FFEEu)
                ++bad;
            magic = 0xDEADu;
            --live;
        }
        bool operator==(const Counted &o) const { return id == o.id; }
        bool operator<(const Counted &o) const { return id < o.id; }
    };
    int Counted::live = 0;
    int Counted::bad = 0;

    struct Pod
    {
        int id;
        int pad;
        bool operator==(const Pod &o) const { return id == o.id; }
    };

    // Elementos identificados por um inteiro; o String tem comprimento que atravessa a fronteira do SSO (23).
    template <typename T>
    struct Make;
    template <>
    struct Make<int>
    {
        static int make(int id) { return id; }
        static int id_of(const int &v) { return v; }
    };
    template <>
    struct Make<Pod>
    {
        static Pod make(int id)
        {
            Pod p;
            p.id = id;
            p.pad = id * 3;
            return p;
        }
        static int id_of(const Pod &v) { return v.id; }
    };
    template <>
    struct Make<Counted>
    {
        static Counted make(int id) { return Counted(id); }
        static int id_of(const Counted &v) { return v.id; }
    };
    template <>
    struct Make<ct::String>
    {
        static ct::String make(int id)
        {
            ct::String s;
            s.append_number(id);
            s.push_back(':');
            s.append(static_cast<size_t>(id % 30), 'x');
            return s;
        }
        static int id_of(const ct::String &v) { return atoi(v.c_str()); }
    };

    enum
    {
        kMaxVec = 160
    };

    template <typename T>
    void fuzz_vector(uint64_t seed, int iters)
    {
        typedef Make<T> M;
        Rng rng(seed);
        ct::Vector<T> v;
        int ref[kMaxVec + 40];
        size_t n = 0;
        // segundo vetor para copia, move e swap
        ct::Vector<T> w;
        int wref[kMaxVec + 40];
        size_t wn = 0;

        for (int it = 0; it < iters; ++it)
        {
            const uint32_t op = rng.below(22);
            const int id = static_cast<int>(rng.below(1000));
            const size_t pos = n ? rng.below(static_cast<uint32_t>(n)) : 0;
            const bool room = n + 40 < sizeof(ref) / sizeof(ref[0]);
            switch (op)
            {
            case 0:
            case 1:
                if (room)
                {
                    v.push_back(M::make(id));
                    ref[n++] = id;
                }
                break;
            case 2:
                if (n && room)
                {
                    v.push_back(v[pos]);
                    ref[n] = ref[pos];
                    ++n;
                }
                break;
            case 3:
                if (n && room)
                {
                    const int old_id = ref[pos];
                    v.push_back(ct::move(v[pos]));
                    ref[n] = old_id;
                    ++n;
                    v[pos] = M::make(old_id);
                }
                break;
            case 4:
                if (room)
                {
                    v.emplace_back(M::make(id));
                    ref[n++] = id;
                }
                break;
            case 5:
                if (n && room)
                {
                    const T &alias = v[rng.below(static_cast<uint32_t>(n))];
                    const int aid = M::id_of(alias);
                    v.insert(v.begin() + pos, alias);
                    for (size_t i = n; i > pos; --i)
                        ref[i] = ref[i - 1];
                    ref[pos] = aid;
                    ++n;
                }
                break;
            case 6:
                if (room)
                {
                    const size_t at = n ? rng.below(static_cast<uint32_t>(n + 1)) : 0;
                    v.insert(v.begin() + at, M::make(id));
                    for (size_t i = n; i > at; --i)
                        ref[i] = ref[i - 1];
                    ref[at] = id;
                    ++n;
                }
                break;
            case 7:
                if (room)
                {
                    const size_t at = n ? rng.below(static_cast<uint32_t>(n + 1)) : 0;
                    v.emplace(v.begin() + at, M::make(id));
                    for (size_t i = n; i > at; --i)
                        ref[i] = ref[i - 1];
                    ref[at] = id;
                    ++n;
                }
                break;
            case 8:
                if (n)
                {
                    v.erase(v.begin() + pos);
                    for (size_t i = pos; i + 1 < n; ++i)
                        ref[i] = ref[i + 1];
                    --n;
                }
                break;
            case 9:
                if (n)
                {
                    const size_t last = pos + rng.below(static_cast<uint32_t>(n - pos) + 1);
                    v.erase(v.begin() + pos, v.begin() + last);
                    const size_t cut = last - pos;
                    for (size_t i = pos; i + cut < n; ++i)
                        ref[i] = ref[i + cut];
                    n -= cut;
                }
                break;
            case 10:
                if (n)
                {
                    v.pop_back();
                    --n;
                }
                break;
            case 11:
            {
                const size_t to = rng.below(kMaxVec);
                v.resize(to);
                for (size_t i = n; i < to; ++i)
                    ref[i] = M::id_of(v[i]);
                n = to;
                break;
            }
            case 12:
                if (n && room)
                {
                    const size_t to = n + rng.below(30);
                    const T &alias = v[pos];
                    const int aid = M::id_of(alias);
                    v.resize(to, alias);
                    for (size_t i = n; i < to; ++i)
                        ref[i] = aid;
                    n = to;
                }
                break;
            case 13:
                v.reserve(rng.below(300));
                break;
            case 14:
                v.shrink_to_fit();
                break;
            case 15:
                if (rng.below(8) == 0)
                {
                    v.clear();
                    n = 0;
                }
                break;
            case 16:
            {
                ct::Vector<T> c(v);
                w = c;
                memcpy(wref, ref, n * sizeof(int));
                wn = n;
                break;
            }
            case 17:
                v.swap(w);
                {
                    int tmp[kMaxVec + 40];
                    memcpy(tmp, ref, n * sizeof(int));
                    memcpy(ref, wref, wn * sizeof(int));
                    memcpy(wref, tmp, n * sizeof(int));
                    const size_t t = n;
                    n = wn;
                    wn = t;
                }
                break;
            case 18:
            {
                ct::Vector<T> moved(ct::move(v));
                v = ct::move(moved);
                break;
            }
            case 19:
                v = v;
                break;
            case 20:
            {
                const size_t to = rng.below(50);
                v.assign(to, M::make(id));
                for (size_t i = 0; i < to; ++i)
                    ref[i] = id;
                n = to;
                break;
            }
            case 21:
                if (n)
                {
                    const size_t at = v.index_of(v[pos]);
                    size_t expect = 0;
                    while (!(v[expect] == v[pos]))
                        ++expect;
                    ASSERT_EQ(at, expect);
                    ASSERT_TRUE(v.contains(v[pos]));
                }
                break;
            }
            ASSERT_EQ(v.size(), n);
            for (size_t i = 0; i < n; ++i)
                ASSERT_EQ(M::id_of(v[i]), ref[i]) << "op " << op << " it " << it << " i " << i;
            ASSERT_LE(n, v.capacity());
            ASSERT_EQ(w.size(), wn);
            for (size_t i = 0; i < wn; ++i)
                ASSERT_EQ(M::id_of(w[i]), wref[i]);
        }
    }

    TEST(Fuzz, VectorInt)
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_vector<int>(seed, fuzz_iters(4000));
    }
    TEST(Fuzz, VectorPod)
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_vector<Pod>(seed, fuzz_iters(4000));
    }
    TEST(Fuzz, VectorString)
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_vector<ct::String>(seed, fuzz_iters(4000));
    }
    TEST(Fuzz, VectorCounted)
    {
        Counted::bad = 0;
        const int before = Counted::live;
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_vector<Counted>(seed, fuzz_iters(4000));
        EXPECT_EQ(Counted::live, before);
        EXPECT_EQ(Counted::bad, 0);
    }

    // ---- Deque -------------------------------------------------------------------------------------------------

    template <typename T>
    void fuzz_deque(uint64_t seed, int iters)
    {
        typedef Make<T> M;
        Rng rng(seed);
        ct::Deque<T> d;
        ct::Deque<T> other;
        int ref[kMaxVec + 40];
        int oref[kMaxVec + 40];
        size_t n = 0, on = 0;
        for (int it = 0; it < iters; ++it)
        {
            const uint32_t op = rng.below(17);
            const int id = static_cast<int>(rng.below(1000));
            const size_t pos = n ? rng.below(static_cast<uint32_t>(n)) : 0;
            const bool room = n + 40 < sizeof(ref) / sizeof(ref[0]);
            switch (op)
            {
            case 0:
            case 1:
                if (room)
                {
                    d.push_back(M::make(id));
                    ref[n++] = id;
                }
                break;
            case 2:
            case 3:
                if (room)
                {
                    d.push_front(M::make(id));
                    for (size_t i = n; i > 0; --i)
                        ref[i] = ref[i - 1];
                    ref[0] = id;
                    ++n;
                }
                break;
            case 4:
                if (n && room)
                {
                    d.push_back(d[pos]);
                    ref[n] = ref[pos];
                    ++n;
                }
                break;
            case 5:
                if (n && room)
                {
                    const int aid = ref[pos];
                    d.push_front(d[pos]);
                    for (size_t i = n; i > 0; --i)
                        ref[i] = ref[i - 1];
                    ref[0] = aid;
                    ++n;
                }
                break;
            case 6:
                if (n)
                {
                    d.pop_back();
                    --n;
                }
                break;
            case 7:
                if (n)
                {
                    d.pop_front();
                    for (size_t i = 0; i + 1 < n; ++i)
                        ref[i] = ref[i + 1];
                    --n;
                }
                break;
            case 8:
                if (room)
                {
                    d.emplace_back(M::make(id));
                    ref[n++] = id;
                }
                break;
            case 9:
                if (room)
                {
                    d.emplace_front(M::make(id));
                    for (size_t i = n; i > 0; --i)
                        ref[i] = ref[i - 1];
                    ref[0] = id;
                    ++n;
                }
                break;
            case 10:
            {
                const size_t to = rng.below(kMaxVec);
                d.resize(to);
                for (size_t i = n; i < to; ++i)
                    ref[i] = M::id_of(d[i]);
                n = to;
                break;
            }
            case 11:
                if (n && room)
                {
                    const size_t to = n + rng.below(30);
                    const int aid = ref[pos];
                    d.resize(to, d[pos]);
                    for (size_t i = n; i < to; ++i)
                        ref[i] = aid;
                    n = to;
                }
                break;
            case 12:
                d.reserve(rng.below(300));
                break;
            case 13:
                d.shrink_to_fit();
                break;
            case 14:
            {
                ct::Deque<T> copy(d);
                other = copy;
                memcpy(oref, ref, n * sizeof(int));
                on = n;
                break;
            }
            case 15:
                d.swap(other);
                {
                    int tmp[kMaxVec + 40];
                    memcpy(tmp, ref, n * sizeof(int));
                    memcpy(ref, oref, on * sizeof(int));
                    memcpy(oref, tmp, n * sizeof(int));
                    const size_t t = n;
                    n = on;
                    on = t;
                }
                break;
            case 16:
                if (rng.below(6) == 0)
                {
                    ct::Deque<T> moved(ct::move(d));
                    d = ct::move(moved);
                    d = d;
                }
                else if (rng.below(8) == 0)
                {
                    d.clear();
                    n = 0;
                }
                break;
            }
            ASSERT_EQ(d.size(), n) << "op " << op << " it " << it;
            for (size_t i = 0; i < n; ++i)
                ASSERT_EQ(M::id_of(d[i]), ref[i]) << "op " << op << " it " << it << " i " << i;
            if (n)
            {
                ASSERT_EQ(M::id_of(d.front()), ref[0]);
                ASSERT_EQ(M::id_of(d.back()), ref[n - 1]);
            }
            size_t seen = 0;
            for (typename ct::Deque<T>::iterator e = d.begin(); e != d.end(); ++e)
                ASSERT_EQ(M::id_of(*e), ref[seen++]);
            ASSERT_EQ(seen, n);
            ASSERT_EQ(other.size(), on);
            ASSERT_LE(n, d.capacity());
        }
    }

    TEST(Fuzz, DequeInt)
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_deque<int>(seed, fuzz_iters(4000));
    }
    TEST(Fuzz, DequeString)
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_deque<ct::String>(seed, fuzz_iters(3000));
    }
    TEST(Fuzz, DequeCounted)
    {
        Counted::bad = 0;
        const int before = Counted::live;
        for (uint64_t seed = 1; seed <= 4; ++seed)
            fuzz_deque<Counted>(seed, fuzz_iters(3000));
        EXPECT_EQ(Counted::live, before);
        EXPECT_EQ(Counted::bad, 0);
    }

    // ---- String ------------------------------------------------------------------------------------------------

    enum
    {
        kMaxStr = 700
    };

    struct RefString
    {
        char buf[kMaxStr + 200];
        size_t len;
        RefString() : len(0) {}
        void insert(size_t pos, const char *p, size_t n)
        {
            memmove(buf + pos + n, buf + pos, len - pos);
            memmove(buf + pos, p, n);
            len += n;
        }
    };

    size_t naive_find(const char *h, size_t hn, const char *nd, size_t nn, size_t pos)
    {
        if (nn == 0)
            return pos <= hn ? pos : ct::String::npos;
        if (pos > hn || nn > hn - pos)
            return ct::String::npos;
        for (size_t i = pos; i + nn <= hn; ++i)
            if (memcmp(h + i, nd, nn) == 0)
                return i;
        return ct::String::npos;
    }

    TEST(Fuzz, String)
    {
        const int iters = fuzz_iters(6000);
        for (uint64_t seed = 1; seed <= 6; ++seed)
        {
            Rng rng(seed);
            ct::String s;
            RefString r;
            ct::String other;
            RefString ro;
            for (int it = 0; it < iters; ++it)
            {
                const uint32_t op = rng.below(24);
                const size_t n = r.len;
                const size_t pos = rng.below(static_cast<uint32_t>(n + 1));
                const size_t cnt = rng.below(static_cast<uint32_t>(n + 1));
                const size_t slice = n ? rng.below(static_cast<uint32_t>(n)) : 0;
                const size_t slice_len = n ? rng.below(static_cast<uint32_t>(n - slice + 1)) : 0;
                char c = static_cast<char>(rng.below(5) == 0 ? 0 : 'a' + rng.below(26));
                const bool room = n + 300 < kMaxStr;
                switch (op)
                {
                case 0:
                case 1:
                    if (room)
                    {
                        s.push_back(c);
                        r.buf[r.len++] = c;
                    }
                    break;
                case 2:
                    if (n)
                    {
                        s.pop_back();
                        --r.len;
                    }
                    break;
                case 3:
                    if (room)
                    {
                        const size_t k = rng.below(30);
                        char tmp[30];
                        for (size_t i = 0; i < k; ++i)
                            tmp[i] = static_cast<char>(rng.below(8) == 0 ? 0 : 'A' + rng.below(26));
                        s.append(tmp, k);
                        memcpy(r.buf + r.len, tmp, k);
                        r.len += k;
                    }
                    break;
                case 4:
                    if (room && n)
                    {
                        s.append(s.data() + slice, slice_len);
                        memmove(r.buf + r.len, r.buf + slice, slice_len);
                        r.len += slice_len;
                    }
                    break;
                case 5:
                    if (n && n < 200)
                    {
                        s.append(s);
                        memmove(r.buf + r.len, r.buf, r.len);
                        r.len *= 2;
                    }
                    break;
                case 6:
                    if (room)
                    {
                        s.insert(pos, s.data() + slice, slice_len);
                        // a fatia vem do conteudo antigo
                        char tmp[kMaxStr + 200];
                        memcpy(tmp, r.buf + slice, slice_len);
                        r.insert(pos, tmp, slice_len);
                    }
                    break;
                case 7:
                    if (room && n < 200)
                    {
                        const ct::String copy(s);
                        s.insert(pos, s);
                        r.insert(pos, copy.data(), copy.size());
                    }
                    break;
                case 8:
                    if (room)
                    {
                        const size_t k = rng.below(40);
                        s.insert(pos, k, c);
                        memmove(r.buf + pos + k, r.buf + pos, r.len - pos);
                        memset(r.buf + pos, c, k);
                        r.len += k;
                    }
                    break;
                case 9:
                {
                    const size_t k = rng.below(static_cast<uint32_t>(n + 3));
                    s.erase(pos, k);
                    const size_t removed = k < n - pos ? k : n - pos;
                    memmove(r.buf + pos, r.buf + pos + removed, n - pos - removed);
                    r.len -= removed;
                    break;
                }
                case 10:
                    if (n)
                    {
                        // assign de uma fatia de si propria
                        s.assign(s.data() + slice, slice_len);
                        memmove(r.buf, r.buf + slice, slice_len);
                        r.len = slice_len;
                    }
                    break;
                case 11:
                {
                    const size_t to = rng.below(kMaxStr / 2);
                    s.resize(to, c);
                    if (to > n)
                        memset(r.buf + n, c, to - n);
                    r.len = to;
                    break;
                }
                case 12:
                    s.reserve(rng.below(400));
                    break;
                case 13:
                    if (rng.below(16) == 0)
                    {
                        s.clear();
                        r.len = 0;
                    }
                    break;
                case 14:
                {
                    ct::String copy(s);
                    ct::String moved(ct::move(copy));
                    s = ct::move(moved);
                    s = s;
                    break;
                }
                case 15:
                {
                    other.swap(s);
                    RefString t = r;
                    r = ro;
                    ro = t;
                    break;
                }
                case 16:
                {
                    const ct::String sub = s.substr(pos, cnt);
                    const size_t take = cnt < n - pos ? cnt : n - pos;
                    ASSERT_EQ(sub.size(), take);
                    ASSERT_EQ(memcmp(sub.data(), r.buf + pos, take), 0);
                    ASSERT_EQ(sub.c_str()[take], '\0');
                    break;
                }
                case 17:
                {
                    char needle[4];
                    const size_t k = 1 + rng.below(3);
                    for (size_t i = 0; i < k; ++i)
                        needle[i] = static_cast<char>(rng.below(3) == 0 ? 0 : 'a' + rng.below(4));
                    const ct::String ns(needle, k);
                    ASSERT_EQ(s.find(ns, pos), naive_find(r.buf, r.len, needle, k, pos));
                    ASSERT_EQ(s.contains(ns), naive_find(r.buf, r.len, needle, k, 0) != ct::String::npos);
                    break;
                }
                case 18:
                {
                    size_t expect = ct::String::npos;
                    for (size_t i = n; i > 0; --i)
                        if (r.buf[i - 1] == c)
                        {
                            expect = i - 1;
                            break;
                        }
                    ASSERT_EQ(s.rfind(c), expect);
                    size_t first = ct::String::npos;
                    for (size_t i = pos; i < n; ++i)
                        if (r.buf[i] == c)
                        {
                            first = i;
                            break;
                        }
                    ASSERT_EQ(s.find(c, pos), first);
                    break;
                }
                case 19:
                {
                    const ct::String a(r.buf, r.len);
                    const ct::String b(ro.buf, ro.len);
                    const size_t m = r.len < ro.len ? r.len : ro.len;
                    int expect = m ? memcmp(r.buf, ro.buf, m) : 0;
                    if (expect == 0)
                        expect = r.len < ro.len ? -1 : (r.len > ro.len ? 1 : 0);
                    const int got = a.compare(b);
                    ASSERT_EQ(got < 0, expect < 0);
                    ASSERT_EQ(got > 0, expect > 0);
                    ASSERT_EQ(a == b, expect == 0);
                    ASSERT_EQ(a < b, expect < 0);
                    break;
                }
                case 20:
                    if (room)
                    {
                        const ct::String joined = s + other;
                        ASSERT_EQ(joined.size(), r.len + ro.len);
                        ASSERT_EQ(memcmp(joined.data(), r.buf, r.len), 0);
                        ASSERT_EQ(memcmp(joined.data() + r.len, ro.buf, ro.len), 0);
                    }
                    break;
                case 21:
                    if (n)
                    {
                        const size_t k = rng.below(static_cast<uint32_t>(n + 1));
                        ASSERT_EQ(s.starts_with(ct::String(r.buf, k)), true);
                        ASSERT_EQ(s.ends_with(ct::String(r.buf + n - k, k)), true);
                    }
                    break;
                case 22:
                    if (room)
                    {
                        const long long num = static_cast<long long>(rng.next()) - 2000000000ll;
                        char tmp[32];
                        const int k = snprintf(tmp, sizeof(tmp), "%lld", num);
                        s.append_number(num);
                        memcpy(r.buf + r.len, tmp, static_cast<size_t>(k));
                        r.len += static_cast<size_t>(k);
                    }
                    break;
                case 23:
                {
                    // construcao e copia a partir de bytes crus (NULs no meio)
                    const ct::String built(r.buf, r.len);
                    const ct::String copy(built);
                    ASSERT_EQ(copy.size(), r.len);
                    ASSERT_EQ(memcmp(copy.data(), r.buf, r.len), 0);
                    ASSERT_EQ(built.hash(), copy.hash());
                    break;
                }
                }
                ASSERT_EQ(s.size(), r.len) << "op " << op << " it " << it;
                ASSERT_EQ(memcmp(s.data(), r.buf, r.len), 0) << "op " << op << " it " << it;
                ASSERT_EQ(s.c_str()[s.size()], '\0') << "op " << op << " it " << it;
                ASSERT_LE(s.size(), s.capacity());
                ASSERT_EQ(s.is_small(), s.capacity() == 23);
                ASSERT_EQ(other.size(), ro.len);
                ASSERT_EQ(memcmp(other.data(), ro.buf, ro.len), 0);
                ASSERT_EQ(other.c_str()[other.size()], '\0');
            }
        }
    }

    // ---- mapas -------------------------------------------------------------------------------------------------

    enum
    {
        kKeySpace = 400
    };

    struct RefMap
    {
        int key[kKeySpace];
        int val[kKeySpace];
        size_t n;
        RefMap() : n(0) {}
        int find(int k) const
        {
            for (size_t i = 0; i < n; ++i)
                if (key[i] == k)
                    return static_cast<int>(i);
            return -1;
        }
        void put(int k, int v)
        {
            const int i = find(k);
            if (i >= 0)
                val[i] = v;
            else
            {
                key[n] = k;
                val[n] = v;
                ++n;
            }
        }
        bool erase(int k)
        {
            const int i = find(k);
            if (i < 0)
                return false;
            key[i] = key[n - 1];
            val[i] = val[n - 1];
            --n;
            return true;
        }
    };

    struct StrongHash
    {
        uint64_t operator()(int k) const { return ct::Hash<int>()(k); }
    };
    struct WeakHash
    {
        uint64_t operator()(int k) const { return static_cast<uint64_t>(k & 7); }
    };
    struct ConstHash
    {
        uint64_t operator()(int) const { return 42; }
    };
    struct StrideHash
    {
        uint64_t operator()(int k) const { return static_cast<uint64_t>(k) << 12; }
    };
    struct TopBitsHash
    {
        uint64_t operator()(int k) const { return static_cast<uint64_t>(k) << 52; }
    };

    template <typename H>
    void fuzz_hashmap(uint64_t seed, int iters)
    {
        Rng rng(seed);
        ct::HashMap<int, Counted, H> m;
        ct::HashMap<int, Counted, H> other;
        RefMap r;
        RefMap ro;
        const int keyspace = 1 + static_cast<int>(rng.below(kKeySpace));
        for (int it = 0; it < iters; ++it)
        {
            const uint32_t op = rng.below(16);
            const int k = static_cast<int>(rng.below(static_cast<uint32_t>(keyspace)));
            const int v = static_cast<int>(rng.below(100000));
            switch (op)
            {
            case 0:
            case 1:
            case 2:
                m.put(k, Counted(v));
                r.put(k, v);
                break;
            case 3:
            {
                // o valor vem de dentro do proprio mapa: tem de sobreviver ao rehash
                if (r.n)
                {
                    const int src = r.key[rng.below(static_cast<uint32_t>(r.n))];
                    const int sv = r.val[r.find(src)];
                    m.put(k, *m.find(src));
                    r.put(k, sv);
                }
                break;
            }
            case 4:
            {
                Counted &c = m[k];
                const int i = r.find(k);
                if (i < 0)
                {
                    ASSERT_EQ(c.id, 0);
                    r.put(k, 0);
                }
                else
                    ASSERT_EQ(c.id, r.val[i]);
                c.id += 7;
                r.val[r.find(k)] += 7;
                break;
            }
            case 5:
            case 6:
            {
                const bool e = r.erase(k);
                ASSERT_EQ(m.erase(k), e);
                break;
            }
            case 7:
            {
                Counted out(-1);
                const int i = r.find(k);
                const bool e = m.erase(k, out);
                ASSERT_EQ(e, i >= 0);
                if (e)
                {
                    ASSERT_EQ(out.id, r.val[i]);
                    r.erase(k);
                }
                break;
            }
            case 8:
            {
                const Counted *f = m.find(k);
                const int i = r.find(k);
                ASSERT_EQ(f != nullptr, i >= 0);
                if (f)
                {
                    ASSERT_EQ(f->id, r.val[i]);
                }
                ASSERT_EQ(m.contains(k), i >= 0);
                break;
            }
            case 9:
                if (rng.below(12) == 0)
                {
                    m.clear();
                    r.n = 0;
                }
                break;
            case 10:
                m.reserve(rng.below(600));
                break;
            case 11:
            {
                ct::HashMap<int, Counted, H> copy(m);
                other = copy;
                ro = r;
                break;
            }
            case 12:
            {
                ct::HashMap<int, Counted, H> moved(ct::move(m));
                m = ct::move(moved);
                break;
            }
            case 13:
            {
                // apaga as chaves de um percurso: recolhe e apaga fora do percurso
                int keys[kKeySpace];
                size_t nk = 0;
                for (typename ct::HashMap<int, Counted, H>::iterator e = m.begin(); e != m.end(); ++e)
                    keys[nk++] = e->key;
                ASSERT_EQ(nk, r.n);
                for (size_t i = 0; i < nk; ++i)
                    if (rng.below(3) == 0)
                    {
                        ASSERT_TRUE(m.erase(keys[i]));
                        ASSERT_TRUE(r.erase(keys[i]));
                    }
                break;
            }
            case 14:
                m = m;
                break;
            case 15:
            {
                // troca com o outro
                ct::HashMap<int, Counted, H> t(ct::move(m));
                m = ct::move(other);
                other = ct::move(t);
                RefMap rt = r;
                r = ro;
                ro = rt;
                break;
            }
            }
            ASSERT_EQ(m.size(), r.n) << "op " << op << " it " << it;
            ASSERT_EQ(other.size(), ro.n);
            if (it % 7 == 0)
            {
                size_t seen = 0;
                for (typename ct::HashMap<int, Counted, H>::const_iterator e = static_cast<const ct::HashMap<int, Counted, H> &>(m).begin();
                     e != static_cast<const ct::HashMap<int, Counted, H> &>(m).end(); ++e)
                {
                    const int i = r.find(e->key);
                    ASSERT_GE(i, 0);
                    ASSERT_EQ(e->value.id, r.val[i]);
                    ++seen;
                }
                ASSERT_EQ(seen, r.n);
                for (size_t i = 0; i < r.n; ++i)
                {
                    const Counted *f = m.find(r.key[i]);
                    ASSERT_NE(f, nullptr);
                    ASSERT_EQ(f->id, r.val[i]);
                }
            }
        }
    }

    TEST(Fuzz, HashMapStrongHash)
    {
        Counted::bad = 0;
        const int before = Counted::live;
        for (uint64_t seed = 1; seed <= 5; ++seed)
            fuzz_hashmap<StrongHash>(seed, fuzz_iters(5000));
        EXPECT_EQ(Counted::live, before);
        EXPECT_EQ(Counted::bad, 0);
    }
    TEST(Fuzz, HashMapWeakHash)
    {
        for (uint64_t seed = 1; seed <= 5; ++seed)
            fuzz_hashmap<WeakHash>(seed, fuzz_iters(3000));
    }
    TEST(Fuzz, HashMapConstantHash)
    {
        for (uint64_t seed = 1; seed <= 3; ++seed)
            fuzz_hashmap<ConstHash>(seed, fuzz_iters(2000));
    }
    TEST(Fuzz, HashMapStrideHash)
    {
        for (uint64_t seed = 1; seed <= 3; ++seed)
            fuzz_hashmap<StrideHash>(seed, fuzz_iters(2000));
        for (uint64_t seed = 1; seed <= 3; ++seed)
            fuzz_hashmap<TopBitsHash>(seed, fuzz_iters(2000));
    }

    TEST(Fuzz, HashMapStringKeys)
    {
        const int iters = fuzz_iters(5000);
        for (uint64_t seed = 1; seed <= 4; ++seed)
        {
            Rng rng(seed);
            ct::HashMap<ct::String, int> m;
            RefMap r;
            for (int it = 0; it < iters; ++it)
            {
                const int k = static_cast<int>(rng.below(300));
                const ct::String key = Make<ct::String>::make(k);
                const uint32_t op = rng.below(6);
                if (op < 3)
                {
                    m.put(key, it);
                    r.put(k, it);
                }
                else if (op == 3)
                    ASSERT_EQ(m.erase(key), r.erase(k));
                else if (op == 4)
                {
                    const int *f = m.find(key.c_str());
                    const int i = r.find(k);
                    ASSERT_EQ(f != nullptr, i >= 0);
                    if (f)
                    {
                        ASSERT_EQ(*f, r.val[i]);
                    }
                    const int *g = m.find(ct::StringView(key.data(), key.size()));
                    ASSERT_EQ(g, f);
                }
                else
                {
                    int &x = m[key];
                    if (r.find(k) < 0)
                    {
                        ASSERT_EQ(x, 0);
                        r.put(k, 0);
                    }
                    ASSERT_EQ(x, r.val[r.find(k)]);
                }
                ASSERT_EQ(m.size(), r.n);
            }
        }
    }

    TEST(Fuzz, HashSet)
    {
        const int iters = fuzz_iters(5000);
        for (uint64_t seed = 1; seed <= 4; ++seed)
        {
            Rng rng(seed);
            ct::HashSet<int, WeakHash> s;
            RefMap r;
            for (int it = 0; it < iters; ++it)
            {
                const int k = static_cast<int>(rng.below(200));
                const uint32_t op = rng.below(5);
                if (op < 2)
                {
                    const bool fresh = r.find(k) < 0;
                    ASSERT_EQ(s.insert(k), fresh);
                    r.put(k, 0);
                }
                else if (op == 2)
                    ASSERT_EQ(s.erase(k), r.erase(k));
                else if (op == 3)
                    ASSERT_EQ(s.contains(k), r.find(k) >= 0);
                else if (rng.below(20) == 0)
                {
                    ct::HashSet<int, WeakHash> copy(s);
                    s = ct::move(copy);
                }
                ASSERT_EQ(s.size(), r.n);
            }
            size_t seen = 0;
            for (ct::HashSet<int, WeakHash>::iterator e = s.begin(); e != s.end(); ++e)
            {
                ASSERT_GE(r.find(*e), 0);
                ++seen;
            }
            ASSERT_EQ(seen, r.n);
        }
    }

    // FlatMap e TreeMap: mesmo modelo, mais a ordem e o lower_bound.
    template <typename Map>
    void check_ordered(Map &m, const RefMap &r)
    {
        int prev = 0;
        size_t seen = 0;
        for (typename Map::iterator e = m.begin(); e != m.end(); ++e)
        {
            if (seen)
            {
                ASSERT_LT(prev, e->key);
            }
            prev = e->key;
            const int i = r.find(e->key);
            ASSERT_GE(i, 0);
            ASSERT_EQ(e->value.id, r.val[i]);
            ++seen;
        }
        ASSERT_EQ(seen, r.n);
    }

    template <typename Map>
    void fuzz_ordered(uint64_t seed, int iters)
    {
        Rng rng(seed);
        Map m;
        Map other;
        RefMap r;
        RefMap ro;
        for (int it = 0; it < iters; ++it)
        {
            const uint32_t op = rng.below(14);
            const int k = static_cast<int>(rng.below(250));
            const int v = static_cast<int>(rng.below(100000));
            switch (op)
            {
            case 0:
            case 1:
            case 2:
                m.put(k, Counted(v));
                r.put(k, v);
                break;
            case 3:
                if (r.n)
                {
                    const int src = r.key[rng.below(static_cast<uint32_t>(r.n))];
                    const int sv = r.val[r.find(src)];
                    m.put(k, *m.find(src));
                    r.put(k, sv);
                }
                break;
            case 4:
            {
                Counted &c = m[k];
                const int i = r.find(k);
                if (i < 0)
                {
                    ASSERT_EQ(c.id, 0);
                    r.put(k, 0);
                }
                else
                    ASSERT_EQ(c.id, r.val[i]);
                c.id += 5;
                r.val[r.find(k)] += 5;
                break;
            }
            case 5:
            case 6:
                ASSERT_EQ(m.erase(k), r.erase(k));
                break;
            case 7:
            {
                const Counted *f = m.find(k);
                const int i = r.find(k);
                ASSERT_EQ(f != nullptr, i >= 0);
                if (f)
                {
                    ASSERT_EQ(f->id, r.val[i]);
                }
                break;
            }
            case 8:
            {
                typename Map::iterator lb = m.lower_bound_it(k);
                int best = -1;
                for (size_t i = 0; i < r.n; ++i)
                    if (r.key[i] >= k && (best < 0 || r.key[i] < best))
                        best = r.key[i];
                if (best < 0)
                {
                    ASSERT_TRUE(lb == m.end());
                }
                else
                {
                    ASSERT_TRUE(lb != m.end());
                    ASSERT_EQ(lb->key, best);
                }
                break;
            }
            case 9:
                if (rng.below(10) == 0)
                {
                    m.clear();
                    r.n = 0;
                }
                break;
            case 10:
            {
                Map copy(m);
                other = copy;
                ro = r;
                break;
            }
            case 11:
            {
                Map moved(ct::move(m));
                m = ct::move(moved);
                // um mapa movido-de tem de continuar utilizavel
                Map empty_again(ct::move(other));
                other = ct::move(empty_again);
                break;
            }
            case 12:
            {
                Map t(ct::move(m));
                m = ct::move(other);
                other = ct::move(t);
                RefMap rt = r;
                r = ro;
                ro = rt;
                break;
            }
            case 13:
                m = m;
                break;
            }
            ASSERT_EQ(m.size(), r.n) << "op " << op << " it " << it;
            ASSERT_EQ(other.size(), ro.n);
            if (it % 5 == 0)
            {
                check_ordered(m, r);
                check_ordered(other, ro);
            }
        }
    }

    template <typename Map>
    void validate_tree(const Map &) {}
    template <typename K, typename V>
    void validate_tree(const ct::TreeMap<K, V> &m)
    {
        ASSERT_TRUE(m.validate());
    }

    TEST(Fuzz, TreeMap)
    {
        Counted::bad = 0;
        const int before = Counted::live;
        for (uint64_t seed = 1; seed <= 5; ++seed)
            fuzz_ordered<ct::TreeMap<int, Counted> >(seed, fuzz_iters(4000));
        EXPECT_EQ(Counted::live, before);
        EXPECT_EQ(Counted::bad, 0);
    }
    TEST(Fuzz, TreeMapKeepsRedBlackInvariants)
    {
        Rng rng(77);
        ct::TreeMap<int, int> m;
        RefMap r;
        for (int it = 0; it < fuzz_iters(6000); ++it)
        {
            const int k = static_cast<int>(rng.below(500));
            if (rng.below(3))
            {
                m.put(k, it);
                r.put(k, it);
            }
            else
                ASSERT_EQ(m.erase(k), r.erase(k));
            if (it % 31 == 0)
                validate_tree(m);
        }
        validate_tree(m);
    }
    TEST(Fuzz, FlatMap)
    {
        Counted::bad = 0;
        const int before = Counted::live;
        for (uint64_t seed = 1; seed <= 5; ++seed)
            fuzz_ordered<ct::FlatMap<int, Counted> >(seed, fuzz_iters(4000));
        EXPECT_EQ(Counted::live, before);
        EXPECT_EQ(Counted::bad, 0);
    }
}
