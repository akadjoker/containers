// Restante: Deque, TreeMap e FlatMap (mapas ordenados), PriorityQueue e Function.
// uso: cmp_misc <familia> <impl> <cenario> <tipo> <N>
//   deque: impl ct std boost         cenarios push_back push_front iterate index drain fifo   tipos int p16 b256
//   ordered: impl ctree cflat stdmap btree bflat   cenarios build hit miss erase iterate layout   tipos int s20
//   pq: impl ct std   cenario pushpop   tipo int
//   function: impl ct std   cenarios construct_small construct_big call copy
#include "harness.hpp"

#include <ct/deque.hpp>
#include <ct/flatmap.hpp>
#include <ct/function.hpp>
#include <ct/priority_queue.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <type_traits>
#include <vector>

#if __has_include(<boost/container/deque.hpp>)
#include <boost/container/deque.hpp>
#include <boost/container/flat_map.hpp>
#define CMP_BOOST 1
#endif
#if __has_include(<absl/container/btree_map.h>)
#include <absl/container/btree_map.h>
#define CMP_ABSL 1
#endif

using namespace cmp;

// ---- deque -------------------------------------------------------------------------------------------------------
template <class D, class T> static bool dq(Runner &R, Result &out, const char *scen)
{
    const std::uint64_t N = R.n;
    D d;
    std::uint64_t chk = 0;
    auto fill = [&] { d.clear(); for (std::uint64_t i = 0; i < N; ++i) d.push_back(Mk<T>::make(i)); };
    if (!std::strcmp(scen, "push_back"))
        out = R.run(N, R.inner_for(N), [] {}, [&] { D x; for (std::uint64_t i = 0; i < N; ++i) x.push_back(Mk<T>::make(i)); R.snap_live(); chk = x.size() + Mk<T>::val(x[N / 2]); keep(x); });
    else if (!std::strcmp(scen, "push_front"))
        out = R.run(N, R.inner_for(N), [] {}, [&] { D x; for (std::uint64_t i = 0; i < N; ++i) x.push_front(Mk<T>::make(i)); R.snap_live(); chk = x.size() + Mk<T>::val(x[N / 2]); keep(x); });
    else if (!std::strcmp(scen, "iterate"))
        out = R.run(N, R.inner_for(N), fill, [&] { std::uint64_t s = 0; for (const T &x : d) s += Mk<T>::val(x); chk = s; touch(s); });
    else if (!std::strcmp(scen, "index"))
    {
        std::vector<std::uint32_t> idx(N);
        Rng rng(7);
        for (std::uint64_t i = 0; i < N; ++i) idx[i] = static_cast<std::uint32_t>(i);
        for (std::uint64_t i = N; i > 1; --i) std::swap(idx[i - 1], idx[rng.below(i)]);
        out = R.run(N, R.inner_for(N), fill, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) s += Mk<T>::val(d[idx[i]]); chk = s; touch(s); });
    }
    else if (!std::strcmp(scen, "drain"))
        out = R.run(N, 1, fill, [&] { std::uint64_t s = 0; while (!d.empty()) { s += Mk<T>::val(d.front()); d.pop_front(); } chk = s; touch(s); });
    else if (!std::strcmp(scen, "fifo"))
        out = R.run(N, R.inner_for(N), [] {}, [&] { D x; std::uint64_t s = 0; for (std::uint64_t i = 0; i < 64 && i < N; ++i) x.push_back(Mk<T>::make(i)); for (std::uint64_t i = 64; i < N; ++i) { x.push_back(Mk<T>::make(i)); s += Mk<T>::val(x.front()); x.pop_front(); } chk = s; touch(s); });
    else
        return false;
    out.check = chk;
    return true;
}
template <class T> static int deque_t(const char *impl, const char *scen, const char *type, Runner &R)
{
    Result r{};
    bool ok = false;
    std::size_t sz = 0;
    if (!std::strcmp(impl, "ct")) { ok = dq<ct::Deque<T>, T>(R, r, scen); sz = sizeof(ct::Deque<T>); }
    else if (!std::strcmp(impl, "std")) { ok = dq<std::deque<T>, T>(R, r, scen); sz = sizeof(std::deque<T>); }
#ifdef CMP_BOOST
    else if (!std::strcmp(impl, "boost")) { ok = dq<boost::container::deque<T>, T>(R, r, scen); sz = sizeof(boost::container::deque<T>); }
#endif
    if (ok) report(impl, scen, type, R.n, r, sz);
    return 0;
}

// ---- mapas ordenados ----------------------------------------------------------------------------------------------
template <class S, bool Str> struct OKey;
template <class S> struct OKey<S, false> { using type = int; static int make(std::uint64_t i) { return static_cast<int>(perm32(static_cast<std::uint32_t>(i))); } };
template <class S> struct OKey<S, true>
{
    using type = S;
    static S make(std::uint64_t i)
    {
        char b[48];
        int n = std::snprintf(b, 48, "entity/%08x/idle", perm32(static_cast<std::uint32_t>(i)));
        return S(b, static_cast<std::size_t>(n));
    }
};

