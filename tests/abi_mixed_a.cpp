// Teste de ligacao (tem de FALHAR): a unidade A usa a HashMap sem a verificacao de depuracao e a B com CT_DEBUG_ITERATORS=1; a
// funcao partilhada recebe uma HashMap, cujo nome leva o namespace inline da verificacao (ct::abi_dbgiter::HashMap), por isso a
// ligacao tem de dar "undefined reference" em vez de deixar passar dois layouts diferentes. Nao usa a biblioteca standard.
#include <ct/hashmap.hpp>

#if defined(CT_DEBUG_ITERATORS)
#error "abi_mixed_a.cpp tem de ser compilado sem CT_DEBUG_ITERATORS"
#endif

int abi_mixed_sum(const ct::HashMap<int, int> &m);

int main()
{
    ct::HashMap<int, int> m;
    for (int i = 0; i < 10; ++i)
        m.put(i, i);
    return abi_mixed_sum(m) == 45 ? 0 : 1;
}
