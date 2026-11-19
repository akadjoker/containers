// O acesso const aos mapas e const em todas as disposicoes, e as chaves nunca se alteram por um iterador.
//
// Antes: num HashMap cujos valores ficam fora da tabela (a entrada com mais de CT_HASH_INLINE_MAX bytes, 128 por omissao) o
// `value` da entrada era uma referencia (`V &value`), e uma referencia como membro nao propaga o const:
// `for (auto &e : constmap) e.value.x = 1;` compilava e escrevia no mapa. E a chave (`K key`) podia alterar-se por um iterador
// nao const em todos os mapas, o que corrompe a tabela.
//
// Verifica-se em tempo de compilacao (static_assert sobre os tipos que o utilizador ve, escritos com os tracos da propria ct:
// nao ha biblioteca standard) e, em baixo, com uma passagem real por cada disposicao. As escritas que tem de ser recusadas
// aparecem como "esta expressao nao e valida" (atribuir por it->value, chamar um membro nao const do valor, ligar o valor a uma
// referencia nao const, atribuir a chave); os programas que falham mesmo a compilar estao em tests/const_fail.cpp (testes
// WILL_FAIL do CMake).
#include <ct/array.hpp>
#include <ct/btreemap.hpp>
#include <ct/btreeset.hpp>
#include <ct/deque.hpp>
#include <ct/flatmap.hpp>
#include <ct/hashmap.hpp>
#include <ct/hashset.hpp>
#include <ct/slotmap.hpp>
#include <ct/slotmap32.hpp>
#include <ct/span.hpp>
#include <ct/string.hpp>
#include <ct/treemap.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <stdint.h>
#include <string.h>

namespace
{
    using ct::detail::declval;
    using ct::detail::false_type;
    using ct::detail::is_same;
    using ct::detail::true_type;

    // valor pequeno (a entrada fica na tabela) e valor grande (256 bytes: com chave pequena o valor vai para o slab)
    struct Small
    {
        int x;
        void bump() { ++x; }
    };
    struct Big
    {
        int x;
        unsigned char pad[252];
        void bump() { ++x; }
    };
    // chave com mais de 32 bytes: com Big a entrada inteira vai para um bloco e o slot guarda so o ponteiro
    struct Key64
    {
        unsigned char b[64];
        uint64_t hash() const
        {
            uint64_t v;
            memcpy(&v, b, sizeof v);
            return ct::detail::hash_mix(v);
        }
        bool operator==(const Key64 &o) const { return memcmp(b, o.b, sizeof b) == 0; }
    };
    Key64 key64(int i)
    {
        Key64 k;
        memset(k.b, 0, sizeof k.b);
        memcpy(k.b, &i, sizeof i);
        return k;
    }

    // ---- tracos: "esta expressao e valida?" e "esta referencia liga?" ----
    template <typename E, typename V>
    auto can_assign_value_(int) -> decltype((void)(declval<E>().value = declval<const V &>()), true_type());
    template <typename E, typename V>
    false_type can_assign_value_(...);
    template <typename E, typename V>
    struct can_assign_value : decltype(can_assign_value_<E, V>(0))
    {
    };

    template <typename E>
    auto can_bump_(int) -> decltype((void)declval<E>().value.bump(), true_type());
    template <typename E>
    false_type can_bump_(...);
    template <typename E>
    struct can_bump : decltype(can_bump_<E>(0))
    {
    };

    template <typename E, typename K>
    auto can_assign_key_(int) -> decltype((void)(declval<E>().key = declval<const K &>()), true_type());
    template <typename E, typename K>
    false_type can_assign_key_(...);
    template <typename E, typename K>
    struct can_assign_key : decltype(can_assign_key_<E, K>(0))
    {
    };

    // f(lvalue) escolhe a primeira se o lvalue pode ligar a `T &` (nao const), a segunda se so liga a `const T &`
    template <typename T>
    struct Binder
    {
        static true_type f(T &);
        static false_type f(const T &);
    };
#define CT_BINDS_MUTABLE(T, expr) (decltype(Binder<T>::f(expr))::value)