struct OCt { static constexpr const char *name = "ctree"; using Str = ct::String; template <class K, class V> using M = ct::TreeMap<K, V>; };
struct OFlat { static constexpr const char *name = "cflat"; using Str = ct::String; template <class K, class V> using M = ct::FlatMap<K, V>; };
struct OStd { static constexpr const char *name = "stdmap"; using Str = std::string; template <class K, class V> using M = std::map<K, V>; };
#ifdef CMP_ABSL
struct OBtree { static constexpr const char *name = "btree"; using Str = std::string; template <class K, class V> using M = absl::btree_map<K, V>; };
#endif
#ifdef CMP_BOOST
struct OBflat { static constexpr const char *name = "bflat"; using Str = std::string; template <class K, class V> using M = boost::container::flat_map<K, V>; };
#endif

template <class K, class V, class M> static V *ofind(M &m, const K &k) { auto it = m.find(k); return it == m.end() ? nullptr : &it->second; }
template <class K, class V> static V *ofind(ct::TreeMap<K, V> &m, const K &k) { return m.find(k); }
template <class K, class V> static V *ofind(ct::FlatMap<K, V> &m, const K &k) { return m.find(k); }
template <class K, class V> static void oput(ct::TreeMap<K, V> &m, const K &k, V v) { m.put(k, v); }
template <class K, class V> static void oput(ct::FlatMap<K, V> &m, const K &k, V v) { m.put(k, v); }
template <class K, class V, class M> static void oput(M &m, const K &k, V v) { m.try_emplace(k, v); }
template <class K, class V> static bool oerase(ct::TreeMap<K, V> &m, const K &k) { return m.erase(k); }
template <class K, class V> static bool oerase(ct::FlatMap<K, V> &m, const K &k) { return m.erase(k); }
template <class K, class V, class M> static bool oerase(M &m, const K &k) { return m.erase(k) != 0; }
template <class K, class V> static std::uint64_t oiter(ct::TreeMap<K, V> &m) { std::uint64_t s = 0; for (auto &e : m) s += static_cast<std::uint64_t>(e.value); return s; }
template <class K, class V> static std::uint64_t oiter(ct::FlatMap<K, V> &m) { std::uint64_t s = 0; for (auto &e : m) s += static_cast<std::uint64_t>(e.value); return s; }
template <class K, class V, class M> static std::uint64_t oiter(M &m) { std::uint64_t s = 0; for (auto &e : m) s += static_cast<std::uint64_t>(e.second); return s; }

template <class X, bool Str> static int ordered(const char *scen, const char *type, Runner &R)
{
    using KM = OKey<typename X::Str, Str>;
    using K = typename KM::type;
    using M = typename X::template M<K, int>;
    const std::uint64_t N = R.n;
    std::vector<K> keys, other;
    for (std::uint64_t i = 0; i < N; ++i) { keys.push_back(KM::make(i)); other.push_back(KM::make(N + i)); }
    Result r{};
    std::uint64_t chk = 0;
    auto fill = [&](M &m) { for (std::uint64_t i = 0; i < N; ++i) oput<K, int>(m, keys[i], static_cast<int>(i)); };
    if (!std::strcmp(scen, "build"))
        r = R.run(N, R.inner_for(N * (std::is_same<X, OFlat>::value ? 20 : 1)), [] {}, [&] { M m; fill(m); R.snap_live(); chk = m.size(); keep(m); });
    else if (!std::strcmp(scen, "hit") || !std::strcmp(scen, "miss"))
    {
        const bool hit = !std::strcmp(scen, "hit");
        M m; fill(m);
        r = R.run(N, R.inner_for(N * 4), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { int *p = ofind<K, int>(m, hit ? keys[i] : other[i]); if (p) s += static_cast<std::uint64_t>(*p); } chk = s; touch(s); });
    }
    else if (!std::strcmp(scen, "erase"))
    {
        M m;
        r = R.run(N, 1, [&] { m = M(); fill(m); }, [&] { std::uint64_t c = 0; for (std::uint64_t i = 0; i < N; ++i) c += oerase<K, int>(m, keys[i]) ? 1 : 0; chk = c + m.size(); });
    }
    else if (!std::strcmp(scen, "iterate"))
    {
        M m; fill(m);
        r = R.run(N, R.inner_for(N), [] {}, [&] { chk = oiter<K, int>(m); touch(chk); });
    }
    else if (!std::strcmp(scen, "layout"))
    {
        std::uint64_t l0 = g_alloc.live, c0 = g_alloc.calls;
        M m; fill(m);
        std::printf("layout\t%s\t%s\t%llu\tsizeof_empty=%zu\tbytes_per_elem=%.2f\tallocs=%llu\n", X::name, type, static_cast<unsigned long long>(N), sizeof(M),
                    static_cast<double>(g_alloc.live - l0) / static_cast<double>(N), static_cast<unsigned long long>(g_alloc.calls - c0));
        return 0;
    }
    else return 0;
    r.check = chk;
    report(X::name, scen, type, N, r, sizeof(M));
    return 0;
}

