// Mapas de hash: ct::HashMap contra std::unordered_map, absl::flat_hash_map, boost::unordered_flat_map e
// ankerl::unordered_dense::map e eastl::hash_map, cada um com o seu hasher por omissao.
// uso: cmp_map <impl> <cenario> <chave-valor> <N>   impl: ct std absl boost ankerl eastl   tipo: int-int int-p16 int-b256 int-mo s8-int s20-int s40-int (e int-b32 a int-b1024 para varrer o tamanho do valor)
#include "harness.hpp"

#include <ct/hashmap.hpp>

#include <algorithm>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if __has_include(<absl/container/flat_hash_map.h>)
#include <absl/container/flat_hash_map.h>
#define CMP_ABSL 1
#endif
#if __has_include(<boost/unordered/unordered_flat_map.hpp>)
#include <boost/unordered/unordered_flat_map.hpp>
#define CMP_BOOST 1
#endif
#if __has_include(<EASTL/hash_map.h>)
#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#define CMP_EASTL 1
#endif
#if __has_include(<ankerl/unordered_dense.h>)
#include <ankerl/unordered_dense.h>
#define CMP_ANKERL 1
#endif

using namespace cmp;

// Valores de tamanho N bytes (so para varrer o tamanho do valor: int-b32 ... int-b1024)
template <std::size_t N> struct Blob
{
    std::uint64_t v[N / 8];
};
namespace cmp
{
    template <std::size_t N> struct Mk<Blob<N>>
    {
        static Blob<N> make(std::uint64_t i)
        {
            Blob<N> b;
            for (std::size_t k = 0; k < N / 8; ++k)
                b.v[k] = i + k;
            return b;
        }
        static std::uint64_t val(const Blob<N> &x) { return x.v[0] + x.v[N / 8 - 1]; }
    };
}

enum Kind { KInt, KS8, KS20, KS40 };

static std::size_t fmt_key(char *buf, Kind k, std::uint64_t i)
{
    const unsigned h = perm32(static_cast<std::uint32_t>(i));
    switch (k)
    {
    case KS8: return static_cast<std::size_t>(std::snprintf(buf, 64, "%08x", h));
    case KS20: return static_cast<std::size_t>(std::snprintf(buf, 64, "entity/%08x/idle", h));
    default: return static_cast<std::size_t>(std::snprintf(buf, 64, "/assets/textures/common/%08x.texture", h));
    }
}

template <class S, Kind K> struct KeyMk;
template <class S> struct KeyMk<S, KInt>
{
    using type = int;
    static int make(std::uint64_t i) { return static_cast<int>(perm32(static_cast<std::uint32_t>(i))); }
};
template <class S, Kind K> struct KeyMk
{
    using type = S;
    static S make(std::uint64_t i)
    {
        char b[64];
        std::size_t n = fmt_key(b, K, i);
        return S(b, n);
    }
};

struct CtTag { static constexpr const char *name = "ct"; using Str = ct::String; };
struct StdTag { static constexpr const char *name = "std"; using Str = std::string; };
struct AbslTag { static constexpr const char *name = "absl"; using Str = std::string; };
struct BoostTag { static constexpr const char *name = "boost"; using Str = std::string; };
struct AnkerlTag { static constexpr const char *name = "ankerl"; using Str = std::string; };
#ifdef CMP_EASTL
struct EastlTag { static constexpr const char *name = "eastl"; using Str = eastl::string; };
#endif

template <class Tag, class K, class V> struct Adapter;

template <class K, class V> struct Adapter<CtTag, K, V>
{
    using Map = ct::HashMap<K, V>;
    static V *find(Map &m, const K &k) { return m.find(k); }
    static void put(Map &m, const K &k, V &&v) { m.put(k, std::move(v)); }
    static bool erase(Map &m, const K &k) { return m.erase(k); }
    template <class F> static void each(const Map &m, F f) { for (const auto &e : m) f(e.key, e.value); }
    static std::size_t slots(const Map &m) { return m.capacity(); }
};

