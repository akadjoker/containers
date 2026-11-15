// Regressoes de bugs apanhados por sanitizers e por testes de stress. Sem biblioteca standard: so ct e os
// cabecalhos C.

#include <ct/hashmap.hpp>
#include <ct/json.hpp>
#include <ct/string.hpp>
#include <ct/thread.hpp>
#include <ct/vector.hpp>
#include <ct/xml.hpp>

#include <gtest/gtest.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <signal.h>
#endif

TEST(Hardening, AtribuirPonteiroNuloAUmaStringEsvaziaSemPassarNuloAoMemmove)
{
    // Falha sob UBSan antes da correcao: "null pointer passed as argument 2, which is declared to never be null"
    // (assign chamava memmove(data(), nullptr, 0)).
    const char *nada = nullptr;
    ct::String s("abc");
    s = nada;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.c_str()[0], '\0');

    ct::String longa("uma string bem mais comprida do que cabe no SSO de vinte e tres bytes");
    longa = nada;
    EXPECT_TRUE(longa.empty());
    EXPECT_EQ(longa.c_str()[0], '\0');

    ct::String vazia;
    vazia.assign(nullptr, 0);
    EXPECT_TRUE(vazia.empty());
}

#if !defined(_WIN32)
namespace
{
    void ignore_signal(int) {}
}

TEST(Hardening, SleepMsDormeOTempoTodoMesmoQueUmSinalOInterrompa)
{
    // Antes da correcao o nanosleep devolvia EINTR e sleep_ms voltava logo: um sleep_ms(300) interrompido aos 30 ms
    // dormia 30 ms.
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = ignore_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    struct sigaction previous;
    ASSERT_EQ(sigaction(SIGUSR2, &action, &previous), 0);

    const pthread_t target = pthread_self();
    ct::Thread sender([target] {
        for (int i = 0; i < 3; ++i)
        {
            ct::Thread::sleep_ms(30);
            pthread_kill(target, SIGUSR2);
        }
    });
    const uint64_t t0 = ct::Thread::monotonic_ms();
    ct::Thread::sleep_ms(300);
    const uint64_t elapsed = ct::Thread::monotonic_ms() - t0;
    sender.join();
    sigaction(SIGUSR2, &previous, nullptr);
    EXPECT_GE(elapsed, 299u);
}
#endif

namespace
{
    // Chi-quadrado por grau de liberdade da ocupacao dos baldes (so os bits baixos, que e o que a mascara usa) para
    // chaves que so diferem num numero. Uma boa funcao fica perto de 1; o FNV-1a que havia dava 1.3 a 1.4 em varios destes formatos.
    double bucket_chi2(const char *format)
    {
        enum
        {
            kBuckets = 1 << 14,
            kKeys = 1 << 18
        };
        static int count[kBuckets];
        memset(count, 0, sizeof(count));
        for (int i = 0; i < kKeys; ++i)
        {
            char key[96];
            const int n = snprintf(key, sizeof(key), format, i);
            ++count[ct::String(key, static_cast<size_t>(n)).hash() & (kBuckets - 1)];
        }
        const double mean = static_cast<double>(kKeys) / kBuckets;
        double chi = 0;
        for (int i = 0; i < kBuckets; ++i)
        {
            const double d = count[i] - mean;
            chi += d * d / mean;
        }
        return chi / (kBuckets - 1);
    }
}

TEST(Hardening, HashDeStringsDistribuiNosBitsBaixosParaChavesQueSoDiferemNumNumero)
{
    EXPECT_LT(bucket_chi2("item-%d"), 1.25);
    EXPECT_LT(bucket_chi2("/home/user/M\xc3\xba" "sica/V\xc3\xad" "deos/clip_%08d.mp4"), 1.25);
    EXPECT_LT(bucket_chi2("%d"), 1.25);
    EXPECT_LT(bucket_chi2("k%d_x"), 1.25);
    EXPECT_LT(bucket_chi2("a%da"), 1.25);
    EXPECT_LT(bucket_chi2("track %d of 99"), 1.25);
}

