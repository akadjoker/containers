
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <locale.h>
#include <new> 
#include <type_traits>

// ---------------------------------------------------------------------------------------------------------------------
// ABI. Nenhum tipo da ct muda de tamanho, de alinhamento nem de membros com NDEBUG, com assert, com o nivel de optimizacao ou com
// qualquer macro de depuracao implicita: um programa em que uma unidade se compila com -DNDEBUG e outra com -UNDEBUG (uma biblioteca
// em Release ligada a uma aplicacao em Debug, um teste com `#undef NDEBUG`) ve os mesmos tipos. As unicas macros que mudam o layout ou o
// comportamento de funcoes inline sao as de baixo, todas EXPLICITAS (nenhuma vem por omissao nem de NDEBUG) e todas num NAMESPACE
// INLINE que leva o valor no nome (`ct::abi_dbgiter::HashMap`): um programa com unidades que discordam troca tipos de nomes
// diferentes, e a ligacao falha (referencia por definir) em vez de corromper memoria em silencio.
//
//   CT_DEBUG_ITERATORS=1   HashMap, HashSet: o iterador aborta se for usado depois de a tabela mudar (a tabela ganha um contador)
//   CT_HASH_INLINE_MAX=N   HashMap: o tamanho acima do qual a entrada fica fora da tabela (N tem de ser um literal inteiro)
//   CT_TLS_MBEDTLS, CT_TLS_DEFAULT_MBEDTLS   TlsLibrary: o motor mbedTLS compilado e por omissao
//   CT_RE_CHECK_CAPS       Regex: uma verificacao de depuracao do motor
// Todas as unidades do programa tem de as definir da mesma maneira (no CMake, pelo alvo `ct`: opcao CT_DEBUG_ITERATORS).
// As outras `#if` dos cabecalhos so olham para a plataforma e para o compilador (_WIN32, __GNUC__, __BYTE_ORDER__, ...).
#if defined(CT_DEBUG_ITERATORS)
// -DCT_DEBUG_ITERATORS=0 e -DCT_DEBUG_ITERATORS (vazio) enganam-se facilmente a ler; so o valor 1 liga a verificacao
#if (CT_DEBUG_ITERATORS + 0) != 1
#error "CT_DEBUG_ITERATORS: defina-a com o valor 1 (-DCT_DEBUG_ITERATORS=1) ou nao a defina; nao depende de NDEBUG"
#endif
#define CT_HASH_TOUCH() (++version_)
#define CT_HASH_ITER_ARGS , &version_
#define CT_ABI_DBGITER_BEGIN inline namespace abi_dbgiter {
#define CT_ABI_DBGITER_END }
#else
#define CT_HASH_TOUCH() ((void)0)
#define CT_HASH_ITER_ARGS
#define CT_ABI_DBGITER_BEGIN
#define CT_ABI_DBGITER_END
#endif
#define CT_ABI_PASTE_(a, b) a##b
#define CT_ABI_PASTE(a, b) CT_ABI_PASTE_(a, b)

#if defined(__GNUC__) || defined(__clang__)
#define CT_LIKELY(x) __builtin_expect(!!(x), 1)
#define CT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define CT_NOINLINE __attribute__((noinline))
#define CT_FORCEINLINE inline __attribute__((always_inline))
#else
#define CT_LIKELY(x) (x)
#define CT_UNLIKELY(x) (x)
#define CT_NOINLINE
#define CT_FORCEINLINE inline
#endif

namespace ct
{
    namespace detail
    {

        template <typename T, T V>
        struct integral_constant
        {
            static constexpr T value = V;
            using value_type = T;
            using type = integral_constant;
            constexpr operator T() const noexcept { return V; }
        };

        using true_type = integral_constant<bool, true>;
        using false_type = integral_constant<bool, false>;

        template <bool B, typename T = void>
        struct enable_if
        {
        };
        template <typename T>
        struct enable_if<true, T>
        {
            using type = T;
        };

