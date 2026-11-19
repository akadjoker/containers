
#pragma once

#include "detail/utils.hpp"
#include <new>

namespace ct
{

    namespace detail
    {

                constexpr std::size_t kSmallSort = 24;
        constexpr std::size_t kRadixMin = 128;   

        template <typename T, typename C>
        inline void insertion_sort(T *lo, T *hi, C cmp)
        {
            for (T *i = lo + 1; i < hi; ++i)
            {
                if (cmp(*i, *(i - 1)))
                {
                    T tmp = detail::move(*i);
                    T *j = i;
                    do
                    {
                        *j = detail::move(*(j - 1));
                        --j;
                    } while (j > lo && cmp(tmp, *(j - 1)));
                    *j = detail::move(tmp);
                }
            }
        }

        template <typename T, typename C>
        inline void sift_down(T *a, std::size_t n, std::size_t i, C cmp)
        {
            for (;;)
            {
                std::size_t l = 2 * i + 1;
                if (l >= n)
                    return;
                std::size_t big = (l + 1 < n && cmp(a[l], a[l + 1])) ? l + 1 : l;
                if (!cmp(a[i], a[big]))
                    return;
                swap_vals(a[i], a[big]);
                i = big;
            }
        }

        template <typename T, typename C>
        inline void heap_sort(T *lo, T *hi, C cmp)
        {
            std::size_t n = static_cast<std::size_t>(hi - lo);
            for (std::size_t i = n / 2; i > 0; --i)
                sift_down(lo, n, i - 1, cmp);
            for (std::size_t i = n; i > 1; --i)
            {
                swap_vals(lo[0], lo[i - 1]);
                sift_down(lo, i - 1, 0, cmp);
            }
        }

        // O ponteiro para a mediana de *a, *b e *c.
        template <typename T, typename C>
        inline T *median_of_3(T *a, T *b, T *c, C cmp)
        {
            if (cmp(*a, *b))
            {
                if (cmp(*b, *c))
                    return b;
                return cmp(*a, *c) ? c : a;
            }
            if (cmp(*a, *c))
                return a;
            return cmp(*b, *c) ? c : b;
        }

        // Poe a mediana (de tres amostras, ou de nove acima de 128 elementos: a mediana das medianas de tres grupos, que
        // da uma divisao mais equilibrada em entradas grandes) em *lo. So le dentro de [lo, hi).
        template <typename T, typename C>
        inline void choose_pivot(T *lo, T *hi, C cmp)
        {
            const std::size_t n = static_cast<std::size_t>(hi - lo);
            T *mid = lo + n / 2;
            T *last = hi - 1;
            T *m;
            if (n > 128)
            {
                const std::size_t s = n / 8;
                T *a = median_of_3(lo + 1, lo + 1 + s, lo + 1 + 2 * s, cmp);
                T *b = median_of_3(mid - s, mid, mid + s, cmp);
                T *c = median_of_3(last - 2 * s, last - s, last, cmp);
                m = median_of_3(a, b, c, cmp);
            }
            else
                m = median_of_3(lo + 1, mid, last, cmp);
            swap_vals(*lo, *m);
        }

        // ---- particao por blocos (BlockQuicksort, Edelkamp e Weiss) ----
        // Os elementos de tipos trivialmente copiaveis e pequenos particionam-se sem saltos que dependam dos dados: cada
        // bloco de 64 elementos guarda os desvios dos que estao do lado errado (um incremento em vez de um salto) e depois
        // trocam-se aos pares. Com dados ao acaso o salto de uma particao normal falha em metade das comparacoes. Le sempre
        // dentro de [l, r) por construcao (os blocos tem tamanho fixo), por isso um comparador que nao e uma ordem fraca
        // estrita (<=) nao faz ler fora do intervalo nem precisa de guardas.
                constexpr unsigned kSortBlock = 64;

        template <typename T>
        struct use_block_partition : integral_constant<bool, is_trivially_copyable_t<T>::value && sizeof(T) <= 32>
        {
        };