#define CMP_STD_LIKE(TAG, ...)                                                                                  \
    template <class K, class V> struct Adapter<TAG, K, V>                                                        \
    {                                                                                                            \
        using Map = __VA_ARGS__;                                                                                        \
        static V *find(Map &m, const K &k) { auto it = m.find(k); return it == m.end() ? nullptr : &it->second; } \
        static void put(Map &m, const K &k, V &&v) { m.try_emplace(k, std::move(v)); }                         \
        static bool erase(Map &m, const K &k) { return m.erase(k) != 0; }                                       \
        template <class F> static void each(const Map &m, F f) { for (const auto &e : m) f(e.first, e.second); } \
        static std::size_t slots(const Map &m) { return m.bucket_count(); }                                     \
    };

CMP_STD_LIKE(StdTag, std::unordered_map<K, V>)
#ifdef CMP_ABSL
CMP_STD_LIKE(AbslTag, absl::flat_hash_map<K, V>)
#endif
#ifdef CMP_BOOST
CMP_STD_LIKE(BoostTag, boost::unordered_flat_map<K, V>)
#endif
#ifdef CMP_ANKERL
CMP_STD_LIKE(AnkerlTag, ankerl::unordered_dense::map<K, V>)
#endif
#ifdef CMP_EASTL
CMP_STD_LIKE(EastlTag, eastl::hash_map<K, V>)
#endif

template <class Tag, Kind KD, class V> struct Ctx
{
    using KM = KeyMk<typename Tag::Str, KD>;
    using K = typename KM::type;
    using A = Adapter<Tag, K, V>;
    using Map = typename A::Map;
};

template <class X, Kind KD, class V> static std::vector<typename Ctx<X, KD, V>::K> make_keys(std::uint64_t from, std::uint64_t n)
{
    std::vector<typename Ctx<X, KD, V>::K> k;
    k.reserve(n);
    for (std::uint64_t i = 0; i < n; ++i)
        k.push_back(Ctx<X, KD, V>::KM::make(from + i));
    return k;
}

static std::vector<std::uint32_t> shuffled(std::uint64_t n, std::uint64_t seed)
{
    std::vector<std::uint32_t> idx(n);
    for (std::uint64_t i = 0; i < n; ++i)
        idx[i] = static_cast<std::uint32_t>(i);
    Rng rng(seed);
    for (std::uint64_t i = n; i > 1; --i)
        std::swap(idx[i - 1], idx[rng.below(i)]);
    return idx;
}

