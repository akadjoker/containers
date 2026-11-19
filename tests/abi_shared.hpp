// Interface comum das unidades do teste de ABI: cada unidade e compilada com opcoes diferentes (NDEBUG numa, -UNDEBUG noutra) e
// todas usam os mesmos tipos da ct. Nao usa a biblioteca standard.
#pragma once

#include <ct/btreemap.hpp>
#include <ct/btreeset.hpp>
#include <ct/deque.hpp>
#include <ct/function.hpp>
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>
#include <ct/variant.hpp>
#include <ct/vector.hpp>

namespace abi
{
    struct Blob256
    {
        unsigned char b[256];
    };

    // Um exemplar de cada tipo; a ordem e o tipo dos membros e o mesmo em todas as unidades
    struct Box
    {
        ct::HashMap<int, int> map;
        ct::HashMap<ct::String, int> smap;
        ct::HashMap<int, ct::String> bigmap; // a entrada de texto
        ct::HashSet<int> set;
        ct::HashSet<ct::String> sset;
        ct::String str;
        ct::Vector<int> vec;
        ct::Variant<int, double, ct::String> var;
        ct::Function<int(int)> fn;
        ct::TreeMap<int, int> tree;
        ct::Deque<int> dq;
        ct::BTreeMap<int, int> bmap;
        ct::BTreeSet<ct::String> bset;
        ct::HashMap<int, Blob256> bigval; // valor grande: a chave no slot e o valor num bloco do slab
    };

    // O que cada unidade ve: sizeof, alignof e a posicao de cada membro dentro de Box, e se NDEBUG estava definido
    struct Report
    {
        unsigned long long ndebug;
        unsigned long long box_size, box_align;
        unsigned long long size[15], align[15], offset[15];
    };

    inline void fill_common(Box &b, int seed)
    {
        for (int i = 0; i < 200; ++i)
        {
            b.map.put(i * 3 + seed, i);
            b.set.insert(i * 7 + seed);
            b.tree.put(i * 5 + seed, i);
            b.bmap.put(i * 11 + seed, i);
            b.vec.push_back(i + seed);
            b.dq.push_back(i);
            b.dq.push_front(-i);
            char buf[48];
            const int n = 1 + (i % 40);
            for (int k = 0; k < n; ++k)
                buf[k] = static_cast<char>('a' + (i + k) % 26);
            buf[n] = '\0';
            b.smap.put(ct::String(buf), i);
            b.sset.insert(ct::String(buf));
            b.bset.insert(ct::String(buf));
            b.bigmap.put(i, ct::String(buf));
        }
        for (int i = 0; i < 50; ++i)
        {
            Blob256 blob;
            for (int k = 0; k < 256; ++k)
                blob.b[k] = static_cast<unsigned char>(i + k + seed);
            b.bigval.put(i, blob);
        }
        b.str = ct::String("a string long enough to leave the small buffer and live on the heap");
        b.var = ct::String("variant holding a string");
        const int add = seed;
        b.fn = [add](int x) { return x * 2 + add; };
    }