    // f(A) escolhe a primeira se A converte implicitamente em B
    template <typename B>
    struct Converts
    {
        static true_type f(B);
        static false_type f(...);
    };

    // ---- mapas com iteracao const: HashMap, BTreeMap, FlatMap ----
    template <typename M, typename K, typename V, bool HasBump>
    struct MapChecks
    {
        typedef typename M::iterator It;
        typedef typename M::const_iterator CIt;
        typedef decltype(*declval<M &>().begin()) ME;       // o que `for (auto &e : map)` da num mapa nao const
        typedef decltype(*declval<const M &>().begin()) CE; // e num mapa const

        // o que o utilizador ve em cada caminho
        static_assert(is_same<decltype(declval<const M &>().begin()), CIt>::value, "begin() const tem de dar const_iterator");
        static_assert(is_same<decltype((declval<CE>().value)), const V &>::value, "o value de uma entrada const tem de ser const");
        static_assert(is_same<decltype((declval<CIt>()->value)), const V &>::value, "const_iterator->value tem de ser const");
        static_assert(is_same<decltype(&declval<CE>().value), const V *>::value, "&e.value de uma entrada const tem de ser const V *");
        static_assert(is_same<decltype((declval<ME>().value)), V &>::value, "o value de um mapa nao const continua a escrever-se");
        static_assert(is_same<decltype((declval<It>()->value)), V &>::value, "iterator->value continua a escrever-se");

        // as escritas que tem de ser recusadas por um caminho const
        static_assert(!can_assign_value<CE, V>::value, "atribuir a it->value de um mapa const compila");
        static_assert(can_assign_value<ME, V>::value, "atribuir a it->value de um mapa nao const deixou de compilar");
        static_assert(!CT_BINDS_MUTABLE(V, declval<CE>().value), "o value de um mapa const liga a V &");
        static_assert(CT_BINDS_MUTABLE(V, declval<ME>().value), "o value de um mapa nao const deixou de ligar a V &");
        static_assert(!can_bump<CE>::value, "chamar um membro nao const do value de um mapa const compila");
        static_assert(can_bump<ME>::value == HasBump, "chamar um membro nao const do value de um mapa nao const");

        // a chave nunca se altera, por nenhum iterador
        static_assert(is_same<decltype((declval<CE>().key)), const K &>::value, "a chave de uma entrada const tem de ser const K &");
        static_assert(is_same<decltype((declval<ME>().key)), const K &>::value, "a chave de um iterador nao const tem de ser const K &");
        static_assert(!can_assign_key<CE, K>::value, "atribuir a chave por um iterador const compila");
        static_assert(!can_assign_key<ME, K>::value, "atribuir a chave por um iterador nao const compila");
        static_assert(!CT_BINDS_MUTABLE(K, declval<CE>().key), "a chave por um iterador const liga a K &");
        static_assert(!CT_BINDS_MUTABLE(K, declval<ME>().key), "a chave por um iterador nao const liga a K &");

        // iterator -> const_iterator continua a converter, o contrario nao
        static_assert(decltype(Converts<CIt>::f(declval<It>()))::value, "iterator nao converte em const_iterator");
        static_assert(!decltype(Converts<It>::f(declval<CIt>()))::value, "const_iterator converte em iterator");

        // find e get
        static_assert(is_same<decltype(declval<const M &>().find(declval<const K &>())), const V *>::value, "find const tem de dar const V *");
        static_assert(is_same<decltype(declval<M &>().find(declval<const K &>())), V *>::value, "find nao const tem de dar V *");
        static_assert(is_same<decltype(declval<const M &>().get(declval<const K &>(), declval<const V &>())), const V &>::value,
                      "get tem de dar const V &");
    };

    // cbegin (o FlatMap nao tem)
    template <typename M>
    struct CbeginChecks
    {
        static_assert(is_same<decltype(declval<M &>().cbegin()), typename M::const_iterator>::value, "cbegin() tem de dar const_iterator");
        static_assert(is_same<decltype(declval<M &>().cend()), typename M::const_iterator>::value, "cend() tem de dar const_iterator");
    };