        // goes_left(x, pivot): x fica a esquerda. Particiona [l, r) e devolve o ponto de corte: [l, p) vai para a esquerda.
        template <typename T, typename Pred>
        inline T *block_partition(T *l, T *r, const T &pivot, Pred goes_left)
        {
            unsigned char offl[kSortBlock];
            unsigned char offr[kSortBlock];
            unsigned nl = 0, nr = 0, sl = 0, sr = 0;
            while (static_cast<std::size_t>(r - l) > 2 * kSortBlock)
            {
                if (nl == 0)
                {
                    sl = 0;
                    for (unsigned i = 0; i < kSortBlock; i += 4)
                    {
                        offl[nl] = static_cast<unsigned char>(i);
                        nl += !goes_left(l[i], pivot);
                        offl[nl] = static_cast<unsigned char>(i + 1);
                        nl += !goes_left(l[i + 1], pivot);
                        offl[nl] = static_cast<unsigned char>(i + 2);
                        nl += !goes_left(l[i + 2], pivot);
                        offl[nl] = static_cast<unsigned char>(i + 3);
                        nl += !goes_left(l[i + 3], pivot);
                    }
                }
                if (nr == 0)
                {
                    sr = 0;
                    for (unsigned i = 0; i < kSortBlock; i += 4)
                    {
                        offr[nr] = static_cast<unsigned char>(i + 1);
                        nr += goes_left(*(r - 1 - i), pivot);
                        offr[nr] = static_cast<unsigned char>(i + 2);
                        nr += goes_left(*(r - 2 - i), pivot);
                        offr[nr] = static_cast<unsigned char>(i + 3);
                        nr += goes_left(*(r - 3 - i), pivot);
                        offr[nr] = static_cast<unsigned char>(i + 4);
                        nr += goes_left(*(r - 4 - i), pivot);
                    }
                }
                const unsigned m = nl < nr ? nl : nr;
                for (unsigned j = 0; j < m; ++j)
                    swap_vals(l[offl[sl + j]], *(r - offr[sr + j]));
                nl -= m;
                nr -= m;
                sl += m;
                sr += m;
                if (nl == 0)
                    l += kSortBlock;
                if (nr == 0)
                    r -= kSortBlock;
            }
            // O que sobra: no maximo um bloco pendente (de um dos lados) mais os elementos ainda por ver, que formam o bloco
            // do outro lado (ou dois blocos, metade cada, se nao ha pendente).
            const std::size_t unknown = static_cast<std::size_t>(r - l) - ((nl || nr) ? kSortBlock : 0);
            std::size_t lsize, rsize;
            if (nr)
            {
                lsize = unknown;
                rsize = kSortBlock;
            }
            else if (nl)
            {
                lsize = kSortBlock;
                rsize = unknown;
            }
            else
            {
                lsize = unknown / 2;
                rsize = unknown - lsize;
            }
            if (unknown && !nl)
            {
                sl = 0;
                for (std::size_t i = 0; i < lsize; ++i)
                {
                    offl[nl] = static_cast<unsigned char>(i);
                    nl += !goes_left(l[i], pivot);
                }
            }
            if (unknown && !nr)
            {
                sr = 0;
                for (std::size_t i = 0; i < rsize; ++i)
                {
                    offr[nr] = static_cast<unsigned char>(i + 1);
                    nr += goes_left(*(r - 1 - i), pivot);
                }
            }
            const unsigned m = nl < nr ? nl : nr;
            for (unsigned j = 0; j < m; ++j)
                swap_vals(l[offl[sl + j]], *(r - offr[sr + j]));
            nl -= m;
            nr -= m;
            sl += m;
            sr += m;
            if (nl == 0)
                l += lsize;
            if (nr == 0)
                r -= rsize;
            // [l, r) ja esta identificado: o que falta e levar os pendentes de um lado para a ponta certa
            if (nl)
            {
                const unsigned char *o = offl + sl;
                while (nl--)
                    swap_vals(l[o[nl]], *--r);
                l = r;
            }
            if (nr)
            {
                const unsigned char *o = offr + sr;
                while (nr--)
                {
                    swap_vals(*(r - o[nr]), *l);
                    ++l;
                }
                r = l;
            }
            return l;
        }

