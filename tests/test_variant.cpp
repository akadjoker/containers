#include <ct/variant.hpp>
#include <ct/function.hpp>
#include <ct/ptr.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

using ct::Rc;
using ct::String;
using ct::Unique;
using ct::Variant;

namespace
{
    using V = Variant<int, double, String>;

    struct Stringify
    {
        String operator()(int x) const { return String::number(x); }
        String operator()(double) const { return String("double"); }
        String operator()(const String &s) const { return s; }
    };

    struct CopyCounter
    {
        static int copies;
        int value;
        explicit CopyCounter(int v) : value(v) {}
        CopyCounter(const CopyCounter &o) : value(o.value) { ++copies; }
        CopyCounter(CopyCounter &&) noexcept = default;
    };
    int CopyCounter::copies = 0;
}

TEST(Variant, DefaultConstructsFirstAlternative)
{
    V v;
    EXPECT_EQ(v.index(), 0u);
    EXPECT_TRUE(v.is<int>());
    EXPECT_EQ(v.get<int>(), 0);
}

TEST(Variant, ConstructFromAlternative)
{
    V a = 42;
    EXPECT_TRUE(a.is<int>());
    EXPECT_EQ(a.get<int>(), 42);

    V b = 3.5;
    EXPECT_TRUE(b.is<double>());
    EXPECT_EQ(b.get<double>(), 3.5);

    V c = String("ola");
    EXPECT_TRUE(c.is<String>());
    EXPECT_EQ(c.get<String>(), "ola");
}

TEST(Variant, AssignSwitchesActiveType)
{
    V v = 1;
    EXPECT_TRUE(v.is<int>());
    v = 2.0;
    EXPECT_TRUE(v.is<double>());
    v = String("s");
    EXPECT_TRUE(v.is<String>());
    v = 5; 
    EXPECT_TRUE(v.is<int>());
    EXPECT_EQ(v.get<int>(), 5);
}

TEST(Variant, GetIf)
{
    V v = 42;
    EXPECT_NE(v.get_if<int>(), nullptr);
    EXPECT_EQ(v.get_if<double>(), nullptr);
    EXPECT_EQ(v.get_if<String>(), nullptr);
    *v.get_if<int>() = 7;
    EXPECT_EQ(v.get<int>(), 7);
}

TEST(Variant, WrongGetIsFatal)
{
    V v = 42;
    EXPECT_DEATH(v.get<String>(), "");
}

TEST(Variant, CopyIsDeep)
{
    V a = String("uma string bem comprida para forcar heap na String!!");
    V b = a;
    b.get<String>().push_back('!');
    EXPECT_NE(a.get<String>(), b.get<String>());
}

TEST(Variant, CopyAssignReplacesActiveType)
{
    V a = 42;
    V b = String("x");
    b = a;
    EXPECT_TRUE(b.is<int>());
    EXPECT_EQ(b.get<int>(), 42);
}

TEST(Variant, MoveLeavesSourceInSameAlternativeButMovedFrom)
{
    V a = String("string longa o suficiente para ir para o heap, de certeza!!");
    V b = ct::detail::move(a);
    EXPECT_TRUE(b.is<String>());
    EXPECT_EQ(b.get<String>(), "string longa o suficiente para ir para o heap, de certeza!!");
    EXPECT_TRUE(a.is<String>()); 
}

TEST(Variant, Swap)
{
    V a = 1;
    V b = String("s");
    a.swap(b);
    EXPECT_TRUE(a.is<String>());
    EXPECT_TRUE(b.is<int>());
    EXPECT_EQ(b.get<int>(), 1);
}

TEST(Variant, VisitDispatchesToActiveType)
{
    Stringify f;
    V v = 42;
    EXPECT_EQ(v.visit(f), "42");
    v = 1.5;
    EXPECT_EQ(v.visit(f), "double");
    v = String("hi");
    EXPECT_EQ(v.visit(f), "hi");
}

