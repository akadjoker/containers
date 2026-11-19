// Strings e conversao de numeros: ct::String contra std::string; numeros contra std::to_string, std::to_chars/from_chars,
// fmt e libc.
// uso: cmp_string <impl> <cenario> <tipo> <N>
//   impl texto: ct std       impl numeros: ct std charconv fmt libc
#include "harness.hpp"

#include <ct/string.hpp>

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<fmt/format.h>)
#define FMT_HEADER_ONLY 1
#include <fmt/format.h>
#define CMP_FMT 1
#endif

using namespace cmp;

static void make_text(std::string &out, std::size_t n, std::uint64_t seed)
{
    Rng r(seed);
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<char>('a' + r.below(26));
}

template <class S> struct Str
{
    static S from(const char *p, std::size_t n) { return S(p, n); }
};

// ---- texto -------------------------------------------------------------------------------------------------------
template <class S> static bool sc_create(Runner &R, Result &out, std::size_t len)
{
    const std::uint64_t N = R.n;
    std::string src;
    make_text(src, len, 1);
    const char *p = src.c_str();
    volatile std::size_t vlen = len;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        std::uint64_t s = 0;
        for (std::uint64_t i = 0; i < N; ++i)
        {
            S x(p, vlen);
            s += static_cast<unsigned char>(x.data()[i % len]);
            keep(x);
        }
        chk = s;
    });
    out.check = chk;
    return true;
}

template <class S> static bool sc_copy(Runner &R, Result &out, std::size_t len)
{
    const std::uint64_t N = R.n;
    std::string src;
    make_text(src, len, 2);
    std::vector<S> v;
    v.reserve(N);
    for (std::uint64_t i = 0; i < N; ++i)
        v.push_back(S(src.c_str(), len));
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        std::uint64_t s = 0;
        for (std::uint64_t i = 0; i < N; ++i)
        {
            S c(v[i]);
            s += c.size();
            keep(c);
        }
        chk = s;
    });
    out.check = chk;
    return true;
}

template <class S> static bool sc_append_char(Runner &R, Result &out)
{
    const std::uint64_t N = R.n;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        S s;
        for (std::uint64_t i = 0; i < N; ++i)
            s.push_back(static_cast<char>('a' + (i & 15)));
        R.snap_live();
        chk = s.size() + static_cast<unsigned char>(s[N / 2]);
        keep(s);
    });
    out.check = chk;
    return true;
}

template <class S> static bool sc_append_piece(Runner &R, Result &out)
{
    const std::uint64_t N = R.n;
    const char piece[] = "0123456789abcdef";
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        S s;
        for (std::uint64_t i = 0; i < N; ++i)
            s.append(piece, 16);
        R.snap_live();
        chk = s.size() + static_cast<unsigned char>(s[N]);
        keep(s);
    });
    out.check = chk;
    return true;
}

template <class S> static bool sc_find(Runner &R, Result &out)
{
    const std::uint64_t N = R.n;
    std::string t;
    make_text(t, N, 3);
    t.replace(N - 8, 8, "zzqqzzqq");
    S hay(t.c_str(), N);
    S needle("zzqqzzqq", 8);
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        auto pos = hay.find(needle);
        chk = static_cast<std::uint64_t>(pos);
        touch(chk);
    });
    out.check = chk;
    return true;
}

template <class S> static bool sc_compare(Runner &R, Result &out, bool less)
{
    const std::uint64_t N = R.n;
    std::string base;
    make_text(base, 40, 4);
    std::vector<S> a, b;
    for (std::uint64_t i = 0; i < 64; ++i)
    {
        std::string x = base, y = base;
        x[39] = static_cast<char>('a' + (i % 13));
        y[39] = static_cast<char>('a' + ((i * 7) % 13));
        a.push_back(S(x.c_str(), 40));
        b.push_back(S(y.c_str(), 40));
    }
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        std::uint64_t s = 0;
        for (std::uint64_t i = 0; i < N; ++i)
            s += less ? (a[i & 63] < b[i & 63]) : (a[i & 63] == b[i & 63]);
        chk = s;
        touch(s);
    });
    out.check = chk;
    return true;
}

template <class S> static bool sc_concat(Runner &R, Result &out)
{
    const std::uint64_t N = R.n;
    std::string x, y, z;
    make_text(x, 10, 5);
    make_text(y, 10, 6);
    make_text(z, 10, 7);
    S a(x.c_str(), 10), b(y.c_str(), 10), c(z.c_str(), 10);
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        std::uint64_t s = 0;
        for (std::uint64_t i = 0; i < N; ++i)
        {
            S r = a + b + c;
            s += r.size() + static_cast<unsigned char>(r[i % 30]);
            keep(r);
        }
        chk = s;
    });
    out.check = chk;
    return true;
}

