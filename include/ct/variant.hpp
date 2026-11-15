#pragma once

#include "detail/utils.hpp"

namespace ct
{
    namespace detail
    {
        constexpr std::size_t variant_max(std::size_t a, std::size_t b) { return a > b ? a : b; }

        template <bool B, typename T, typename F>
        struct conditional
        {
            using type = F;
        };
        template <typename T, typename F>
        struct conditional<true, T, F>
        {
            using type = T;
        };

        template <typename... Ts>
        struct VariantMaxSize; 
        template <typename T>
        struct VariantMaxSize<T>
        {
            static constexpr std::size_t value = sizeof(T);
        };
        template <typename T, typename... Rest>
        struct VariantMaxSize<T, Rest...>
        {
            static constexpr std::size_t value = variant_max(sizeof(T), VariantMaxSize<Rest...>::value);
        };

        template <typename... Ts>
        struct VariantMaxAlign; 
        template <typename T>
        struct VariantMaxAlign<T>
        {
            static constexpr std::size_t value = alignof(T);
        };
        template <typename T, typename... Rest>
        struct VariantMaxAlign<T, Rest...>
        {
            static constexpr std::size_t value = variant_max(alignof(T), VariantMaxAlign<Rest...>::value);
        };

        template <typename T, typename... Ts>
        struct VariantIndexOf;
        template <typename T, typename... Rest>
        struct VariantIndexOf<T, T, Rest...>
        {
            static constexpr std::size_t value = 0;
        };
        template <typename T, typename U, typename... Rest>
        struct VariantIndexOf<T, U, Rest...>
        {
            static constexpr std::size_t value = 1 + VariantIndexOf<T, Rest...>::value;
        };

        template <std::size_t I, typename... Ts>
        struct VariantTypeAt;
        template <typename T, typename... Rest>
        struct VariantTypeAt<0, T, Rest...>
        {
            using type = T;
        };
        template <std::size_t I, typename T, typename... Rest>
        struct VariantTypeAt<I, T, Rest...>
        {
            using type = typename VariantTypeAt<I - 1, Rest...>::type;
        };

        // T sabe ser atribuida (por copia ou por move) a partir de outra T? Tipos com membros const ou com operator=
        // apagado, e os que so se constroem, dizem que nao, e o Variant destroi e reconstroi em vez de atribuir.
        template <typename T>
        struct variant_copy_assignable
        {
            template <typename U>
            static auto test(int) -> decltype((void)(declval<U &>() = declval<const U &>()), true_type());
            template <typename U>
            static false_type test(...);
            static constexpr bool value = decltype(test<T>(0))::value;
        };
        template <typename T>
        struct variant_move_assignable
        {
            template <typename U>
            static auto test(int) -> decltype((void)(declval<U &>() = declval<U &&>()), true_type());
            template <typename U>
            static false_type test(...);
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        inline bool variant_assign_copy(void *dst, const void *src, true_type)
        {
            *static_cast<T *>(dst) = *static_cast<const T *>(src);
            return true;
        }
        template <typename T>
        inline bool variant_assign_copy(void *, const void *, false_type)
        {
            return false;
        }
        template <typename T>
        inline bool variant_assign_move(void *dst, void *src, true_type)
        {
            *static_cast<T *>(dst) = move(*static_cast<T *>(src));
            return true;
        }
        template <typename T>
        inline bool variant_assign_move(void *, void *, false_type)
        {
            return false;
        }

        // Bit i a 1 se a alternativa i tem destrutor (nao e trivial): so essas podem ser donas de memoria onde viva
        // a origem de uma atribuicao. Com mais de 64 alternativas as de indice 64 em diante contam sempre como donas.
        template <typename... Ts>
        struct VariantOwnsMask
        {
            static constexpr unsigned long long value = 0;
        };
        template <typename T, typename... Rest>
        struct VariantOwnsMask<T, Rest...>
        {
            static constexpr unsigned long long value =
                (is_trivially_destructible_t<T>::value ? 0ull : 1ull) | (VariantOwnsMask<Rest...>::value << 1);
        };
        template <typename... Ts>
        inline bool variant_owns(std::size_t idx) noexcept
        {
            return sizeof...(Ts) > 64 ||
                   (VariantOwnsMask<Ts...>::value != 0 &&
                    (idx >= 64 || ((VariantOwnsMask<Ts...>::value >> idx) & 1ull) != 0));
        }

