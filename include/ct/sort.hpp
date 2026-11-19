
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

        // Poe em *result a mediana de *a, *b e *c.
        template <typename T, typename C>
        inline void move_median_to_first(T *result, T *a, T *b, T *c, C cmp)
        {
            if (cmp(*a, *b))
            {
                if (cmp(*b, *c))
                    swap_vals(*result, *b);
                else if (cmp(*a, *c))
                    swap_vals(*result, *c);
                else
                    swap_vals(*result, *a);
            }
            else if (cmp(*a, *c))
                swap_vals(*result, *a);
            else if (cmp(*b, *c))
                swap_vals(*result, *c);
            else
                swap_vals(*result, *b);
        }

        // Particao de Hoare com o pivo (a mediana de tres) em *lo: as duas pontas param nos iguais ao pivo, por isso
        // uma entrada com muitos repetidos parte-se ao meio em vez de ficar toda de um lado (o que gastava a profundidade
        // e caia no heapsort), e uma entrada ordenada ao contrario nao troca quase nada. As guardas lo e hi custam um
        // teste e mantem a leitura dentro do intervalo mesmo com um comparador que nao e uma ordem fraca estrita (<=).
        template <typename T, typename C>
        inline void intro_rec(T *lo, T *hi, int depth, C cmp)
        {
            while (static_cast<std::size_t>(hi - lo) > kSmallSort)
            {
                if (depth-- == 0)
                {
                    heap_sort(lo, hi, cmp);
                    return;
                }
                move_median_to_first(lo, lo + 1, lo + (hi - lo) / 2, hi - 1, cmp);
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
                intro_rec(left, hi, depth, cmp);
                hi = left;
            }
            insertion_sort(lo, hi, cmp);
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
        template <typename T, typename C>
        inline void intro_sort(T *lo, T *hi, C cmp)
        {
            const std::size_t n = static_cast<std::size_t>(hi - lo);
            if (n < 2)
                return;
            if (n > kSmallSort)
            {
                T *p = lo + 1;
                while (p < hi && !cmp(*p, *(p - 1)))
                    ++p;
                if (p == hi)
                    return;
                if (p == lo + 1)
                {
                    T *q = lo + 2;
                    while (q < hi && cmp(*q, *(q - 1)))
                        ++q;
                    if (q == hi)
                    {
                        for (T *a = lo, *b = hi - 1; a < b; ++a, --b)
                            swap_vals(*a, *b);
                        return;
                    }
                }
            }
            intro_rec(lo, hi, 2 * log2_floor(n), cmp);
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

        template <typename T, typename C>
        inline void stable_sort_impl(T *lo, T *hi, C cmp)
        {
            std::size_t n = static_cast<std::size_t>(hi - lo);
            if (n <= kSmallSort)
            {
                insertion_sort(lo, hi, cmp);
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