TEST(Hardening, HashDeStringsDependeDoComprimentoEDosNULsENaoDoAlinhamento)
{
    const char text[] = "0123456789abcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t n = 0; n <= 40; ++n)
    {
        const uint64_t reference = ct::String(text, n).hash();
        char shifted[64 + 8];
        for (size_t offset = 0; offset < 8; ++offset)
        {
            memcpy(shifted + offset, text, n);
            EXPECT_EQ(ct::detail::hash_bytes(shifted + offset, n), reference) << "n " << n << " offset " << offset;
        }
        if (n)
        {
            ct::String longer(text, n);
            longer.push_back('\0');
            EXPECT_NE(longer.hash(), reference) << "um NUL no fim muda o hash, n " << n;
        }
        if (n < 40)
        {
            EXPECT_NE(ct::String(text, n + 1).hash(), reference);
        }
    }
    EXPECT_NE(ct::String("ab").hash(), ct::String("ba").hash());
    EXPECT_NE(ct::String().hash(), ct::String("\0", 1).hash());
}

#include <ct/stream.hpp>

namespace
{
    // Um stream que so da 1 a 3 bytes de cada vez e nao sabe o seu tamanho: read_all tem de juntar tudo.
    class Trickle : public ct::Stream
    {
    public:
        Trickle(size_t total, uint32_t seed) : total_(total), pos_(0), seed_(seed) {}
        size_t read(void *dst, size_t n) override
        {
            if (pos_ >= total_)
                return 0;
            seed_ = seed_ * 1664525u + 1013904223u;
            size_t take = 1 + (seed_ >> 24) % 3;
            if (take > n)
                take = n;
            if (take > total_ - pos_)
                take = total_ - pos_;
            for (size_t i = 0; i < take; ++i)
                static_cast<unsigned char *>(dst)[i] = byte_at(pos_ + i);
            pos_ += take;
            return take;
        }
        size_t write(const void *, size_t) override { return 0; }
        bool seek(int64_t, ct::Seek) override { return false; }
        int64_t tell() const override { return static_cast<int64_t>(pos_); }
        int64_t size() const override { return -1; }
        void close() override {}
        bool is_open() const override { return true; }
        bool eof() const override { return pos_ >= total_; }
        bool can_read() const override { return true; }
        bool can_write() const override { return false; }
        bool can_seek() const override { return false; }
        static unsigned char byte_at(size_t i) { return static_cast<unsigned char>(i * 31u + (i >> 8)); }

    private:
        size_t total_, pos_;
        uint32_t seed_;
    };
}

TEST(Hardening, ReadAllDeVectorJuntaTudoDeUmStreamQueDaPoucosBytesDeCadaVez)
{
    const size_t sizes[] = {0, 1, 4095, 4096, 4097, 20000, 70000};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        Trickle stream(sizes[i], static_cast<uint32_t>(i + 1));
        ct::Vector<uint8_t> all;
        all.push_back(0xAA); // read_all começa por limpar
        ASSERT_TRUE(stream.read_all(all));
        ASSERT_EQ(all.size(), sizes[i]);
        for (size_t k = 0; k < all.size(); ++k)
            ASSERT_EQ(all[k], Trickle::byte_at(k)) << "tamanho " << sizes[i] << " byte " << k;
    }
}

TEST(Hardening, ReadAllDeVectorDeUmaMemoriaSemFimDesconhecidoEDeUmaParteDelaDaOTamanhoCerto)
{
    ct::Vector<uint8_t> blob;
    for (size_t i = 0; i < 100000; ++i)
        blob.push_back(static_cast<uint8_t>(i * 7u));
    ct::MemoryStream whole(blob.data(), blob.size());
    ct::Vector<uint8_t> a;
    ASSERT_TRUE(whole.read_all(a));
    ASSERT_EQ(a.size(), blob.size());
    EXPECT_EQ(memcmp(a.data(), blob.data(), blob.size()), 0);

    ct::MemoryStream rest(blob.data(), blob.size());
    ASSERT_TRUE(rest.seek(99000, ct::Seek::Set));
    ct::Vector<uint8_t> b;
    ASSERT_TRUE(rest.read_all(b));
    ASSERT_EQ(b.size(), 1000u);
    EXPECT_EQ(memcmp(b.data(), blob.data() + 99000, 1000), 0);
}