        template <typename... Ts>
        struct VariantOps
        {
            static void destroy(std::size_t, void *) { fatal("ct::Variant: indice invalido"); }
            static void copy_construct(std::size_t, void *, const void *)
            {
                fatal("ct::Variant: indice invalido");
            }
            static void move_construct(std::size_t, void *, void *)
            {
                fatal("ct::Variant: indice invalido");
            }
            static bool copy_assign(std::size_t, void *, const void *)
            {
                fatal("ct::Variant: indice invalido");
            }
            static bool move_assign(std::size_t, void *, void *)
            {
                fatal("ct::Variant: indice invalido");
            }
            template <typename Full>
            static void replace_by_copy(std::size_t, std::size_t, void *, const void *)
            {
                fatal("ct::Variant: indice invalido");
            }
            template <typename Full>
            static void replace_by_move(std::size_t, std::size_t, void *, void *)
            {
                fatal("ct::Variant: indice invalido");
            }
        };
        template <typename T, typename... Rest>
        struct VariantOps<T, Rest...>
        {
            static void destroy(std::size_t idx, void *p)
            {
                if (idx == 0)
                    static_cast<T *>(p)->~T();
                else
                    VariantOps<Rest...>::destroy(idx - 1, p);
            }
            static void copy_construct(std::size_t idx, void *dst, const void *src)
            {
                if (idx == 0)
                    ::new (dst) T(*static_cast<const T *>(src));
                else
                    VariantOps<Rest...>::copy_construct(idx - 1, dst, src);
            }
            static void move_construct(std::size_t idx, void *dst, void *src)
            {
                if (idx == 0)
                    ::new (dst) T(move(*static_cast<T *>(src)));
                else
                    VariantOps<Rest...>::move_construct(idx - 1, dst, src);
            }
            // Os dois estao na alternativa idx: atribui no sitio, com o operator= da alternativa, e diz que o fez.
            // Devolve false (sem tocar em nada) se a alternativa nao sabe ser atribuida.
            static bool copy_assign(std::size_t idx, void *dst, const void *src)
            {
                if (idx == 0)
                    return variant_assign_copy<T>(dst, src,
                                                  integral_constant<bool, variant_copy_assignable<T>::value>());
                return VariantOps<Rest...>::copy_assign(idx - 1, dst, src);
            }
            static bool move_assign(std::size_t idx, void *dst, void *src)
            {
                if (idx == 0)
                    return variant_assign_move<T>(dst, src,
                                                  integral_constant<bool, variant_move_assignable<T>::value>());
                return VariantOps<Rest...>::move_assign(idx - 1, dst, src);
            }
            // Troca a alternativa cur que esta em dst pela alternativa idx de src, passando por um T local: a origem
            // copia-se (ou move-se) antes de a actual ser destruida, porque pode viver dentro dela. Full e o
            // VariantOps da lista toda, que sabe destruir qualquer indice (este so conhece o fim da lista).
            template <typename Full>
            static void replace_by_copy(std::size_t idx, std::size_t cur, void *dst, const void *src)
            {
                if (idx == 0)
                {
                    T local(*static_cast<const T *>(src));
                    Full::destroy(cur, dst);
                    ::new (dst) T(move(local));
                }
                else
                    VariantOps<Rest...>::template replace_by_copy<Full>(idx - 1, cur, dst, src);
            }
            template <typename Full>
            static void replace_by_move(std::size_t idx, std::size_t cur, void *dst, void *src)
            {
                if (idx == 0)
                {
                    T local(move(*static_cast<T *>(src)));
                    Full::destroy(cur, dst);
                    ::new (dst) T(move(local));
                }
                else
                    VariantOps<Rest...>::template replace_by_move<Full>(idx - 1, cur, dst, src);
            }
        };

