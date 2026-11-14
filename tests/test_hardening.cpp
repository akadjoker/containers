// Regressoes de bugs apanhados por sanitizers e por testes de stress. Sem biblioteca standard: so ct e os
// cabecalhos C.

#include <ct/hashmap.hpp>
#include <ct/string.hpp>
#include <ct/thread.hpp>

#include <gtest/gtest.h>

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
            EXPECT_NE(ct::String(text, n + 1).hash(), reference);
    }
    EXPECT_NE(ct::String("ab").hash(), ct::String("ba").hash());
    EXPECT_NE(ct::String().hash(), ct::String("\0", 1).hash());
}