    // ---- TreeMap: nao tem iteracao const; o iterador e o find ----
    template <typename M, typename K, typename V, bool HasBump>
    struct TreeChecks
    {
        typedef decltype(*declval<M &>().begin()) ME;
        static_assert(is_same<decltype((declval<ME>().value)), V &>::value, "o value de um TreeMap nao const continua a escrever-se");
        static_assert(can_assign_value<ME, V>::value, "atribuir a it->value de um TreeMap");
        static_assert(can_bump<ME>::value == HasBump, "chamar um membro nao const num TreeMap");
        static_assert(is_same<decltype((declval<ME>().key)), const K &>::value, "a chave de um iterador de TreeMap tem de ser const K &");
        static_assert(!can_assign_key<ME, K>::value, "atribuir a chave por um iterador de TreeMap compila");
        static_assert(!CT_BINDS_MUTABLE(K, declval<ME>().key), "a chave de um TreeMap liga a K &");
        static_assert(is_same<decltype(declval<const M &>().find(declval<const K &>())), const V *>::value, "find const de TreeMap");
        static_assert(is_same<decltype(declval<M &>().find(declval<const K &>())), V *>::value, "find de TreeMap");
        static_assert(is_same<decltype(declval<const M &>().get(declval<const K &>(), declval<const V &>())), const V &>::value,
                      "get de TreeMap");
    };

    // ---- conjuntos: o elemento e sempre const ----
    template <typename S, typename K>
    struct SetChecks
    {
        static_assert(is_same<decltype(*declval<const S &>().begin()), const K &>::value, "o elemento de um conjunto const");
        static_assert(is_same<decltype(*declval<S &>().begin()), const K &>::value, "o elemento de um conjunto: a chave nunca se altera");
        static_assert(!CT_BINDS_MUTABLE(K, *declval<S &>().begin()), "o elemento de um conjunto liga a K &");
    };

    // ---- sequencias (so completude: ja valiam) ----
    template <typename C, typename T>
    struct SeqChecks
    {
        static_assert(is_same<decltype(declval<const C &>()[0]), const T &>::value, "operator[] const");
        static_assert(is_same<decltype(declval<const C &>().front()), const T &>::value, "front() const");
        static_assert(is_same<decltype(declval<const C &>().back()), const T &>::value, "back() const");
        static_assert(is_same<decltype(*declval<const C &>().begin()), const T &>::value, "begin() const");
        static_assert(is_same<decltype(*declval<C &>().cbegin()), const T &>::value, "cbegin()");
        static_assert(is_same<decltype(declval<C &>()[0]), T &>::value, "operator[] nao const");
        static_assert(!CT_BINDS_MUTABLE(T, declval<const C &>()[0]), "operator[] const liga a T &");
    };

    // ---- SlotMap e SlotMap32 (associativos por handle) ----
    template <typename S, typename T>
    struct HandleChecks
    {
        typedef typename S::handle_type H;
        static_assert(is_same<decltype(declval<const S &>().get(declval<H>())), const T *>::value, "get const");
        static_assert(is_same<decltype(declval<S &>().get(declval<H>())), T *>::value, "get");
        static_assert(is_same<decltype(declval<const S &>()[declval<H>()]), const T &>::value, "operator[] const");
        static_assert(is_same<decltype(*declval<const S &>().begin()), const T &>::value, "begin() const");
        static_assert(is_same<decltype(*declval<S &>().begin()), T &>::value, "begin()");
    };
    template struct HandleChecks<ct::SlotMap<Small>, Small>;
    template struct HandleChecks<ct::SlotMap32<Small>, Small>;

    // Um Span e uma vista (como o span da biblioteca standard): o const do proprio Span nao passa aos elementos; o que protege e o Span<const T>.
    static_assert(is_same<decltype(declval<const ct::Span<const int> &>()[0]), const int &>::value, "Span<const T>::operator[]");
    static_assert(is_same<decltype(*declval<const ct::Span<const int> &>().begin()), const int &>::value, "Span<const T>::begin");
    static_assert(is_same<decltype(declval<const ct::Span<const int> &>().front()), const int &>::value, "Span<const T>::front");
    static_assert(is_same<decltype(declval<const ct::Span<const int> &>().back()), const int &>::value, "Span<const T>::back");
    static_assert(is_same<decltype(declval<const ct::Span<const int> &>().data()), const int *>::value, "Span<const T>::data");
    static_assert(is_same<decltype(declval<const ct::Span<int> &>()[0]), int &>::value, "Span<T> e uma vista: o const do Span nao passa");