        template <typename C>
        struct LessThanPivot
        {
            C cmp;
            template <typename T>
            bool operator()(const T &x, const T &pivot) const { return cmp(x, pivot); }
        };
        template <typename C>
        struct LessEqualPivot
        {
            C cmp;
            template <typename T>
            bool operator()(const T &x, const T &pivot) const { return !cmp(pivot, x); }
        };

        // Particao de Hoare com o pivo (a mediana de tres) em *lo: as duas pontas param nos iguais ao pivo, por isso
        // uma entrada com muitos repetidos parte-se ao meio em vez de ficar toda de um lado (o que gastava a profundidade
        // e caia no heapsort), e uma entrada ordenada ao contrario nao troca quase nada. As guardas lo e hi custam um
        // teste e mantem a leitura dentro do intervalo mesmo com um comparador que nao e uma ordem fraca estrita (<=).
        template <typename T, typename C>
        inline void intro_rec_hoare(T *lo, T *hi, int depth, C cmp)
        {
            while (static_cast<std::size_t>(hi - lo) > kSmallSort)
            {
                if (depth-- == 0)
                {
                    heap_sort(lo, hi, cmp);
                    return;
                }
                choose_pivot(lo, hi, cmp);
                T *left = lo + 1;
                T *right = hi;
                for (;;)
                {
                    while (left < hi && cmp(*left, *lo))
                        ++left;
                    --right;
                    while (right > lo && cmp(*lo, *right))
                        --right;
                    if (!(left < right))
                        break;
                    swap_vals(*left, *right);
                    ++left;
                }
                intro_rec_hoare(left, hi, depth, cmp);
                hi = left;
            }
            insertion_sort(lo, hi, cmp);
        }

        // Tipos pequenos e trivialmente copiaveis: particao por blocos. `leftmost` diz se [lo, hi) comeca no inicio do
        // intervalo todo; se nao, *(lo - 1) e o pivo de um nivel acima (ou um elemento anterior), e se o pivo novo nao e
        // maior que ele (ha muitos repetidos) os iguais ao pivo vao todos para a esquerda de uma vez e ficam prontos, em
        // vez de o pivo ir sempre para o mesmo lado.
        template <typename T, typename C>
        inline void intro_rec_blocks(T *lo, T *hi, int depth, C cmp, bool leftmost)
        {
            while (static_cast<std::size_t>(hi - lo) > kSmallSort)
            {
                if (depth-- == 0)
                {
                    heap_sort(lo, hi, cmp);
                    return;
                }
                choose_pivot(lo, hi, cmp);
                const T pivot = *lo;
                if (!leftmost && !cmp(*(lo - 1), pivot))
                {
                    lo = block_partition(lo + 1, hi, pivot, LessEqualPivot<C>{cmp});
                    leftmost = false;
                    continue;
                }
                T *p = block_partition(lo + 1, hi, pivot, LessThanPivot<C>{cmp});
                swap_vals(*lo, *(p - 1));
                intro_rec_blocks(p, hi, depth, cmp, false);
                hi = p - 1;
            }
            insertion_sort(lo, hi, cmp);
        }

        template <typename T, typename C>
        inline void intro_rec(T *lo, T *hi, int depth, C cmp, true_type)
        {
            intro_rec_blocks(lo, hi, depth, cmp, true);
        }
        template <typename T, typename C>
        inline void intro_rec(T *lo, T *hi, int depth, C cmp, false_type)
        {
            intro_rec_hoare(lo, hi, depth, cmp);
        }

        inline int log2_floor(std::size_t n)
        {
            int r = 0;
            while (n >>= 1)
                ++r;
            return r;
        }

        // Uma entrada ja ordenada (ou ordenada ao contrario, estritamente) e muito comum e fica em n comparacoes: a
        // primeira passagem para no primeiro par fora de ordem, por isso numa entrada qualquer custa duas ou tres.
        // Devolve true se o intervalo ficou ordenado (estava, ou foi invertido: so se for estritamente decrescente, por
        // isso e estavel).
        template <typename T, typename C>
        inline bool presorted(T *lo, T *hi, C cmp)
        {
            T *p = lo + 1;
            while (p < hi && !cmp(*p, *(p - 1)))
                ++p;
            if (p == hi)
                return true;
            if (p == lo + 1)
            {
                T *q = lo + 2;
                while (q < hi && cmp(*q, *(q - 1)))
                    ++q;
                if (q == hi)
                {
                    for (T *a = lo, *b = hi - 1; a < b; ++a, --b)
                        swap_vals(*a, *b);
                    return true;
                }
            }
            return false;
        }

