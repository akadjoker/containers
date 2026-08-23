// ct::Pool vs new/delete, malloc/free e o BlockAllocator do box3d (phys).
// Cenários de jogo: churn de spawn/kill de objetos pequenos.
#include <ct/pool.hpp>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "bench.hpp"

#ifdef CT_BENCH_HAVE_PHYS
#include <block_allocator.h>
#endif

volatile std::uint64_t bench::sink = 0;

namespace
{

    struct Bullet // 24 bytes, POD, sem construtores
    {
        float x, y, vx, vy;
        int damage;
        int owner;
    };

    constexpr int N = 100000;   // objetos por vaga
    constexpr int WAVES = 10;   // vagas de spawn+kill
    constexpr int CHURN = 1000000;

    // ---- vagas: spawn N, matar todos, repetir ----

    template <typename AllocFn, typename FreeFn>
    std::uint64_t waves(AllocFn af, FreeFn ff)
    {
        std::uint64_t acc = 0;
        std::vector<Bullet *> v;
        v.reserve(N);
        for (int w = 0; w < WAVES; ++w)
        {
            for (int i = 0; i < N; ++i)
            {
                Bullet *b = af();
                b->damage = i;
                v.push_back(b);
            }
            acc += static_cast<std::uint64_t>(v.back()->damage);
            for (Bullet *b : v)
                ff(b);
            v.clear();
        }
        return acc;
    }

    // ---- churn misto: 75% spawn / 25% kill aleatório, 1M operações ----

    template <typename AllocFn, typename FreeFn>
    std::uint64_t churn(AllocFn af, FreeFn ff)
    {
        std::uint64_t acc = 0;
        std::vector<Bullet *> alive;
        alive.reserve(CHURN);
        unsigned seed = 7;
        for (int i = 0; i < CHURN; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            if (alive.empty() || (seed & 3) != 0)
            {
                Bullet *b = af();
                b->damage = i;
                alive.push_back(b);
            }
            else
            {
                std::size_t idx = (seed >> 8) % alive.size();
                acc += static_cast<std::uint64_t>(alive[idx]->damage);
                ff(alive[idx]);
                alive[idx] = alive.back();
                alive.pop_back();
            }
        }
        for (Bullet *b : alive)
            ff(b);
        return acc;
    }

    // ---- par quente: spawn+kill imediato (pior caso de alocador) ----

    template <typename AllocFn, typename FreeFn>
    std::uint64_t hot_pair(AllocFn af, FreeFn ff)
    {
        std::uint64_t acc = 0;
        for (int i = 0; i < CHURN; ++i)
        {
            Bullet *b = af();
            bench::escape(b); // impede o compilador de elidir o par alloc/free
            b->damage = i;
            acc += static_cast<std::uint64_t>(b->damage);
            ff(b);
        }
        return acc;
    }

} // namespace

int main()
{
    std::printf("ct::Pool<Bullet> (%zu B/slot) — cenários de spawn/kill de jogo\n",
                ct::Pool<Bullet>::slot_size());

    // adversários
    auto new_a = [] { return new Bullet; };
    auto new_f = [](Bullet *b) { delete b; };
    auto mal_a = [] { return static_cast<Bullet *>(std::malloc(sizeof(Bullet))); };
    auto mal_f = [](Bullet *b) { std::free(b); };

    bench::header("vs new/delete (coluna std)");
    {
        ct::Pool<Bullet> pool;
        auto pa = [&] { return pool.allocate(); };
        auto pf = [&](Bullet *b) { pool.deallocate(b); };
        bench::compare("10 vagas de 100k spawn+kill",
                       [&] { bench::sink += waves(pa, pf); },
                       [&] { bench::sink += waves(new_a, new_f); });
        bench::compare("1M churn misto 75/25",
                       [&] { bench::sink += churn(pa, pf); },
                       [&] { bench::sink += churn(new_a, new_f); });
        bench::compare("1M spawn+kill imediato",
                       [&] { bench::sink += hot_pair(pa, pf); },
                       [&] { bench::sink += hot_pair(new_a, new_f); });
    }

    bench::header("vs malloc/free (coluna std)");
    {
        ct::Pool<Bullet> pool;
        auto pa = [&] { return pool.allocate(); };
        auto pf = [&](Bullet *b) { pool.deallocate(b); };
        bench::compare("10 vagas de 100k spawn+kill",
                       [&] { bench::sink += waves(pa, pf); },
                       [&] { bench::sink += waves(mal_a, mal_f); });
        bench::compare("1M churn misto 75/25",
                       [&] { bench::sink += churn(pa, pf); },
                       [&] { bench::sink += churn(mal_a, mal_f); });
    }

#ifdef CT_BENCH_HAVE_PHYS
    bench::header("vs BlockAllocator do box3d (coluna std)");
    {
        ct::Pool<Bullet> pool;
        phys::BlockAllocator ba;
        auto pa = [&] { return pool.allocate(); };
        auto pf = [&](Bullet *b) { pool.deallocate(b); };
        auto ba_a = [&] { return static_cast<Bullet *>(ba.Allocate(sizeof(Bullet))); };
        auto ba_f = [&](Bullet *b) { ba.Free(b, sizeof(Bullet)); };
        bench::compare("10 vagas de 100k spawn+kill",
                       [&] { bench::sink += waves(pa, pf); },
                       [&] { bench::sink += waves(ba_a, ba_f); });
        bench::compare("1M churn misto 75/25",
                       [&] { bench::sink += churn(pa, pf); },
                       [&] { bench::sink += churn(ba_a, ba_f); });
        bench::compare("1M spawn+kill imediato",
                       [&] { bench::sink += hot_pair(pa, pf); },
                       [&] { bench::sink += hot_pair(ba_a, ba_f); });
    }
#else
    std::printf("\n(BlockAllocator do box3d não encontrado — bench saltado)\n");
#endif

    return 0;
}
