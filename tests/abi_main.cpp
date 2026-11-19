// Teste de ABI: duas unidades da mesma ligacao, uma com NDEBUG e outra com -UNDEBUG (e, no segundo executavel, as duas com
// CT_DEBUG_ITERATORS=1), usam os mesmos tipos da ct. Tem de concordar no tamanho, no alinhamento e na posicao de cada membro, e um
// objecto preenchido numa unidade tem de ler-se na outra. Antes de o layout deixar de depender de NDEBUG, o HashMap e o HashSet
// ganhavam 8 bytes na unidade sem NDEBUG: a mesma classe com tamanhos diferentes em duas unidades corrompia memoria em silencio.
// Nao usa a biblioteca standard.
#include "abi_shared.hpp"

#include <stdio.h>

namespace abi
{
    void fill_a(Box &);
    void fill_b(Box &);
    bool check_a(const Box &);
    bool check_b(const Box &);
    Report report_a();
    Report report_b();
}

int main()
{
    int bad = 0;
    abi::Report a = abi::report_a();
    abi::Report b = abi::report_b();
    abi::Report c = abi::make_report_t(2, 0); // esta unidade, compilada com as opcoes por omissao
    printf("NDEBUG: A=%llu B=%llu C=%llu\n", a.ndebug, b.ndebug, c.ndebug);
    if (a.ndebug != 1 || b.ndebug != 0)
    {
        printf("as unidades nao estao compiladas com opcoes diferentes\n");
        ++bad;
    }
    if (a.box_size != b.box_size || a.box_align != b.box_align || a.box_size != c.box_size)
    {
        printf("Box: A %llu/%llu, B %llu/%llu, C %llu/%llu\n", a.box_size, a.box_align, b.box_size, b.box_align, c.box_size, c.box_align);
        ++bad;
    }
    for (int i = 0; i < 15; ++i)
        if (a.size[i] != b.size[i] || a.align[i] != b.align[i] || a.offset[i] != b.offset[i] || a.size[i] != c.size[i] ||
            a.offset[i] != c.offset[i])
        {
            printf("membro %d: A %llu/%llu/%llu, B %llu/%llu/%llu, C %llu/%llu/%llu\n", i, a.size[i], a.align[i], a.offset[i], b.size[i],
                   b.align[i], b.offset[i], c.size[i], c.align[i], c.offset[i]);
            ++bad;
        }
    // um mapa preenchido numa unidade le-se na outra, e a outra pode continuar a escreve-lo
    abi::Box *boxa = new abi::Box;
    abi::Box *boxb = new abi::Box;
    abi::fill_a(*boxa);
    abi::fill_b(*boxb);
    if (!abi::check_b(*boxa))
    {
        printf("o Box preenchido em A nao se le em B\n");
        ++bad;
    }
    if (!abi::check_a(*boxb))
    {
        printf("o Box preenchido em B nao se le em A\n");
        ++bad;
    }
    // copiar, mover e apagar os de outra unidade
    abi::Box copy_of_a_in_c;
    copy_of_a_in_c.map = boxa->map;
    copy_of_a_in_c.set = boxa->set;
    copy_of_a_in_c.smap = boxa->smap;
    for (int i = 0; i < 200; i += 2)
    {
        copy_of_a_in_c.map.erase(i * 3 + 11);
        copy_of_a_in_c.set.erase(i * 7 + 11);
    }
    if (copy_of_a_in_c.map.size() != 100u || copy_of_a_in_c.set.size() != 100u || boxa->map.size() != 200u)
    {
        printf("copia e remocao falharam\n");
        ++bad;
    }
    delete boxa;
    delete boxb;
    printf(bad ? "ABI: FALHOU (%d)\n" : "ABI: ok\n", bad);
    return bad ? 1 : 0;
}