TEST(Variant, VisitOnConstVariant)
{
    Stringify f;
    const V v = String("hi");
    EXPECT_EQ(v.visit(f), "hi");
}

TEST(Variant, CopyConstructsTargetOnce)
{
    CopyCounter::copies = 0;
    Variant<int, CopyCounter> a = CopyCounter(3);
    CopyCounter::copies = 0;
    Variant<int, CopyCounter> b = a; 
    EXPECT_EQ(CopyCounter::copies, 1);
    EXPECT_EQ(b.get<CopyCounter>().value, 3);
}

TEST(Variant, TwoAlternativesTypeAtWorks)
{
    Variant<int, int> nope = 1; 

    EXPECT_EQ(nope.index(), 0u);
    EXPECT_EQ(nope.get<int>(), 1);
}

TEST(Variant, SingleAlternative)
{
    Variant<int> v = 5;
    EXPECT_EQ(v.get<int>(), 5);
    v = 6;
    EXPECT_EQ(v.get<int>(), 6);
}
// ---- atribuir a partir de um membro do que o proprio Variant guarda ------------------------------------------------

namespace
{
    // Listas ligadas em que o proximo no e um membro do no que o cursor guarda: a origem da atribuicao vive dentro da
    // alternativa que a atribuicao vai substituir.
    struct NoRc
    {
        static int vivos;
        Variant<int, Rc<NoRc>> next;
        String rotulo;
        NoRc() { ++vivos; }
        ~NoRc() { --vivos; }
    };
    int NoRc::vivos = 0;

    struct NoUnico
    {
        static int vivos;
        Variant<int, Unique<NoUnico>> next;
        String rotulo;
        NoUnico() { ++vivos; }
        ~NoUnico() { --vivos; }
    };
    int NoUnico::vivos = 0;

    // Dois tipos de no que se alternam: todos os passos mudam o indice (Rc<NoA> para Rc<NoB> e de volta).
    struct NoA;
    struct NoB;
    typedef Variant<int, Rc<NoA>, Rc<NoB>> Elo;
    struct NoA
    {
        static int vivos;
        Elo next;
        String rotulo;
        NoA() { ++vivos; }
        ~NoA() { --vivos; }
    };
    struct NoB
    {
        static int vivos;
        Elo next;
        String rotulo;
        NoB() { ++vivos; }
        ~NoB() { --vivos; }
    };
    int NoA::vivos = 0;
    int NoB::vivos = 0;

    const char *const kRotulo = "um rotulo que nao cabe no SSO da String, para o no ter memoria propria";

    // cabeca -> n1 -> n2 -> n3 -> 42; so a cabeca (o Variant devolvido) e dona do primeiro no.
    Variant<int, Rc<NoRc>> faz_lista_rc()
    {
        Rc<NoRc> n3 = ct::make_rc<NoRc>();
        n3->next = 42;
        n3->rotulo = kRotulo;
        Rc<NoRc> n2 = ct::make_rc<NoRc>();
        n2->next = n3;
        n2->rotulo = kRotulo;
        Rc<NoRc> n1 = ct::make_rc<NoRc>();
        n1->next = n2;
        n1->rotulo = kRotulo;
        return Variant<int, Rc<NoRc>>(n1);
    }

    // a1 -> b1 -> a2 -> b2 -> 99
    Elo faz_lista_alternada()
    {
        Rc<NoB> b2 = ct::make_rc<NoB>();
        b2->next = 99;
        b2->rotulo = kRotulo;
        Rc<NoA> a2 = ct::make_rc<NoA>();
        a2->next = b2;
        a2->rotulo = kRotulo;
        Rc<NoB> b1 = ct::make_rc<NoB>();
        b1->next = a2;
        b1->rotulo = kRotulo;
        Rc<NoA> a1 = ct::make_rc<NoA>();
        a1->next = b1;
        a1->rotulo = kRotulo;
        return Elo(a1);
    }
}

