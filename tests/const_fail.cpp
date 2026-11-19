// Programas que NAO PODEM compilar: escrever por um caminho const num mapa, ou alterar uma chave por um iterador. Cada
// -DCT_CASE=n liga uma das escritas; o CT_CASE=0 e o controlo (so as leituras e as escritas validas) e tem de compilar, por isso
// uma falha de um caso e da escrita e nao de um erro no resto do ficheiro. O CMake regista cada caso como um teste WILL_FAIL
// (ct_const_fail_<n>) e o tools/check.sh (etapa constfail) confirma que a falha e por causa do const. Se juntar um caso, junte
// tambem o numero ao foreach do CMakeLists.txt.
//
//   1  valor pequeno (entrada na tabela), mapa const:   for (auto &e : cm) e.value.x = 1;
//   2  valor grande (fora da tabela), mapa const:       o mesmo
//   3  valor grande, mapa const:                        cm.begin()->value.bump();  (membro nao const)
//   4  valor grande, mapa const:                        Big &r = cm.begin()->value;  (ligar a uma referencia nao const)
//   5  valor grande, mapa nao const:                    m.begin()->key = 1;
//   6  valor pequeno, mapa nao const:                   m.begin()->key = 1;
//   7  entrada grande com chave grande, mapa const:     for (auto &e : cm) e.value.x = 1;
//   8  BTreeMap, mapa nao const:                        m.begin()->key = 1;
//   9  FlatMap, mapa nao const:                         m.begin()->key = 1;
//  10  TreeMap, mapa nao const:                         m.begin()->key = 1;
//  11  HashMap<int, Big>, const_iterator:               typename M::const_iterator it = cm.begin(); it->value.x = 1;
#include <ct/btreemap.hpp>
#include <ct/flatmap.hpp>
#include <ct/hashmap.hpp>
#include <ct/treemap.hpp>

#include <stdint.h>
#include <string.h>

#ifndef CT_CASE
#define CT_CASE 0
#endif

namespace
{
    struct Small
    {
        int x;
        void bump() { ++x; }
    };
    struct Big
    {
        int x;
        unsigned char pad[252];
        void bump() { ++x; }
    };
    struct Key64
    {
        unsigned char b[64];
        uint64_t hash() const
        {
            uint64_t v;
            memcpy(&v, b, sizeof v);
            return ct::detail::hash_mix(v);
        }
        bool operator==(const Key64 &o) const { return memcmp(b, o.b, sizeof b) == 0; }
    };
}

int main()
{
    ct::HashMap<int, Small> small;
    ct::HashMap<int, Big> big;
    ct::HashMap<Key64, Big> bigkey;
    ct::BTreeMap<int, Big> bt;
    ct::FlatMap<int, Big> flat;
    ct::TreeMap<int, Big> tree;
    small.put(1, Small());
    big.put(1, Big());
    bigkey.put(Key64(), Big());
    bt.put(1, Big());
    flat.put(1, Big());
    tree.put(1, Big());
    const ct::HashMap<int, Small> &csmall = small;
    const ct::HashMap<int, Big> &cbig = big;
    const ct::HashMap<Key64, Big> &cbigkey = bigkey;
    (void)csmall;
    (void)cbig;
    (void)cbigkey;

    // o que e valido (controlo): ler por const, escrever o valor por um mapa nao const, ler a chave
    int sum = 0;
    for (auto &e : csmall)
        sum += e.key + e.value.x;
    for (auto &e : cbig)
        sum += e.key + e.value.x;
    for (auto &e : cbigkey)
        sum += e.key.b[0] + e.value.x;
    for (auto &e : big)
    {
        e.value.x = 2;
        e.value.bump();
        sum += e.key;
    }
    for (auto &e : small)
        e.value.bump();
    for (auto &e : bt)
        e.value.bump();
    for (auto &e : flat)
        e.value.bump();
    for (auto &e : tree)
        e.value.bump();

#if CT_CASE == 1
    for (auto &e : csmall)
        e.value.x = 1;
#elif CT_CASE == 2
    for (auto &e : cbig)
        e.value.x = 1;
#elif CT_CASE == 3
    cbig.begin()->value.bump();
#elif CT_CASE == 4
    Big &r = cbig.begin()->value;
    (void)r;
#elif CT_CASE == 5
    big.begin()->key = 1;
#elif CT_CASE == 6
    small.begin()->key = 1;
#elif CT_CASE == 7
    for (auto &e : cbigkey)
        e.value.x = 1;
#elif CT_CASE == 8
    bt.begin()->key = 1;
#elif CT_CASE == 9
    flat.begin()->key = 1;
#elif CT_CASE == 10
    tree.begin()->key = 1;
#elif CT_CASE == 11
    ct::HashMap<int, Big>::const_iterator it = cbig.begin();
    it->value.x = 1;
#endif
    return sum == 12345678 ? 1 : 0;
}
