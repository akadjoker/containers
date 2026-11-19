// Ver abi_mixed_a.cpp: esta unidade e compilada com -DCT_DEBUG_ITERATORS=1 e define abi_mixed_sum com a HashMap dela.
#include <ct/hashmap.hpp>

#if !defined(CT_DEBUG_ITERATORS)
#error "abi_mixed_b.cpp tem de ser compilado com CT_DEBUG_ITERATORS=1"
#endif

int abi_mixed_sum(const ct::HashMap<int, int> &m)
{
    int s = 0;
    for (const auto &e : m)
        s += e.value;
    return s;
}