TEST(VariantAtribuir, PercorrerUmaListaDeRcPorCopia)
{
    // Antes de o Variant copiar a origem antes de destruir a alternativa actual, isto lia memoria libertada (ASan:
    // heap-use-after-free) e, sem ASan, o no seguinte ja tinha sido destruido com o anterior (vivos dava 0).
    NoRc::vivos = 0;
    {
        Variant<int, Rc<NoRc>> cursor = faz_lista_rc();
        ASSERT_EQ(NoRc::vivos, 3);
        // Rc para Rc: mesmo indice, atribuicao no sitio
        cursor = cursor.get<Rc<NoRc>>()->next;
        ASSERT_EQ(NoRc::vivos, 2);
        ASSERT_EQ(cursor.index(), 1u);
        cursor = cursor.get<Rc<NoRc>>()->next;
        ASSERT_EQ(NoRc::vivos, 1);
        ASSERT_EQ(cursor.index(), 1u);
        // Rc para int: o indice muda e a origem (o int) vive no no que a atribuicao vai libertar
        cursor = cursor.get<Rc<NoRc>>()->next;
        EXPECT_EQ(NoRc::vivos, 0);
        ASSERT_EQ(cursor.index(), 0u);
        EXPECT_EQ(cursor.get<int>(), 42);
    }
    EXPECT_EQ(NoRc::vivos, 0);
}

TEST(VariantAtribuir, PercorrerUmaListaDeRcPorMove)
{
    NoRc::vivos = 0;
    {
        Variant<int, Rc<NoRc>> cursor = faz_lista_rc();
        ASSERT_EQ(NoRc::vivos, 3);
        cursor = ct::move(cursor.get<Rc<NoRc>>()->next);
        ASSERT_EQ(NoRc::vivos, 2);
        ASSERT_EQ(cursor.index(), 1u);
        cursor = ct::move(cursor.get<Rc<NoRc>>()->next);
        ASSERT_EQ(NoRc::vivos, 1);
        ASSERT_EQ(cursor.index(), 1u);
        cursor = ct::move(cursor.get<Rc<NoRc>>()->next);
        EXPECT_EQ(NoRc::vivos, 0);
        ASSERT_EQ(cursor.index(), 0u);
        EXPECT_EQ(cursor.get<int>(), 42);
    }
    EXPECT_EQ(NoRc::vivos, 0);
}

TEST(VariantAtribuir, PercorrerUmaListaDeUniqueSoPorMove)
{
    NoUnico::vivos = 0;
    {
        Unique<NoUnico> n3 = ct::make_unique<NoUnico>();
        n3->next = 7;
        n3->rotulo = kRotulo;
        Unique<NoUnico> n2 = ct::make_unique<NoUnico>();
        n2->next = ct::move(n3);
        n2->rotulo = kRotulo;
        Unique<NoUnico> n1 = ct::make_unique<NoUnico>();
        n1->next = ct::move(n2);
        n1->rotulo = kRotulo;
        Variant<int, Unique<NoUnico>> cursor(ct::move(n1));
        ASSERT_EQ(NoUnico::vivos, 3);
        // Unique para Unique: atribuicao no sitio, pelo operator= do Unique
        cursor = ct::move(cursor.get<Unique<NoUnico>>()->next);
        ASSERT_EQ(NoUnico::vivos, 2);
        ASSERT_EQ(cursor.index(), 1u);
        cursor = ct::move(cursor.get<Unique<NoUnico>>()->next);
        ASSERT_EQ(NoUnico::vivos, 1);
        ASSERT_EQ(cursor.index(), 1u);
        // Unique para int
        cursor = ct::move(cursor.get<Unique<NoUnico>>()->next);
        EXPECT_EQ(NoUnico::vivos, 0);
        ASSERT_EQ(cursor.index(), 0u);
        EXPECT_EQ(cursor.get<int>(), 7);
    }
    EXPECT_EQ(NoUnico::vivos, 0);
}