        template <typename T, typename C>
        inline void intro_sort(T *lo, T *hi, C cmp)
        {
            const std::size_t n = static_cast<std::size_t>(hi - lo);
            if (n < 2)
                return;
            if (n > kSmallSort && presorted(lo, hi, cmp))
                return;
            intro_rec(lo, hi, 2 * log2_floor(n), cmp, use_block_partition<T>());
        }

        template <typename T>
        inline bool is_sorted_fast(const T *lo, const T *hi)
        {
            for (const T *p = lo + 1; p < hi; ++p)
                if (*p < *(p - 1))
                    return false;
            return true;
        }

        template <typename T, typename U, typename KeyFn>
        inline void radix_sort(T *a, std::size_t n, KeyFn key_of)
        {
            std::size_t bytes = 0;
            if (!checked_mul(n, sizeof(T), bytes))
                fatal("ct::sort: tamanho invalido");
            HeapAlloc allocator;
            T *buf = static_cast<T *>(allocator.allocate(bytes, alignof(T)));
            T *src = a;
            T *dst = buf;
            constexpr int passes = static_cast<int>(sizeof(U));
            std::size_t count[256];
            for (int pass = 0; pass < passes; ++pass)
            {
                int shift = pass * 8;
                std::memset(count, 0, sizeof(count));
                for (std::size_t i = 0; i < n; ++i)
                    ++count[(key_of(src[i]) >> shift) & 0xFF];

                if (count[(key_of(src[0]) >> shift) & 0xFF] == n)
                    continue;
                std::size_t pos = 0;
                for (int b = 0; b < 256; ++b)
                {
                    std::size_t c = count[b];
                    count[b] = pos;
                    pos += c;
                }
                for (std::size_t i = 0; i < n; ++i)
                    dst[count[(key_of(src[i]) >> shift) & 0xFF]++] = src[i];
                T *t = src;
                src = dst;
                dst = t;
            }
            if (src != a)
                std::memcpy(a, src, bytes);
            allocator.deallocate(buf, bytes);
        }

        template <typename T, typename C>
        inline void merge_forward(T *lo, T *mid, T *hi, T *buf, C cmp)
        {
            std::size_t nl = static_cast<std::size_t>(mid - lo);
            for (std::size_t i = 0; i < nl; ++i)
                new (buf + i) T(detail::move(lo[i]));
            T *l = buf;
            T *le = buf + nl;
            T *r = mid;
            T *out = lo;
            while (l < le && r < hi)
            {
                if (cmp(*r, *l))
                    *out++ = detail::move(*r++);
                else
                    *out++ = detail::move(*l++);
            }
            while (l < le)
                *out++ = detail::move(*l++);
            for (std::size_t i = 0; i < nl; ++i)
                buf[i].~T();
        }

        template <typename T, typename C>
        inline void merge_backward(T *lo, T *mid, T *hi, T *buf, C cmp)
        {
            std::size_t nr = static_cast<std::size_t>(hi - mid);
            for (std::size_t i = 0; i < nr; ++i)
                new (buf + i) T(detail::move(mid[i]));
            T *l = mid;
            T *r = buf + nr;
            T *out = hi;
            while (l > lo && r > buf)
            {
                if (cmp(*(r - 1), *(l - 1)))
                    *--out = detail::move(*--l);
                else
                    *--out = detail::move(*--r);
            }
            while (r > buf)
                *--out = detail::move(*--r);
            for (std::size_t i = 0; i < nr; ++i)
                buf[i].~T();
        }

