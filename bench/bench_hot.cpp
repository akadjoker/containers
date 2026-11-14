// Os caminhos quentes que as aplicacoes usam, so com a ct (sem biblioteca standard): Vector, HashMap (int e String),
// String, sort, Function, TreeMap/FlatMap, Mutex/Atomic e ThreadPool. Cada linha e o melhor de varias repeticoes, em
// milissegundos. Serve para comparar antes e depois de mexer num cabecalho: compilar contra duas arvores de
// include com -I e comparar as colunas.
//
//   c++ -std=c++14 -O3 -march=native -I include bench/bench_hot.cpp -o bench_hot -lpthread

#include <ct/flatmap.hpp>
#include <ct/function.hpp>
#include <ct/hashmap.hpp>
#include <ct/sort.hpp>
#include <ct/string.hpp>
#include <ct/thread.hpp>
#include <ct/threadpool.hpp>
#include <ct/treemap.hpp>
#include <ct/vector.hpp>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{
    volatile uint64_t g_sink = 0;

    double now_ms()
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) * 1e3 + static_cast<double>(ts.tv_nsec) / 1e6;
    }

    template <typename F>
    void run(const char *name, int reps, F f)
    {
        double best = 1e300;
        for (int i = 0; i < reps; ++i)
        {
            const double t0 = now_ms();
            g_sink += f();
            const double t1 = now_ms();
            if (t1 - t0 < best)
                best = t1 - t0;
        }
        printf("%-44s %10.3f ms\n", name, best);
        fflush(stdout);
    }

    uint32_t next(uint32_t &s) { return s = s * 1664525u + 1013904223u; }

    struct Item
    {
        int key;
        int payload;
        float a, b;
    };

    uint64_t vector_push_int(bool reserve)
    {
        ct::Vector<int> v;
        if (reserve)
            v.reserve(4000000);
        for (int i = 0; i < 4000000; ++i)
            v.push_back(i);
        return v.size();
    }

    uint64_t vector_iterate(const ct::Vector<int> &v)
    {
        uint64_t sum = 0;
        for (int rep = 0; rep < 20; ++rep)
            for (int x : v)
                sum += static_cast<uint64_t>(x);
        return sum;
    }

    uint64_t vector_index(const ct::Vector<int> &v)
    {
        uint64_t sum = 0;
        for (int rep = 0; rep < 20; ++rep)
            for (size_t i = 0; i < v.size(); ++i)
                sum += static_cast<uint64_t>(v[i]);
        return sum;
    }

    uint64_t vector_push_struct()
    {
        ct::Vector<Item> v;
        for (int i = 0; i < 1000000; ++i)
        {
            Item it = {i, i * 2, 1.0f, 2.0f};
            v.push_back(it);
        }
        return v.size();
    }

    uint64_t vector_of_strings()
    {
        ct::Vector<ct::String> v;
        for (int i = 0; i < 300000; ++i)
        {
            ct::String s("item-");
            s.append_number(i);
            v.push_back(ct::move(s));
        }
        ct::Vector<ct::String> copy(v);
        return copy.size();
    }

    uint64_t hash_int_insert()
    {
        ct::HashMap<int, int> m;
        uint32_t s = 1;
        for (int i = 0; i < 1000000; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }

    uint64_t hash_int_find(const ct::HashMap<int, int> &m, bool hit)
    {
        uint64_t acc = 0;
        uint32_t s = hit ? 1 : 12345;
        for (int i = 0; i < 1000000; ++i)
        {
            const int *p = m.find(static_cast<int>(next(s)));
            if (p)
                acc += static_cast<uint64_t>(*p);
        }
        return acc;
    }

    uint64_t hash_int_erase()
    {
        ct::HashMap<int, int> m;
        m.reserve(500000);
        uint32_t s = 1;
        for (int i = 0; i < 500000; ++i)
            m.put(static_cast<int>(next(s)), i);
        s = 1;
        uint64_t erased = 0;
        for (int i = 0; i < 500000; ++i)
            erased += m.erase(static_cast<int>(next(s)));
        return erased;
    }

    uint64_t hash_int_churn()
    {
        ct::HashMap<int, int> m;
        uint32_t s = 7;
        for (int i = 0; i < 2000000; ++i)
        {
            const int k = static_cast<int>(next(s) % 50000);
            if (next(s) & 1)
                m.put(k, i);
            else
                m.erase(k);
        }
        return m.size();
    }

    ct::String make_key(int i, int len)
    {
        ct::String s;
        s.append_number(i);
        s.push_back('-');
        while (static_cast<int>(s.size()) < len)
            s.push_back(static_cast<char>('a' + (i + static_cast<int>(s.size())) % 26));
        return s;
    }

    uint64_t hash_string_insert(int len)
    {
        ct::HashMap<ct::String, int> m;
        for (int i = 0; i < 200000; ++i)
            m.put(make_key(i, len), i);
        return m.size();
    }

    uint64_t hash_string_find(const ct::HashMap<ct::String, int> &m, int len, const ct::Vector<ct::String> &keys)
    {
        (void)len;
        uint64_t acc = 0;
        for (int rep = 0; rep < 5; ++rep)
            for (size_t i = 0; i < keys.size(); ++i)
            {
                const int *p = m.find(keys[i]);
                if (p)
                    acc += static_cast<uint64_t>(*p);
            }
        return acc;
    }

    uint64_t hash_string_erase(int len)
    {
        ct::HashMap<ct::String, int> m;
        for (int i = 0; i < 100000; ++i)
            m.put(make_key(i, len), i);
        uint64_t erased = 0;
        for (int i = 0; i < 100000; ++i)
            erased += m.erase(make_key(i, len));
        return erased;
    }

    uint64_t string_push_back()
    {
        ct::String s;
        for (int i = 0; i < 8000000; ++i)
            s.push_back(static_cast<char>('a' + i % 26));
        return s.size();
    }

    uint64_t string_append_pieces()
    {
        ct::String s;
        for (int i = 0; i < 1000000; ++i)
        {
            s.append("key=");
            s.append_number(i);
            s.push_back(';');
        }
        return s.size();
    }

    uint64_t string_concat()
    {
        uint64_t n = 0;
        for (int i = 0; i < 500000; ++i)
        {
            ct::String a("clip-");
            a.append_number(i);
            ct::String b = a + "/" + a + ".mp4";
            n += b.size();
        }
        return n;
    }

    uint64_t string_number_double()
    {
        uint64_t n = 0;
        for (int i = 0; i < 300000; ++i)
        {
            ct::String s;
            s.append_number(i * 0.37, 6);
            n += s.size();
        }
        return n;
    }

    uint64_t string_find_split(const ct::String &text)
    {
        uint64_t n = text.find("needle-not-present");
        ct::Vector<ct::String> parts = text.split(',');
        return n + parts.size();
    }

    uint64_t sort_structs(bool dup)
    {
        ct::Vector<Item> v;
        v.resize(1000000);
        uint32_t s = 5;
        for (size_t i = 0; i < v.size(); ++i)
        {
            v[i].key = dup ? static_cast<int>(next(s) % 16) : static_cast<int>(next(s));
            v[i].payload = static_cast<int>(i);
        }
        ct::sort(v.begin(), v.end(), [](const Item &a, const Item &b) { return a.key < b.key; });
        return static_cast<uint64_t>(v[500000].key);
    }

    uint64_t stable_sort_structs()
    {
        ct::Vector<Item> v;
        v.resize(1000000);
        uint32_t s = 5;
        for (size_t i = 0; i < v.size(); ++i)
        {
            v[i].key = static_cast<int>(next(s) % 1000);
            v[i].payload = static_cast<int>(i);
        }
        ct::stable_sort(v.begin(), v.end(), [](const Item &a, const Item &b) { return a.key < b.key; });
        return static_cast<uint64_t>(v[500000].key);
    }

    uint64_t sort_ints()
    {
        ct::Vector<int> v;
        v.resize(2000000);
        uint32_t s = 5;
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = static_cast<int>(next(s));
        ct::sort(v.begin(), v.end());
        return static_cast<uint64_t>(v[1000000]);
    }

    uint64_t sort_strings()
    {
        ct::Vector<ct::String> v;
        uint32_t s = 5;
        for (int i = 0; i < 200000; ++i)
            v.push_back(make_key(static_cast<int>(next(s) % 1000000), 14));
        ct::sort(v.begin(), v.end());
        return v[100000].size();
    }

    uint64_t function_call_small()
    {
        int base = 3;
        ct::Function<int(int)> f = [base](int x) { return x + base; };
        int acc = 0;
        for (int i = 0; i < 100000000; ++i)
            acc = f(acc);
        return static_cast<uint64_t>(acc);
    }

    uint64_t function_call_large()
    {
        double big[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        ct::Function<int(int)> f = [big](int x) { return x + static_cast<int>(big[x & 7]); };
        int acc = 0;
        for (int i = 0; i < 100000000; ++i)
            acc = f(acc);
        return static_cast<uint64_t>(acc);
    }

    uint64_t function_copy_move()
    {
        int a = 1, b = 2;
        double big[6] = {1, 2, 3, 4, 5, 6};
        ct::Function<int()> small = [a, b] { return a + b; };
        ct::Function<int()> large = [big] { return static_cast<int>(big[0]); };
        uint64_t acc = 0;
        for (int i = 0; i < 3000000; ++i)
        {
            ct::Function<int()> c1(small);
            ct::Function<int()> c2(large);
            ct::Function<int()> m1(ct::move(c1));
            acc += static_cast<uint64_t>(m1() + c2());
        }
        return acc;
    }

    uint64_t vector_of_functions()
    {
        ct::Vector<ct::Function<int(int)>> v;
        for (int i = 0; i < 200000; ++i)
            v.push_back([i](int x) { return x + i; });
        int acc = 0;
        for (size_t i = 0; i < v.size(); ++i)
            acc = v[i](acc);
        return static_cast<uint64_t>(acc);
    }

    uint64_t tree_insert()
    {
        ct::TreeMap<int, int> m;
        uint32_t s = 3;
        for (int i = 0; i < 200000; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }

    uint64_t tree_find(ct::TreeMap<int, int> &m)
    {
        uint64_t acc = 0;
        uint32_t s = 3;
        for (int i = 0; i < 1000000; ++i)
        {
            const int *p = m.find(static_cast<int>(next(s)));
            if (p)
                acc += static_cast<uint64_t>(*p);
        }
        return acc;
    }

    uint64_t tree_churn()
    {
        ct::TreeMap<int, int> m;
        uint32_t s = 9;
        for (int i = 0; i < 400000; ++i)
        {
            const int k = static_cast<int>(next(s) % 20000);
            if (next(s) & 1)
                m.put(k, i);
            else
                m.erase(k);
        }
        return m.size();
    }

    uint64_t flat_insert()
    {
        ct::FlatMap<int, int> m;
        uint32_t s = 3;
        for (int i = 0; i < 20000; ++i)
            m.put(static_cast<int>(next(s)), i);
        return m.size();
    }

    uint64_t mutex_uncontended()
    {
        ct::Mutex m;
        uint64_t n = 0;
        for (int i = 0; i < 30000000; ++i)
        {
            m.lock();
            ++n;
            m.unlock();
        }
        return n;
    }

    uint64_t atomic_add()
    {
        ct::Atomic<int> a(0);
        for (int i = 0; i < 50000000; ++i)
            a.fetch_add(1);
        return static_cast<uint64_t>(a.load());
    }

    uint64_t threadpool_jobs()
    {
        ct::ThreadPool pool(3);
        ct::Atomic<int> counter(0);
        for (int i = 0; i < 100000; ++i)
            pool.submit([&counter] { counter.fetch_add(1); });
        pool.wait_all();
        return static_cast<uint64_t>(counter.load());
    }

    uint64_t threadpool_parallel_for()
    {
        ct::ThreadPool pool(3);
        ct::Vector<int> v;
        v.resize(4000000);
        for (int rep = 0; rep < 10; ++rep)
            pool.parallel_for(0, v.size(), [&v](size_t i) { v[i] = static_cast<int>(i * 3); });
        return static_cast<uint64_t>(v[12345]);
    }
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : "";
    auto want = [&](const char *group) { return !*only || strstr(group, only) != nullptr; };

    if (want("vector"))
    {
        run("vector push_back int x4M (grow)", 7, [] { return vector_push_int(false); });
        run("vector push_back int x4M (reserved)", 7, [] { return vector_push_int(true); });
        ct::Vector<int> big;
        big.resize(4000000);
        for (size_t i = 0; i < big.size(); ++i)
            big[i] = static_cast<int>(i);
        run("vector iterate range-for 4M x20", 7, [&] { return vector_iterate(big); });
        run("vector iterate by index 4M x20", 7, [&] { return vector_index(big); });
        run("vector push_back struct 16B x1M", 7, [] { return vector_push_struct(); });
        run("vector<String> 300k build + copy", 5, [] { return vector_of_strings(); });
    }
    if (want("hashmap"))
    {
        run("hashmap<int> put 1M random", 7, [] { return hash_int_insert(); });
        ct::HashMap<int, int> m;
        uint32_t s = 1;
        for (int i = 0; i < 1000000; ++i)
            m.put(static_cast<int>(next(s)), i);
        run("hashmap<int> find hit 1M", 7, [&] { return hash_int_find(m, true); });
        run("hashmap<int> find miss 1M", 7, [&] { return hash_int_find(m, false); });
        run("hashmap<int> erase 500k", 7, [] { return hash_int_erase(); });
        run("hashmap<int> churn put/erase 2M", 7, [] { return hash_int_churn(); });
    }
    if (want("hashstring"))
    {
        const int lens[] = {8, 16, 32, 64};
        for (int li = 0; li < 4; ++li)
        {
            const int len = lens[li];
            char name[64];
            snprintf(name, sizeof name, "hashmap<String> put 200k, key %dB", len);
            run(name, 5, [len] { return hash_string_insert(len); });
            ct::HashMap<ct::String, int> m;
            ct::Vector<ct::String> keys;
            for (int i = 0; i < 200000; ++i)
            {
                keys.push_back(make_key(i, len));
                m.put(keys.back(), i);
            }
            snprintf(name, sizeof name, "hashmap<String> find 200k x5, key %dB", len);
            run(name, 7, [&] { return hash_string_find(m, len, keys); });
            snprintf(name, sizeof name, "hashmap<String> put+erase 100k, key %dB", len);
            run(name, 5, [len] { return hash_string_erase(len); });
        }
    }
    if (want("string"))
    {
        run("string push_back x8M", 7, [] { return string_push_back(); });
        run("string append pieces x1M", 7, [] { return string_append_pieces(); });
        run("string a + \"/\" + a + \".mp4\" x500k", 7, [] { return string_concat(); });
        run("string append_number(double) x300k", 7, [] { return string_number_double(); });
        ct::String text;
        for (int i = 0; i < 100000; ++i)
        {
            text.append_number(i);
            text.push_back(',');
        }
        run("string find (miss) + split 100k fields", 7, [&] { return string_find_split(text); });
    }
    if (want("sort"))
    {
        run("sort struct 16B x1M random keys", 5, [] { return sort_structs(false); });
        run("sort struct 16B x1M 16 distinct keys", 5, [] { return sort_structs(true); });
        run("stable_sort struct 16B x1M", 5, [] { return stable_sort_structs(); });
        run("sort int x2M (radix)", 5, [] { return sort_ints(); });
        run("sort String x200k", 5, [] { return sort_strings(); });
    }
    if (want("function"))
    {
        run("function call, small capture x100M", 5, [] { return function_call_small(); });
        run("function call, large capture x100M", 5, [] { return function_call_large(); });
        run("function copy+move small/large x3M", 5, [] { return function_copy_move(); });
        run("vector<Function> 200k push + call", 5, [] { return vector_of_functions(); });
    }
    if (want("tree"))
    {
        run("treemap put 200k random", 7, [] { return tree_insert(); });
        ct::TreeMap<int, int> t;
        uint32_t s = 3;
        for (int i = 0; i < 200000; ++i)
            t.put(static_cast<int>(next(s)), i);
        run("treemap find 1M", 7, [&] { return tree_find(t); });
        run("treemap churn put/erase 400k", 7, [] { return tree_churn(); });
        run("flatmap put 20k random", 7, [] { return flat_insert(); });
    }
    if (want("thread"))
    {
        run("mutex lock+unlock uncontended x30M", 5, [] { return mutex_uncontended(); });
        run("atomic fetch_add x50M", 5, [] { return atomic_add(); });
        run("threadpool submit 100k jobs + wait_all", 5, [] { return threadpool_jobs(); });
        run("threadpool parallel_for 4M x10", 5, [] { return threadpool_parallel_for(); });
    }
    printf("sink %llu\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