    // As tres disposicoes do HashMap estao todas aqui (o numero da disposicao vem do tipo)
    static_assert(ct::detail::HashMode<int, int>::value == 0, "int->int: entrada inline");
    static_assert(ct::detail::HashMode<int, Small>::value == 0, "int->Small: entrada inline");
    static_assert(ct::detail::HashMode<ct::String, Small>::value == 0, "String->Small: entrada inline");
    static_assert(ct::detail::HashMode<int, Big>::value == 1, "int->Big: chave no slot e valor no slab");
    static_assert(ct::detail::HashMode<ct::String, Big>::value == 1, "String->Big: chave no slot e valor no slab");
    static_assert(ct::detail::HashMode<Key64, Big>::value == 2, "Key64->Big: o slot e um ponteiro para a entrada");

    template struct MapChecks<ct::HashMap<int, int>, int, int, false>;
    template struct MapChecks<ct::HashMap<int, Small>, int, Small, true>;
    template struct MapChecks<ct::HashMap<ct::String, Small>, ct::String, Small, true>;
    template struct MapChecks<ct::HashMap<int, Big>, int, Big, true>;
    template struct MapChecks<ct::HashMap<ct::String, Big>, ct::String, Big, true>;
    template struct MapChecks<ct::HashMap<Key64, Big>, Key64, Big, true>;
    template struct MapChecks<ct::BTreeMap<int, int>, int, int, false>;
    template struct MapChecks<ct::BTreeMap<int, Small>, int, Small, true>;
    template struct MapChecks<ct::BTreeMap<ct::String, Big>, ct::String, Big, true>;
    template struct MapChecks<ct::FlatMap<int, Small>, int, Small, true>;
    template struct MapChecks<ct::FlatMap<ct::String, Big>, ct::String, Big, true>;
    template struct CbeginChecks<ct::HashMap<int, Small>>;
    template struct CbeginChecks<ct::HashMap<int, Big>>;
    template struct CbeginChecks<ct::HashMap<Key64, Big>>;
    template struct CbeginChecks<ct::BTreeMap<int, Small>>;
    template struct TreeChecks<ct::TreeMap<int, Small>, int, Small, true>;
    template struct TreeChecks<ct::TreeMap<ct::String, Big>, ct::String, Big, true>;
    template struct SetChecks<ct::HashSet<int>, int>;
    template struct SetChecks<ct::HashSet<ct::String>, ct::String>;
    template struct SetChecks<ct::BTreeSet<int>, int>;
    template struct SetChecks<ct::BTreeSet<ct::String>, ct::String>;
    template struct SeqChecks<ct::Vector<int>, int>;
    template struct SeqChecks<ct::Vector<Small>, Small>;
    template struct SeqChecks<ct::Deque<int>, int>;
    template struct SeqChecks<ct::Array<int, 4>, int>;

