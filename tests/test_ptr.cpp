#include <ct/ptr.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

using ct::make_rc;
using ct::make_unique;
using ct::Rc;
using ct::Unique;
using ct::Weak;

namespace
{
    struct Contado
    {
        static int vivos;
        static int destruidos;
        int valor;

        explicit Contado(int v = 0) : valor(v) { ++vivos; }
        Contado(const Contado &o) : valor(o.valor) { ++vivos; }
        ~Contado()
        {
            --vivos;
            ++destruidos;
        }
    };
    int Contado::vivos = 0;
    int Contado::destruidos = 0;

    struct BaseVirtual
    {
        virtual ~BaseVirtual() {}
        virtual int quem() const { return 1; }
    };
    struct DerivadaVirtual : BaseVirtual
    {
        static int vivos;
        DerivadaVirtual() { ++vivos; }
        ~DerivadaVirtual() override { --vivos; }
        int quem() const override { return 2; }
    };
    int DerivadaVirtual::vivos = 0;

    // de propósito SEM destrutor virtual
    struct BaseSimples
    {
        int a = 1;
    };
    struct DerivadaSimples : BaseSimples
    {
        static int vivos;
        ct::String texto;
        DerivadaSimples() : texto("uma string grande que vai mesmo ao heap para se notar")
        {
            ++vivos;
        }
        ~DerivadaSimples() { --vivos; }
    };
    int DerivadaSimples::vivos = 0;

    struct Nodo // para o teste dos ciclos
    {
        static int vivos;
        Rc<Nodo> forte;
        Weak<Nodo> fraco;
        Nodo() { ++vivos; }
        ~Nodo() { --vivos; }
    };
    int Nodo::vivos = 0;

    struct alignas(64) Alinhado
    {
        char c;
    };

    void reset_contadores()
    {
        Contado::vivos = 0;
        Contado::destruidos = 0;
    }
}

// ================= Unique =================

TEST(Unique, RaiiBasics)
{
    reset_contadores();
    EXPECT_EQ(sizeof(Unique<Contado>), sizeof(void *)); // sem gordura

    {
        Unique<Contado> u = make_unique<Contado>(7);
        EXPECT_TRUE(static_cast<bool>(u));
        EXPECT_EQ(u->valor, 7);
        EXPECT_EQ((*u).valor, 7);
        EXPECT_EQ(Contado::vivos, 1);
    }
    EXPECT_EQ(Contado::vivos, 0); // saiu do scope, morreu

    Unique<Contado> vazio;
    EXPECT_FALSE(static_cast<bool>(vazio));
    EXPECT_EQ(vazio.get(), nullptr);
    EXPECT_TRUE(vazio == nullptr);
    EXPECT_DEATH(*vazio, "ponteiro vazio");
    EXPECT_DEATH(vazio->valor, "ponteiro vazio");
}

TEST(Unique, MoveSemantics)
{
    reset_contadores();
    Unique<Contado> a = make_unique<Contado>(1);
    Contado *cru = a.get();

    Unique<Contado> b(std::move(a));
    EXPECT_EQ(a.get(), nullptr); // o movido fica vazio
    EXPECT_EQ(b.get(), cru);
    EXPECT_EQ(Contado::vivos, 1);

    Unique<Contado> c = make_unique<Contado>(2);
    EXPECT_EQ(Contado::vivos, 2);
    c = std::move(b); // assign larga o que tinha
    EXPECT_EQ(Contado::vivos, 1);
    EXPECT_EQ(c->valor, 1);

    c = std::move(c); // self-move não pode destruir nada
    EXPECT_EQ(Contado::vivos, 1);
    EXPECT_EQ(c->valor, 1);

    Contado *libertado = c.release();
    EXPECT_EQ(c.get(), nullptr);
    EXPECT_EQ(Contado::vivos, 1); // release não destrói
    Unique<Contado>::adopt(libertado);
    EXPECT_EQ(Contado::vivos, 0);
}

TEST(Unique, ResetAndSwap)
{
    reset_contadores();
    Unique<Contado> a = make_unique<Contado>(1);
    Unique<Contado> b = make_unique<Contado>(2);
    a.swap(b);
    EXPECT_EQ(a->valor, 2);
    EXPECT_EQ(b->valor, 1);

    a.reset();
    EXPECT_EQ(Contado::vivos, 1);
    a.reset(); // duas vezes não rebenta
    EXPECT_EQ(a.get(), nullptr);
    b.reset();
    EXPECT_EQ(Contado::vivos, 0);
}