        template <typename F, typename... Ts>
        struct VariantVisit
        {
            template <typename ReturnT, typename V>
            static ReturnT apply(std::size_t, V *, F &)
            {
                fatal("ct::Variant: indice invalido em visit");
            }
        };
        template <typename F, typename T, typename... Rest>
        struct VariantVisit<F, T, Rest...>
        {
            template <typename ReturnT, typename V>
            static ReturnT apply(std::size_t idx, V *p, F &f)
            {
                if (idx == 0)
                    return f(*static_cast<
                              typename detail::conditional<is_const<V>::value, const T, T>::type *>(p));
                return VariantVisit<F, Rest...>::template apply<ReturnT>(idx - 1, p, f);
            }
        };
    } 

    template <typename... Ts>
    class Variant
    {
    public:
        static_assert(sizeof...(Ts) > 0, "ct::Variant precisa de pelo menos um tipo");

        Variant() : index_(0) { ::new (static_cast<void *>(storage_)) First(); }

        template <typename T,
                  typename detail::enable_if<
                      !detail::is_same<typename std::decay<T>::type, Variant>::value, int>::type = 0>
        Variant(T value) : index_(detail::VariantIndexOf<typename std::decay<T>::type, Ts...>::value)
        {
            using D = typename std::decay<T>::type;
            ::new (static_cast<void *>(storage_)) D(detail::move(value));
        }

        Variant(const Variant &o) : index_(o.index_)
        {
            detail::VariantOps<Ts...>::copy_construct(index_, storage_, o.storage_);
        }
        Variant(Variant &&o) noexcept : index_(o.index_)
        {
            detail::VariantOps<Ts...>::move_construct(index_, storage_, o.storage_);
        }

        // A origem da atribuicao (o) pode viver dentro do que este Variant guarda: o cursor de uma lista,
        // cursor = cursor.get<Rc<No>>()->next, onde next e um membro do no que o cursor segura. Por isso:
        //  - com o mesmo indice e uma alternativa atribuivel, atribui-se no sitio com o operator= da alternativa
        //    (como o variant da biblioteca standard), e a seguranca fica nesse operator= (o do Rc e o do Unique
        //    tiram a origem antes de largar o que tinham);
        //  - nos outros casos, se a alternativa actual tem destrutor (pode ser dona da origem) a origem passa
        //    primeiro para um objecto local, a alternativa actual destroi-se, e a nova constroi-se por move a partir
        //    do local: custa um move e uma destruicao a mais. Sem destrutor nao pode ser dona de nada e destroi-se e
        //    constroi-se directamente. (O variant da biblioteca standard destroi antes de copiar quando a copia nao
        //    lanca, e ai tem o mesmo uso depois de libertar com o indice a mudar.)
        // Os dois operadores sao CT_FORCEINLINE: com os ramos a mais o compilador deixava de os meter inline em
        // alguns sitios (a atribuicao de escalares ficava 2 a 3 vezes mais lenta), e inline e como sempre foram.
        CT_FORCEINLINE Variant &operator=(const Variant &o)
        {
            if (this == &o)
                return *this;
            const std::size_t from = index_;
            const std::size_t to = o.index_;
            if (from == to || detail::variant_owns<Ts...>(from))
            {
                if (from == to && detail::VariantOps<Ts...>::copy_assign(from, storage_, o.storage_))
                    return *this;
                if (detail::variant_owns<Ts...>(from))
                {
                    // o pode ja nao existir depois da troca (vivia na alternativa que se destruiu)
                    detail::VariantOps<Ts...>::template replace_by_copy<detail::VariantOps<Ts...>>(
                        to, from, storage_, o.storage_);
                    index_ = to;
                    return *this;
                }
            }
            detail::VariantOps<Ts...>::destroy(from, storage_);
            detail::VariantOps<Ts...>::copy_construct(to, storage_, o.storage_);
            index_ = to;
            return *this;
        }
        CT_FORCEINLINE Variant &operator=(Variant &&o) noexcept
        {
            if (this == &o)
                return *this;
            const std::size_t from = index_;
            const std::size_t to = o.index_;
            if (from == to || detail::variant_owns<Ts...>(from))
            {
                if (from == to && detail::VariantOps<Ts...>::move_assign(from, storage_, o.storage_))
                    return *this;
                if (detail::variant_owns<Ts...>(from))
                {
                    // o pode ja nao existir depois da troca (vivia na alternativa que se destruiu)
                    detail::VariantOps<Ts...>::template replace_by_move<detail::VariantOps<Ts...>>(
                        to, from, storage_, o.storage_);
                    index_ = to;
                    return *this;
                }
            }
            detail::VariantOps<Ts...>::destroy(from, storage_);
            detail::VariantOps<Ts...>::move_construct(to, storage_, o.storage_);
            index_ = to;
            return *this;
        }