// ---- CondVar::wait_for_ms ----------------------------------------------------------------------------------------

namespace
{
    struct Gate
    {
        ct::Mutex mutex;
        ct::CondVar cv;
        bool open;
        Gate() : open(false) {}
    };
}

TEST(CondVarTimed, SemNotificacaoDevolveFalseDepoisDoPrazo)
{
    Gate g;
    ct::LockGuard lock(g.mutex);
    const uint64_t t0 = ct::Thread::monotonic_ms();
    EXPECT_FALSE(g.cv.wait_for_ms(g.mutex, 80, [&] { return g.open; }));
    const uint64_t elapsed = ct::Thread::monotonic_ms() - t0;
    EXPECT_GE(elapsed, 79u);
    EXPECT_LT(elapsed, 2000u);

    // sem predicado: um prazo que acaba devolve false (a menos de um despertar espurio, raro)
    const uint64_t t1 = ct::Thread::monotonic_ms();
    bool woke = true;
    for (int tries = 0; tries < 5 && woke; ++tries)
        woke = g.cv.wait_for_ms(g.mutex, 30);
    EXPECT_FALSE(woke);
    EXPECT_GE(ct::Thread::monotonic_ms() - t1, 29u);
}

TEST(CondVarTimed, NotifyAcordaAntesDoPrazo)
{
    Gate g;
    ct::Thread opener([&g] {
        ct::Thread::sleep_ms(30);
        {
            ct::LockGuard lock(g.mutex);
            g.open = true;
        }
        g.cv.notify_all();
    });
    {
        ct::LockGuard lock(g.mutex);
        const uint64_t t0 = ct::Thread::monotonic_ms();
        EXPECT_TRUE(g.cv.wait_for_ms(g.mutex, 10000, [&] { return g.open; }));
        EXPECT_LT(ct::Thread::monotonic_ms() - t0, 5000u);
    }
    opener.join();
}

TEST(CondVarTimed, PrazoZeroNaoBloqueiaEPredicadoJaVerdadeiroDevolveTrue)
{
    Gate g;
    ct::LockGuard lock(g.mutex);
    const uint64_t t0 = ct::Thread::monotonic_ms();
    EXPECT_FALSE(g.cv.wait_for_ms(g.mutex, 0, [&] { return g.open; }));
    g.open = true;
    EXPECT_TRUE(g.cv.wait_for_ms(g.mutex, 0, [&] { return g.open; }));
    EXPECT_TRUE(g.cv.wait_for_ms(g.mutex, 5000, [&] { return g.open; }));
    EXPECT_LT(ct::Thread::monotonic_ms() - t0, 1000u);
}

TEST(CondVarTimed, VariosNotificadoresEUmEsperadorComPrazoLongoNaoPerdemAcordares)
{
    // O esperador so pode sair quando o contador chega a 50; os notificadores incrementam e notificam em ciclos.
    Gate g;
    int counter = 0;
    ct::Thread a([&] { for (int i = 0; i < 25; ++i) { { ct::LockGuard l(g.mutex); ++counter; } g.cv.notify_one(); ct::Thread::yield(); } });
    ct::Thread b([&] { for (int i = 0; i < 25; ++i) { { ct::LockGuard l(g.mutex); ++counter; } g.cv.notify_one(); ct::Thread::yield(); } });
    {
        ct::LockGuard lock(g.mutex);
        EXPECT_TRUE(g.cv.wait_for_ms(g.mutex, 20000, [&] { return counter == 50; }));
    }
    a.join();
    b.join();
    EXPECT_EQ(counter, 50);
}

// ---- Function<void(...)> com chamaveis que devolvem valor -------------------------------------------------------

#include <ct/function.hpp>

namespace
{
    int g_calls = 0;
    int bump() { return ++g_calls; }
    struct Counter
    {
        int total;
        int operator()(int x) { total += x; return total; }
    };
}

