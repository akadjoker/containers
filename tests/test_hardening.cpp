// Regressoes de bugs apanhados por sanitizers e por testes de stress. Sem biblioteca standard: so ct e os
// cabecalhos C.

#include <ct/string.hpp>

#include <gtest/gtest.h>

#include <string.h>

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