        template <typename A, typename B>
        struct is_same : false_type
        {
        };
        template <typename A>
        struct is_same<A, A> : true_type
        {
        };

        template <typename T>
        struct remove_ref
        {
            using type = T;
        };
        template <typename T>
        struct remove_ref<T &>
        {
            using type = T;
        };
        template <typename T>
        struct remove_ref<T &&>
        {
            using type = T;
        };

        template <typename T>
        struct remove_cv
        {
            using type = T;
        };
        template <typename T>
        struct remove_cv<const T>
        {
            using type = T;
        };
        template <typename T>
        struct remove_cv<volatile T>
        {
            using type = T;
        };
        template <typename T>
        struct remove_cv<const volatile T>
        {
            using type = T;
        };

        template <typename T>
        struct bare
        {
            using type = typename remove_cv<typename remove_ref<T>::type>::type;
        };

        template <typename T>
        struct is_void : false_type
        {
        };
        template <>
        struct is_void<void> : true_type
        {
        };
        template <>
        struct is_void<const void> : true_type
        {
        };

        template <typename T>
        struct is_const : false_type
        {
        };
        template <typename T>
        struct is_const<const T> : true_type
        {
        };

        template <typename T>
        struct is_pointer : false_type
        {
        };
        template <typename T>
        struct is_pointer<T *> : true_type
        {
        };
        template <typename T>
        struct is_pointer<T *const> : true_type
        {
        };
        template <typename T>
        struct is_pointer<T *volatile> : true_type
        {
        };
        template <typename T>
        struct is_pointer<T *const volatile> : true_type
        {
        };

        // O inteiro sem sinal do tamanho de T, para contas que dao a volta sem overflow com sinal. So para tipos
        // inteiros (nao bool): o resultado serve para converter de volta para T.
        template <std::size_t Size>
        struct unsigned_of_size;
        template <>
        struct unsigned_of_size<1>
        {
            using type = unsigned char;
        };
        template <>
        struct unsigned_of_size<2>
        {
            using type = unsigned short;
        };
        template <>
        struct unsigned_of_size<4>
        {
            using type = unsigned int;
        };
        template <>
        struct unsigned_of_size<8>
        {
            using type = unsigned long long;
        };

        template <typename T>
        struct make_unsigned
        {
            using type = typename unsigned_of_size<sizeof(T)>::type;
        };

        template <typename T>
        using is_trivially_copyable_t = integral_constant<bool, std::is_trivially_copyable<T>::value>;

        template <typename T>
        using is_trivially_destructible_t =
            integral_constant<bool, std::is_trivially_destructible<T>::value>;

        // Construir por omissao nao escreve nada (int, structs de tipos assim, arrays): __is_trivially_constructible e do
        // GCC, do clang e do MSVC e evita juntar mais um nome do std
        template <typename T>
        using is_trivially_default_constructible_t = integral_constant<bool, __is_trivially_constructible(T)>;

        // Variant: T e "folha" se nao pode ser dona, directa ou por ponteiro, de um objecto que contenha o Variant de que
        // e alternativa. Atribuir ao Variant a partir de um membro do que ele guarda (cursor = cursor.get<Rc<No>>()->next)
        // so e perigoso quando a alternativa actual e dona da origem, porque destrui-la antes de copiar a origem e ler
        // memoria libertada; uma alternativa folha nao o pode ser, e o Variant destroi-a e constroi a nova directamente.
        // Por omissao so os tipos de destrutor trivial (nao sao donos de nada) sao folha, e tudo o resto, que e quem pode
        // ser dono de um no (Rc, Unique, Function, Vector de nos, um struct do utilizador), faz o caminho seguro. Quem tem
        // um tipo que garante que so e dono de bytes ou de outras folhas especializa:
        //     namespace ct { namespace detail { template <> struct variant_leaf<MeuTipo> : true_type {}; } }
        // e se enganar a garantia volta o uso depois de libertar. Os tipos da biblioteca especializam no cabecalho de cada um.
        template <typename T>
        struct variant_leaf : is_trivially_destructible_t<T>
        {
        };

