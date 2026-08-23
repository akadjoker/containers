#pragma once

#include "detail/utils.hpp"

namespace ct
{

    // Array fixo, agregado puro: sizeof(Array<T,N>) == N*sizeof(T), sem ponteiros,
    // sem heap, sem overhead. Acesso constexpr (C++14: as versões não-const também
    // servem para escrever em contexto constante); fill e comparação despacham para
    // memset/memcmp quando isso é *seguro* bit a bit.
    template <typename T, std::size_t N>
    struct Array
    {
        // público de propósito — é o que mantém o agregado ({{1,2,3}} funciona)
        T elems[N];

        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T &;
        using const_reference = const T &;
        using pointer = T *;
        using const_pointer = const T *;
        using iterator = T *;
        using const_iterator = const T *;
        using reverse_iterator = detail::ReverseIt<T *>;
        using const_reverse_iterator = detail::ReverseIt<const T *>;

        // ---- acesso ---------------------------------------------------------

        constexpr T &operator[](size_type i) { return elems[i]; }
        constexpr const T &operator[](size_type i) const { return elems[i]; }

        constexpr T &at(size_type i)
        {
            if (CT_UNLIKELY(i >= N))
                detail::fatal("ct::Array::at: index fora dos limites");
            return elems[i];
        }
        constexpr const T &at(size_type i) const
        {
            if (CT_UNLIKELY(i >= N))
                detail::fatal("ct::Array::at: index fora dos limites");
            return elems[i];
        }

        constexpr T &front() { return elems[0]; }
        constexpr const T &front() const { return elems[0]; }
        constexpr T &back() { return elems[N - 1]; }
        constexpr const T &back() const { return elems[N - 1]; }

        constexpr T *data() noexcept { return elems; }
        constexpr const T *data() const noexcept { return elems; }

        // ---- iteradores -----------------------------------------------------

        constexpr iterator begin() noexcept { return elems; }
        constexpr const_iterator begin() const noexcept { return elems; }
        constexpr const_iterator cbegin() const noexcept { return elems; }
        constexpr iterator end() noexcept { return elems + N; }
        constexpr const_iterator end() const noexcept { return elems + N; }
        constexpr const_iterator cend() const noexcept { return elems + N; }

        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
        const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
        const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

        // ---- capacidade -----------------------------------------------------

        static constexpr size_type size() noexcept { return N; }
        static constexpr size_type max_size() noexcept { return N; }
        static constexpr bool empty() noexcept { return N == 0; }

        // ---- operações ------------------------------------------------------

        // memset para tipos de 1 byte, loop sem aliasing (valor em local) para os
        // outros triviais — ver detail::fill_fast_n
        void fill(const T &v) { detail::fill_fast_n(elems, N, v); }

        void swap(Array &o)
        {
            for (size_type i = 0; i < N; ++i)
                detail::swap_vals(elems[i], o.elems[i]);
        }
    };

    // ---- comparações ---------------------------------------------------------
    // == usa memcmp em inteiros/enums/ponteiros; floats e structs vão pelo loop
    // (com floats memcmp daria -0.0 != 0.0 e NaN == NaN)

    template <typename T, std::size_t N>
    inline bool operator==(const Array<T, N> &a, const Array<T, N> &b)
    {
        return detail::equal_n(a.elems, b.elems, N);
    }

    template <typename T, std::size_t N>
    inline bool operator!=(const Array<T, N> &a, const Array<T, N> &b)
    {
        return !(a == b);
    }

    template <typename T, std::size_t N>
    inline bool operator<(const Array<T, N> &a, const Array<T, N> &b)
    {
        return detail::lex_less_n(a.elems, N, b.elems, N);
    }

    template <typename T, std::size_t N>
    inline bool operator>(const Array<T, N> &a, const Array<T, N> &b)
    {
        return b < a;
    }

    template <typename T, std::size_t N>
    inline bool operator<=(const Array<T, N> &a, const Array<T, N> &b)
    {
        return !(b < a);
    }

    template <typename T, std::size_t N>
    inline bool operator>=(const Array<T, N> &a, const Array<T, N> &b)
    {
        return !(a < b);
    }

    template <typename T, std::size_t N>
    inline void swap(Array<T, N> &a, Array<T, N> &b)
    {
        a.swap(b);
    }

} // namespace ct
