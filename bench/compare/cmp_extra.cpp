// O que a ronda 1 nao mediu: Variant, Arena, Pool, fila circular (Queue sobre Deque), Span e Function com captura, contra o
// equivalente da biblioteca standard (std::variant, std::pmr::monotonic_buffer_resource, new/delete, std::queue,
// ponteiro cru).
// uso: cmp_extra <familia> <impl> <cenario> <N>
//   variant: ct std      cenarios construct visit copy_assign
//   arena:   ct std      cenarios alloc64 create_nodes        (std = std::pmr::monotonic_buffer_resource)
//   pool:    ct std      cenarios cycle                       (std = new e delete)
//   ring:    ct std      cenarios fifo                        (std = std::queue<int>)
//   span:    ct raw      cenarios sum sub
#include "harness.hpp"

#include <ct/arena.hpp>
#include <ct/pool.hpp>
#include <ct/queue.hpp>
#include <ct/span.hpp>
#include <ct/string.hpp>
#include <ct/variant.hpp>
#include <ct/vector.hpp>

#include <memory_resource>
#include <queue>
#include <variant>
#include <vector>

using namespace cmp;

// ---- variant -----------------------------------------------------------------------------------------------------------
template <class V> struct VSum
{
    std::uint64_t operator()(int x) const { return static_cast<std::uint64_t>(x); }
    std::uint64_t operator()(double x) const { return static_cast<std::uint64_t>(x * 2); }
    std::uint64_t operator()(const ct::String &x) const { return x.size(); }
};

template <class V> static V make_v(std::uint64_t i)
{
    switch (i % 3)
    {
    case 0: return V(static_cast<int>(i));
    case 1: return V(static_cast<double>(i));
    default: return V(ct::String("variante-numero-um-bastante-longa", 5 + i % 20));
    }
}

template <class V, class Visit> static int variant_t(const char *impl, const char *scen, Runner &R, Visit visit)
{
    const std::uint64_t N = R.n;
    std::vector<V> v;
    for (std::uint64_t i = 0; i < 1024; ++i)
        v.push_back(make_v<V>(i));
    Result r{};
    std::uint64_t chk = 0;
    if (!std::strcmp(scen, "construct"))
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { V x = make_v<V>(i); s += visit(x); keep(x); } chk = s; touch(s); });
    else if (!std::strcmp(scen, "visit"))
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) s += visit(v[i & 1023]); chk = s; touch(s); });
    else if (!std::strcmp(scen, "copy_assign"))
    {
        V a = make_v<V>(0);
        r = R.run(N, R.inner_for(N), [] {}, [&] { std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { a = v[i & 1023]; s += visit(a); } chk = s; touch(s); });
    }
    else return 0;
    r.check = chk;
    report(impl, scen, "mix3", N, r, sizeof(V));
    return 0;
}

// ---- arena -------------------------------------------------------------------------------------------------------------
struct Node { Node *next; std::uint64_t v; };

static int arena_t(const char *impl, const char *scen, Runner &R)
{
    const std::uint64_t N = R.n;
    Result r{};
    std::uint64_t chk = 0;
    const bool ct_impl = !std::strcmp(impl, "ct");
    if (!std::strcmp(scen, "alloc64"))
    {
        if (ct_impl)
            r = R.run(N, 1, [] {}, [&] { ct::Arena a(1 << 20); std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { char *p = static_cast<char *>(a.allocate(64, 16)); p[0] = static_cast<char>(i); s += static_cast<unsigned char>(p[0]); } chk = s; touch(s); });
        else
            r = R.run(N, 1, [] {}, [&] { std::pmr::monotonic_buffer_resource a(1 << 20); std::uint64_t s = 0; for (std::uint64_t i = 0; i < N; ++i) { char *p = static_cast<char *>(a.allocate(64, 16)); p[0] = static_cast<char>(i); s += static_cast<unsigned char>(p[0]); } chk = s; touch(s); });
    }
    else if (!std::strcmp(scen, "create_nodes"))
    {
        if (ct_impl)
            r = R.run(N, 1, [] {}, [&] { ct::Arena a(1 << 20); Node *head = nullptr; for (std::uint64_t i = 0; i < N; ++i) head = a.create<Node>(Node{head, i}); std::uint64_t s = 0; for (Node *p = head; p; p = p->next) s += p->v; chk = s; touch(s); });
        else
            r = R.run(N, 1, [] {}, [&] { std::pmr::monotonic_buffer_resource a(1 << 20); Node *head = nullptr; for (std::uint64_t i = 0; i < N; ++i) { Node *n = static_cast<Node *>(a.allocate(sizeof(Node), alignof(Node))); n->next = head; n->v = i; head = n; } std::uint64_t s = 0; for (Node *p = head; p; p = p->next) s += p->v; chk = s; touch(s); });
    }
    else return 0;
    r.check = chk;
    report(impl, scen, "node16", N, r, 0);
    return 0;
}

