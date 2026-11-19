// Contador de alocacoes: interpoe malloc e companhia (glibc). Conta chamadas, bytes pedidos e bytes vivos
// (tamanho utilizavel mais 8 bytes de cabecalho do glibc, que e o que a memoria realmente custa).
#include "harness.hpp"

#include <errno.h>
#include <malloc.h>

extern "C"
{
    void *__libc_malloc(std::size_t);
    void __libc_free(void *);
    void *__libc_calloc(std::size_t, std::size_t);
    void *__libc_realloc(void *, std::size_t);
    void *__libc_memalign(std::size_t, std::size_t);
}

namespace cmp
{
    AllocStats g_alloc = {0, 0, 0, 0, 0};
    volatile std::uint64_t g_sink = 0;

    static inline void on_alloc(void *p, std::size_t req)
    {
        if (!p)
            return;
        g_alloc.calls += 1;
        g_alloc.req += req;
        g_alloc.live += malloc_usable_size(p) + 8;
        if (g_alloc.live > g_alloc.peak)
            g_alloc.peak = g_alloc.live;
    }
}

extern "C"
{
    void *malloc(std::size_t n)
    {
        void *p = __libc_malloc(n);
        cmp::on_alloc(p, n);
        return p;
    }
    void free(void *p)
    {
        if (!p)
            return;
        cmp::g_alloc.frees += 1;
        cmp::g_alloc.live -= malloc_usable_size(p) + 8;
        __libc_free(p);
    }
    void *calloc(std::size_t a, std::size_t b)
    {
        void *p = __libc_calloc(a, b);
        cmp::on_alloc(p, a * b);
        return p;
    }
    void *realloc(void *old, std::size_t n)
    {
        if (old)
            cmp::g_alloc.live -= malloc_usable_size(old) + 8;
        void *p = __libc_realloc(old, n);
        if (p)
            cmp::on_alloc(p, n);
        else if (old)
            cmp::g_alloc.live += malloc_usable_size(old) + 8;
        return p;
    }
    void *memalign(std::size_t al, std::size_t n)
    {
        void *p = __libc_memalign(al, n);
        cmp::on_alloc(p, n);
        return p;
    }
    void *aligned_alloc(std::size_t al, std::size_t n) { return memalign(al, n); }
    int posix_memalign(void **out, std::size_t al, std::size_t n)
    {
        void *p = __libc_memalign(al, n);
        if (!p)
            return ENOMEM;
        cmp::on_alloc(p, n);
        *out = p;
        return 0;
    }
}