TEST(VariantAtribuir, ListaEmQueTodosOsPassosMudamDeIndice)
{
    // Rc<NoA> para Rc<NoB>, Rc<NoB> para Rc<NoA>, e por fim Rc<NoB> para int: as duas direcoes, por copia e por move.
    NoA::vivos = 0;
    NoB::vivos = 0;
    {
        Elo cursor = faz_lista_alternada();
        ASSERT_EQ(NoA::vivos + NoB::vivos, 4);
        ASSERT_EQ(cursor.index(), 1u);
        cursor = cursor.get<Rc<NoA>>()->next; // A para B
        ASSERT_EQ(cursor.index(), 2u);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 3);
        cursor = cursor.get<Rc<NoB>>()->next; // B para A
        ASSERT_EQ(cursor.index(), 1u);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 2);
        cursor = cursor.get<Rc<NoA>>()->next; // A para B
        ASSERT_EQ(cursor.index(), 2u);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 1);
        cursor = cursor.get<Rc<NoB>>()->next; // B para int
        ASSERT_EQ(cursor.index(), 0u);
        EXPECT_EQ(cursor.get<int>(), 99);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 0);
    }
    {
        Elo cursor = faz_lista_alternada();
        cursor = ct::move(cursor.get<Rc<NoA>>()->next);
        ASSERT_EQ(cursor.index(), 2u);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 3);
        cursor = ct::move(cursor.get<Rc<NoB>>()->next);
        ASSERT_EQ(cursor.index(), 1u);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 2);
        cursor = ct::move(cursor.get<Rc<NoA>>()->next);
        ASSERT_EQ(cursor.index(), 2u);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 1);
        cursor = ct::move(cursor.get<Rc<NoB>>()->next);
        ASSERT_EQ(cursor.index(), 0u);
        EXPECT_EQ(cursor.get<int>(), 99);
        EXPECT_EQ(NoA::vivos + NoB::vivos, 0);
    }
    EXPECT_EQ(NoA::vivos + NoB::vivos, 0);
}

TEST(VariantAtribuir, SemAliasingEAutoAtribuicaoContinuamIguais)
{
    Variant<int, String> a(String("uma string com mais de vinte e tres bytes de comprimento, na memoria dinamica"));
    Variant<int, String> b(5);
    b = a;
    EXPECT_EQ(b.index(), 1u);
    a = 3;
    EXPECT_EQ(a.get<int>(), 3);
    a = b;
    EXPECT_TRUE(a.get<String>() == b.get<String>());
    Variant<int, String> &mesmo = a;
    a = mesmo;
    a = ct::move(mesmo);
    EXPECT_TRUE(a.get<String>() == "uma string com mais de vinte e tres bytes de comprimento, na memoria dinamica");
}

namespace
{
    // Conta cada construcao, copia, move, atribuicao e destruicao; Tag separa os contadores de tipos diferentes.
    template <int Tag>
    struct Conta
    {
        static int vivos, construidos, copias, moves, atrib_copias, atrib_moves, destruidos;
        int v;
        explicit Conta(int x) : v(x)
        {
            ++construidos;
            ++vivos;
        }
        Conta(const Conta &o) : v(o.v)
        {
            ++construidos;
            ++copias;
            ++vivos;
        }
        Conta(Conta &&o) noexcept : v(o.v)
        {
            ++construidos;
            ++moves;
            ++vivos;
        }
        Conta &operator=(const Conta &o)
        {
            v = o.v;
            ++atrib_copias;
            return *this;
        }
        Conta &operator=(Conta &&o) noexcept
        {
            v = o.v;
            ++atrib_moves;
            return *this;
        }
        ~Conta()
        {
            ++destruidos;
            --vivos;
        }
        // zera as contas das operacoes; vivos nao se zera (diz quantos objectos existem)
        static void zera() { construidos = copias = moves = atrib_copias = atrib_moves = destruidos = 0; }
    };
    template <int Tag> int Conta<Tag>::vivos = 0;
    template <int Tag> int Conta<Tag>::construidos = 0;
    template <int Tag> int Conta<Tag>::copias = 0;
    template <int Tag> int Conta<Tag>::moves = 0;
    template <int Tag> int Conta<Tag>::atrib_copias = 0;
    template <int Tag> int Conta<Tag>::atrib_moves = 0;
    template <int Tag> int Conta<Tag>::destruidos = 0;