        // Tipos pequenos e trivialmente copiaveis: a fusao passa de um vetor para o outro (um buffer do tamanho do
        // intervalo) em cada nivel, sem copiar primeiro metade para o buffer, e escolhe o menor dos dois elementos sem salto
        // (os dados ao acaso falham metade dos saltos de uma fusao normal). Uma vez por nivel cada elemento e escrito uma
        // vez; no fim, se o resultado ficou no buffer, copia-se de volta.
        template <typename T, typename C>
        inline void merge_to(const T *l, const T *le, const T *r, const T *re, T *out, C cmp)
        {
            // Os primeiros 32 elementos saem com saltos e contam-se as mudancas de lado: dados ao acaso mudam em metade
            // (um salto que falha metade das vezes: passa-se a escolher sem salto), repetidos e inversoes saem em blocos
            // (o salto e previsivel e a escolha sem salto, que depende da carga anterior, seria mais lenta).
            unsigned changes = 0;
            bool prev = false;
            for (unsigned k = 0; k < 32 && l < le && r < re; ++k)
            {
                const bool take_right = cmp(*r, *l); // so o da direita se for estritamente menor: estavel
                if (take_right)
                    *out++ = *r++;
                else
                    *out++ = *l++;
                changes += take_right != prev ? 1u : 0u;
                prev = take_right;
            }
            if (changes > 8)
            {
                while (l < le && r < re)
                {
                    const bool take_right = cmp(*r, *l);
                    const T *from = take_right ? r : l; // escolher o ponteiro (um cmov) e carregar uma vez
                    *out++ = *from;
                    r += static_cast<std::size_t>(take_right);
                    l += static_cast<std::size_t>(!take_right);
                }
            }
            else
            {
                while (l < le && r < re)
                {
                    if (cmp(*r, *l))
                        *out++ = *r++;
                    else
                        *out++ = *l++;
                }
            }
            if (l < le)
                std::memcpy(static_cast<void *>(out), static_cast<const void *>(l), static_cast<std::size_t>(le - l) * sizeof(T));
            else if (r < re)
                std::memcpy(static_cast<void *>(out), static_cast<const void *>(r), static_cast<std::size_t>(re - r) * sizeof(T));
        }

        template <typename T, typename C>
        inline void stable_sort_pingpong(T *lo, T *hi, C cmp)
        {
            const std::size_t n = static_cast<std::size_t>(hi - lo);
            for (T *p = lo; p < hi; p += kSmallSort)
                insertion_sort(p, (hi - p > static_cast<std::ptrdiff_t>(kSmallSort)) ? p + kSmallSort : hi, cmp);
            std::size_t bytes = 0;
            if (!checked_mul(n, sizeof(T), bytes))
                fatal("ct::stable_sort: tamanho invalido");
            HeapAlloc allocator;
            T *buf = static_cast<T *>(allocator.allocate(bytes, alignof(T)));
            T *src = lo;
            T *dst = buf;
            for (std::size_t width = kSmallSort; width < n; width *= 2)
            {
                for (std::size_t i = 0; i < n; i += 2 * width)
                {
                    const std::size_t mid = i + width < n ? i + width : n;
                    const std::size_t end = i + 2 * width < n ? i + 2 * width : n;
                    if (mid >= end || !cmp(src[mid], src[mid - 1])) // os dois pedacos ja estao em ordem: so copiar
                        std::memcpy(static_cast<void *>(dst + i), static_cast<const void *>(src + i), (end - i) * sizeof(T));
                    else
                        merge_to(src + i, src + mid, src + mid, src + end, dst + i, cmp);
                }
                T *t = src;
                src = dst;
                dst = t;
            }
            if (src != lo)
                std::memcpy(static_cast<void *>(lo), static_cast<const void *>(src), bytes);
            allocator.deallocate(buf, bytes);
        }

