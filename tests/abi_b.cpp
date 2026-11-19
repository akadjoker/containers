// Unidade B do teste de ABI: compilada com -UNDEBUG (ver CMakeLists.txt). Nao usa a biblioteca standard.
#include "abi_shared.hpp"

#if defined(NDEBUG)
#error "abi_b.cpp tem de ser compilado sem NDEBUG"
#endif

namespace abi
{
    void fill_b(Box &b) { fill_common(b, 22); }
    bool check_b(const Box &b) { return check_common(b, 11) == 0; } // confere o que a unidade A preencheu
    Report report_b() { return make_report_t(0, 0); }
}