    // Como Conta, mas sem operator= (membro const): o Variant tem de destruir e reconstruir.
    template <int Tag>
    struct ContaFixa
    {
        static int vivos, construidos, copias, moves, destruidos;
        const int v;
        explicit ContaFixa(int x) : v(x)
        {
            ++construidos;
            ++vivos;
        }
        ContaFixa(const ContaFixa &o) : v(o.v)
        {
            ++construidos;
            ++copias;
            ++vivos;
        }
        ContaFixa(ContaFixa &&o) noexcept : v(o.v)
        {
            ++construidos;
            ++moves;
            ++vivos;
        }
        ~ContaFixa()
        {
            ++destruidos;
            --vivos;
        }
        static void zera() { construidos = copias = moves = destruidos = 0; }
    };
    template <int Tag> int ContaFixa<Tag>::vivos = 0;
    template <int Tag> int ContaFixa<Tag>::construidos = 0;
    template <int Tag> int ContaFixa<Tag>::copias = 0;
    template <int Tag> int ContaFixa<Tag>::moves = 0;
    template <int Tag> int ContaFixa<Tag>::destruidos = 0;

    typedef Conta<0> C0;
    typedef Conta<1> C1;
    typedef ContaFixa<2> F2;
    typedef ContaFixa<3> F3;
    typedef Variant<int, C0, C1> VC;
    typedef Variant<int, F2, F3> VF;

    void zera_tudo()
    {
        C0::zera();
        C1::zera();
        F2::zera();
        F3::zera();
    }
}

TEST(VariantAtribuir, MesmoIndiceAtribuiNoSitio)
{
    zera_tudo();
    {
        VC a{C0(1)};
        VC b{C0(2)};
        zera_tudo();

        a = b; // copia: um operator= da alternativa, nada mais
        EXPECT_EQ(C0::atrib_copias, 1);
        EXPECT_EQ(C0::atrib_moves, 0);
        EXPECT_EQ(C0::construidos, 0);
        EXPECT_EQ(C0::destruidos, 0);
        EXPECT_EQ(a.get<C0>().v, 2);

        VC t{C0(3)};
        zera_tudo();
        b = ct::move(t); // move: um operator= de move, nada mais
        EXPECT_EQ(C0::atrib_moves, 1);
        EXPECT_EQ(C0::atrib_copias, 0);
        EXPECT_EQ(C0::copias + C0::moves, 0);
        EXPECT_EQ(C0::construidos, 0);
        EXPECT_EQ(C0::destruidos, 0);
        EXPECT_EQ(b.get<C0>().v, 3);
    }
    EXPECT_EQ(C0::vivos, 0);
}

TEST(VariantAtribuir, IndiceDiferenteComAlternativaActualComDestrutorPassaPorUmLocal)
{
    zera_tudo();
    {
        VC a{C0(1)};
        VC c{C1(7)};
        zera_tudo();

        a = c; // copia para um local, destroi o C0, move o local para o sitio, destroi o local
        EXPECT_EQ(C1::copias, 1);
        EXPECT_EQ(C1::moves, 1);
        EXPECT_EQ(C1::construidos, 2);
        EXPECT_EQ(C1::destruidos, 1); // o local, ja movido
        EXPECT_EQ(C1::atrib_copias + C1::atrib_moves, 0);
        EXPECT_EQ(C0::destruidos, 1); // a alternativa que la estava
        EXPECT_EQ(C0::construidos, 0);
        ASSERT_EQ(a.index(), 2u);
        EXPECT_EQ(a.get<C1>().v, 7);
        EXPECT_EQ(c.get<C1>().v, 7);

        zera_tudo();
        VC d{C0(5)};
        zera_tudo();
        a = ct::move(d); // move: dois moves de C0 (para o local e para o sitio); o C1 que la estava e destruido
        EXPECT_EQ(C0::moves, 2);
        EXPECT_EQ(C0::copias, 0);
        EXPECT_EQ(C0::construidos, 2);
        EXPECT_EQ(C0::destruidos, 1); // o local, ja movido
        EXPECT_EQ(C1::destruidos, 1); // o C1 que a tinha
        ASSERT_EQ(a.index(), 1u);
        EXPECT_EQ(a.get<C0>().v, 5);
    }
    EXPECT_EQ(C0::vivos, 0);
    EXPECT_EQ(C1::vivos, 0);
}

