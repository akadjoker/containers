// Os operadores new[] com nome e linha que a EASTL pede ao alocador por omissao (so existe com a EASTL nas includes).
#if __has_include(<EASTL/allocator.h>)
#include <cstddef>
#include <new>

void *operator new[](std::size_t size, const char *, int, unsigned, const char *, int) { return ::operator new[](size); }
void *operator new[](std::size_t size, std::size_t, std::size_t, const char *, int, unsigned, const char *, int) { return ::operator new[](size); }
#endif