template <class X, Kind KD, class V> struct Sc
{
    using C = Ctx<X, KD, V>;
    using A = typename C::A;
    using Map = typename C::Map;
    using K = typename C::K;

    static void fill(Map &m, const std::vector<K> &keys, std::uint64_t n)
    {
        for (std::uint64_t i = 0; i < n; ++i)
            A::put(m, keys[i], Mk<V>::make(i));
    }

    static bool build(Runner &R, Result &out, bool reserve)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        std::uint64_t chk = 0;
        out = R.run(N, R.inner_for(N), [] {}, [&] {
            Map m;
            if (reserve)
                m.reserve(N);
            fill(m, keys, N);
            R.snap_live();
            chk = m.size();
            keep(m);
        });
        out.check = chk;
        return true;
    }

    static bool lookup(Runner &R, Result &out, bool hit)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(hit ? 0 : N, N);
        auto ins = make_keys<X, KD, V>(0, N);
        auto order = shuffled(N, 11);
        Map m;
        fill(m, ins, N);
        std::uint64_t chk = 0;
        out = R.run(N, R.inner_for(N), [] {}, [&] {
            std::uint64_t s = 0;
            for (std::uint64_t i = 0; i < N; ++i)
            {
                V *p = A::find(m, keys[order[i]]);
                if (p)
                    s += Mk<V>::val(*p);
            }
            chk = s;
            touch(s);
        });
        out.check = chk;
        return true;
    }

    static bool erase_all(Runner &R, Result &out)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        auto order = shuffled(N, 13);
        Map m;
        std::uint64_t chk = 0;
        out = R.run(N, 1, [&] { m = Map(); fill(m, keys, N); }, [&] {
            std::uint64_t c = 0;
            for (std::uint64_t i = 0; i < N; ++i)
                c += A::erase(m, keys[order[i]]) ? 1 : 0;
            chk = c + m.size();
        });
        out.check = chk;
        return true;
    }

    static bool iterate(Runner &R, Result &out)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        Map m;
        fill(m, keys, N);
        std::uint64_t chk = 0;
        out = R.run(N, R.inner_for(N), [] {}, [&] {
            std::uint64_t s = 0;
            A::each(m, [&](const K &, const V &v) { s += Mk<V>::val(v); });
            chk = s;
            touch(s);
        });
        out.check = chk;
        return true;
    }

    template <class VV = V> static typename std::enable_if<std::is_copy_constructible<VV>::value, bool>::type copy(Runner &R, Result &out)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        Map m;
        fill(m, keys, N);
        std::uint64_t chk = 0;
        out = R.run(N, R.inner_for(N), [] {}, [&] {
            Map c(m);
            chk = c.size();
            keep(c);
        });
        out.check = chk;
        return true;
    }
    template <class VV = V> static typename std::enable_if<!std::is_copy_constructible<VV>::value, bool>::type copy(Runner &, Result &) { return false; }

    static bool move(Runner &R, Result &out)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        Map m;
        fill(m, keys, N);
        out = R.run(3, R.inner_for(3), [] {}, [&] {
            Map a(std::move(m));
            Map b;
            b = std::move(a);
            m = std::move(b);
            keep(m);
        });
        out.check = m.size();
        return true;
    }

    static bool clear(Runner &R, Result &out)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        Map m;
        std::uint64_t chk = 0;
        out = R.run(N, 1, [&] { m = Map(); fill(m, keys, N); }, [&] {
            m.clear();
            chk = m.size();
            keep(m);
        });
        out.check = chk + 1;
        return true;
    }

    // Tamanho fixo N, 1e6 passos de "apaga o mais velho, insere um novo": a tabela nao pode crescer.
    static bool churn(Runner &R, Result &out)
    {
        const std::uint64_t N = R.n;
        const std::uint64_t K2 = 1000000;
        auto keys = make_keys<X, KD, V>(0, N + K2);
        Map m;
        std::uint64_t chk = 0;
        out = R.run(K2, 1, [&] { m = Map(); fill(m, keys, N); }, [&] {
            for (std::uint64_t i = 0; i < K2; ++i)
            {
                A::erase(m, keys[i]);
                A::put(m, keys[N + i], Mk<V>::make(i));
            }
            R.snap_live();
            chk = m.size();
            keep(m);
        });
        out.check = chk;
        return true;
    }

    static void layout(Runner &R, const char *impl, const char *type)
    {
        const std::uint64_t N = R.n;
        auto keys = make_keys<X, KD, V>(0, N);
        std::uint64_t l0 = g_alloc.live;
        Map m;
        fill(m, keys, N);
        std::uint64_t used = g_alloc.live - l0;
        std::printf("layout\t%s\t%s\t%llu\tsizeof_empty=%zu\tsize=%zu\tslots=%zu\tload=%.3f\tbytes_per_elem=%.2f\theap_bytes=%llu\n", impl, type,
                    static_cast<unsigned long long>(N), sizeof(Map), m.size(), A::slots(m),
                    static_cast<double>(m.size()) / static_cast<double>(A::slots(m)),
                    static_cast<double>(used) / static_cast<double>(N), static_cast<unsigned long long>(used));
    }
};