TEST(Unique, UpcastComDestrutorVirtual)
{
    DerivadaVirtual::vivos = 0;
    {
        Unique<DerivadaVirtual> d = make_unique<DerivadaVirtual>();
        EXPECT_EQ(DerivadaVirtual::vivos, 1);
        Unique<BaseVirtual> b(std::move(d));
        EXPECT_EQ(d.get(), nullptr);
        EXPECT_EQ(b->quem(), 2); // continua a ser a derivada
    }
    EXPECT_EQ(DerivadaVirtual::vivos, 0);
}

TEST(Unique, DentroDeUmVector)
{
    reset_contadores();
    {
        ct::Vector<Unique<Contado>> v; // move-only dentro do nosso Vector
        for (int i = 0; i < 100; ++i)
            v.push_back(make_unique<Contado>(i)); // cresce e realoca pelo meio
        EXPECT_EQ(Contado::vivos, 100);
        EXPECT_EQ(v[42]->valor, 42);
        v.pop_back();
        EXPECT_EQ(Contado::vivos, 99);
    }
    EXPECT_EQ(Contado::vivos, 0);
}

// ================= Rc =================

TEST(Rc, ContagemEDestruicao)
{
    reset_contadores();
    EXPECT_EQ(sizeof(Rc<Contado>), 2 * sizeof(void *));

    {
        Rc<Contado> a = make_rc<Contado>(5);
        EXPECT_EQ(a.use_count(), 1u);
        EXPECT_TRUE(a.unique());
        {
            Rc<Contado> b = a;
            EXPECT_EQ(a.use_count(), 2u);
            EXPECT_FALSE(a.unique());
            EXPECT_EQ(b->valor, 5);
            EXPECT_TRUE(a == b);
            EXPECT_EQ(Contado::vivos, 1); // uma cópia do ponteiro, não do objeto
        }
        EXPECT_EQ(a.use_count(), 1u);
        EXPECT_EQ(Contado::vivos, 1);
    }
    EXPECT_EQ(Contado::vivos, 0);

    Rc<Contado> vazio;
    EXPECT_FALSE(static_cast<bool>(vazio));
    EXPECT_EQ(vazio.use_count(), 0u);
    EXPECT_TRUE(vazio == nullptr);
    EXPECT_DEATH(*vazio, "ponteiro vazio");
    EXPECT_DEATH(vazio->valor, "ponteiro vazio");
}

TEST(Rc, AtribuicoesIncluindoAsProprias)
{
    reset_contadores();
    Rc<Contado> a = make_rc<Contado>(1);
    Rc<Contado> b = make_rc<Contado>(2);
    EXPECT_EQ(Contado::vivos, 2);

    b = a; // larga o 2, fica com o 1
    EXPECT_EQ(Contado::vivos, 1);
    EXPECT_EQ(a.use_count(), 2u);
    EXPECT_EQ(b->valor, 1);

    a = a; // auto-atribuição não pode matar o objeto
    EXPECT_EQ(a.use_count(), 2u);
    EXPECT_EQ(a->valor, 1);
    EXPECT_EQ(Contado::vivos, 1);

    Rc<Contado> c = std::move(a);
    EXPECT_EQ(a.get(), nullptr);
    EXPECT_EQ(c.use_count(), 2u); // b e c
    c.reset();
    EXPECT_EQ(b.use_count(), 1u);
    b.reset();
    EXPECT_EQ(Contado::vivos, 0);
}

TEST(Rc, UpcastDestroiOTipoOriginalSemVirtual)
{
    // o bloco de controlo guarda como destruir o tipo com que foi criado, por isso
    // isto funciona mesmo sem destrutor virtual (o unique_ptr/delete não funcionaria)
    DerivadaSimples::vivos = 0;
    {
        Rc<BaseSimples> b;
        {
            Rc<DerivadaSimples> d = make_rc<DerivadaSimples>();
            EXPECT_EQ(DerivadaSimples::vivos, 1);
            b = d; // upcast
            EXPECT_EQ(b.use_count(), 2u);
            EXPECT_EQ(b->a, 1);
        }
        EXPECT_EQ(DerivadaSimples::vivos, 1); // o b ainda o segura
        EXPECT_EQ(b.use_count(), 1u);
    }
    EXPECT_EQ(DerivadaSimples::vivos, 0); // ~DerivadaSimples correu (e a String foi liberta)

    DerivadaVirtual::vivos = 0;
    {
        Rc<BaseVirtual> b = make_rc<DerivadaVirtual>();
        EXPECT_EQ(b->quem(), 2);
    }
    EXPECT_EQ(DerivadaVirtual::vivos, 0);
}