TEST(VariantAtribuir, IndiceDiferenteComAlternativaActualSemDestrutorConstroiDirecto)
{
    zera_tudo();
    {
        VC a{3}; // int: nao pode ser dona de nada
        VC c{C1(8)};
        zera_tudo();
        a = c;
        EXPECT_EQ(C1::copias, 1);
        EXPECT_EQ(C1::moves, 0);
        EXPECT_EQ(C1::construidos, 1);
        EXPECT_EQ(C1::destruidos, 0);
        ASSERT_EQ(a.index(), 2u);
        EXPECT_EQ(a.get<C1>().v, 8);

        VC d{4};
        VC e{C1(9)};
        zera_tudo();
        d = ct::move(e);
        EXPECT_EQ(C1::moves, 1);
        EXPECT_EQ(C1::copias, 0);
        EXPECT_EQ(C1::construidos, 1);
        EXPECT_EQ(C1::destruidos, 0);
        EXPECT_EQ(d.get<C1>().v, 9);
    }
    EXPECT_EQ(C1::vivos, 0);
}

TEST(VariantAtribuir, AlternativaSemOperatorIgualDestroiEReconstroi)
{
    zera_tudo();
    {
        VF a{F2(1)};
        VF b{F2(2)};
        zera_tudo();
        a = b; // mesmo indice mas sem operator=: tem destrutor, passa por um local
        EXPECT_EQ(F2::copias, 1);
        EXPECT_EQ(F2::moves, 1);
        EXPECT_EQ(F2::construidos, 2);
        EXPECT_EQ(F2::destruidos, 2); // o antigo e o local
        EXPECT_EQ(a.get<F2>().v, 2);

        a = VF(F2(3));
        EXPECT_EQ(a.get<F2>().v, 3);
        EXPECT_EQ(F2::vivos, 2); // a e b

        a = VF(F3(4)); // outro indice
        EXPECT_EQ(a.index(), 2u);
        EXPECT_EQ(a.get<F3>().v, 4);
    }
    EXPECT_EQ(F2::vivos, 0);
    EXPECT_EQ(F3::vivos, 0);
}

TEST(VariantAtribuir, EscalaresContinuamAAtribuir)
{
    // Sem alternativas que possam ser donas de um Variant nunca ha local (a mascara e 0 e o ramo desaparece)
    static_assert(ct::detail::VariantOwnsMask<int, double, char>::value == 0, "escalares nao sao donos de nada");
    static_assert(ct::detail::VariantOwnsMask<int, String>::value == 0, "a String so e dona de bytes");
    static_assert(ct::detail::VariantOwnsMask<int, String, Rc<int>>::value == 4, "so o Rc (indice 2) pode ser dono");
    Variant<int, double, char> a(1);
    Variant<int, double, char> b(2.5);
    a = b;
    EXPECT_EQ(a.get<double>(), 2.5);
    b = Variant<int, double, char>('x');
    EXPECT_EQ(b.get<char>(), 'x');
    a = b;
    a = Variant<int, double, char>(7);
    EXPECT_EQ(a.get<int>(), 7);
}