template <class S> static int run_text(const char *impl, const char *scen, const char *type, Runner &R)
{
    Result r{};
    bool ok = false;
    std::size_t len = 0;
    if (!std::strncmp(type, "len", 3))
        len = static_cast<std::size_t>(std::atoi(type + 3));
    if (!std::strcmp(scen, "create")) ok = sc_create<S>(R, r, len);
    else if (!std::strcmp(scen, "copy")) ok = sc_copy<S>(R, r, len);
    else if (!std::strcmp(scen, "append_char")) ok = sc_append_char<S>(R, r);
    else if (!std::strcmp(scen, "append_piece")) ok = sc_append_piece<S>(R, r);
    else if (!std::strcmp(scen, "find")) ok = sc_find<S>(R, r);
    else if (!std::strcmp(scen, "eq")) ok = sc_compare<S>(R, r, false);
    else if (!std::strcmp(scen, "lt")) ok = sc_compare<S>(R, r, true);
    else if (!std::strcmp(scen, "concat")) ok = sc_concat<S>(R, r);
    if (ok)
        report(impl, scen, type, R.n, r, sizeof(S));
    return 0;
}

// ---- numeros -----------------------------------------------------------------------------------------------------
static std::vector<int> ints(std::uint64_t n, bool small)
{
    std::vector<int> v(n);
    for (std::uint64_t i = 0; i < n; ++i)
    {
        std::uint32_t x = perm32(static_cast<std::uint32_t>(i));
        v[i] = static_cast<int>(small ? x % 1000u : x);
    }
    return v;
}

static std::vector<double> doubles(std::uint64_t n)
{
    std::vector<double> v(n);
    Rng r(99);
    for (std::uint64_t i = 0; i < n; ++i)
        v[i] = static_cast<double>(r.next() >> 11) / 9007199254740992.0 * 1e6;
    return v;
}

template <class F> static std::uint64_t i2s_loop(const std::vector<int> &v, F f)
{
    std::uint64_t s = 0;
    for (int x : v)
        s += f(x);
    return s;
}

