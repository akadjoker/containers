// Regressoes de bugs apanhados por sanitizers e por testes de stress. Sem biblioteca standard: so ct e os
// cabecalhos C.

#include <ct/string.hpp>
#include <ct/thread.hpp>

#include <gtest/gtest.h>

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
