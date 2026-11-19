// Custo de compilacao: uma unidade que instancia cada contentor com 10 tipos. Escolhe-se a biblioteca com
// -DCMP_LIB_CT, -DCMP_LIB_STD, -DCMP_LIB_ABSL ou -DCMP_LIB_BOOST (ver tools/bench_compare.sh, etapa compile).
#include <cstdint>
#include <string>

#if defined(CMP_LIB_CT)
#include <ct/deque.hpp>
#include <ct/flatmap.hpp>
#include <ct/function.hpp>
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/priority_queue.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>
#include <ct/vector.hpp>
#elif defined(CMP_LIB_STD)
#include <deque>
#include <functional>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#elif defined(CMP_LIB_ABSL)
#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#elif defined(CMP_LIB_BOOST)
#include <boost/container/deque.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/vector.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#endif

struct A { int x; double y; bool operator==(const A &o) const { return x == o.x; } bool operator<(const A &o) const { return x < o.x; } };
struct B { std::uint64_t v[32]; bool operator==(const B &o) const { return v[0] == o.v[0]; } bool operator<(const B &o) const { return v[0] < o.v[0]; } };
enum class E : int { a, b };
struct C { char c[3]; bool operator==(const C &o) const { return c[0] == o.c[0]; } bool operator<(const C &o) const { return c[0] < o.c[0]; } };

#if defined(CMP_LIB_CT)
using Str = ct::String;
#else
using Str = std::string;
#endif

#if !defined(CMP_LIB_ABSL)
template <class V, class T> static std::size_t seq()
{
    V v;
    for (int i = 0; i < 10; ++i)
        v.push_back(T());
    V w(v);
    v.pop_back();
    v = w;
    std::size_t n = v.size();
    for (auto &x : v)
        n += sizeof(x);
    return n + w.size();
}
#endif

#if !defined(CMP_LIB_CT)
template <class M, class K, class V> static std::size_t mapuse(const K &k, const V &v)
{
    M m;
    m[k] = v;
    M c(m);
    std::size_t n = m.size() + c.count(k);
    for (auto &e : c)
        n += sizeof(e);
    n += c.erase(k);
    return n;
}

template <class S, class K> static std::size_t setuse(const K &k)
{
    S s;
    s.insert(k);
    S c(s);
    return s.size() + c.count(k) + c.erase(k);
}
#endif

using LL = long long;
using US = unsigned short;
#define ALL_T(F) F(int) F(LL) F(double) F(char) F(US) F(A) F(B) F(E) F(C) F(Str)
#define HASH_T(F) F(int) F(LL) F(char) F(US) F(E) F(Str)

std::size_t instantiate_all();

#if defined(CMP_LIB_CT)
#define SEQ(T) n += seq<ct::Vector<T>, T>() + seq<ct::Deque<T>, T>();
#define MAPS(T) n += ct_map<T>() + ct_ord<T>();
template <class T> static std::size_t ct_map() { ct::HashMap<int, T> m; m.put(1, T()); ct::HashMap<int, T> c(m); std::size_t n = c.size(); for (auto &e : c) n += sizeof(e); return n + c.erase(1); }
template <class T> static std::size_t ct_ord() { ct::TreeMap<int, T> m; m.put(1, T()); ct::FlatMap<int, T> f; f.put(1, T()); std::size_t n = m.size() + f.size(); for (auto &e : m) n += sizeof(e); return n + m.erase(1) + f.erase(1); }
template <class K> static std::size_t ct_set() { ct::HashSet<K> s; s.insert(K()); ct::HashSet<K> c(s); return s.size() + c.contains(K()) + c.erase(K()); }
#define SETS(T) n += ct_set<T>();
#elif defined(CMP_LIB_STD)
#define SEQ(T) n += seq<std::vector<T>, T>() + seq<std::deque<T>, T>();
#define MAPS(T) n += mapuse<std::unordered_map<int, T>>(1, T()) + mapuse<std::map<int, T>>(1, T());
#define SETS(T) n += setuse<std::unordered_set<T>>(T());
#elif defined(CMP_LIB_ABSL)
#define SEQ(T)
#define MAPS(T) n += mapuse<absl::flat_hash_map<int, T>>(1, T()) + mapuse<absl::btree_map<int, T>>(1, T());
#define SETS(T) n += setuse<absl::flat_hash_set<T>>(T());
#elif defined(CMP_LIB_BOOST)
#define SEQ(T) n += seq<boost::container::vector<T>, T>() + seq<boost::container::deque<T>, T>();
#define MAPS(T) n += mapuse<boost::unordered_flat_map<int, T>>(1, T()) + mapuse<boost::container::flat_map<int, T>>(1, T());
#define SETS(T) n += setuse<boost::unordered_flat_set<T>>(T());
#endif

std::size_t instantiate_all()
{
    std::size_t n = 0;
    ALL_T(SEQ)
    ALL_T(MAPS)
    HASH_T(SETS)
#if defined(CMP_LIB_CT)
    ct::Function<int(int)> f([](int x) { return x + 1; });
    ct::PriorityQueue<int> q;
    q.push(1);
    n += static_cast<std::size_t>(f(1)) + q.size();
#elif defined(CMP_LIB_STD)
    std::function<int(int)> f([](int x) { return x + 1; });
    std::priority_queue<int> q;
    q.push(1);
    n += static_cast<std::size_t>(f(1)) + q.size();
#endif
    return n;
}