        // Variant: o operator= de move de T faz exactamente o mesmo que destruir e construir por move (String e Vector: soltam
        // o que tinham e ficam com o bloco da origem, sem reaproveitar nada). Nesses o Variant nao atribui no sitio ao mover
        // para a mesma alternativa, e destroi e constroi como sempre fez: o resultado e o mesmo e o codigo e mais curto.
        // (A atribuicao por copia atribui no sitio: reaproveita o bloco, e ai ha ganho.) Por omissao e falso: um tipo que
        // reaproveita algo ao mover (um buffer, uma ligacao) tem de ser atribuido no sitio.
        template <typename T>
        struct variant_move_rebuilds : false_type
        {
        };

        template <typename T>
        struct is_bytewise_comparable
            : integral_constant<bool, std::is_integral<T>::value ||
                                          std::is_enum<T>::value ||
                                          std::is_pointer<T>::value>
        {
        };

        template <typename T>
        struct is_memcmp_ordered
            : integral_constant<bool, std::is_integral<T>::value && sizeof(T) == 1 &&
                                          !std::is_signed<T>::value>
        {
        };

        template <typename T>
        constexpr typename remove_ref<T>::type &&move(T &&t) noexcept
        {
            return static_cast<typename remove_ref<T>::type &&>(t);
        }

        template <typename T>
        constexpr T &&forward(typename remove_ref<T>::type &t) noexcept
        {
            return static_cast<T &&>(t);
        }
        template <typename T>
        constexpr T &&forward(typename remove_ref<T>::type &&t) noexcept
        {
            return static_cast<T &&>(t);
        }

        template <typename T>
        T &&declval() noexcept; 

        template <typename T>
        inline void swap_vals(T &a, T &b)
        {
            T t = detail::move(a);
            a = detail::move(b);
            b = detail::move(t);
        }

        // O separador decimal do locale actual (',' em pt_PT). Quem escreve ou le numeros em texto troca-o por '.'.
        inline char decimal_point() noexcept
        {
            const lconv *lc = localeconv();
            return (lc && lc->decimal_point && lc->decimal_point[0]) ? lc->decimal_point[0] : '.';
        }

        [[noreturn]] inline void fatal(const char *msg)
        {
            std::fputs(msg, stderr);
            std::fputc('\n', stderr);
            std::abort();
        }

        template <typename It>
        class ReverseIt
        {
            It it_;

        public:
            using iterator_category = typename std::iterator_traits<It>::iterator_category;
            using value_type = typename std::iterator_traits<It>::value_type;
            using difference_type = typename std::iterator_traits<It>::difference_type;
            using pointer = typename std::iterator_traits<It>::pointer;
            using reference = typename std::iterator_traits<It>::reference;

            ReverseIt() : it_() {}
            explicit ReverseIt(It it) : it_(it) {}
            reference operator*() const
            {
                It t = it_;
                return *--t;
            }
            pointer operator->() const
            {
                It t = it_;
                --t;
                return &*t;
            }
            reference operator[](difference_type n) const { return *(*this + n); }
            ReverseIt &operator++()
            {
                --it_;
                return *this;
            }
            ReverseIt operator++(int)
            {
                ReverseIt t = *this;
                --it_;
                return t;
            }
            ReverseIt &operator--()
            {
                ++it_;
                return *this;
            }
            ReverseIt operator--(int)
            {
                ReverseIt t = *this;
                ++it_;
                return t;
            }
            ReverseIt &operator+=(difference_type n)
            {
                it_ -= n;
                return *this;
            }
            ReverseIt &operator-=(difference_type n)
            {
                it_ += n;
                return *this;
            }
            ReverseIt operator+(difference_type n) const { return ReverseIt(it_ - n); }
            ReverseIt operator-(difference_type n) const { return ReverseIt(it_ + n); }
            difference_type operator-(const ReverseIt &o) const { return o.it_ - it_; }
            It base() const { return it_; }
            bool operator==(const ReverseIt &o) const { return it_ == o.it_; }
            bool operator!=(const ReverseIt &o) const { return it_ != o.it_; }
            bool operator<(const ReverseIt &o) const { return o.it_ < it_; }
            bool operator>(const ReverseIt &o) const { return o.it_ > it_; }
            bool operator<=(const ReverseIt &o) const { return o.it_ <= it_; }
            bool operator>=(const ReverseIt &o) const { return o.it_ >= it_; }
        };