template <bool Str> static int ordered_impl(const char *impl, const char *scen, const char *type, Runner &R)
{
    if (!std::strcmp(impl, "ctree")) return ordered<OCt, Str>(scen, type, R);
    if (!std::strcmp(impl, "cflat")) return ordered<OFlat, Str>(scen, type, R);
    if (!std::strcmp(impl, "stdmap")) return ordered<OStd, Str>(scen, type, R);
#ifdef CMP_ABSL
    if (!std::strcmp(impl, "btree")) return ordered<OBtree, Str>(scen, type, R);
#endif
#ifdef CMP_BOOST
    if (!std::strcmp(impl, "bflat")) return ordered<OBflat, Str>(scen, type, R);
#endif
    return 2;
}

// ---- priority queue ----------------------------------------------------------------------------------------------
template <class Q> static int pq(const char *impl, Runner &R)
{
    const std::uint64_t N = R.n;
    std::vector<int> v(N);
    for (std::uint64_t i = 0; i < N; ++i) v[i] = static_cast<int>(perm32(static_cast<std::uint32_t>(i)));
    std::uint64_t chk = 0;
    Result r = R.run(N, R.inner_for(N), [] {}, [&] {
        Q q;
        for (std::uint64_t i = 0; i < N; ++i) q.push(v[i]);
        std::uint64_t s = 0;
        while (!q.empty()) { s = s * 31 + static_cast<std::uint64_t>(q.top()); q.pop(); }
        chk = s;
        touch(s);
    });
    r.check = chk;
    report(impl, "pushpop", "int", N, r, sizeof(Q));
    return 0;
}

// ---- function ----------------------------------------------------------------------------------------------------
template <class F> static int fn(const char *impl, const char *scen, Runner &R)
{
    const std::uint64_t N = R.n;
    Result r{};
    std::uint64_t chk = 0;
    std::uint64_t a = 3, b = 5;
    struct Big { std::uint64_t v[8]; } big;
    for (int i = 0; i < 8; ++i) big.v[i] = static_cast<std::uint64_t>(i);
    auto small = [a, b](int x) { return static_cast<int>(a * static_cast<std::uint64_t>(x) + b); };
    auto large = [big](int x) { return static_cast<int>(big.v[x & 7] + static_cast<std::uint64_t>(x)); };
    if (!std::strcmp(scen, "construct_small"))
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { F f(small); s += static_cast<std::uint64_t>(f(static_cast<int>(i))); keep(f); } chk = s; touch(s); });
    else if (!std::strcmp(scen, "construct_big"))
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { F f(large); s += static_cast<std::uint64_t>(f(static_cast<int>(i))); keep(f); } chk = s; touch(s); });
    else if (!std::strcmp(scen, "call"))
    {
        F f(small);
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) s += static_cast<std::uint64_t>(f(static_cast<int>(i))); chk = s; touch(s); });
    }
    else if (!std::strcmp(scen, "copy"))
    {
        F f(large);
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { F g(f); s += static_cast<std::uint64_t>(g(static_cast<int>(i))); keep(g); } chk = s; touch(s); });
    }
    else return 0;
    r.check = chk;
    report(impl, scen, "int(int)", N, r, sizeof(F));
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr, "uso: %s familia impl cenario tipo N\n", argv[0]);
        return 2;
    }
    const std::uint64_t n = std::strtoull(argv[5], nullptr, 10);
    const bool cg = arg_valgrind();
    Runner R(n, cg ? 1 : (n >= 10000000 ? 3 : (n >= 100000 ? 5 : 7)), cg);
    const char *fam = argv[1], *impl = argv[2], *scen = argv[3], *type = argv[4];
    if (!std::strcmp(fam, "deque"))
    {
        if (!std::strcmp(type, "int")) return deque_t<int>(impl, scen, type, R);
        if (!std::strcmp(type, "p16")) return deque_t<P16>(impl, scen, type, R);
        if (!std::strcmp(type, "b256")) return deque_t<B256>(impl, scen, type, R);
    }
    if (!std::strcmp(fam, "ordered")) return !std::strcmp(type, "int") ? ordered_impl<false>(impl, scen, type, R) : ordered_impl<true>(impl, scen, type, R);
    if (!std::strcmp(fam, "pq")) return !std::strcmp(impl, "ct") ? pq<ct::PriorityQueue<int>>(impl, R) : pq<std::priority_queue<int>>(impl, R);
    if (!std::strcmp(fam, "function")) return !std::strcmp(impl, "ct") ? fn<ct::Function<int(int)>>(impl, scen, R) : fn<std::function<int(int)>>(impl, scen, R);
    std::fprintf(stderr, "familia desconhecida\n");
    return 2;
}