TEST(Hardening, FunctionVoidAceitaChamaveisQueDevolvemValor)
{
    // Antes da correcao nada disto compilava: "return-statement with a value, in function returning void".
    g_calls = 0;
    ct::Function<void()> from_lambda = [] { return bump(); };
    ct::Function<void()> from_pointer = bump;
    from_lambda();
    from_pointer();
    EXPECT_EQ(g_calls, 2);

    Counter c = {0};
    ct::Function<void(int)> from_functor = c;
    from_functor(5);
    from_functor(7);
    ct::Function<void(int)> from_lambda_arg = [](int x) { return x * 2; };
    from_lambda_arg(3);

    ct::String s;
    ct::Function<void(const char *)> append = [&s](const char *t) { return &s.append(t); };
    append("ab");
    append("cd");
    EXPECT_EQ(s, "abcd");

    ct::Function<void()> copy(from_lambda);
    ct::Function<void()> moved(ct::move(copy));
    moved();
    EXPECT_EQ(g_calls, 3);

    // os que ja funcionavam continuam: conversao do valor devolvido
    ct::Function<long(int)> widen = [](int x) { return x; };
    EXPECT_EQ(widen(9), 9L);
}

namespace
{
    // Objeto que detecta o uso de um elemento ja destruido (a copia de um destruido conta como erro).
    struct Mortal
    {
        static int live;
        static int used_after_destroy;
        int id;
        unsigned magic;
        Mortal() : id(0), magic(0xC0FFEEu) { ++live; }
        explicit Mortal(int v) : id(v), magic(0xC0FFEEu) { ++live; }
        Mortal(const Mortal &o) : id(o.id), magic(0xC0FFEEu)
        {
            if (o.magic != 0xC0FFEEu)
                ++used_after_destroy;
            ++live;
        }
        Mortal(Mortal &&o) noexcept : id(o.id), magic(0xC0FFEEu)
        {
            if (o.magic != 0xC0FFEEu)
                ++used_after_destroy;
            ++live;
        }
        Mortal &operator=(const Mortal &o)
        {
            if (magic != 0xC0FFEEu || o.magic != 0xC0FFEEu)
                ++used_after_destroy;
            id = o.id;
            return *this;
        }
        Mortal &operator=(Mortal &&o) noexcept
        {
            if (magic != 0xC0FFEEu || o.magic != 0xC0FFEEu)
                ++used_after_destroy;
            id = o.id;
            return *this;
        }
        ~Mortal()
        {
            magic = 0xDEADu;
            --live;
        }
    };
    int Mortal::live = 0;
    int Mortal::used_after_destroy = 0;
}

TEST(Hardening, VectorAssignComUmElementoDoProprioVetor)
{
    // Antes da correcao assign(n, value) fazia clear() (destruia value) e so depois resize(n, value) copiava-o:
    // uso depois de destruir (ASan: heap-use-after-free com Vector<String>; aqui o Mortal conta as copias de um
    // objeto destruido).
    Mortal::live = 0;
    Mortal::used_after_destroy = 0;
    {
        ct::Vector<Mortal> v;
        for (int i = 0; i < 5; ++i)
            v.emplace_back(i + 10);
        v.assign(7, v[2]);
        ASSERT_EQ(v.size(), 7u);
        for (size_t i = 0; i < v.size(); ++i)
            EXPECT_EQ(v[i].id, 12);
        v.assign(2, v[1]);
        ASSERT_EQ(v.size(), 2u);
        EXPECT_EQ(v[0].id, 12);
        EXPECT_EQ(v[1].id, 12);
        v.assign(40, v[0]);
        ASSERT_EQ(v.size(), 40u);
        EXPECT_EQ(v[39].id, 12);
        v.assign(0, v[0]);
        EXPECT_TRUE(v.empty());
    }
    EXPECT_EQ(Mortal::used_after_destroy, 0);
    EXPECT_EQ(Mortal::live, 0);

    ct::Vector<ct::String> s;
    for (int i = 0; i < 4; ++i)
    {
        ct::String t("uma string maior do que cabe no SSO de vinte e tres bytes #");
        t.append_number(i);
        s.push_back(t);
    }
    s.assign(9, s[3]);
    ASSERT_EQ(s.size(), 9u);
    for (size_t i = 0; i < s.size(); ++i)
        EXPECT_TRUE(s[i] == "uma string maior do que cabe no SSO de vinte e tres bytes #3");
}

