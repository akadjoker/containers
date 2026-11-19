// Ordenacao: ct::sort / ct::stable_sort contra std::sort, std::stable_sort e boost::sort (pdqsort, spreadsort).
// uso: cmp_sort <impl> <algoritmo> <tipo.padrao> <N>
//   impl: ct std boost    algoritmo: sort stable    tipo: int p16 b256 str    padrao: random sorted reversed dups
#include "harness.hpp"

#include <ct/sort.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

#if __has_include(<boost/sort/pdqsort/pdqsort.hpp>)
#include <boost/sort/pdqsort/pdqsort.hpp>
#include <boost/sort/spreadsort/spreadsort.hpp>
#define CMP_BOOST 1
#endif

using namespace cmp;

enum Pat { Random, Sorted, Reversed, Dups };

static std::uint64_t key_of(std::uint64_t i, std::uint64_t n, Pat p)
{
    switch (p)
    {
    case Sorted: return i;
    case Reversed: return n - i;
    case Dups: return perm32(static_cast<std::uint32_t>(i)) & 15u;
    default: return perm32(static_cast<std::uint32_t>(i));
    }
}

template <class T> struct Gen;
template <> struct Gen<int>
{
    static int make(std::uint64_t k, std::uint64_t) { return static_cast<int>(k); }
    static std::uint64_t key(int x) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)); }
    static bool less(int a, int b) { return a < b; }
};
template <> struct Gen<P16>
{
    static P16 make(std::uint64_t k, std::uint64_t i) { return P16{k, i}; }
    static std::uint64_t key(const P16 &x) { return x.a; }
    static bool less(const P16 &a, const P16 &b) { return a.a < b.a; }
};
template <> struct Gen<B256>
{
    static B256 make(std::uint64_t k, std::uint64_t i)
    {
        B256 b;
        b.v[0] = k;
        for (int j = 1; j < 32; ++j)
            b.v[j] = i + static_cast<std::uint64_t>(j);
        return b;
    }
    static std::uint64_t key(const B256 &x) { return x.v[0]; }
    static bool less(const B256 &a, const B256 &b) { return a.v[0] < b.v[0]; }
};
template <class S> struct GenStr
{
    static S make(std::uint64_t k, std::uint64_t)
    {
        char b[32];
        int n = std::snprintf(b, 32, "key-%012llx", static_cast<unsigned long long>(k));
        return S(b, static_cast<std::size_t>(n));
    }
    static std::uint64_t key(const S &x) { return static_cast<std::uint64_t>(x.size()) * 31 + static_cast<unsigned char>(x[x.size() - 1]); }
    static bool less(const S &a, const S &b) { return a < b; }
};
template <> struct Gen<ct::String> : GenStr<ct::String> {};
template <> struct Gen<std::string> : GenStr<std::string> {};

struct Algo
{
    enum Kind { CtSort, CtStable, StdSort, StdStable, BoostPdq, BoostSpread } k;
};

template <class V, class T> static void do_sort(Algo a, V &v)
{
    auto cmp = [](const T &x, const T &y) { return Gen<T>::less(x, y); };
    switch (a.k)
    {
    case Algo::CtSort: ct::sort(v.begin(), v.end(), cmp); break;
    case Algo::CtStable: ct::stable_sort(v.begin(), v.end(), cmp); break;
    case Algo::StdSort: std::sort(v.begin(), v.end(), cmp); break;
    case Algo::StdStable: std::stable_sort(v.begin(), v.end(), cmp); break;
#ifdef CMP_BOOST
    case Algo::BoostPdq: boost::sort::pdqsort(v.begin(), v.end(), cmp); break;
    default: break;
#else
    default: break;
#endif
    }
}

// ct::sort sem comparador num vetor de int vai pelo radix; com comparador vai pelo introsort. Os dois sao medidos.
template <class V, class T> static bool sc(Runner &R, Result &out, Algo a, Pat p, bool nocmp)
{
    const std::uint64_t N = R.n;
    V v;
    std::uint64_t chk = 0;
    out = R.run(N, 1, [&] {
        v.clear();
        v.reserve(N);
        for (std::uint64_t i = 0; i < N; ++i)
            v.push_back(Gen<T>::make(key_of(i, N, p), i));
    }, [&] {
        if (nocmp)
        {
            if constexpr (std::is_same<T, int>::value)
            {
                if (a.k == Algo::CtSort) ct::sort(v.begin(), v.end());
#ifdef CMP_BOOST
                else if (a.k == Algo::BoostSpread) boost::sort::spreadsort::integer_sort(v.begin(), v.end());
#endif
            }
        }
        else
            do_sort<V, T>(a, v);
        keep(v);
    });
    bool sorted = true;
    for (std::uint64_t i = 1; i < N; ++i)
        if (Gen<T>::less(v[i], v[i - 1]))
            sorted = false;
    for (std::uint64_t i = 0; i < N; i += 1 + N / 64)
        chk = chk * 31 + Gen<T>::key(v[i]);
    out.check = chk + (sorted ? 0 : 1000000007);
    return true;
}

template <class T, class V> static int run_t(const char *impl, const char *algo, const char *type, Pat p, Runner &R)
{
    Result r{};
    Algo a{Algo::StdSort};
    bool nocmp = false;
    if (!std::strcmp(impl, "ct")) { a.k = !std::strcmp(algo, "stable") ? Algo::CtStable : Algo::CtSort; nocmp = !std::strcmp(algo, "sort_nocmp"); }
    else if (!std::strcmp(impl, "std")) a.k = !std::strcmp(algo, "stable") ? Algo::StdStable : Algo::StdSort;
#ifdef CMP_BOOST
    else if (!std::strcmp(impl, "boost")) { a.k = !std::strcmp(algo, "spread") ? Algo::BoostSpread : Algo::BoostPdq; nocmp = !std::strcmp(algo, "spread"); }
#endif
    else return 2;
    if (!sc<V, T>(R, r, a, p, nocmp))
        return 0;
    report(impl, algo, type, R.n, r, sizeof(T));
    return 0;
}

template <class T> static int by_impl(const char *impl, const char *algo, const char *type, Pat p, Runner &R)
{
    if (!std::strcmp(impl, "ct")) return run_t<T, ct::Vector<T>>(impl, algo, type, p, R);
    return run_t<T, std::vector<T>>(impl, algo, type, p, R);
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "uso: %s impl algoritmo tipo.padrao N\n", argv[0]);
        return 2;
    }
    const std::uint64_t n = std::strtoull(argv[4], nullptr, 10);
    const bool cg = arg_valgrind();
    Runner R(n, cg ? 1 : (n >= 10000000 ? 3 : (n >= 100000 ? 5 : 7)), cg);
    std::string tp = argv[3];
    std::string type = tp.substr(0, tp.find('.')), pat = tp.substr(tp.find('.') + 1);
    Pat p = pat == "sorted" ? Sorted : pat == "reversed" ? Reversed : pat == "dups" ? Dups : Random;
    const char *impl = argv[1], *algo = argv[2];
    const bool isct = !std::strcmp(impl, "ct");
    if (type == "int") return by_impl<int>(impl, algo, argv[3], p, R);
    if (type == "p16") return by_impl<P16>(impl, algo, argv[3], p, R);
    if (type == "b256") return by_impl<B256>(impl, algo, argv[3], p, R);
    if (type == "str")
        return isct ? run_t<ct::String, ct::Vector<ct::String>>(impl, algo, argv[3], p, R)
                    : run_t<std::string, std::vector<std::string>>(impl, algo, argv[3], p, R);
    return 2;
}