        template <typename T,
                  typename detail::enable_if<
                      !detail::is_same<typename std::decay<T>::type, Variant>::value, int>::type = 0>
        Variant &operator=(T value)
        {
            using D = typename std::decay<T>::type;
            constexpr std::size_t idx = detail::VariantIndexOf<D, Ts...>::value;
            detail::VariantOps<Ts...>::destroy(index_, storage_);
            ::new (static_cast<void *>(storage_)) D(detail::move(value));
            index_ = idx;
            return *this;
        }

        ~Variant() { detail::VariantOps<Ts...>::destroy(index_, storage_); }

        std::size_t index() const noexcept { return index_; }

        template <typename T>
        bool is() const noexcept
        {
            return index_ == detail::VariantIndexOf<T, Ts...>::value;
        }

        template <typename T>
        T &get()
        {
            if (!is<T>())
                detail::fatal("ct::Variant::get: tipo ativo diferente do pedido");
            return *reinterpret_cast<T *>(storage_);
        }
        template <typename T>
        const T &get() const
        {
            if (!is<T>())
                detail::fatal("ct::Variant::get: tipo ativo diferente do pedido");
            return *reinterpret_cast<const T *>(storage_);
        }

        template <typename T>
        T *get_if() noexcept
        {
            return is<T>() ? reinterpret_cast<T *>(storage_) : nullptr;
        }
        template <typename T>
        const T *get_if() const noexcept
        {
            return is<T>() ? reinterpret_cast<const T *>(storage_) : nullptr;
        }

        template <typename F>
        auto visit(F &&f)
            -> decltype(f(detail::declval<typename detail::VariantTypeAt<0, Ts...>::type &>()))
        {
            using ReturnT =
                decltype(f(detail::declval<typename detail::VariantTypeAt<0, Ts...>::type &>()));
            return detail::VariantVisit<F, Ts...>::template apply<ReturnT>(
                index_, static_cast<void *>(storage_), f);
        }
        template <typename F>
        auto visit(F &&f) const -> decltype(
            f(detail::declval<const typename detail::VariantTypeAt<0, Ts...>::type &>()))
        {
            using ReturnT = decltype(
                f(detail::declval<const typename detail::VariantTypeAt<0, Ts...>::type &>()));
            return detail::VariantVisit<F, Ts...>::template apply<ReturnT>(
                index_, static_cast<const void *>(storage_), f);
        }

        void swap(Variant &o) noexcept
        {
            if (this == &o)
                return;
            Variant tmp(detail::move(*this));
            *this = detail::move(o);
            o = detail::move(tmp);
        }

    private:
        using First = typename detail::VariantTypeAt<0, Ts...>::type;

        alignas(detail::VariantMaxAlign<Ts...>::value) unsigned char
            storage_[detail::VariantMaxSize<Ts...>::value];
        std::size_t index_;
    };

    template <typename... Ts>
    inline void swap(Variant<Ts...> &a, Variant<Ts...> &b) noexcept
    {
        a.swap(b);
    }

} 