        template <typename T, typename C>
        inline void stable_sort_impl(T *lo, T *hi, C cmp)
        {
            std::size_t n = static_cast<std::size_t>(hi - lo);
            if (n <= kSmallSort)
            {
                insertion_sort(lo, hi, cmp);
                return;
            }
            if (presorted(lo, hi, cmp))
                return;
            if (use_block_partition<T>::value)
            {
                stable_sort_pingpong(lo, hi, cmp);
                return;
            }
            for (T *p = lo; p < hi; p += kSmallSort)
                insertion_sort(p, (hi - p > static_cast<std::ptrdiff_t>(kSmallSort)) ? p + kSmallSort : hi, cmp);

            std::size_t half = n / 2 + 1;
            std::size_t bytes = 0;
            if (!checked_mul(half, sizeof(T), bytes))
                fatal("ct::stable_sort: tamanho invalido");
            HeapAlloc allocator;
            T *buf = static_cast<T *>(allocator.allocate(bytes, alignof(T)));
            for (std::size_t width = kSmallSort; width < n; width *= 2)
            {
                for (T *p = lo; hi - p > static_cast<std::ptrdiff_t>(width); p += 2 * width)
                {
                    T *mid = p + width;
                    T *end = (hi - p > static_cast<std::ptrdiff_t>(2 * width)) ? p + 2 * width : hi;
                    if (mid - p <= end - mid)
                        merge_forward(p, mid, end, buf, cmp);
                    else
                        merge_backward(p, mid, end, buf, cmp);
                }
            }
            allocator.deallocate(buf, bytes);
        }

        // Ponteiros: os elementos sao contiguos e ordena-se no sitio. Qualquer outro iterador (o do Deque, que da a
        // volta ao buffer, os inversos, os dos outros) nao garante que &*first + n seja o fim, por isso passa para
        // um buffer contiguo, ordena-se la e volta para o sitio, a menos que o proprio iterador diga que o intervalo
        // e contiguo (try_contiguous).
        template <typename It, typename C, typename Sorter>
        inline void sort_iter(It first, It last, C cmp, Sorter sorter, true_type)
        {
            if (first == last)
                return;
            sorter(&*first, &*first + (last - first), cmp);
        }

        // Um iterador que sabe dizer se o intervalo e contiguo na memoria (o do Deque: contiguous_range) deixa
        // ordenar no sitio quando e; os outros dizem que nao.
        template <typename It, typename P>
        inline auto try_contiguous(const It &first, const It &last, P *&lo, P *&hi, int)
            -> decltype(first.contiguous_range(last, lo, hi))
        {
            return first.contiguous_range(last, lo, hi);
        }

        template <typename It, typename P>
        inline bool try_contiguous(const It &, const It &, P *&, P *&, long)
        {
            return false;
        }

        template <typename It, typename C, typename Sorter>
        inline void sort_iter(It first, It last, C cmp, Sorter sorter, false_type)
        {
            using T = typename remove_ref<decltype(*first)>::type;
            const std::size_t n = static_cast<std::size_t>(last - first);
            if (n < 2)
                return;
            T *range_lo = nullptr;
            T *range_hi = nullptr;
            if (try_contiguous(first, last, range_lo, range_hi, 0))
            {
                sorter(range_lo, range_hi, cmp);
                return;
            }
            std::size_t bytes = 0;
            if (!checked_mul(n, sizeof(T), bytes))
                fatal("ct::sort: tamanho invalido");
            alignas(T) unsigned char small[512]; // os intervalos pequenos nao pagam um malloc
            HeapAlloc allocator;
            T *buf = bytes <= sizeof(small) ? reinterpret_cast<T *>(small)
                                            : static_cast<T *>(allocator.allocate(bytes, alignof(T)));
            It it = first;
            for (std::size_t i = 0; i < n; ++i, ++it)
                ::new (static_cast<void *>(buf + i)) T(detail::move(*it));
            sorter(buf, buf + n, cmp);
            it = first;
            for (std::size_t i = 0; i < n; ++i, ++it)
            {
                *it = detail::move(buf[i]);
                buf[i].~T();
            }
            if (bytes > sizeof(small))
                allocator.deallocate(buf, bytes);
        }

        struct IntroSorter
        {
            template <typename T, typename C>
            void operator()(T *lo, T *hi, C cmp) const { intro_sort(lo, hi, cmp); }
        };

        struct StableSorter
        {
            template <typename T, typename C>
            void operator()(T *lo, T *hi, C cmp) const { stable_sort_impl(lo, hi, cmp); }
        };

    } 