    // O que o utilizador via nao muda de tamanho: a entrada e o mapa (x86-64 LP64; os numeros sao os de antes da correccao)
#if defined(__LP64__)
// o CT_DEBUG_ITERATORS=1 junta a versao da tabela aos mapas e conjuntos (ver abi_shared.hpp): o tamanho do contentor so se fixa sem ele
#if defined(CT_DEBUG_ITERATORS)
#define CT_SIZE_NODBG(S, A, ...)
#else
#define CT_SIZE_NODBG(S, A, ...) CT_SIZE(S, A, __VA_ARGS__)
#endif
#define CT_SIZE(S, A, ...) \
    static_assert(sizeof(__VA_ARGS__) == (S) && alignof(__VA_ARGS__) == (A), "o tamanho ou o alinhamento de " #__VA_ARGS__ " mudou")
    CT_SIZE_NODBG(32, 8, ct::HashMap<int, int>);
    CT_SIZE(8, 4, ct::HashMap<int, int>::Entry);
    CT_SIZE_NODBG(32, 8, ct::HashMap<ct::String, int>);
    CT_SIZE(32, 8, ct::HashMap<ct::String, int>::Entry);
    CT_SIZE_NODBG(80, 8, ct::HashMap<int, Big>);
    CT_SIZE(16, 8, ct::HashMap<int, Big>::Entry);
    CT_SIZE_NODBG(80, 8, ct::HashMap<ct::String, Big>);
    CT_SIZE(32, 8, ct::HashMap<ct::String, Big>::Entry);
    CT_SIZE_NODBG(80, 8, ct::HashMap<Key64, Big>);
    CT_SIZE(320, 4, ct::HashMap<Key64, Big>::Entry);
    CT_SIZE_NODBG(32, 8, ct::HashSet<int>);
    CT_SIZE_NODBG(32, 8, ct::HashSet<ct::String>);
    CT_SIZE_NODBG(80, 8, ct::BTreeMap<int, int>);
    CT_SIZE(8, 4, ct::BTreeMap<int, int>::Entry);
    CT_SIZE(280, 8, ct::BTreeMap<ct::String, Big>::Entry);
    CT_SIZE_NODBG(80, 8, ct::BTreeSet<int>);
    CT_SIZE(112, 8, ct::TreeMap<int, int>);
    CT_SIZE(40, 8, ct::TreeMap<int, int>::Entry);
    CT_SIZE(312, 8, ct::TreeMap<ct::String, Big>::Entry);
    CT_SIZE(24, 8, ct::FlatMap<int, int>);
    CT_SIZE(8, 4, ct::FlatMap<int, int>::Entry);
    CT_SIZE(280, 8, ct::FlatMap<ct::String, Big>::Entry);
#undef CT_SIZE
#undef CT_SIZE_NODBG
#endif

    // ---- uma passagem real por cada disposicao: o que se le por const e o que se escreveu, e o que se escreve por um
    // iterador nao const chega ao mapa ----
    template <typename M, typename V>
    void exercise_int_keys()
    {
        M m;
        const int n = 500;
        for (int i = 0; i < n; ++i)
        {
            V v = V();
            v.x = i;
            m.put(i, v);
        }
        const M &cm = m;
        long sum = 0;
        int seen = 0;
        for (auto &e : cm) // e e uma entrada const: so se le
        {
            sum += e.key + e.value.x;
            ++seen;
        }
        EXPECT_EQ(seen, n);
        EXPECT_EQ(sum, 2L * (n - 1) * n / 2);

        for (auto &e : m) // por um mapa nao const o valor escreve-se
            e.value.x += 1;
        sum = 0;
        for (typename M::const_iterator it = m.begin(); it != cm.end(); ++it) // iterator converte em const_iterator
            sum += it->key + it->value.x;
        EXPECT_EQ(sum, 2L * (n - 1) * n / 2 + n);

        const V *p = cm.find(7);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->x, 8);
        EXPECT_EQ(cm.get(7, V()).x, 8);
        // o endereco que o mapa const mostra e o do mesmo valor
        EXPECT_EQ(&cm.begin()->value, &m.begin()->value);
        EXPECT_EQ(&cm.find(7)->x, &m.find(7)->x);
    }

    TEST(ConstAccess, HashMapInlineEntry)
    {
        exercise_int_keys<ct::HashMap<int, Small>, Small>();
    }
    TEST(ConstAccess, HashMapKeyInSlotValueInSlab) { exercise_int_keys<ct::HashMap<int, Big>, Big>(); }
    TEST(ConstAccess, BTreeMap) { exercise_int_keys<ct::BTreeMap<int, Big>, Big>(); }
    TEST(ConstAccess, FlatMap) { exercise_int_keys<ct::FlatMap<int, Big>, Big>(); }

