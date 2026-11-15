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

        // Atribuir no sitio so se distingue de destruir e construir num tipo que sabe ser atribuido e nao e trivialmente
        // copiavel: num tipo trivialmente copiavel o operator= e uma copia de bytes, igual a construir por cima, e o Variant
        // destroi e constroi como sempre fez (e o compilador ve o mesmo codigo de antes).
        template <typename T>
        struct variant_in_place
        {
            static constexpr bool copy = variant_copy_assignable<T>::value && !is_trivially_copyable_t<T>::value;
            static constexpr bool move = variant_move_assignable<T>::value && !is_trivially_copyable_t<T>::value &&
                                         !variant_move_rebuilds<T>::value;
        };
        // Uma alternativa so entra na cadeia de atribuicao (VariantOps::prepare_copy e prepare_move) se sair dela pede
        // algo: ter destrutor (que corre antes de construir a nova), poder ser dona da origem (o local) ou atribuir-se no
        // sitio. As outras (int, double, ponteiros) nao geram codigo nenhum, e a atribuicao entre elas e destruir e construir.
        template <typename T>
        struct variant_pre_copy
        {
            static constexpr bool value =
                !is_trivially_destructible_t<T>::value || !variant_leaf<T>::value || variant_in_place<T>::copy;
        };
        template <typename T>
        struct variant_pre_move
        {
            static constexpr bool value =
                !is_trivially_destructible_t<T>::value || !variant_leaf<T>::value || variant_in_place<T>::move;
        };

        // Nenhuma alternativa precisa de nada de especial ao ser atribuida (todas folha, nenhuma atribui no sitio): a
        // atribuicao e destruir a actual e construir a nova, como sempre foi, e escreve-se assim, sem a cadeia.
        template <typename... Ts>
        struct VariantPlainCopy
        {
            static constexpr bool value = true;
        };
        template <typename T, typename... Rest>
        struct VariantPlainCopy<T, Rest...>
        {
            static constexpr bool value =
                variant_leaf<T>::value && !variant_in_place<T>::copy && VariantPlainCopy<Rest...>::value;
        };
        template <typename... Ts>
        struct VariantPlainMove
        {
            static constexpr bool value = true;
        };
        template <typename T, typename... Rest>
        struct VariantPlainMove<T, Rest...>
        {
            static constexpr bool value =
                variant_leaf<T>::value && !variant_in_place<T>::move && VariantPlainMove<Rest...>::value;
        };

        template <typename T>
        CT_FORCEINLINE void variant_assign_copy(void *dst, const void *src, true_type)
        {
            *static_cast<T *>(dst) = *static_cast<const T *>(src);
        }
        template <typename T>
        CT_FORCEINLINE void variant_assign_copy(void *, const void *, false_type)
        {
        }
        template <typename T>
        CT_FORCEINLINE void variant_assign_move(void *dst, void *src, true_type)
        {
            *static_cast<T *>(dst) = move(*static_cast<T *>(src));
        }
        template <typename T>
        CT_FORCEINLINE void variant_assign_move(void *, void *, false_type)
        {
        }

        // A alternativa actual e a K (de tipo T) e o destino e to: ou atribui no sitio (true), ou passa a origem por um
        // local se T pode ser dona dela (true), ou destroi a actual (false: falta construir a nova).
        template <typename T, typename Full, std::size_t K>
        CT_FORCEINLINE bool variant_prepare_copy_one(std::size_t to, void *dst, const void *src)
        {
            if (variant_leaf<T>::value && (!variant_in_place<T>::copy || CT_LIKELY(to != K)))
            {
                static_cast<T *>(dst)->~T();
                return false;
            }
            if (variant_in_place<T>::copy && to == K)
            {
                variant_assign_copy<T>(dst, src, integral_constant<bool, variant_in_place<T>::copy>());
                return true;
            }
            Full::template replace_by_copy<Full>(to, K, dst, src);
            return true;
        }
        template <typename T, typename Full, std::size_t K>
        CT_FORCEINLINE bool variant_prepare_move_one(std::size_t to, void *dst, void *src)
        {
            if (variant_in_place<T>::move && to == K)
            {
                variant_assign_move<T>(dst, src, integral_constant<bool, variant_in_place<T>::move>());
                return true;
            }
            if (!variant_leaf<T>::value)
            {
                Full::template replace_by_move<Full>(to, K, dst, src);
                return true;
            }
            static_cast<T *>(dst)->~T();
            return false;
        }

        // Bit i a 1 se a alternativa i pode ser dona do Variant de que e alternativa, isto e, nao e folha (variant_leaf,
        // em utils.hpp: por omissao so os tipos de destrutor trivial, e os que a biblioteca ou o utilizador garantem).
        // So essas obrigam a passar a origem de uma atribuicao por um local. Com mais de 64 alternativas as de indice 64
        // em diante contam sempre como donas.
        template <typename... Ts>
        struct VariantOwnsMask
        {
            static constexpr unsigned long long value = 0;
        };
        template <typename T, typename... Rest>
        struct VariantOwnsMask<T, Rest...>
        {
            static constexpr unsigned long long value =
                (variant_leaf<T>::value ? 0ull : 1ull) | (VariantOwnsMask<Rest...>::value << 1);
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
            template <typename Full, std::size_t K>
            static bool prepare_copy(std::size_t, std::size_t, void *, const void *)
            {
                return false;
            }
            template <typename Full, std::size_t K>
            static bool prepare_move(std::size_t, std::size_t, void *, void *)
            {
                return false;
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
            // Prepara a atribuicao de uma origem do indice to a dst, que esta na alternativa from (K e o indice da
            // primeira alternativa desta lista na lista toda): percorre so as alternativas que pedem algo ao sair (as
            // outras nao geram codigo) e diz se a atribuicao acabou (true) ou se so se destruiu a actual e falta construir.
            template <typename Full, std::size_t K>
            static bool prepare_copy(std::size_t from, std::size_t to, void *dst, const void *src)
            {
                return prepare_copy_at<Full, K>(from, to, dst, src, integral_constant<bool, variant_pre_copy<T>::value>());
            }
            template <typename Full, std::size_t K>
            static bool prepare_move(std::size_t from, std::size_t to, void *dst, void *src)
            {
                return prepare_move_at<Full, K>(from, to, dst, src, integral_constant<bool, variant_pre_move<T>::value>());
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

        private:
            template <typename Full, std::size_t K>
            static bool prepare_copy_at(std::size_t from, std::size_t to, void *dst, const void *src, true_type)
            {
                if (from == K)
                    return variant_prepare_copy_one<T, Full, K>(to, dst, src);
                return VariantOps<Rest...>::template prepare_copy<Full, K + 1>(from, to, dst, src);
            }
            template <typename Full, std::size_t K>
            static bool prepare_copy_at(std::size_t from, std::size_t to, void *dst, const void *src, false_type)
            {
                return VariantOps<Rest...>::template prepare_copy<Full, K + 1>(from, to, dst, src);
            }
            template <typename Full, std::size_t K>
            static bool prepare_move_at(std::size_t from, std::size_t to, void *dst, void *src, true_type)
            {
                if (from == K)
                    return variant_prepare_move_one<T, Full, K>(to, dst, src);
                return VariantOps<Rest...>::template prepare_move<Full, K + 1>(from, to, dst, src);
            }
            template <typename Full, std::size_t K>
            static bool prepare_move_at(std::size_t from, std::size_t to, void *dst, void *src, false_type)
            {
                return VariantOps<Rest...>::template prepare_move<Full, K + 1>(from, to, dst, src);
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
        //  - com o mesmo indice e uma alternativa que se sabe atribuir no sitio (e nao e trivialmente copiavel) atribui-se
        //    com o operator= da alternativa, como o variant da biblioteca standard (a String reaproveita o bloco), e a
        //    seguranca fica nesse operator= (o do Rc e o do Unique tiram a origem antes de largar o que tinham); o move de
        //    String e de Vector e destruir e construir por move (variant_move_rebuilds) e nao se atribui no sitio;
        //  - nos outros casos, se a alternativa actual pode ser dona da origem (nao e folha: Rc, Unique, Function, um
        //    Vector de nos, um struct do utilizador) a origem passa primeiro para um objecto local, a alternativa
        //    actual destroi-se, e a nova constroi-se por move a partir do local: custa um move e uma destruicao a mais;
        //  - uma alternativa folha (destrutor trivial, String, Vector de folhas, ou o que o utilizador declarar
        //    especializando detail::variant_leaf, ver utils.hpp) nao pode ser dona de um Variant e destroi-se e
        //    constroi-se directamente, como sempre foi; se todas as alternativas forem assim e nenhuma atribuir no sitio
        //    (VariantPlainCopy e VariantPlainMove) o operador e so destruir e construir, sem mais nada.
        //    (O variant da biblioteca standard destroi antes de copiar quando a copia nao lanca, e ai tem o mesmo uso
        //    depois de libertar com o indice a mudar.)
        // Os dois operadores sao CT_FORCEINLINE: com os ramos a mais o compilador deixava de os meter inline em
        // alguns sitios (a atribuicao de escalares ficava 2 a 3 vezes mais lenta), e inline e como sempre foram.
        CT_FORCEINLINE Variant &operator=(const Variant &o)
        {
            if (this == &o)
                return *this;
            assign_copy(o, detail::integral_constant<bool, detail::VariantPlainCopy<Ts...>::value>());
            return *this;
        }
        CT_FORCEINLINE Variant &operator=(Variant &&o) noexcept
        {
            if (this == &o)
                return *this;
            assign_move(o, detail::integral_constant<bool, detail::VariantPlainMove<Ts...>::value>());
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

        // Sem alternativa que peca algo: destruir a actual e construir a nova (a origem sobrevive, nenhuma alternativa
        // actual e dona dela)
        CT_FORCEINLINE void assign_copy(const Variant &o, detail::true_type)
        {
            detail::VariantOps<Ts...>::destroy(index_, storage_);
            detail::VariantOps<Ts...>::copy_construct(o.index_, storage_, o.storage_);
            index_ = o.index_;
        }
        CT_FORCEINLINE void assign_move(Variant &o, detail::true_type) noexcept
        {
            detail::VariantOps<Ts...>::destroy(index_, storage_);
            detail::VariantOps<Ts...>::move_construct(o.index_, storage_, o.storage_);
            index_ = o.index_;
        }
        CT_FORCEINLINE void assign_copy(const Variant &o, detail::false_type)
        {
            const std::size_t to = o.index_; // o pode ja nao existir depois da troca: le-se o indice antes
            if (!detail::VariantOps<Ts...>::template prepare_copy<detail::VariantOps<Ts...>, 0>(
                    index_, to, storage_, o.storage_))
                detail::VariantOps<Ts...>::copy_construct(to, storage_, o.storage_);
            index_ = to;
        }
        CT_FORCEINLINE void assign_move(Variant &o, detail::false_type) noexcept
        {
            const std::size_t to = o.index_;
            if (!detail::VariantOps<Ts...>::template prepare_move<detail::VariantOps<Ts...>, 0>(
                    index_, to, storage_, o.storage_))
                detail::VariantOps<Ts...>::move_construct(to, storage_, o.storage_);
            index_ = to;
        }

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