// ---- que alternativas podem ser donas do Variant: detail::variant_leaf ------------------------------------------------

namespace
{
    template <int Tag>
    struct ContaFolha;
}

namespace ct
{
    namespace detail
    {
        // um tipo do utilizador que garante que so e dono de bytes: declara-se folha
        template <int Tag>
        struct variant_leaf<::ContaFolha<Tag>> : integral_constant<bool, (Tag % 2) == 0>
        {
        };
    }
}

namespace
{
    template <int Tag>
    struct ContaFolha
    {
        static int vivos, copias, moves, destruidos;
        int v;
        explicit ContaFolha(int x) : v(x) { ++vivos; }
        ContaFolha(const ContaFolha &o) : v(o.v)
        {
            ++copias;
            ++vivos;
        }
        ContaFolha(ContaFolha &&o) noexcept : v(o.v)
        {
            ++moves;
            ++vivos;
        }
        ContaFolha &operator=(const ContaFolha &) = delete;
        ~ContaFolha()
        {
            ++destruidos;
            --vivos;
        }
        static void zera() { copias = moves = destruidos = 0; }
    };
    template <int Tag> int ContaFolha<Tag>::vivos = 0;
    template <int Tag> int ContaFolha<Tag>::copias = 0;
    template <int Tag> int ContaFolha<Tag>::moves = 0;
    template <int Tag> int ContaFolha<Tag>::destruidos = 0;
}

TEST(VariantFolha, OTracoPorTipo)
{
    using ct::detail::variant_leaf;
    // destrutor trivial: nao e dono de nada
    static_assert(variant_leaf<int>::value, "int");
    static_assert(variant_leaf<double>::value, "double");
    static_assert(variant_leaf<char>::value, "char");
    static_assert(variant_leaf<void *>::value, "ponteiro");
    static_assert(variant_leaf<const char *>::value, "ponteiro para const");
    static_assert(variant_leaf<NoRc *>::value, "um ponteiro para um no nao e dono dele");
    // String: so e dona de bytes
    static_assert(variant_leaf<String>::value, "String");
    // Vector: so e dono do bloco e dos elementos, por isso e folha se os elementos o forem
    static_assert(variant_leaf<ct::Vector<int>>::value, "Vector<int>");
    static_assert(variant_leaf<ct::Vector<String>>::value, "Vector<String>");
    static_assert(variant_leaf<ct::Vector<ct::Vector<String>>>::value, "Vector<Vector<String>>");
    static_assert(variant_leaf<ct::Vector<NoRc *>>::value, "Vector de ponteiros");
    // o que pode ser dono de um no (e do Variant que ele tem dentro) nao e folha
    static_assert(!variant_leaf<Rc<NoRc>>::value, "Rc");
    static_assert(!variant_leaf<Unique<NoUnico>>::value, "Unique");
    static_assert(!variant_leaf<ct::Vector<Rc<NoRc>>>::value, "Vector<Rc>");
    static_assert(!variant_leaf<ct::Vector<Unique<NoUnico>>>::value, "Vector<Unique>");
    static_assert(!variant_leaf<ct::Vector<ct::Vector<Rc<NoRc>>>>::value, "Vector<Vector<Rc>>");
    static_assert(!variant_leaf<ct::Function<void()>>::value, "Function: guarda um callable que pode capturar um no");
    static_assert(!variant_leaf<NoRc>::value, "um struct com destrutor e um Variant dentro");
    static_assert(!variant_leaf<Variant<int, Rc<NoRc>>>::value, "um Variant e dono do que as suas alternativas forem");
    static_assert(!variant_leaf<Variant<int, String>>::value, "e um Variant de folhas tambem fica de fora (seguro)");
    // o utilizador declara: ContaFolha<2> e folha e ContaFolha<3> nao (o traco acima, por Tag par)
    static_assert(variant_leaf<ContaFolha<2>>::value, "especializado");
    static_assert(!variant_leaf<ContaFolha<3>>::value, "especializado a falso");
    static_assert(ct::detail::VariantOwnsMask<int, ContaFolha<2>, ContaFolha<3>, String>::value == 4,
                  "so o indice 2 (ContaFolha<3>)");
    // o move de String e de Vector e destruir e construir por move: o Variant nao os atribui no sitio ao mover
    static_assert(ct::detail::variant_move_rebuilds<String>::value, "String");
    static_assert(ct::detail::variant_move_rebuilds<ct::Vector<int>>::value, "Vector<int>");
    static_assert(!ct::detail::variant_move_rebuilds<ContaFolha<2>>::value, "por omissao atribui-se no sitio");
    // atribuicao escrita como destruir e construir (nenhuma alternativa pede mais) ou pela cadeia
    using ct::detail::VariantPlainCopy;
    using ct::detail::VariantPlainMove;
    static_assert(VariantPlainCopy<int, double>::value, "escalares");
    static_assert(VariantPlainMove<int, double>::value, "escalares");
    static_assert(!VariantPlainCopy<int, double, String>::value, "a String copia-se no sitio");
    static_assert(VariantPlainMove<int, double, String>::value, "a String move-se como destruir e construir");
    static_assert(!VariantPlainMove<int, Rc<NoRc>>::value, "o Rc pode ser dono da origem");
    static_assert(VariantPlainCopy<int, ContaFolha<2>>::value, "ContaFolha<2> e folha e nao se sabe atribuir");
    static_assert(!VariantPlainCopy<int, ContaFolha<3>>::value, "ContaFolha<3> nao e folha");
    SUCCEED();
}