        template <typename T>
        inline void relocate_n(T *dst, T *src, std::size_t n, true_type )
        {
            if (n)
                std::memcpy(static_cast<void *>(dst), static_cast<const void *>(src),
                            n * sizeof(T));
        }

        template <typename T>
        inline void relocate_n(T *dst, T *src, std::size_t n, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                ::new (static_cast<void *>(dst + i)) T(detail::move(src[i]));
                src[i].~T();
            }
        }

        template <typename T>
        inline void relocate_n(T *dst, T *src, std::size_t n)
        {
            relocate_n(dst, src, n, is_trivially_copyable_t<T>{});
        }

        template <typename T>
        inline void destroy_n(T *, std::size_t, true_type ) {}

        template <typename T>
        inline void destroy_n(T *p, std::size_t n, false_type)
        {
            for (std::size_t i = n; i > 0; --i)
                p[i - 1].~T();
        }

        template <typename T>
        inline void destroy_n(T *p, std::size_t n)
        {
            destroy_n(p, n, is_trivially_destructible_t<T>{});
        }

        template <typename T>
        inline void copy_construct_n(T *dst, const T *src, std::size_t n, true_type)
        {
            if (n)
                std::memcpy(static_cast<void *>(dst), static_cast<const void *>(src),
                            n * sizeof(T));
        }

        template <typename T>
        inline void copy_construct_n(T *dst, const T *src, std::size_t n, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
                ::new (static_cast<void *>(dst + i)) T(src[i]);
        }

        template <typename T>
        inline void copy_construct_n(T *dst, const T *src, std::size_t n)
        {
            copy_construct_n(dst, src, n, is_trivially_copyable_t<T>{});
        }

        template <typename T>
        inline void fill_construct_n(T *dst, std::size_t n, const T &value)
        {
            for (std::size_t i = 0; i < n; ++i)
                ::new (static_cast<void *>(dst + i)) T(value);
        }

        template <typename T>
        CT_NOINLINE void fill_assign_n(T *dst, std::size_t n, T value)
        {
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = value;
        }

        template <typename T>
        inline void fill_trivial_n(T *dst, std::size_t n, const T &v, true_type )
        {
            unsigned char byte;
            std::memcpy(&byte, static_cast<const void *>(&v), 1);
            if (n)
                std::memset(static_cast<void *>(dst), byte, n);
        }

        template <typename T>
        inline void fill_trivial_n(T *dst, std::size_t n, const T &v, false_type)
        {
            const T tmp = v;
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = tmp;
        }

        template <typename T>
        inline void fill_fast_n(T *dst, std::size_t n, const T &v, true_type )
        {
            fill_trivial_n(dst, n, v, integral_constant<bool, sizeof(T) == 1>{});
        }

        template <typename T>
        inline void fill_fast_n(T *dst, std::size_t n, const T &v, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = v;
        }

        template <typename T>
        inline void fill_fast_n(T *dst, std::size_t n, const T &v)
        {
            fill_fast_n(dst, n, v, is_trivially_copyable_t<T>{});
        }