TEST(Rc, Alinhamento)
{
    Rc<Alinhado> a = make_rc<Alinhado>();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a.get()) % 64u, 0u);
    Weak<Alinhado> w = a;
    a.reset();
    EXPECT_TRUE(w.expired()); // o bloco continua acessível para o Weak
}

TEST(Rc, DentroDeUmVector)
{
    reset_contadores();
    {
        ct::Vector<Rc<Contado>> v;
        Rc<Contado> partilhado = make_rc<Contado>(9);
        for (int i = 0; i < 50; ++i)
            v.push_back(partilhado);
        EXPECT_EQ(partilhado.use_count(), 51u);
        EXPECT_EQ(Contado::vivos, 1);
        v.clear();
        EXPECT_EQ(partilhado.use_count(), 1u);
    }
    EXPECT_EQ(Contado::vivos, 0);
}

// ================= Weak =================

TEST(Weak, ObservaSemSegurar)
{
    reset_contadores();
    Weak<Contado> w;
    EXPECT_TRUE(w.expired());
    EXPECT_EQ(w.lock().get(), nullptr);
    EXPECT_EQ(w.use_count(), 0u);

    {
        Rc<Contado> a = make_rc<Contado>(3);
        w = a;
        EXPECT_FALSE(w.expired());
        EXPECT_EQ(w.use_count(), 1u); // o weak não conta como dono

        Rc<Contado> b = w.lock();
        ASSERT_TRUE(static_cast<bool>(b));
        EXPECT_EQ(b->valor, 3);
        EXPECT_EQ(a.use_count(), 2u); // o lock passa a ser dono enquanto vive
    }

    EXPECT_EQ(Contado::vivos, 0); // o weak não impediu a morte
    EXPECT_TRUE(w.expired());
    EXPECT_EQ(w.lock().get(), nullptr); // e não te dá um ponteiro pendurado
    EXPECT_EQ(w.use_count(), 0u);
}

TEST(Weak, CopiasMovesEReset)
{
    reset_contadores();
    Rc<Contado> a = make_rc<Contado>(1);
    Weak<Contado> w1 = a;
    Weak<Contado> w2 = w1;
    Weak<Contado> w3;
    w3 = w1;
    Weak<Contado> w4(std::move(w2));
    EXPECT_FALSE(w4.expired());

    w1 = w1; // auto-atribuição
    EXPECT_FALSE(w1.expired());
    w3.reset();
    w3.reset();
    EXPECT_TRUE(w3.expired());

    a.reset();
    EXPECT_EQ(Contado::vivos, 0);
    EXPECT_TRUE(w1.expired());
    EXPECT_TRUE(w4.expired());
}

TEST(Weak, MuitosWeaksSobrevivemAoObjeto)
{
    reset_contadores();
    ct::Vector<Weak<Contado>> weaks;
    {
        Rc<Contado> a = make_rc<Contado>(1);
        for (int i = 0; i < 100; ++i)
            weaks.push_back(Weak<Contado>(a));
        EXPECT_EQ(a.use_count(), 1u);
        for (const Weak<Contado> &w : weaks)
            EXPECT_FALSE(w.expired());
    }
    EXPECT_EQ(Contado::vivos, 0); // o objeto morreu com os 100 weaks vivos
    for (const Weak<Contado> &w : weaks)
        EXPECT_TRUE(w.expired());
    weaks.clear(); // e agora é que o bloco é libertado (o ASan confirma)
}

TEST(Weak, QuebraCiclos)
{
    Nodo::vivos = 0;
    {
        // dois Rc a apontarem um para o outro: ninguém morre (é o preço do refcount)
        Rc<Nodo> a = make_rc<Nodo>();
        Rc<Nodo> b = make_rc<Nodo>();
        a->forte = b;
        b->forte = a;
        EXPECT_EQ(Nodo::vivos, 2);
    }
    EXPECT_EQ(Nodo::vivos, 2); // ciclo: fugiram os dois, de propósito neste teste
    EXPECT_EQ(Nodo::vivos, 2);

    Nodo::vivos = 0;
    {
        // com o de trás fraco, o ciclo desfaz-se
        Rc<Nodo> pai = make_rc<Nodo>();
        Rc<Nodo> filho = make_rc<Nodo>();
        pai->forte = filho;  // pai segura o filho
        filho->fraco = pai;  // filho só observa o pai
        EXPECT_EQ(Nodo::vivos, 2);
        EXPECT_FALSE(filho->fraco.expired());
    }
    EXPECT_EQ(Nodo::vivos, 0);
}
