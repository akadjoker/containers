// Vector: ct::Vector contra std::vector e boost::container::vector.
// uso: cmp_vector <impl> <cenario> <tipo> <N>     impl: ct std boost    tipo: int p16 b256 mo
#include "harness.hpp"

#include <ct/vector.hpp>

#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<boost/container/vector.hpp>)
#include <boost/container/vector.hpp>
#define CMP_BOOST 1
#endif

using namespace cmp;

template <class C, class T> static std::uint64_t fill(C &v, std::size_t n)
{
    v.clear();
    for (std::size_t i = 0; i < n; ++i)
        v.push_back(Mk<T>::make(i));
    return v.size();
}

template <class C, class T> static bool sc_build(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        C v;
        for (std::size_t i = 0; i < N; ++i)
            v.push_back(Mk<T>::make(i));
        R.snap_live();
        chk = v.size() + Mk<T>::val(v[N / 2]);
        keep(v);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_build_reserved(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        C v;
        v.reserve(N);
        for (std::size_t i = 0; i < N; ++i)
            v.push_back(Mk<T>::make(i));
        R.snap_live();
        chk = v.size() + Mk<T>::val(v[N / 2]);
        keep(v);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_emplace(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        C v;
        for (std::size_t i = 0; i < N; ++i)
            v.emplace_back(Mk<T>::make(i));
        chk = v.size() + Mk<T>::val(v[N / 2]);
        keep(v);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_iterate(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    C v;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [&] { fill<C, T>(v, N); }, [&] {
        std::uint64_t s = 0;
        for (const T &x : v)
            s += Mk<T>::val(x);
        chk = s;
        touch(s);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_index(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    C v;
    std::vector<std::uint32_t> idx(N);
    Rng rng(7);
    for (std::size_t i = 0; i < N; ++i)
        idx[i] = static_cast<std::uint32_t>(i);
    for (std::size_t i = N; i > 1; --i)
        std::swap(idx[i - 1], idx[rng.below(i)]);
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [&] { fill<C, T>(v, N); }, [&] {
        std::uint64_t s = 0;
        for (std::size_t i = 0; i < N; ++i)
            s += Mk<T>::val(v[idx[i]]);
        chk = s;
        touch(s);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool copy_body(const C &v, std::uint64_t &chk, std::true_type)
{
    C c(v);
    chk = c.size() + Mk<T>::val(c[c.size() / 2]);
    keep(c);
    return true;
}
template <class C, class T> static bool copy_body(const C &, std::uint64_t &, std::false_type) { return false; }

template <class C, class T> static bool sc_copy(Runner &R, Result &out)
{
    if (!std::is_copy_constructible<T>::value)
        return false;
    const std::size_t N = R.n;
    C v;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [&] { fill<C, T>(v, N); }, [&] { copy_body<C, T>(v, chk, typename std::is_copy_constructible<T>::type()); });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_move(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    C v;
    std::uint64_t chk = 0;
    out = R.run(3, R.inner_for(3), [&] { fill<C, T>(v, N); }, [&] {
        C a(std::move(v));
        C b;
        b = std::move(a);
        v = std::move(b);
        keep(v);
    });
    chk = v.size();
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_clear(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    C v;
    std::uint64_t chk = 0;
    out = R.run(N, 1, [&] { fill<C, T>(v, N); }, [&] {
        v.clear();
        chk = v.size();
        keep(v);
    });
    out.check = chk + 1;
    return true;
}

template <class C, class T> static bool sc_resize(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    std::uint64_t chk = 0;
    out = R.run(N, R.inner_for(N), [] {}, [&] {
        C v;
        v.resize(N);
        chk = v.size();
        keep(v);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_insert_mid(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    const std::size_t K = N < 2000 ? N : 2000;
    C v;
    std::uint64_t chk = 0;
    out = R.run(K, 1, [&] { fill<C, T>(v, N); }, [&] {
        for (std::size_t i = 0; i < K; ++i)
            v.insert(v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), Mk<T>::make(i));
        chk = v.size();
        keep(v);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static bool sc_erase_front(Runner &R, Result &out)
{
    const std::size_t N = R.n;
    const std::size_t K = N < 2000 ? N : 2000;
    C v;
    std::uint64_t chk = 0;
    out = R.run(K, 1, [&] { fill<C, T>(v, N); }, [&] {
        for (std::size_t i = 0; i < K; ++i)
            v.erase(v.begin());
        chk = v.size();
        keep(v);
    });
    out.check = chk;
    return true;
}

template <class C, class T> static int run_c(const char *impl, const char *scen, const char *type, Runner &R)
{
    Result r{};
    bool ok = false;
#define SC(nm, fn)                          \
    if (!std::strcmp(scen, nm))             \
        ok = fn<C, T>(R, r);
    SC("build", sc_build)
    SC("build_reserved", sc_build_reserved)
    SC("emplace", sc_emplace)
    SC("iterate", sc_iterate)
    SC("index", sc_index)
    SC("copy", sc_copy)
    SC("move", sc_move)
    SC("clear", sc_clear)
    SC("resize", sc_resize)
    SC("insert_mid", sc_insert_mid)
    SC("erase_front", sc_erase_front)
#undef SC
    if (!ok)
        return 0;
    report(impl, scen, type, R.n, r, sizeof(C));
    return 0;
}

template <class C, class T> static void growth(const char *impl, const char *type, std::size_t n)
{
    C v;
    std::size_t last = 0, changes = 0;
    std::printf("growth\t%s\t%s\tcap:", impl, type);
    for (std::size_t i = 0; i < n; ++i)
    {
        v.push_back(Mk<T>::make(i));
        if (v.capacity() != last)
        {
            if (changes < 14)
                std::printf(" %zu", v.capacity());
            last = v.capacity();
            ++changes;
        }
    }
    std::printf("\treallocs=%zu\tfinal_cap=%zu\tfinal_size=%zu\n", changes, v.capacity(), v.size());
}

template <class T> static int by_impl(const char *impl, const char *scen, const char *type, Runner &R)
{
    const bool g = !std::strcmp(scen, "growth");
    if (!std::strcmp(impl, "ct"))
    {
        if (g) { growth<ct::Vector<T>, T>(impl, type, R.n); return 0; }
        return run_c<ct::Vector<T>, T>(impl, scen, type, R);
    }
    if (!std::strcmp(impl, "std"))
    {
        if (g) { growth<std::vector<T>, T>(impl, type, R.n); return 0; }
        return run_c<std::vector<T>, T>(impl, scen, type, R);
    }
#ifdef CMP_BOOST
    if (!std::strcmp(impl, "boost"))
    {
        if (g) { growth<boost::container::vector<T>, T>(impl, type, R.n); return 0; }
        return run_c<boost::container::vector<T>, T>(impl, scen, type, R);
    }
#endif
    std::fprintf(stderr, "impl desconhecida %s\n", impl);
    return 2;
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
    const char *t = argv[3];
    if (!std::strcmp(t, "int")) return by_impl<int>(argv[1], argv[2], t, R);
    if (!std::strcmp(t, "p16")) return by_impl<P16>(argv[1], argv[2], t, R);
    if (!std::strcmp(t, "b256")) return by_impl<B256>(argv[1], argv[2], t, R);
    if (!std::strcmp(t, "mo")) return by_impl<MO>(argv[1], argv[2], t, R);
    std::fprintf(stderr, "tipo desconhecido\n");
    return 2;
}
