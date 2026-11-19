// Comparacao ct contra std e outras bibliotecas. Esta pasta e a EXCECAO declarada a regra "sem std":
// compara com a biblioteca standard, por isso inclui-a. So compila com -DCT_BUILD_COMPARE=ON.
#pragma once

#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/resource.h>

#if defined(__has_include)
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define CMP_HAVE_CALLGRIND 1
#endif
#endif
#ifndef CMP_HAVE_CALLGRIND
#define CALLGRIND_TOGGLE_COLLECT
#endif

namespace cmp
{
    struct AllocStats
    {
        std::uint64_t calls;
        std::uint64_t frees;
        std::uint64_t req;
        std::uint64_t live;
        std::uint64_t peak;
    };
    extern AllocStats g_alloc;
    extern volatile std::uint64_t g_sink;

    template <class T>
    inline void keep(const T &v)
    {
        asm volatile("" : : "g"(&v) : "memory");
    }
    inline void touch(std::uint64_t v) { g_sink = g_sink + v; }

    struct Rng
    {
        std::uint64_t s;
        explicit Rng(std::uint64_t seed) : s(seed) {}
        std::uint64_t next()
        {
            std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }
        std::uint64_t below(std::uint64_t n) { return next() % n; }
    };

    // Permutacao dos uint32: chaves distintas e de aspecto aleatorio para i = 0..N-1.
    inline std::uint32_t perm32(std::uint32_t i)
    {
        std::uint32_t x = i * 0x9e3779b1u + 0x7f4a7c15u;
        x ^= x >> 15;
        x *= 0x2c1b3c6du;
        x ^= x >> 12;
        x *= 0x297a2d39u;
        x ^= x >> 15;
        return x;
    }

    struct P16
    {
        std::uint64_t a, b;
    };
    struct B256
    {
        std::uint64_t v[32];
    };
    struct MO
    {
        std::int64_t a, b;
        bool live;
        explicit MO(std::int64_t x = 0) : a(x), b(x * 3), live(true) {}
        MO(MO &&o) noexcept : a(o.a), b(o.b), live(o.live) { o.live = false; }
        MO &operator=(MO &&o) noexcept
        {
            a = o.a;
            b = o.b;
            live = o.live;
            o.live = false;
            return *this;
        }
        MO(const MO &) = delete;
        MO &operator=(const MO &) = delete;
        ~MO()
        {
            if (live)
                g_sink = g_sink + static_cast<std::uint64_t>(a);
        }
    };

    template <class T> struct Mk;
    template <> struct Mk<int>
    {
        static int make(std::uint64_t i) { return static_cast<int>(i * 2654435761u); }
        static std::uint64_t val(int x) { return static_cast<std::uint64_t>(x); }
    };
    template <> struct Mk<P16>
    {
        static P16 make(std::uint64_t i) { return P16{i, i * 3}; }
        static std::uint64_t val(const P16 &x) { return x.a + x.b; }
    };
    template <> struct Mk<B256>
    {
        static B256 make(std::uint64_t i)
        {
            B256 b;
            for (int k = 0; k < 32; ++k)
                b.v[k] = i + static_cast<std::uint64_t>(k);
            return b;
        }
        static std::uint64_t val(const B256 &x) { return x.v[0] + x.v[31]; }
    };
    template <> struct Mk<MO>
    {
        static MO make(std::uint64_t i) { return MO(static_cast<std::int64_t>(i)); }
        static std::uint64_t val(const MO &x) { return static_cast<std::uint64_t>(x.a + x.b); }
    };

    struct Result
    {
        double ns_op;
        std::uint64_t ops;
        std::uint64_t calls;
        std::uint64_t req;
        std::uint64_t peak;
        std::uint64_t live;
        std::uint64_t check;
    };

    // Com CMP_CPUTIME=1 o tempo e o de CPU do processo (CLOCK_PROCESS_CPUTIME_ID) em vez do relogio: numa maquina partilhada e
    // carregada o relogio mede quem mais estava a correr, o tempo de CPU so o que o processo gastou.
    inline double now_ns()
    {
        static const bool cpu = std::getenv("CMP_CPUTIME") != nullptr;
        if (cpu)
        {
            timespec ts;
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
            return static_cast<double>(ts.tv_sec) * 1e9 + static_cast<double>(ts.tv_nsec);
        }
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // reps: repeticoes cronometradas (a melhor conta). Sob callgrind (reps == 1, inner == 1) so a regiao medida conta.
    struct Runner
    {
        std::uint64_t n;
        int reps;
        bool callgrind;
        Result r;
        std::uint64_t base_live;
        std::uint64_t live_snap;

        Runner(std::uint64_t n_, int reps_, bool cg) : n(n_), reps(reps_), callgrind(cg), r(), base_live(0), live_snap(0) {}

        std::uint64_t inner_for(std::uint64_t per_inner_ops) const
        {
            if (callgrind)
                return 1;
            std::uint64_t k = 4000000 / (per_inner_ops ? per_inner_ops : 1);
            return k < 1 ? 1 : (k > 100000 ? 100000 : k);
        }

        void snap_live() { live_snap = g_alloc.live - base_live; }

        // prep corre fora da medicao; body corre `inner` vezes dentro dela; ops_per_inner e o numero de operacoes do body.
        template <class Prep, class Body>
        Result run(std::uint64_t ops_per_inner, std::uint64_t inner, Prep prep, Body body)
        {
            double best = 1e300;
            for (int rep = 0; rep < reps; ++rep)
            {
                prep();
                std::uint64_t c0 = g_alloc.calls, q0 = g_alloc.req;
                base_live = g_alloc.live;
                g_alloc.peak = g_alloc.live;
                live_snap = 0;
                CALLGRIND_TOGGLE_COLLECT;
                double t0 = now_ns();
                for (std::uint64_t i = 0; i < inner; ++i)
                    body();
                double t1 = now_ns();
                CALLGRIND_TOGGLE_COLLECT;
                double per = (t1 - t0) / static_cast<double>(inner * ops_per_inner);
                if (per < best)
                    best = per;
                r.calls = (g_alloc.calls - c0) / inner;
                r.req = (g_alloc.req - q0) / inner;
                r.peak = g_alloc.peak - base_live;
                r.live = live_snap;
            }
            r.ns_op = best;
            r.ops = ops_per_inner * inner;
            return r;
        }
    };

    inline void report(const char *impl, const char *scen, const char *type, std::uint64_t n, const Result &r, std::size_t sz)
    {
        struct rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        std::printf("%s\t%s\t%s\t%llu\t%.3f\t%llu\t%llu\t%llu\t%llu\t%llu\t%ld\t%zu\t%llu\n", impl, scen, type,
                    static_cast<unsigned long long>(n), r.ns_op, static_cast<unsigned long long>(r.ops),
                    static_cast<unsigned long long>(r.calls), static_cast<unsigned long long>(r.req),
                    static_cast<unsigned long long>(r.peak), static_cast<unsigned long long>(r.live), ru.ru_maxrss, sz,
                    static_cast<unsigned long long>(r.check));
    }

    inline bool arg_valgrind()
    {
        const char *e = std::getenv("CMP_CALLGRIND");
        return e && e[0] == '1';
    }
}