        template <typename T>
        inline bool equal_n(const T *a, const T *b, std::size_t n, true_type )
        {
            return n == 0 || std::memcmp(a, b, n * sizeof(T)) == 0;
        }

        template <typename T>
        inline bool equal_n(const T *a, const T *b, std::size_t n, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
                if (!(a[i] == b[i]))
                    return false;
            return true;
        }

        template <typename T>
        inline bool equal_n(const T *a, const T *b, std::size_t n)
        {
            return equal_n(a, b, n, is_bytewise_comparable<T>{});
        }

        template <typename T>
        inline bool lex_less_n(const T *a, std::size_t na, const T *b, std::size_t nb,
                               true_type )
        {
            const std::size_t n = na < nb ? na : nb;
            const int c = n ? std::memcmp(a, b, n) : 0;
            return c < 0 || (c == 0 && na < nb);
        }

        template <typename T>
        inline bool lex_less_n(const T *a, std::size_t na, const T *b, std::size_t nb,
                               false_type)
        {
            const std::size_t n = na < nb ? na : nb;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (a[i] < b[i])
                    return true;
                if (b[i] < a[i])
                    return false;
            }
            return na < nb;
        }

        template <typename T>
        inline bool lex_less_n(const T *a, std::size_t na, const T *b, std::size_t nb)
        {
            return lex_less_n(a, na, b, nb, is_memcmp_ordered<T>{});
        }

        inline unsigned ctz64(std::uint64_t x) noexcept
        {
#if defined(__GNUC__) || defined(__clang__)
            return static_cast<unsigned>(__builtin_ctzll(x));
#else
            unsigned n = 0;
            while (!(x & 1))
            {
                x >>= 1;
                ++n;
            }
            return n;
#endif
        }

        inline void copy_bytes(void *dst, const void *src, std::size_t n) noexcept
        {
            std::memcpy(dst, src, n);
        }

        // Os n bytes (1 a 8) de meta a partir de m (cada um 0 = livre ou 1 = ocupado) como uma palavra em que o bit 8k
        // esta aceso quando o slot k esta ocupado.
        // Com Tagged a meta de um ocupado tem o bit alto aceso (0x80 mais a distancia do elemento ao seu slot de origem), e a
        // palavra devolvida tem o bit 8k + 7 aceso nos ocupados.
        template <bool Tagged = false>
        inline std::uint64_t meta_group(const unsigned char *m, std::size_t n) noexcept
        {
            std::uint64_t w = 0;
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            std::memcpy(&w, m, n);
            if (Tagged)
                w &= 0x8080808080808080ull;
#else
            for (std::size_t k = 0; k < n; ++k)
                if (m[k])
                    w |= std::uint64_t(1) << (8 * k);
#endif
            return w;
        }

        // Iterador das tabelas de hash: le a meta 8 slots de cada vez e visita os ocupados pelos bits acesos da palavra,
        // por isso o custo por elemento nao depende de quantos buracos ha pelo meio (a 38 % de carga eram quatro
        // quintos dos ciclos). E o ocupado atual e o bit mais baixo que resta.
        template <typename E, bool Tagged = false>
        class SlotIter
        {
            E *eg_;
            const unsigned char *mg_;
            const unsigned char *mend_;
            std::uint64_t bits_;

            template <typename U, bool T>
            friend class SlotIter;

        public:
            SlotIter(E *e, const unsigned char *m, const unsigned char *mend) noexcept : eg_(e), mg_(m), mend_(mend), bits_(0)
            {
                load();
            }
            template <typename U, typename = typename enable_if<is_same<const U, E>::value && !is_same<U, E>::value>::type>
            SlotIter(const SlotIter<U, Tagged> &o) noexcept : eg_(o.eg_), mg_(o.mg_), mend_(o.mend_), bits_(o.bits_)
            {
            }
            E &operator*() const noexcept { return eg_[ctz64(bits_) >> 3]; }
            E *operator->() const noexcept { return eg_ + (ctz64(bits_) >> 3); }
            SlotIter &operator++() noexcept
            {
                bits_ &= bits_ - 1;
                if (!bits_)
                {
                    const std::size_t n = group_size();
                    mg_ += n;
                    eg_ += n;
                    load();
                }
                return *this;
            }
            bool operator==(const SlotIter &o) const noexcept { return mg_ == o.mg_ && bits_ == o.bits_; }
            bool operator!=(const SlotIter &o) const noexcept { return !(*this == o); }

        private:
            std::size_t group_size() const noexcept
            {
                const std::size_t left = static_cast<std::size_t>(mend_ - mg_);
                return left < 8 ? left : 8;
            }
            void load() noexcept
            {
                while (mg_ != mend_)
                {
                    const std::size_t n = group_size();
                    bits_ = meta_group<Tagged>(mg_, n);
                    if (bits_)
                        return;
                    mg_ += n;
                    eg_ += n;
                }
            }
        };