TEST(VariantFolha, UmaAlternativaFolhaDestroiEConstroiDirectoENaoPassaPorUmLocal)
{
    typedef Variant<int, ContaFolha<2>, ContaFolha<3>> V;
    ContaFolha<2>::zera();
    ContaFolha<3>::zera();
    {
        V a{ContaFolha<2>(1)}; // folha
        V b{ContaFolha<3>(2)}; // nao folha
        V c{ContaFolha<2>(3)};
        ContaFolha<2>::zera();
        ContaFolha<3>::zera();

        a = b; // actual folha (2): destroi e copia directo, sem local
        EXPECT_EQ(ContaFolha<3>::copias, 1);
        EXPECT_EQ(ContaFolha<3>::moves, 0);
        EXPECT_EQ(ContaFolha<2>::destruidos, 1);
        EXPECT_EQ(ContaFolha<3>::destruidos, 0);
        ASSERT_EQ(a.index(), 2u);
        EXPECT_EQ(a.get<ContaFolha<3>>().v, 2);

        ContaFolha<2>::zera();
        ContaFolha<3>::zera();
        a = c; // actual nao folha (3): passa por um local (1 copia, 1 move, 2 destruicoes)
        EXPECT_EQ(ContaFolha<2>::copias, 1);
        EXPECT_EQ(ContaFolha<2>::moves, 1);
        EXPECT_EQ(ContaFolha<2>::destruidos, 1); // o local
        EXPECT_EQ(ContaFolha<3>::destruidos, 1); // o que la estava
        ASSERT_EQ(a.index(), 1u);
        EXPECT_EQ(a.get<ContaFolha<2>>().v, 3);

        ContaFolha<2>::zera();
        ContaFolha<3>::zera();
        V d{ContaFolha<2>(9)};
        ContaFolha<2>::zera();
        a = ct::move(d); // actual folha (2), mesmo indice mas sem operator= (apagado): destroi e move directo
        EXPECT_EQ(ContaFolha<2>::moves, 1);
        EXPECT_EQ(ContaFolha<2>::copias, 0);
        EXPECT_EQ(ContaFolha<2>::destruidos, 1);
        EXPECT_EQ(a.get<ContaFolha<2>>().v, 9);
    }
    EXPECT_EQ(ContaFolha<2>::vivos, 0);
    EXPECT_EQ(ContaFolha<3>::vivos, 0);
}