TEST(Hardening, JsonAtribuirPorMoveUmFilhoDoProprioDocumento)
{
    // Antes da correcao operator=(Json&&) destruia o valor atual (e com ele o filho que se estava a mover) antes
    // de lhe ler o conteudo: heap-use-after-free sob ASan; sem sanitizer o filho lia-se de memoria ja libertada.
    const char *text = "{\"a\":{\"x\":\"uma string comprida que nao cabe no SSO, nem de longe\",\"y\":[1,2,3]},"
                       "\"s\":\"outra string comprida, igualmente fora do SSO de vinte e tres bytes\",\"n\":7}";
    ct::Json j = ct::Json::parse(text);
    ASSERT_TRUE(j.is_object());
    const ct::String inner = j["a"].dump();
    j = ct::move(j["a"]);
    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(j.dump(), inner);
    EXPECT_EQ(j["y"].size(), 3u);
    EXPECT_EQ(j["y"][2].as_int(), 3);

    ct::Json s = ct::Json::parse(text);
    s = ct::move(s["s"]);
    ASSERT_TRUE(s.is_string());
    EXPECT_TRUE(s.str() == "outra string comprida, igualmente fora do SSO de vinte e tres bytes");

    ct::Json arr = ct::Json::parse("[[1,2],[\"um texto bem comprido para sair do SSO da String\",{\"k\":[true,null]}],5]");
    arr = ct::move(arr[1]);
    ASSERT_TRUE(arr.is_array());
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_TRUE(arr[0].str() == "um texto bem comprido para sair do SSO da String");
    EXPECT_EQ(arr[1].dump(), "{\"k\":[true,null]}");

    ct::Json deep = ct::Json::parse("[[[[[[\"fundo, fundo, fundo, fundo, fundo, fundo\"]]]]]]");
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_TRUE(deep.is_array());
        deep = ct::move(deep[0]);
    }
    ASSERT_TRUE(deep.is_string());
    EXPECT_TRUE(deep.str() == "fundo, fundo, fundo, fundo, fundo, fundo");

    ct::Json self = ct::Json::parse("[1,2]");
    ct::Json &alias = self;
    self = ct::move(alias);
    EXPECT_EQ(self.dump(), "[1,2]");
}

TEST(Hardening, XmlAtribuirPorMoveUmFilhoDoProprioDocumento)
{
    // Antes da correcao operator=(Xml&&) apagava os filhos (e com eles o proprio argumento) antes de lhe ler o
    // ponteiro dos filhos: heap-use-after-free sob ASan; sem sanitizer o resultado vinha sem os netos.
    ct::Xml x = ct::Xml::parse("<r a=\"1\"><item k=\"um valor de atributo comprido, fora do SSO\">"
                               "<sub>texto comprido dentro do sub-elemento, fora do SSO</sub><sub n=\"2\"/></item>"
                               "<other/></r>");
    ASSERT_TRUE(x.tag() == "r");
    ASSERT_EQ(x.children().size(), 2u);
    const ct::String esperado = x.children()[0].dump();
    x = ct::move(x.children()[0]);
    EXPECT_TRUE(x.tag() == "item");
    EXPECT_TRUE(x.attr_cstr("k") == ct::String("um valor de atributo comprido, fora do SSO"));
    ASSERT_EQ(x.children().size(), 2u);
    EXPECT_TRUE(x.children()[0].text() == "texto comprido dentro do sub-elemento, fora do SSO");
    EXPECT_EQ(x.dump(), esperado);

    ct::Xml chain = ct::Xml::parse("<a><b><c><d>fim da cadeia, com texto fora do SSO</d></c></b></a>");
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_EQ(chain.children().size(), 1u);
        chain = ct::move(chain.children()[0]);
    }
    EXPECT_TRUE(chain.tag() == "d");
    EXPECT_TRUE(chain.text() == "fim da cadeia, com texto fora do SSO");
}

