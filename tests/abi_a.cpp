// Unidade A do teste de ABI: compilada com -DNDEBUG (ver CMakeLists.txt). Nao usa a biblioteca standard.
#include "abi_shared.hpp"

#if !defined(NDEBUG)
#error "abi_a.cpp tem de ser compilado com NDEBUG"
#endif

namespace abi
{
    void fill_a(Box &b) { fill_common(b, 11); }
    bool check_a(const Box &b) { return check_common(b, 22) == 0; } // confere o que a unidade B preencheu
    Report report_a() { return make_report_t(1, 0); }
}
