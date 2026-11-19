// Controlo do MemorySanitizer para Vector::resize_for_overwrite e append_uninitialized: o contentor nao le os elementos
// novos (escrever-os-todos e ler-tudo nao da aviso), e o argumento "neg" le um sem o escrever, que o MSan tem de apanhar.
//
//   clang++ -std=c++14 -O1 -g -fsanitize=memory -Iinclude tests/msan_overwrite.cpp -o msan_overwrite
//   setarch -R ./msan_overwrite        (sem argumentos: acaba em 0 e imprime ok)
//   setarch -R ./msan_overwrite neg    (tem de dar "use-of-uninitialized-value")
#include <ct/vector.hpp>

#include <stdio.h>

int main(int argc, char **)
{
    ct::Vector<int> v;
    for (int i = 0; i < 5; ++i)
        v.push_back(i);
    const unsigned sizes[] = {6u, 9u, 8u, 100u, 17u, 100u, 1000u};
    for (unsigned n : sizes)
    {
        const size_t old = v.size();
        v.resize_for_overwrite(n);
        for (size_t i = old; i < n; ++i)
            v[i] = static_cast<int>(i);
        long s = 0;
        for (size_t i = 0; i < n; ++i)
            s += v[i];
        if (s == 1)
            puts("x");
    }
    int *p = v.append_uninitialized(500);
    for (int i = 0; i < 500; ++i)
        p[i] = i;
    long s = 0;
    for (size_t i = 0; i < v.size(); ++i)
        s += v[i];
    printf("ok %ld\n", s);
    if (argc > 1)
    {
        int *q = v.append_uninitialized(4);
        if (q[2] == 12345)
            puts("coincidencia");
    }
    return 0;
}