template <class X, Kind KD, class V> static int run_c(const char *scen, const char *type, Runner &R)
{
    using S = Sc<X, KD, V>;
    Result r{};
    bool ok = false;
    if (!std::strcmp(scen, "layout")) { S::layout(R, X::name, type); return 0; }
    if (!std::strcmp(scen, "build")) ok = S::build(R, r, false);
    else if (!std::strcmp(scen, "build_reserved")) ok = S::build(R, r, true);
    else if (!std::strcmp(scen, "hit")) ok = S::lookup(R, r, true);
    else if (!std::strcmp(scen, "miss")) ok = S::lookup(R, r, false);
    else if (!std::strcmp(scen, "erase")) ok = S::erase_all(R, r);
    else if (!std::strcmp(scen, "iterate")) ok = S::iterate(R, r);
    else if (!std::strcmp(scen, "copy")) ok = S::copy(R, r);
    else if (!std::strcmp(scen, "move")) ok = S::move(R, r);
    else if (!std::strcmp(scen, "clear")) ok = S::clear(R, r);
    else if (!std::strcmp(scen, "churn")) ok = S::churn(R, r);
    if (!ok)
        return 0;
    report(X::name, scen, type, R.n, r, sizeof(typename S::Map));
    return 0;
}

template <class X> static int by_type(const char *scen, const char *type, Runner &R)
{
    if (!std::strcmp(type, "int-int")) return run_c<X, KInt, int>(scen, type, R);
    if (!std::strcmp(type, "int-p16")) return run_c<X, KInt, P16>(scen, type, R);
    if (!std::strcmp(type, "int-b256")) return run_c<X, KInt, B256>(scen, type, R);
    if (!std::strcmp(type, "int-mo")) return run_c<X, KInt, MO>(scen, type, R);
    if (!std::strcmp(type, "int-b32")) return run_c<X, KInt, Blob<32>>(scen, type, R);
    if (!std::strcmp(type, "int-b64")) return run_c<X, KInt, Blob<64>>(scen, type, R);
    if (!std::strcmp(type, "int-b96")) return run_c<X, KInt, Blob<96>>(scen, type, R);
    if (!std::strcmp(type, "int-b128")) return run_c<X, KInt, Blob<128>>(scen, type, R);
    if (!std::strcmp(type, "int-b192")) return run_c<X, KInt, Blob<192>>(scen, type, R);
    if (!std::strcmp(type, "int-b512")) return run_c<X, KInt, Blob<512>>(scen, type, R);
    if (!std::strcmp(type, "int-b1024")) return run_c<X, KInt, Blob<1024>>(scen, type, R);
    if (!std::strcmp(type, "s8-int")) return run_c<X, KS8, int>(scen, type, R);
    if (!std::strcmp(type, "s20-int")) return run_c<X, KS20, int>(scen, type, R);
    if (!std::strcmp(type, "s40-int")) return run_c<X, KS40, int>(scen, type, R);
    std::fprintf(stderr, "tipo desconhecido\n");
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
    const char *impl = argv[1];
    if (!std::strcmp(impl, "ct")) return by_type<CtTag>(argv[2], argv[3], R);
    if (!std::strcmp(impl, "std")) return by_type<StdTag>(argv[2], argv[3], R);
#ifdef CMP_ABSL
    if (!std::strcmp(impl, "absl")) return by_type<AbslTag>(argv[2], argv[3], R);
#endif
#ifdef CMP_BOOST
    if (!std::strcmp(impl, "boost")) return by_type<BoostTag>(argv[2], argv[3], R);
#endif
#ifdef CMP_ANKERL
    if (!std::strcmp(impl, "ankerl")) return by_type<AnkerlTag>(argv[2], argv[3], R);
#endif
#ifdef CMP_EASTL
    if (!std::strcmp(impl, "eastl")) return by_type<EastlTag>(argv[2], argv[3], R);
#endif
    std::fprintf(stderr, "impl desconhecida %s\n", impl);
    return 2;
}