// ---- pool --------------------------------------------------------------------------------------------------------------
static int pool_t(const char *impl, Runner &R)
{
    const std::uint64_t N = R.n;
    Result r{};
    std::uint64_t chk = 0;
    // N objectos vivos de uma vez, apaga-se metade pela ordem inversa e volta-se a criar (o que uma pool deve acelerar)
    if (!std::strcmp(impl, "ct"))
        r = R.run(N, 1, [] {}, [&] {
            ct::Pool<Node> pool;
            ct::Vector<Node *> live;
            live.reserve(N);
            for (std::uint64_t i = 0; i < N; ++i) live.push_back(pool.create(Node{nullptr, i}));
            for (std::uint64_t i = N; i > N / 2; --i) { pool.destroy(live[i - 1]); live.pop_back(); }
            for (std::uint64_t i = 0; i < N / 2; ++i) live.push_back(pool.create(Node{nullptr, i}));
            std::uint64_t s = 0;
            for (Node *p : live) s += p->v;
            chk = s;
            touch(s);
            for (Node *p : live) pool.destroy(p);
        });
    else
        r = R.run(N, 1, [] {}, [&] {
            std::vector<Node *> live;
            live.reserve(N);
            for (std::uint64_t i = 0; i < N; ++i) live.push_back(new Node{nullptr, i});
            for (std::uint64_t i = N; i > N / 2; --i) { delete live[i - 1]; live.pop_back(); }
            for (std::uint64_t i = 0; i < N / 2; ++i) live.push_back(new Node{nullptr, i});
            std::uint64_t s = 0;
            for (Node *p : live) s += p->v;
            chk = s;
            touch(s);
            for (Node *p : live) delete p;
        });
    r.check = chk;
    report(impl, "cycle", "node16", N, r, 0);
    return 0;
}

// ---- fila circular -----------------------------------------------------------------------------------------------------
template <class Q> static int ring_t(const char *impl, Runner &R)
{
    const std::uint64_t N = R.n;
    Result r{};
    std::uint64_t chk = 0;
    r = R.run(N, R.inner_for(N), [] {}, [&] {
        Q q;
        std::uint64_t s = 0;
        for (std::uint64_t i = 0; i < 64; ++i) q.push(static_cast<int>(i));
        for (std::uint64_t i = 0; i < N; ++i)
        {
            q.push(static_cast<int>(i));
            s += static_cast<std::uint64_t>(q.front());
            q.pop();
        }
        chk = s + q.size();
        touch(s);
    });
    r.check = chk;
    report(impl, "fifo", "int", N, r, sizeof(Q));
    return 0;
}

// ---- span --------------------------------------------------------------------------------------------------------------
static int span_t(const char *impl, const char *scen, Runner &R)
{
    const std::uint64_t N = R.n;
    std::vector<int> data(N);
    for (std::uint64_t i = 0; i < N; ++i) data[i] = static_cast<int>(i * 7);
    Result r{};
    std::uint64_t chk = 0;
    const bool sub = !std::strcmp(scen, "sub");
    if (!std::strcmp(impl, "ct"))
        r = R.run(N, R.inner_for(N), [] {}, [&] {
            ct::Span<int> s(data.data(), data.size());
            std::uint64_t acc = 0;
            if (sub) { for (std::uint64_t i = 0; i + 16 <= N; i += 16) { auto t = s.subspan(i, 16); for (int x : t) acc += static_cast<std::uint64_t>(x); } }
            else for (int x : s) acc += static_cast<std::uint64_t>(x);
            chk = acc;
            touch(acc);
        });
    else
        r = R.run(N, R.inner_for(N), [] {}, [&] {
            const int *p = data.data();
            std::uint64_t acc = 0;
            if (sub) { for (std::uint64_t i = 0; i + 16 <= N; i += 16) for (std::uint64_t k = 0; k < 16; ++k) acc += static_cast<std::uint64_t>(p[i + k]); }
            else for (std::uint64_t i = 0; i < N; ++i) acc += static_cast<std::uint64_t>(p[i]);
            chk = acc;
            touch(acc);
        });
    r.check = chk;
    report(impl, scen, "int", N, r, 0);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "uso: %s familia impl cenario N\n", argv[0]);
        return 2;
    }
    const std::uint64_t n = std::strtoull(argv[4], nullptr, 10);
    const bool cg = arg_valgrind();
    Runner R(n, cg ? 1 : (n >= 10000000 ? 3 : (n >= 100000 ? 5 : 7)), cg);
    const char *fam = argv[1], *impl = argv[2], *scen = argv[3];
    if (!std::strcmp(fam, "variant"))
    {
        if (!std::strcmp(impl, "ct"))
            return variant_t<ct::Variant<int, double, ct::String>>(impl, scen, R, [](const ct::Variant<int, double, ct::String> &x) { return x.visit(VSum<int>()); });
        return variant_t<std::variant<int, double, ct::String>>(impl, scen, R, [](const std::variant<int, double, ct::String> &x) { return std::visit(VSum<int>(), x); });
    }
    if (!std::strcmp(fam, "arena")) return arena_t(impl, scen, R);
    if (!std::strcmp(fam, "pool")) return pool_t(impl, R);
    if (!std::strcmp(fam, "ring")) return !std::strcmp(impl, "ct") ? ring_t<ct::Queue<int>>(impl, R) : ring_t<std::queue<int>>(impl, R);
    if (!std::strcmp(fam, "span")) return span_t(impl, scen, R);
    std::fprintf(stderr, "familia desconhecida\n");
    return 2;
}