    // devolve o numero de discordancias
    inline int check_common(const Box &b, int seed)
    {
        int bad = 0;
        if (b.bmap.size() != 200u || b.bset.size() != b.sset.size() || !b.bmap.validate() || !b.bset.validate())
            ++bad;
        if (b.map.size() != 200u || b.set.size() != 200u || b.tree.size() != 200u || b.vec.size() != 200u || b.dq.size() != 400u)
            ++bad;
        for (int i = 0; i < 200; ++i)
        {
            const int *p = b.map.find(i * 3 + seed);
            if (!p || *p != i)
                ++bad;
            if (!b.set.contains(i * 7 + seed))
                ++bad;
            const int *bm = b.bmap.find(i * 11 + seed);
            if (!bm || *bm != i)
                ++bad;
            const int *t = b.tree.find(i * 5 + seed);
            if (!t || *t != i)
                ++bad;
            if (b.vec[static_cast<size_t>(i)] != i + seed)
                ++bad;
            char buf[48];
            const int n = 1 + (i % 40);
            for (int k = 0; k < n; ++k)
                buf[k] = static_cast<char>('a' + (i + k) % 26);
            buf[n] = '\0';
            const int *s = b.smap.find(ct::String(buf));
            if (!s || b.sset.contains(ct::String(buf)) == false)
                ++bad;
            const ct::String *bs = b.bigmap.find(i);
            if (!bs || !(*bs == buf))
                ++bad;
        }
        for (int i = 0; i < 50; ++i)
        {
            const Blob256 *v = b.bigval.find(i);
            if (!v || v->b[0] != static_cast<unsigned char>(i + seed) || v->b[255] != static_cast<unsigned char>(i + 255 + seed))
                ++bad;
        }
        if (!(b.str == "a string long enough to leave the small buffer and live on the heap"))
            ++bad;
        if (!b.var.template is<ct::String>())
            ++bad;
        if (b.fn(10) != 20 + seed)
            ++bad;
        // percorrer
        size_t n = 0;
        long long sum = 0;
        for (const auto &e : b.map)
        {
            sum += e.value;
            ++n;
        }
        if (n != 200u || sum != 199 * 200 / 2)
            ++bad;
        return bad;
    }

    template <typename F>
    inline Report make_report_t(unsigned long long ndebug, F)
    {
        Box b;
        const char *base = reinterpret_cast<const char *>(&b);
        Report r = {};
        r.ndebug = ndebug;
        r.box_size = sizeof(Box);
        r.box_align = alignof(Box);
        r.size[0] = sizeof(b.map);
        r.align[0] = alignof(decltype(b.map));
        r.offset[0] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.map) - base);
        r.size[1] = sizeof(b.smap);
        r.align[1] = alignof(decltype(b.smap));
        r.offset[1] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.smap) - base);
        r.size[2] = sizeof(b.bigmap);
        r.align[2] = alignof(decltype(b.bigmap));
        r.offset[2] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.bigmap) - base);
        r.size[3] = sizeof(b.set);
        r.align[3] = alignof(decltype(b.set));
        r.offset[3] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.set) - base);
        r.size[4] = sizeof(b.sset);
        r.align[4] = alignof(decltype(b.sset));
        r.offset[4] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.sset) - base);
        r.size[5] = sizeof(b.str);
        r.align[5] = alignof(decltype(b.str));
        r.offset[5] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.str) - base);
        r.size[6] = sizeof(b.vec);
        r.align[6] = alignof(decltype(b.vec));
        r.offset[6] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.vec) - base);
        r.size[7] = sizeof(b.var);
        r.align[7] = alignof(decltype(b.var));
        r.offset[7] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.var) - base);
        r.size[8] = sizeof(b.fn);
        r.align[8] = alignof(decltype(b.fn));
        r.offset[8] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.fn) - base);
        r.size[9] = sizeof(b.tree);
        r.align[9] = alignof(decltype(b.tree));
        r.offset[9] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.tree) - base);
        r.size[10] = sizeof(b.dq);
        r.align[10] = alignof(decltype(b.dq));
        r.offset[10] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.dq) - base);
        r.size[12] = sizeof(b.bmap);
        r.align[12] = alignof(decltype(b.bmap));
        r.offset[12] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.bmap) - base);
        r.size[13] = sizeof(b.bset);
        r.align[13] = alignof(decltype(b.bset));
        r.offset[13] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.bset) - base);
        r.size[14] = sizeof(b.bigval);
        r.align[14] = alignof(decltype(b.bigval));
        r.offset[14] = static_cast<unsigned long long>(reinterpret_cast<const char *>(&b.bigval) - base);
        // o tamanho dos iteradores das tabelas (com a verificacao de depuracao ligada um iterador guarda a versao)
        r.size[11] = sizeof(ct::HashMap<int, int>::iterator) * 1000000ull + sizeof(ct::HashSet<int>::iterator) * 1000ull + sizeof(ct::HashMap<int, int>::const_iterator);
        r.align[11] = alignof(ct::HashMap<int, int>::iterator);
        r.offset[11] = 0;
        return r;
    }
}