#if defined(CT_DEBUG_ITERATORS)
        // So com CT_DEBUG_ITERATORS=1 (explicito, nunca por omissao nem por causa de NDEBUG, e igual em todas as unidades; os
        // tipos que o usam levam o namespace inline abi_dbgiter): o iterador de HashMap e HashSet guarda a versao da tabela
        // quando nasce e aborta se for usado (*, ->, ++) depois de a tabela ter mudado, que e o que apagar ou inserir durante
        // um ciclo for faz. Sem ela o iterador e o SlotIter de sempre, sem custo nenhum.
        template <typename E, bool Tagged = false>
        class CheckedSlotIter
        {
            SlotIter<E, Tagged> it_;
            const std::size_t *version_;
            std::size_t seen_;

            template <typename U, bool T>
            friend class CheckedSlotIter;

            void check() const
            {
                if (CT_UNLIKELY(*version_ != seen_))
                    fatal("ct::HashMap/HashSet: iterador usado depois de a tabela mudar (apagar ou inserir durante o ciclo "
                          "invalida os iteradores; para apagar varios elementos use erase_if)");
            }

        public:
            CheckedSlotIter(E *e, const unsigned char *m, const unsigned char *mend, const std::size_t *version) noexcept
                : it_(e, m, mend), version_(version), seen_(*version)
            {
            }
            template <typename U, typename = typename enable_if<is_same<const U, E>::value && !is_same<U, E>::value>::type>
            CheckedSlotIter(const CheckedSlotIter<U, Tagged> &o) noexcept : it_(o.it_), version_(o.version_), seen_(o.seen_)
            {
            }
            E &operator*() const
            {
                check();
                return *it_;
            }
            E *operator->() const
            {
                check();
                return it_.operator->();
            }
            CheckedSlotIter &operator++()
            {
                check();
                ++it_;
                return *this;
            }
            bool operator==(const CheckedSlotIter &o) const noexcept { return it_ == o.it_; }
            bool operator!=(const CheckedSlotIter &o) const noexcept { return !(it_ == o.it_); }
        };