    template <typename It, typename C>
    inline void sort(It first, It last, C cmp)
    {
        detail::sort_iter(first, last, cmp, detail::IntroSorter(),
                          detail::integral_constant<bool, detail::is_pointer<It>::value>());
    }

    template <typename It>
    inline void sort(It first, It last)
    {
        using T = typename detail::remove_ref<decltype(*first)>::type;
        detail::sort_iter(first, last, [](const T &a, const T &b) { return a < b; }, detail::IntroSorter(),
                          detail::integral_constant<bool, detail::is_pointer<It>::value>());
    }

    template <typename It, typename C>
    inline void stable_sort(It first, It last, C cmp)
    {
        detail::sort_iter(first, last, cmp, detail::StableSorter(),
                          detail::integral_constant<bool, detail::is_pointer<It>::value>());
    }

    template <typename It>
    inline void stable_sort(It first, It last)
    {
        using T = typename detail::remove_ref<decltype(*first)>::type;
        detail::sort_iter(first, last, [](const T &a, const T &b) { return a < b; }, detail::StableSorter(),
                          detail::integral_constant<bool, detail::is_pointer<It>::value>());
    }

#define CT_RADIX_SORT(T, U, KEY_EXPR)                                          \
    inline void sort(T *first, T *last)                                        \
    {                                                                          \
        std::size_t n = static_cast<std::size_t>(last - first);                \
        if (n < detail::kRadixMin)                                             \
        {                                                                      \
            detail::intro_sort(first, last,                                    \
                               [](T a, T b) { return a < b; });                \
            return;                                                            \
        }                                                                      \
        if (detail::is_sorted_fast(first, last))                               \
            return;                                                            \
        detail::radix_sort<T, U>(first, n, [](T x) -> U { return KEY_EXPR; }); \
    }

    CT_RADIX_SORT(unsigned char, std::uint8_t, static_cast<std::uint8_t>(x))
    CT_RADIX_SORT(unsigned short, std::uint16_t, static_cast<std::uint16_t>(x))
    CT_RADIX_SORT(short, std::uint16_t,
                  static_cast<std::uint16_t>(x) ^ std::uint16_t(0x8000))
    CT_RADIX_SORT(unsigned, std::uint32_t, static_cast<std::uint32_t>(x))
    CT_RADIX_SORT(int, std::uint32_t,
                  static_cast<std::uint32_t>(x) ^ 0x80000000u)
    CT_RADIX_SORT(unsigned long long, std::uint64_t, static_cast<std::uint64_t>(x))
    CT_RADIX_SORT(long long, std::uint64_t,
                  static_cast<std::uint64_t>(x) ^ (std::uint64_t(1) << 63))

    CT_RADIX_SORT(unsigned long, std::uint64_t, static_cast<std::uint64_t>(x))
    CT_RADIX_SORT(long, std::uint64_t,
                  static_cast<std::uint64_t>(x) ^ (std::uint64_t(1) << 63))

    inline void sort(float *first, float *last)
    {
        std::size_t n = static_cast<std::size_t>(last - first);
        if (n < detail::kRadixMin)
        {
            detail::intro_sort(first, last, [](float a, float b) { return a < b; });
            return;
        }
        if (detail::is_sorted_fast(first, last))
            return;
        detail::radix_sort<float, std::uint32_t>(first, n, [](float f) -> std::uint32_t {
            std::uint32_t b;
            std::memcpy(&b, &f, 4);
            return (b & 0x80000000u) ? ~b : (b | 0x80000000u);
        });
    }

    inline void sort(double *first, double *last)
    {
        std::size_t n = static_cast<std::size_t>(last - first);
        if (n < detail::kRadixMin)
        {
            detail::intro_sort(first, last, [](double a, double b) { return a < b; });
            return;
        }
        if (detail::is_sorted_fast(first, last))
            return;
        detail::radix_sort<double, std::uint64_t>(first, n, [](double f) -> std::uint64_t {
            std::uint64_t b;
            std::memcpy(&b, &f, 8);
            return (b & (std::uint64_t(1) << 63)) ? ~b : (b | (std::uint64_t(1) << 63));
        });
    }

#undef CT_RADIX_SORT

} 