    TEST(ConstAccess, HashMapPointerSlot)
    {
        typedef ct::HashMap<Key64, Big> M;
        M m;
        const int n = 300;
        for (int i = 0; i < n; ++i)
        {
            Big b = Big();
            b.x = i;
            m.put(key64(i), b);
        }
        const M &cm = m;
        long sum = 0;
        for (auto &e : cm)
        {
            int k;
            memcpy(&k, e.key.b, sizeof k);
            sum += k + e.value.x;
        }
        EXPECT_EQ(sum, 2L * (n - 1) * n / 2);
        for (auto &e : m)
            e.value.x += 2;
        sum = 0;
        for (M::const_iterator it = m.begin(); it != cm.end(); ++it)
            sum += it->value.x;
        EXPECT_EQ(sum, 1L * (n - 1) * n / 2 + 2L * n);
        const Big *p = cm.find(key64(9));
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->x, 11);
    }

    TEST(ConstAccess, HashMapStringKeys)
    {
        typedef ct::HashMap<ct::String, Big> M;
        M m;
        for (int i = 0; i < 100; ++i)
        {
            Big b = Big();
            b.x = i;
            m.put(ct::String::number(i), b);
        }
        const M &cm = m;
        long sum = 0;
        for (auto &e : cm)
            sum += e.value.x + static_cast<long>(e.key.size());
        EXPECT_EQ(sum, 99L * 100 / 2 + 10 * 1 + 90 * 2);
    }

    TEST(ConstAccess, TreeMap)
    {
        ct::TreeMap<int, Small> t;
        for (int i = 0; i < 100; ++i)
        {
            Small s = Small();
            s.x = i;
            t.put(i, s);
        }
        long sum = 0;
        for (auto &e : t)
        {
            e.value.x += 1;
            sum += e.key + e.value.x;
        }
        EXPECT_EQ(sum, 2L * 99 * 100 / 2 + 100);
        const ct::TreeMap<int, Small> &ct_ = t;
        ASSERT_NE(ct_.find(5), nullptr);
        EXPECT_EQ(ct_.find(5)->x, 6);
    }

#if __cplusplus >= 201703L
    // Os bindings estruturados (C++17: o ct_tests compila-se em C++14, isto so entra numa unidade compilada com -std=c++17)
    template <typename M, typename K, typename V>
    void structured_bindings(M &m, const K &key)
    {
        m.put(key, V());
        const M &cm = m;
        const auto &[ck, cv] = *cm.begin();
        EXPECT_TRUE(ct::detail::is_const<typename ct::detail::remove_ref<decltype(cv)>::type>::value);
        EXPECT_TRUE(ct::detail::is_const<typename ct::detail::remove_ref<decltype(ck)>::type>::value);
        auto &[mk, mv] = *m.begin();
        EXPECT_FALSE(ct::detail::is_const<typename ct::detail::remove_ref<decltype(mv)>::type>::value);
        EXPECT_TRUE(ct::detail::is_const<typename ct::detail::remove_ref<decltype(mk)>::type>::value);
        mv.x = 5;
        EXPECT_EQ(cm.find(key)->x, 5);
    }
    TEST(ConstAccess, StructuredBindings)
    {
        ct::HashMap<int, Small> a;
        ct::HashMap<int, Big> b;
        ct::HashMap<Key64, Big> c;
        ct::BTreeMap<int, Big> d;
        ct::FlatMap<int, Big> e;
        structured_bindings<ct::HashMap<int, Small>, int, Small>(a, 1);
        structured_bindings<ct::HashMap<int, Big>, int, Big>(b, 1);
        structured_bindings<ct::HashMap<Key64, Big>, Key64, Big>(c, key64(1));
        structured_bindings<ct::BTreeMap<int, Big>, int, Big>(d, 1);
        structured_bindings<ct::FlatMap<int, Big>, int, Big>(e, 1);
    }
#endif

    TEST(ConstAccess, Sets)
    {
        ct::HashSet<int> hs;
        ct::BTreeSet<int> bs;
        for (int i = 0; i < 50; ++i)
        {
            hs.insert(i);
            bs.insert(i);
        }
        long a = 0, b = 0;
        for (auto &k : hs)
            a += k;
        for (auto &k : bs)
            b += k;
        EXPECT_EQ(a, 49L * 50 / 2);
        EXPECT_EQ(b, 49L * 50 / 2);
    }
} // namespace