#endif

        // Iterador de tabelas cujos slots guardam ponteiros para as entradas (HashMap com entradas grandes): o mesmo
        // iterador por baixo, e o * e o -> vao ao bloco da entrada.
        template <typename Inner, typename E>
        class IndirectIter
        {
            Inner it_;

            template <typename I2, typename E2>
            friend class IndirectIter;

        public:
            template <typename P, typename... R>
            IndirectIter(P *e, const unsigned char *m, const unsigned char *mend, R... r) noexcept : it_(e, m, mend, r...)
            {
            }
            template <typename I2, typename E2, typename = typename enable_if<!is_same<I2, Inner>::value>::type>
            IndirectIter(const IndirectIter<I2, E2> &o) noexcept : it_(o.it_)
            {
            }
            E &operator*() const { return **it_; }
            E *operator->() const { return *it_; }
            IndirectIter &operator++()
            {
                ++it_;
                return *this;
            }
            bool operator==(const IndirectIter &o) const noexcept { return it_ == o.it_; }
            bool operator!=(const IndirectIter &o) const noexcept { return !(it_ == o.it_); }
        };

        inline std::uint64_t hash_mix(std::uint64_t x) noexcept
        {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            return x;
        }

        // MurmurHash64A: oito bytes por passo (o FNV-1a que havia fazia um byte por passo, com a multiplicacao na cadeia
        // de dependencias) e os bits baixos, os que a mascara da tabela usa, saem bem misturados.
        inline std::uint64_t hash_bytes(const char *data, std::size_t n) noexcept
        {
            const std::uint64_t m = 0xc6a4a7935bd1e995ull;
            std::uint64_t h = 0x9ae16a3b2f90404full ^ (static_cast<std::uint64_t>(n) * m);
            while (n >= 8)
            {
                std::uint64_t k;
                std::memcpy(&k, data, 8);
                k *= m;
                k ^= k >> 47;
                k *= m;
                h ^= k;
                h *= m;
                data += 8;
                n -= 8;
            }
            if (n)
            {
                std::uint64_t k = 0;
                std::memcpy(&k, data, n);
                h ^= k;
                h *= m;
            }
            h ^= h >> 47;
            h *= m;
            h ^= h >> 47;
            return h;
        }

        inline std::size_t next_pow2(std::size_t n)
        {
            if (n < 2)
                return 2;
            if (n > (std::numeric_limits<std::size_t>::max)() / 2)
                return 0;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
#if SIZE_MAX > 0xffffffff
            n |= n >> 32;
#endif
            return n + 1;
        }

        inline bool is_power_of_two(std::size_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        inline bool checked_add(std::size_t a, std::size_t b, std::size_t &result)
        {
            if (b > (std::numeric_limits<std::size_t>::max)() - a)
                return false;
            result = a + b;
            return true;
        }

        inline bool checked_mul(std::size_t a, std::size_t b, std::size_t &result)
        {
            if (a != 0 && b > (std::numeric_limits<std::size_t>::max)() / a)
                return false;
            result = a * b;
            return true;
        }

        inline bool checked_pow2(std::size_t value, std::size_t &result)
        {
            if (value < 2)
            {
                result = 2;
                return true;
            }
            if (value > (std::numeric_limits<std::size_t>::max)() / 2)
                return false;
            result = next_pow2(value);
            return result >= value;
        }

    } 

    template <typename K>
    struct Less
    {
        bool operator()(const K &a, const K &b) const { return a < b; }
    };

    struct HeapAlloc
    {
#if defined(_WIN32)
        struct Header
        {
            void *raw;
        };
#endif

        // O malloc do sistema ja entrega 2 * sizeof(void *) bytes de alinhamento: ate ai o bloco e o do malloc, sem
        // cabecalho (o que custava 8 bytes por alocacao, e uma cadeia de 24 a 40 caracteres passava de 32 para 48
        // bytes no malloc). Mais alinhado que isso pede posix_memalign, que free() tambem solta, por isso
        // deallocate nao precisa de saber o alinhamento. No Windows free() nao solta memoria de _aligned_malloc, e
        // fica o cabecalho de sempre.
#if !defined(_WIN32)
        static constexpr std::size_t kMallocAlign = 2 * sizeof(void *);

        void *allocate(std::size_t bytes, std::size_t align)
        {
            if (!detail::is_power_of_two(align))
                detail::fatal("ct::HeapAlloc: alinhamento invalido");
            if (bytes == 0)
                bytes = 1;
            void *p = nullptr;
            if (align <= kMallocAlign)
                p = std::malloc(bytes);
            else if (::posix_memalign(&p, align, bytes) != 0)
                p = nullptr;
            if (CT_UNLIKELY(!p))
                detail::fatal("ct::HeapAlloc: sem memoria");
            return p;
        }

        void deallocate(void *p, std::size_t)
        {
            std::free(p);
        }

        void *reallocate(void *p, std::size_t old_bytes, std::size_t new_bytes, std::size_t align)
        {
            if (!p)
                return allocate(new_bytes, align);
            if (new_bytes == 0)
                new_bytes = 1;
            if (align <= kMallocAlign)
            {
                void *q = std::realloc(p, new_bytes);
                if (CT_UNLIKELY(!q))
                    detail::fatal("ct::HeapAlloc: sem memoria");
                return q;
            }
            void *q = allocate(new_bytes, align);
            const std::size_t copied = old_bytes < new_bytes ? old_bytes : new_bytes;
            if (copied)
                std::memcpy(q, p, copied);
            deallocate(p, old_bytes);
            return q;
        }
#else
        void *allocate(std::size_t bytes, std::size_t align)
        {
            if (!detail::is_power_of_two(align))
                detail::fatal("ct::HeapAlloc: alinhamento invalido");

            if (align < alignof(Header))
                align = alignof(Header);
            if (bytes == 0)
                bytes = 1;

            if (align == alignof(Header))
            {
                std::size_t total = 0;
                if (!detail::checked_add(sizeof(Header), bytes, total))
                    detail::fatal("ct::HeapAlloc: tamanho invalido");
                void *raw = std::malloc(total);
                if (CT_UNLIKELY(!raw))
                    detail::fatal("ct::HeapAlloc: sem memoria");
                Header *header = static_cast<Header *>(raw);
                header->raw = raw;
                return static_cast<void *>(header + 1);
            }

            std::size_t total = 0;
            if (!detail::checked_add(sizeof(Header), bytes, total) ||
                !detail::checked_add(total, align - 1, total))
                detail::fatal("ct::HeapAlloc: tamanho invalido");

            void *raw = std::malloc(total);
            if (CT_UNLIKELY(!raw))
                detail::fatal("ct::HeapAlloc: sem memoria");

            const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
            const std::uintptr_t aligned = (start + align - 1) & ~(align - 1);
            Header *header = reinterpret_cast<Header *>(aligned - sizeof(Header));
            header->raw = raw;
            return reinterpret_cast<void *>(aligned);
        }

        void deallocate(void *p, std::size_t )
        {
            if (!p)
                return;
            Header *header = reinterpret_cast<Header *>(reinterpret_cast<std::uintptr_t>(p) -
                                                        sizeof(Header));
            std::free(header->raw);
        }

        void *reallocate(void *p, std::size_t old_bytes, std::size_t new_bytes,
                         std::size_t align)
        {
            if (!p)
                return allocate(new_bytes, align);
            if (new_bytes == 0)
                new_bytes = 1;

            if (align < alignof(Header))
                align = alignof(Header);
            if (align == alignof(Header))
            {
                Header *header = reinterpret_cast<Header *>(reinterpret_cast<std::uintptr_t>(p) -
                                                              sizeof(Header));
                std::size_t total = 0;
                if (!detail::checked_add(sizeof(Header), new_bytes, total))
                    detail::fatal("ct::HeapAlloc: tamanho invalido");
                void *raw = std::realloc(header->raw, total);
                if (CT_UNLIKELY(!raw))
                    detail::fatal("ct::HeapAlloc: sem memoria");
                Header *new_header = static_cast<Header *>(raw);
                new_header->raw = raw;
                return static_cast<void *>(new_header + 1);
            }

            void *q = allocate(new_bytes, align);
            const std::size_t copied = old_bytes < new_bytes ? old_bytes : new_bytes;
            if (copied)
                std::memcpy(q, p, copied);
            deallocate(p, old_bytes);
            return q;
        }
#endif
    };

    // Para quem usa a ct de fora, sem a biblioteca standard: ct::move e ct::forward fazem o que std::move e
    // std::forward fazem.
    using detail::forward;
    using detail::move;

}