static int run_i2s(const char *impl, const char *type, Runner &R)
{
    const std::uint64_t N = R.n;
    auto v = ints(N, !std::strcmp(type, "small"));
    Result r{};
    std::uint64_t chk = 0;
    auto go = [&](auto f) {
        r = R.run(N, R.inner_for(N), [] {}, [&] {
            chk = i2s_loop(v, f);
            touch(chk);
        });
    };
    if (!std::strcmp(impl, "ct")) go([](int x) { ct::String s = ct::String::number(x); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
    else if (!std::strcmp(impl, "std")) go([](int x) { std::string s = std::to_string(x); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
    else if (!std::strcmp(impl, "charconv")) go([](int x) { char b[16]; auto e = std::to_chars(b, b + 16, x); std::string s(b, e.ptr); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
    else if (!std::strcmp(impl, "libc")) go([](int x) { char b[16]; int n = std::snprintf(b, 16, "%d", x); std::string s(b, static_cast<std::size_t>(n)); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
#ifdef CMP_FMT
    else if (!std::strcmp(impl, "fmt")) go([](int x) { fmt::format_int f(x); std::string s(f.data(), f.size()); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
#endif
    else return 0;
    r.check = chk;
    report(impl, "int2s", type, N, r, 0);
    return 0;
}

static int run_d2s(const char *impl, const char *type, Runner &R)
{
    const std::uint64_t N = R.n;
    auto v = doubles(N);
    Result r{};
    std::uint64_t chk = 0;
    auto go = [&](auto f) {
        r = R.run(N, R.inner_for(N), [] {}, [&] {
            std::uint64_t s = 0;
            for (double d : v)
                s += f(d);
            chk = s;
            touch(s);
        });
    };
    // %.6g em todos, que e o que ct::String::number(double) faz.
    if (!std::strcmp(impl, "ct")) go([](double d) { ct::String s = ct::String::number(d); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
    else if (!std::strcmp(impl, "charconv")) go([](double d) { char b[40]; auto e = std::to_chars(b, b + 40, d, std::chars_format::general, 6); std::string s(b, e.ptr); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
    else if (!std::strcmp(impl, "libc")) go([](double d) { char b[40]; int n = std::snprintf(b, 40, "%.6g", d); std::string s(b, static_cast<std::size_t>(n)); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
#ifdef CMP_FMT
    else if (!std::strcmp(impl, "fmt")) go([](double d) { std::string s = fmt::format("{:.6g}", d); keep(s); return s.size() + static_cast<unsigned char>(s[0]); });
#endif
    else return 0;
    r.check = chk;
    report(impl, "dbl2s", type, N, r, 0);
    return 0;
}

static int run_s2int(const char *impl, const char *type, Runner &R)
{
    const std::uint64_t N = R.n;
    auto v = ints(N, !std::strcmp(type, "small"));
    std::vector<std::string> ss;
    std::vector<ct::String> cs;
    for (int x : v) { ss.push_back(std::to_string(x)); cs.push_back(ct::String::number(x)); }
    Result r{};
    std::uint64_t chk = 0;
    auto go = [&](auto f) {
        r = R.run(N, R.inner_for(N), [] {}, [&] {
            std::uint64_t s = 0;
            for (std::uint64_t i = 0; i < N; ++i)
                s += static_cast<std::uint64_t>(f(i));
            chk = s;
            touch(s);
        });
    };
    if (!std::strcmp(impl, "ct")) go([&](std::uint64_t i) { return cs[i].to_int(); });
    else if (!std::strcmp(impl, "std")) go([&](std::uint64_t i) { return std::stoi(ss[i]); });
    else if (!std::strcmp(impl, "charconv")) go([&](std::uint64_t i) { int x = 0; std::from_chars(ss[i].data(), ss[i].data() + ss[i].size(), x); return x; });
    else if (!std::strcmp(impl, "libc")) go([&](std::uint64_t i) { return static_cast<int>(std::strtol(ss[i].c_str(), nullptr, 10)); });
    else return 0;
    r.check = chk;
    report(impl, "s2int", type, N, r, 0);
    return 0;
}

static int run_s2f(const char *impl, const char *type, Runner &R)
{
    const std::uint64_t N = R.n;
    auto v = doubles(N);
    std::vector<std::string> ss;
    std::vector<ct::String> cs;
    for (double d : v)
    {
        char b[40];
        int n = std::snprintf(b, 40, "%.6g", d);
        ss.push_back(std::string(b, static_cast<std::size_t>(n)));
        cs.push_back(ct::String(b, static_cast<std::size_t>(n)));
    }
    Result r{};
    std::uint64_t chk = 0;
    auto go = [&](auto f) {
        r = R.run(N, R.inner_for(N), [] {}, [&] {
            std::uint64_t s = 0;
            for (std::uint64_t i = 0; i < N; ++i)
            {
                float x = f(i);
                std::uint32_t bits;
                std::memcpy(&bits, &x, 4);
                s += bits;
            }
            chk = s;
            touch(s);
        });
    };
    if (!std::strcmp(impl, "ct")) go([&](std::uint64_t i) { return cs[i].to_float(); });
    else if (!std::strcmp(impl, "std")) go([&](std::uint64_t i) { return std::stof(ss[i]); });
    else if (!std::strcmp(impl, "charconv")) go([&](std::uint64_t i) { float x = 0; std::from_chars(ss[i].data(), ss[i].data() + ss[i].size(), x); return x; });
    else if (!std::strcmp(impl, "libc")) go([&](std::uint64_t i) { return std::strtof(ss[i].c_str(), nullptr); });
    else return 0;
    r.check = chk;
    report(impl, "s2f32", type, N, r, 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "uso: %s impl cenario tipo N\n", argv[0]);
        return 2;
    }
    const std::uint64_t n = std::strtoull(argv[4], nullptr, 10);
    const bool cg = arg_valgrind();
    Runner R(n, cg ? 1 : (n >= 10000000 ? 3 : (n >= 100000 ? 5 : 7)), cg);
    const char *impl = argv[1], *scen = argv[2], *type = argv[3];
    if (!std::strcmp(scen, "int2s")) return run_i2s(impl, type, R);
    if (!std::strcmp(scen, "dbl2s")) return run_d2s(impl, type, R);
    if (!std::strcmp(scen, "s2int")) return run_s2int(impl, type, R);
    if (!std::strcmp(scen, "s2f32")) return run_s2f(impl, type, R);
    if (!std::strcmp(impl, "ct")) return run_text<ct::String>(impl, scen, type, R);
    if (!std::strcmp(impl, "std")) return run_text<std::string>(impl, scen, type, R);
    std::fprintf(stderr, "impl desconhecida\n");
    return 2;
}