TEST(Hardening, AtomicIncrementoNoLimiteDaVoltaSemOverflowComSinal)
{
    // Antes da correcao ++, --, += e -= calculavam o valor devolvido como fetch_add(d) + d em T: com int no maximo
    // era um overflow com sinal (UBSan: "signed integer overflow: 2147483647 + 1 cannot be represented in type
    // 'int'"), apesar de a operacao atomica em si dar a volta.
    ct::Atomic<int> a(INT_MAX);
    EXPECT_EQ(++a, INT_MIN);
    EXPECT_EQ(a.load(), INT_MIN);
    EXPECT_EQ(--a, INT_MAX);
    EXPECT_EQ(a++, INT_MAX);
    EXPECT_EQ(a.load(), INT_MIN);
    EXPECT_EQ(a--, INT_MIN);
    EXPECT_EQ(a.load(), INT_MAX);
    EXPECT_EQ(a += 2, INT_MIN + 1);
    EXPECT_EQ(a -= 3, INT_MAX - 1);
    EXPECT_EQ(a += INT_MAX, -3);

    ct::Atomic<long long> b(LLONG_MAX);
    EXPECT_EQ(++b, LLONG_MIN);
    EXPECT_EQ(b -= 1, LLONG_MAX);

    ct::Atomic<int8_t> c(127);
    EXPECT_EQ(++c, -128);
    EXPECT_EQ(--c, 127);
    ct::Atomic<uint32_t> d(0xFFFFFFFFu);
    EXPECT_EQ(++d, 0u);
    EXPECT_EQ(--d, 0xFFFFFFFFu);
}

TEST(Hardening, AtomicFetchAddEFetchSubDePonteirosAvancamEmElementos)
{
    // Antes da correcao fetch_add e fetch_sub recebiam um T (um ponteiro, em Atomic<int *>): p.fetch_add(3) nao
    // compilava ("invalid conversion from 'int' to 'int*'") e o unico modo de os chamar somava bytes
    // (__atomic_fetch_add num ponteiro soma bytes), ao contrario de ++ e +=. Agora recebem a distancia.
    int values[32] = {};
    ct::Atomic<int *> p(values);
    EXPECT_EQ(p.fetch_add(3), values);
    EXPECT_EQ(p.load(), values + 3);
    EXPECT_EQ(p.fetch_add(2, ct::MemoryOrder::Relaxed), values + 3);
    EXPECT_EQ(p.fetch_sub(4), values + 5);
    EXPECT_EQ(p.load(), values + 1);
    EXPECT_EQ(p.fetch_add(-1, ct::MemoryOrder::AcqRel), values + 1);
    EXPECT_EQ(p.load(), values);

    struct Wide
    {
        char bytes[24];
    } wide[8];
    ct::Atomic<Wide *> q(wide);
    EXPECT_EQ(q.fetch_add(5, ct::MemoryOrder::Release), wide);
    EXPECT_EQ(reinterpret_cast<char *>(q.load()) - reinterpret_cast<char *>(wide), 5 * 24);
    EXPECT_EQ(q.fetch_sub(2), wide + 5);
    EXPECT_EQ(q.load(), wide + 3);

    // quatro threads reservam posicoes de um buffer com fetch_add: cada posicao sai uma vez
    static int claimed[4 * 4000];
    memset(claimed, 0, sizeof(claimed));
    ct::Atomic<int *> cursor(claimed);
    ct::Vector<ct::Thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back(ct::Thread::Fn([&] {
            for (int k = 0; k < 4000; ++k)
                ++*cursor.fetch_add(1);
        }));
    for (size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    EXPECT_EQ(cursor.load(), claimed + 4 * 4000);
    int wrong = 0;
    for (int i = 0; i < 4 * 4000; ++i)
        wrong += claimed[i] != 1;
    EXPECT_EQ(wrong, 0);
}
