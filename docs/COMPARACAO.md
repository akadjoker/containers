# Comparacao da ct com a biblioteca standard e com outras

Medicoes de velocidade, memoria e custo de compilacao dos contentores e utilitarios da `ct` contra `std` e contra as
alternativas conhecidas, mais os casos limite que se testaram a seguir. Tudo o que esta aqui foi corrido de verdade, com
`tools/bench_compare.sh` (ver o fim); o que nao foi corrido esta dito.

## 1. Resumo

Tres rondas. A **ronda 1** mediu a `ct` contra `std`, abseil, boost e ankerl e deixou a lista de perdas das seccoes 5.x; a
**ronda 2** fechou o que se podia fechar e disse com provas o que nao; a **ronda 3** fez o que a 2 deixou por fazer (a troca da `HashMap` com valores grandes, a arvore B, a ordenacao, os
leitores de numeros) e repara o que a 2 piorou. Os numeros da ronda 2 estao nas seccoes 3.1 (instrucoes de todos os cenarios), 3.5 (antes e depois) e 3.6 (`-O3`); os da ronda 3 na **3.7**; o resumo de cada ponto e a seccao 5.

**Ronda 3** (o "antes" e origin/main de 63793ca; instrucoes e contagens, porque o dono usava a maquina e o tempo de CPU nao vale mais que uma indicacao):
- **Defeito da ronda 2, corrigido primeiro** (commit 62f7954, publicavel sozinho; 6.12): a `CT_DEBUG_ITERATORS` ligava-se pela ausencia de `NDEBUG` e mudava o layout do `HashMap` e do `HashSet`, por isso duas unidades de um programa com `NDEBUG` diferente corrompiam memoria em silencio. Agora a macro e explicita e as quatro macros que mudam layout ou comportamento inline levam um namespace inline com o valor no nome; teste de duas unidades e teste de ligacao que tem de falhar.
- **`HashMap` com valores grandes** (3.7.1, 5.2): a chave fica no slot e o valor num bloco de um slab por mapa. `int-b256`, N = 1e5, instrucoes ronda 2 para ronda 3 (inline antes da ronda 2): `build` 788 para 321 (351), `hit` 36,7 para 30,8 (31,1),
  `miss` 31,0 para 26,9 (27,7), `erase` 291 para 71 (57), `copy` 566 para 70 (58); alocacoes por construcao 100015 para 6265; **bytes por elemento 294,9 / 302,6 / 293,7 contra 290,5 / 295,6 / 290,9 da ronda 2 (alvo falhado por 1,5 a 2,4 %)**.
  `CT_HASH_INLINE_MAX` fica em 128 (curva em 3.7.1). `iterate` de todos os mapas desce de 5 a 33 % (uma so carga para um grupo completo de metas).
- **`BTreeMap` e `BTreeSet`** (3.7.2, 5.9): B+ arvore de nos de 512 bytes. Contra a `btree_map` do abseil, `int` a 1e4 e 1e6: instrucoes `hit` 263 contra 270, `erase` 272 contra 499, iterar 6,4 contra 18,7; bytes por elemento 10,3 e 10,0 contra 11,1;
  o tempo (com ruido) a 1,1 a 1,17 vezes no `build`, `hit` e `miss` e a 0,5 a 0,8 no `erase` e na iteracao. **Com chaves de texto o `hit` custa 1,7 vezes as instrucoes do abseil** (3.7.2).
- **`ct::sort` e `stable_sort`** (3.7.3, 5.7): particao por blocos para tipos pequenos e trivialmente copiaveis. `int` aleatorio a 1e6: **59,5 para 24,2 ns por elemento** (std 57,1), mispredicoes 6,97 para 1,96 por elemento, **mas 163 para 209 instrucoes**; repetidos 135 para 58 instrucoes;
  `stable` ordenada e invertida de 92 e 201 para 7 e 11 instrucoes.
- **Json, Ini, Xml, TextReader** sobre `numconv.hpp` (3.7.4, 5.4): `Json::dump` 324 para 82 ns por numero (2922 para 705 instrucoes), `parse` 230 para 148 ns (1067 para 559); 1e7 valores de ida e volta sem falhas; o texto muda em 2,6 % dos valores, sempre para mais curto ou mais proximo.
- **`Pool`** 717 para 174 instrucoes por ciclo (new e delete 583) e o `TreeMap` -17 % no `build` e -37 % no `erase` (3.7.5). `Span::subspan` e `Deque::push_front` nao se fecham e `String::==` desfez-se, com a causa (3.7.5).
- **`HashMap` `miss` com chaves `int`: saltos falhados**: um prototipo de sondagem de dois slots nao reduz os 0,84 saltos falhados e custa de 14 a 19 % de instrucoes: nao se fez (3.7.6).
- **EASTL e RE2** medidos (3.7.7): a `hash_map` da EASTL ganha a ct em instrucoes de `hit` e `miss` de `int-int` (19,0 e 15,5 contra 24,9 e 24,5) e perde o resto; a `ct::Regex` e mais depressa que a `std::regex` em tudo e 0,2 a 39 vezes o tempo do RE2.
- **Mudancas que se notam**: 5.2 (a referencia ao valor de uma entrada de mais de 128 bytes sobrevive ao rehash, a `.value` de uma `const Entry &` nao e const, os blocos dos apagados so voltam no `clear`), 5.7 (`stable_sort` pede n elementos de memoria em vez de n/2 para tipos pequenos),
  5.4 (o texto de numeros reais do Json e do Ini, 3.7.4) e 6.13 (`Xml::attr_double` com virgula decimal no locale).

- **Onde a ct ganha agora** (instrucoes por operacao a N = 1e5; ct contra o melhor dos outros, e o que era antes): `String::number(double)`
  307 contra 849 de `std::to_chars` (antes 2572); `String::to_float` 133 contra 185 de `from_chars` (antes 659); `String::to_int` 90 contra 141 (antes 175);
  `String::number` de inteiros 126 contra 147 de `fmt::format_int` (antes 186); texto mais curto de `double` 477 contra 929; `PriorityQueue` 273 contra 309
  de `std::priority_queue` (antes 358); `a + b + c` de strings 392 contra 516 (antes 629); `Vector::resize_for_overwrite` 5 instrucoes por elemento contra 9 do
  `resize` (como o `default_init` do boost); `HashMap` com chaves de texto (`miss` -15 %, `build` -15 %, `erase` -9 %, `churn` -21 % com 8 caracteres);
  e, em bytes, `HashMap` com valores de 256 bytes: 296 por elemento contra 302 do `std::unordered_map` (antes 695). Do que ja ganhava na ronda 1: `Vector`
  sem reserve (2 a 4 vezes mais depressa que `std::vector`), `HashMap` contra `std::unordered_map`, `HashMap` a 1e7 elementos, `TreeMap` nas instrucoes, `String`
  ate 23 caracteres, `Variant` e fila circular (seccao 5.11), compilacao.
- **Onde empata**: o que ja empatava (leitura e acesso de `Vector`, `String::find`, `sort` com comparador em tempo) e,
  nesta ronda, `append_char` (20 contra 19 instrucoes), `Arena` (28 contra 27) e `==` de 40 caracteres (54 contra 46: as instrucoes do SSO).
- **Onde ainda perde** (seccao 5, com causa e prova; os pontos da ronda 2 que a 3 fechou estao marcados com *ronda 3*): `HashMap` com chaves `int` em tabelas de tamanho medio contra abseil e
  boost (o `miss` custa 0,84 mispredicoes de salto por operacao contra 0,40 e 0,18: e o salto do ciclo de sondagem, e subir a
  carga maxima com sondagem linear rebenta o `miss` 3 a 5 vezes); bytes por elemento de `HashMap` com chaves `int` (23,6 contra 11,8 do abseil, carga maxima de
  75 %); iterar um `HashMap` (17 contra 5 da ankerl, que guarda os pares densos); `HashMap` com valores grandes **subia** nas instrucoes (*resolvido na ronda 3, 3.7.1*; uma alocacao por elemento:
  `build` 351 para 788, `hit` 31 para 37, `miss` 28 para 31, `erase` 57 para 291, `copy` 58 para 566; em tempo `build` 2,8 vezes mais rapido, `hit` +49 %, `miss` +59 %, `erase` 5,7 vezes mais
  lento) como preco de metade da memoria, e perde para o abseil e o boost nas instrucoes (`CT_HASH_INLINE_MAX` desliga-o); `hit` de `HashMap` com chaves de texto sobe 1 a 3 % (o preco da distancia na meta, que paga o resto); `Pool` (718 contra 583 de `new`/`delete`)
  e `Span::subspan` (6,4 contra 3,5); `TreeMap` a 1e6 contra a `btree` do abseil (*ronda 3: `BTreeMap`*), `ct::sort` com comparador (25 % mais instrucoes que `std::sort`; *ronda 3: 28 % mais instrucoes e 2,4 vezes mais depressa*) e
  `Deque::push_front` (21 contra 13; *ronda 3: analisado, nao se fecha*): nao se tentaram na ronda 2.
- **Corrigido** (seccao 6): rondas 1 e 2. Os defeitos reais da ronda 2: `String::to_int` dava a volta fora do intervalo
  (3000000000 dava -1294967296), apagar durante um ciclo num `HashMap` nao se detectava, e dois defeitos do codigo novo apanhados
  pelos testes antes de entrar. **Mudancas que se notam**: seccao 5.4 (`to_int` satura, `to_float` ja nao aceita a virgula do locale), 5.2 (referencias a
  valores de `HashMap` de mais de 128 bytes sobrevivem ao rehash), 5.5 (a capacidade de `a + b` e a exacta) e 7 (apagar durante um ciclo aborta em depuracao).

## 2. Metodo

**Maquina**: AMD Ryzen 5 5500U (6 nucleos, 12 fios, 8 MiB de L3), 18 GB de RAM, Linux 7.0 (Ubuntu 24.04), glibc 2.39.
A maquina estava partilhada com outros trabalhos durante todas as medicoes (carga de 4 a 12 numa maquina de 12 fios).
**Por isso se confia nas instrucoes e nas contagens de alocacoes e nao no relogio**: o relogio e a melhor de 3 a 7
repeticoes e ainda tem ruido de 10 a 30 %. Quando o tempo e a instrucao discordam, a seccao 5 diz qual e a causa.

**Compilador**: g++ 13.3.0, `-std=c++17 -O2` (sem `-march=native`; C++17 so porque `std::string_view`, `to_chars` e os
tipos das outras bibliotecas o pedem, a ct e C++14). `-O3` so se corre na seccao 3.6 (um subconjunto das instrucoes). Mesmo compilador e mesmas
opcoes para todas as bibliotecas.

**Bibliotecas**: libstdc++ 13.3; abseil 20220623.1 (`flat_hash_map`, `btree_map`; os cabecalhos do pacote Ubuntu e as
bibliotecas ja instaladas); Boost 1.83 (`unordered_flat_map`, `container::vector`, `deque`, `flat_map`, `sort::pdqsort`,
`spreadsort`); ankerl::unordered_dense 4.5.0 (um so cabecalho, descarregado da versao oficial); fmt 9.1.0 (modo so de
cabecalhos); valgrind 3.22. Os pacotes Ubuntu de abseil, Boost, fmt e valgrind foram descarregados com `apt-get download` e
extraidos para uma pasta propria (`dpkg -x`), sem instalar nada no sistema; o repositorio nao os guarda
(`tools/bench_compare.sh fetch` repete o passo). **Ronda 3**: EASTL (`git clone --depth 1` da EASTL e da EABase, as fontes da EASTL compiladas com o resto) e RE2 (o `libre2.so.10` do sistema e os cabecalhos de `libre2-dev` extraidos) e `std::regex` entram em 3.7.7. A ronda 2
mediu `Variant`, `Arena`, `Pool`, fila circular e `Span` (`bench/compare/cmp_extra.cpp`, familia `extra`).

**Programas** (`bench/compare`, so compilam com `-DCT_BUILD_COMPARE=ON`, que vem desligado; esta pasta e a excecao
declarada ao "sem std": inclui a biblioteca standard e as outras, e `tools/check.sh` nostd nao a olha):

- `cmp_vector`, `cmp_map`, `cmp_string`, `cmp_sort`, `cmp_misc` (deque, mapas ordenados, `PriorityQueue`, `Function`),
  `cmp_extra` (ronda 2: `Variant`, `Arena`, `Pool`, fila circular, `Span`); `compile_unit.cpp` para o custo de compilacao.
  A ronda 2 acrescentou aos de antes: `d2short` e `s2f64` em `cmp_string` (texto mais curto de `double`, texto para `double`),
  `resize_fill` e `resize_overwrite` em `cmp_vector`, e `CMP_CPUTIME=1` (tempo de CPU do processo em vez do relogio, que numa
  maquina com carga de 25 a 60 mede quem mais corre).
- Cada processo faz um cenario (`impl cenario tipo N`) e imprime uma linha: ns por operacao (melhor de 3 a 7 repeticoes),
  operacoes, alocacoes, bytes pedidos, pico e bytes vivos no fim, RSS maximo, `sizeof` e um somatorio de verificacao.
  Todos os cenarios para todas as bibliotecas dao o mesmo somatorio (e o programa de conferencia): o trabalho e igual.
- As alocacoes contam-se interpondo `malloc`, `calloc`, `realloc`, `memalign`, `posix_memalign` e `free` (glibc): bytes
  pedidos, numero de chamadas e bytes vivos = tamanho utilizavel mais 8 bytes de cabecalho do glibc, que e o que
  a memoria custa de verdade. Vale para todas as bibliotecas, porque todas acabam em `malloc`.
- As instrucoes vem de `valgrind --tool=callgrind`, ligando a coleccao so dentro da regiao medida
  (`CALLGRIND_TOGGLE_COLLECT`), uma execucao, N = 1e5, a dividir pelo numero de operacoes. `perf` nao se usou. As falhas de
  cache e as mispredicoes de salto (seccao 5.1) vem do mesmo callgrind com `--cache-sim=yes --D1=32768,8,64 --LL=8388608,16,64` e
  `--branch-sim=yes`. A correccao dos numeros (seccao 7) tem um diferencial a parte, `tests/numconv_diff.cpp`.
- **Ronda 2**: a arvore "antes" e a de origin/main de 7fef995 (o fim da ronda 1) e a "depois" e a deste ramo, compiladas da
  mesma maneira (g++ 13.3 `-O2 -DNDEBUG`); cada mudanca foi medida em todas as operacoes do contentor afectado, nos dois N (1e3 e
  1e5) e em todos os tipos de chave e de valor (seccao 3.5), e uma mudanca so ficou se nenhuma instrucao subiu, ou se a subida esta
  dita com a causa e o saldo.
- **Ronda 3**: o "antes" e o cabecalho de origin/main de 63793ca e o "depois" o desta arvore, **compilados com o mesmo `bench/compare`** (uma unidade de compilacao maior muda as decisoes de inlining do GCC e dava diferencas de 1 a 8 instrucoes
  em codigo da biblioteca que nao mudou; ver 6.15); mais, para cada mudanca, o assembler da operacao isolada, quando a diferenca era pequena. Os cenarios novos: `int-b32` a `int-b1024` em `cmp_map` (o valor do varrimento de `CT_HASH_INLINE_MAX`), `cbtree` e os cenarios `bulk` e `range` em
  `cmp_misc`, `s8` nos mapas ordenados, `cmp_regex`, e a EASTL em `cmp_vector`, `cmp_map` e `cmp_sort`. Mispredicoes de salto com `--branch-sim=yes`. A maquina esteve a carga de 3 a 13 e, no fim, com prioridade baixa (`nice -n 15 ionice -c3`) a pedido do dono: **o tempo de CPU da ronda 3 e o minimo de 3 execucoes
  alternadas e vale como indicacao** (ruido de 10 a 30 %); as decisoes assentam nas instrucoes, nas alocacoes, nos bytes e nos saltos falhados.
- Tipos: `int`, `P16` (16 bytes triviais), `B256` (256 bytes triviais), `MO` (so move, destrutor nao trivial); chaves
  `int`, `s8` (8 caracteres, SSO nos dois lados), `s20` (SSO da ct, heap do libstdc++), `s40` (heap nos dois). Chaves
  distintas e de aspecto aleatorio (uma permutacao de `uint32`). N de 1e3, 1e5 e 1e7; `B256` e as chaves de texto so ate
  1e5 ou 1e6, por memoria e por tempo.
- Cada biblioteca usa o seu hasher por omissao (`ct::Hash`, `std::hash`, `absl::Hash`, `boost::hash`, o wyhash da ankerl);
  `std::hash<int>` e a identidade.

**O que nao e igual** (por isso um empate ou uma vitoria nao e sempre um merito): `HashMap` da ct e enderecamento aberto
com sondagem linear e remocao por deslocamento (sem marcadores), as referencias nao sobrevivem a `put` (excepto nas entradas de mais
de 128 bytes, que ficam fora da tabela); `std::unordered_map`
garante estabilidade de referencias (e por isso aloca um no por elemento); `absl` e `boost` sao tabelas SIMD com
etiquetas; ankerl guarda os valores densos num vetor e por isso itera mais depressa e usa menos memoria por valor; `Vector`
da ct so move por copia de bytes quando o tipo e trivialmente copiavel e usa `realloc` (o `std::vector` nunca pode);
`sort` de `int` sem comparador da ct e um radix (O(n), nao uma ordenacao por comparacao) e as outras colunas de `sort`
com comparador sao comparacoes; `TreeMap` e uma arvore rubro-negra com um `Pool` por tras; a ct aborta em vez de lancar
excecoes, e `std` e as outras tem de se preparar para elas.


## 3. Resultados

### 3.1 Instrucoes por operacao (callgrind, N = 1e5, arvore final da ronda 2)

Menos e melhor. A ultima coluna e `ct` a dividir pelo melhor dos outros; a negrito quando `ct` perde mais de 10 %. No `sort` as
colunas sao `impl:algoritmo`. As colunas das outras bibliotecas vem da corrida da ronda 1 (mesmas bibliotecas, mesmo compilador,
as instrucoes sao deterministicas); as da `ct` sao desta ronda. A familia `extra` e nova (`Variant`, `Arena`, `Pool`, fila, `Span`).

#### Ir por operacao, vector

| cenario | tipo | N | ct | std | boost | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|
| build | int | 100000 | 13.4 | 16.2 | 18.8 | 0.82 |
| build_reserved | int | 100000 | 12.0 | 11.0 | 13.0 | 1.09 |
| iterate | int | 100000 | 5.0 | 5.0 | 5.0 | 1.00 |
| copy | int | 100000 | 4.0 | 4.0 | 4.0 | 1.00 |
| insert_mid | int | 100000 | 19007.6 | 18994.2 | 19184.8 | 1.00 |
| build | p16 | 100000 | 15.4 | 24.6 | 43.2 | 0.62 |
| build_reserved | p16 | 100000 | 14.0 | 13.0 | 16.0 | 1.07 |
| iterate | p16 | 100000 | 6.0 | 6.0 | 6.0 | 1.00 |
| copy | p16 | 100000 | 1.8 | 1.8 | 1.8 | 1.00 |
| insert_mid | p16 | 100000 | 75822.0 | 75804.5 | 76581.9 | 1.00 |
| build | mo | 100000 | 35.6 | 33.6 | 51.7 | 1.06 |
| build_reserved | mo | 100000 | 25.0 | 23.0 | 27.0 | 1.09 |
| iterate | mo | 100000 | 6.0 | 6.0 | 6.0 | 1.00 |
| insert_mid | mo | 100000 | 404063.0 | 404036.0 | 404588.8 | 1.00 |
| resize_fill | int | 100000 | 9.0 | 9.0 | 9.0 | 1.00 |
| resize_overwrite | int | 100000 | 5.0 | 9.0 | 5.0 | 1.00 |
| resize_fill | p16 | 100000 | 9.5 | 12.0 | 12.0 | 0.79 |
| resize_overwrite | p16 | 100000 | 7.0 | 12.0 | 7.0 | 1.00 |
| resize_fill | mo | 100000 | 21.0 | 20.0 | 22.0 | 1.05 |
| resize_overwrite | mo | 100000 | 21.0 | 20.0 | 22.0 | 1.05 |

#### Ir por operacao, map

| cenario | tipo | N | ct | std | absl | boost | ankerl | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| build | int-int | 100000 | 129.5 | 583.8 | 223.8 | 137.9 | 199.0 | 0.94 |
| hit | int-int | 100000 | 24.9 | 30.4 | 47.6 | 41.5 | 49.2 | 0.82 |
| miss | int-int | 100000 | 24.5 | 24.1 | 45.7 | 37.9 | 48.3 | 1.01 |
| erase | int-int | 100000 | 41.9 | 213.4 | 78.4 | 52.3 | 110.1 | 0.80 |
| iterate | int-int | 100000 | 17.4 | 5.0 | 14.0 | 16.9 | 5.0 | **3.47** |
| copy | int-int | 100000 | 23.0 | 453.5 | 53.5 | 12.5 | 12.7 | **1.84** |
| build | s8-int | 100000 | 332.1 | 735.1 | 536.3 | 368.6 | 375.4 | 0.90 |
| hit | s8-int | 100000 | 125.0 | 133.3 | 149.6 | 149.0 | 151.1 | 0.94 |
| miss | s8-int | 100000 | 83.1 | 94.0 | 101.7 | 109.5 | 124.5 | 0.88 |
| erase | s8-int | 100000 | 139.4 | 328.1 | 176.0 | 198.5 | 308.5 | 0.79 |
| iterate | s8-int | 100000 | 18.8 | 5.0 | 14.2 | 17.9 | 5.0 | **3.74** |
| copy | s8-int | 100000 | 60.0 | 516.5 | 160.2 | 88.4 | 54.5 | **1.10** |
| build | s40-int | 100000 | 882.0 | 1200.5 | 1098.3 | 849.7 | 817.5 | 1.08 |
| hit | s40-int | 100000 | 165.0 | 178.8 | 221.9 | 182.2 | 170.2 | 0.97 |
| miss | s40-int | 100000 | 124.9 | 142.0 | 175.7 | 144.8 | 146.1 | 0.88 |
| erase | s40-int | 100000 | 329.2 | 519.4 | 400.5 | 377.8 | 475.1 | 0.87 |
| iterate | s40-int | 100000 | 18.8 | 5.0 | 14.2 | 17.9 | 5.0 | **3.74** |
| copy | s40-int | 100000 | 483.0 | 937.5 | 655.2 | 507.5 | 475.8 | 1.02 |
| build | s20-int | 100000 | 466.9 | 1195.1 | 1074.4 | 799.2 | 794.7 | 0.59 |
| hit | s20-int | 100000 | 169.4 | 176.6 | 214.1 | 161.1 | 163.2 | 1.05 |
| miss | s20-int | 100000 | 127.3 | 137.1 | 166.0 | 121.3 | 136.3 | 1.05 |
| erase | s20-int | 100000 | 186.8 | 517.3 | 392.6 | 356.8 | 458.2 | 0.52 |
| iterate | s20-int | 100000 | 18.8 | 5.0 | 14.2 | 17.9 | 5.0 | **3.74** |
| copy | s20-int | 100000 | 60.0 | 936.5 | 643.2 | 506.5 | 474.8 | 0.13 |
| build | int-b256 | 100000 | 788.1 | 824.0 | 432.5 | 328.4 | 416.0 | **2.40** |
| hit | int-b256 | 100000 | 36.7 | 31.8 | 53.6 | 44.6 | 53.2 | **1.15** |
| miss | int-b256 | 100000 | 31.0 | 23.4 | 48.1 | 38.0 | 48.3 | **1.32** |
| erase | int-b256 | 100000 | 291.3 | 306.9 | 78.4 | 54.3 | 149.4 | **5.36** |
| iterate | int-b256 | 100000 | 19.4 | 6.0 | 15.5 | 19.0 | 6.0 | **3.23** |
| copy | int-b256 | 100000 | 565.7 | 595.8 | 87.0 | 30.6 | 48.5 | **18.47** |

#### Ir por operacao, string

| cenario | tipo | N | ct | std | charconv | libc | fmt | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| create | len8 | 100000 | 52.0 | 73.0 | - | - | - | 0.71 |
| create | len16 | 100000 | 50.0 | 323.0 | - | - | - | 0.15 |
| create | len24 | 100000 | 262.0 | 323.0 | - | - | - | 0.81 |
| create | len40 | 100000 | 263.0 | 324.0 | - | - | - | 0.81 |
| append_char | char | 100000 | 20.1 | 19.3 | - | - | - | 1.04 |
| eq | len40 | 100000 | 53.6 | 45.6 | - | - | - | **1.18** |
| concat | len30 | 100000 | 392.0 | 516.1 | - | - | - | 0.76 |
| int2s | full | 100000 | 126.3 | 218.9 | 230.5 | 665.8 | 146.5 | 0.86 |
| s2int | full | 100000 | 90.0 | 306.2 | 141.3 | 290.2 | - | 0.64 |
| dbl2s | d | 100000 | 306.5 | - | 849.0 | 2450.2 | 1036.1 | 0.36 |
| s2f32 | f | 100000 | 132.3 | 549.3 | 184.5 | 537.3 | - | 0.72 |
| d2short | d | 100000 | 476.6 | - | 929.2 | 4037.5 | 1212.4 | 0.51 |
| d2short | wide | 100000 | 453.5 | - | 837.7 | 3696.2 | 1281.0 | 0.54 |
| s2f64 | f | 100000 | 134.2 | 554.3 | 187.3 | 542.3 | - | 0.72 |
| s2f64 | long | 100000 | 240.2 | 1151.1 | 290.3 | 1139.1 | - | 0.83 |
| s2f64 | wide | 100000 | 325.7 | 1212.8 | 350.4 | 1200.8 | - | 0.93 |

#### Ir por operacao, sort

| cenario | tipo | N | ct:sort | ct:sort_nocmp | std:sort | boost:sort | boost:spread | ct:stable | std:stable | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| - | int.random | 100000 | 163.1 | 76.1 | 128.2 | 118.0 | 95.5 | 221.6 | 193.4 | 0.80 |
| - | p16.random | 100000 | 167.1 | - | 135.9 | 121.2 | - | 264.7 | 206.6 | **1.38** |
| - | int.sorted | 100000 | 7.0 | 7.0 | 117.2 | 11.0 | 8.0 | 92.0 | 119.4 | 0.88 |
| - | p16.sorted | 100000 | 7.0 | - | 105.6 | 10.0 | - | 139.6 | 176.0 | 0.70 |
| - | int.reversed | 100000 | 10.0 | 69.1 | 88.1 | 18.5 | 83.8 | 201.4 | 132.5 | 0.54 |
| - | p16.reversed | 100000 | 10.0 | - | 84.4 | 18.5 | - | 293.7 | 205.4 | 0.54 |
| - | int.dups | 100000 | 134.8 | 47.1 | 110.3 | 32.5 | 29.6 | 213.9 | 189.7 | **1.59** |
| - | p16.dups | 100000 | 134.7 | - | 125.3 | 32.3 | - | 257.5 | 203.8 | **4.17** |

#### Ir por operacao, misc

| cenario | tipo | N | ct | std | boost | ctree | cflat | stdmap | btree | bflat | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| push_back | int | 100000 | 15.4 | 16.5 | 21.9 | - | - | - | - | - | 0.93 |
| push_front | int | 100000 | 21.5 | 13.3 | 19.7 | - | - | - | - | - | **1.62** |
| iterate | int | 100000 | 7.0 | 7.0 | 7.1 | - | - | - | - | - | 1.00 |
| drain | int | 100000 | 6.0 | 12.1 | 12.1 | - | - | - | - | - | 0.50 |
| fifo | int | 100000 | 18.0 | 29.5 | 27.9 | - | - | - | - | - | 0.65 |
| push_back | p16 | 100000 | 18.4 | 44.4 | 41.5 | - | - | - | - | - | 0.44 |
| push_front | p16 | 100000 | 40.2 | 27.0 | 38.5 | - | - | - | - | - | **1.49** |
| iterate | p16 | 100000 | 10.0 | 8.1 | 8.2 | - | - | - | - | - | **1.23** |
| drain | p16 | 100000 | 10.0 | 19.5 | 19.5 | - | - | - | - | - | 0.51 |
| fifo | p16 | 100000 | 25.0 | 46.2 | 42.5 | - | - | - | - | - | 0.59 |
| build | int | 10000 | - | - | - | 397.2 | 2100.1 | 628.3 | 442.6 | 15343.2 |  |
| hit | int | 10000 | - | - | - | 85.0 | 152.5 | 145.6 | 269.7 | 149.5 |  |
| erase | int | 10000 | - | - | - | 298.9 | 18883.6 | 374.3 | 498.0 | 12517.1 |  |
| pushpop | int | 100000 | 272.7 | 308.8 | - | - | - | - | - | - | 0.88 |
| construct_small | int(int) | 100000 | 28.0 | 27.0 | - | - | - | - | - | - | 1.04 |
| construct_big | int(int) | 100000 | 269.0 | 297.0 | - | - | - | - | - | - | 0.91 |
| call | int(int) | 100000 | 18.0 | 17.0 | - | - | - | - | - | - | 1.06 |
| copy | int(int) | 100000 | 298.0 | 326.0 | - | - | - | - | - | - | 0.91 |

#### Ir por operacao, extra

| cenario | tipo | N | ct | std | raw | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|
| construct | mix3 | 100000 | 68.7 | 70.4 | - | 0.98 |
| visit | mix3 | 100000 | 17.0 | 18.4 | - | 0.93 |
| copy_assign | mix3 | 100000 | 34.1 | 36.3 | - | 0.94 |
| alloc64 | node16 | 100000 | 28.0 | 27.1 | - | 1.03 |
| create_nodes | node16 | 100000 | 32.0 | 33.1 | - | 0.97 |
| cycle | node16 | 100000 | 718.3 | 582.5 | - | **1.23** |
| fifo | int | 100000 | 18.0 | 22.6 | - | 0.80 |
| sum | int | 100000 | 5.0 | - | 5.0 | 1.00 |
| sub | int | 100000 | 6.4 | - | 3.5 | **1.85** |

### 3.2 Tempo por operacao em ns (melhor de varias repeticoes; maquina carregada, ver o metodo)

Todas as colunas, `ct` incluida, sao da mesma execucao (a ct de origin/main, antes das correcoes: a execucao completa so se fez uma vez, com a maquina carregada, e repeti-la com a ct nova daria uma ct medida com outra carga que as outras). O que mudou na ct esta na seccao 4, medido aos pares. Aqui so N = 1e5 e, para `build`, os N de 1e6 e 1e7; a matriz completa (todos os N) esta em `docs/comparacao-tabelas.md`.


#### ns por operacao, vector

| cenario | tipo | N | ct | std | boost | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|
| build | int | 100000 | 2.55 | 8.29 | 8.92 | 0.31 |
| build_reserved | int | 100000 | 1.82 | 1.27 | 1.34 | **1.43** |
| iterate | int | 100000 | 0.62 | 0.49 | 0.69 | **1.27** |
| index | int | 100000 | 0.88 | 0.83 | 0.79 | **1.12** |
| copy | int | 100000 | 0.12 | 0.12 | 0.11 | 1.04 |
| move | int | 100000 | 0.06 | 0.11 | 0.06 | 1.02 |
| clear | int | 100000 | 0.00 | 0.00 | 0.00 |  |
| insert_mid | int | 100000 | 4821.40 | 5950.80 | 4517.49 | 1.07 |
| build | int | 10000000 | 5.44 | 10.81 | 11.95 | 0.50 |
| build | p16 | 100000 | 9.76 | 41.41 | 48.04 | 0.24 |
| build_reserved | p16 | 100000 | 10.19 | 9.37 | 9.43 | 1.09 |
| iterate | p16 | 100000 | 0.75 | 0.54 | 1.40 | **1.40** |
| index | p16 | 100000 | 1.92 | 2.11 | 2.23 | 0.91 |
| copy | p16 | 100000 | 0.97 | 1.00 | 1.14 | 0.97 |
| move | p16 | 100000 | 0.06 | 0.10 | 0.12 | 0.55 |
| clear | p16 | 100000 | 0.00 | 0.00 | 0.00 |  |
| insert_mid | p16 | 100000 | 23815.71 | 22005.52 | 23789.01 | 1.08 |
| build | p16 | 10000000 | 24.08 | 53.60 | 55.27 | 0.45 |
| build | mo | 100000 | 53.19 | 58.01 | 14.54 | **3.66** |
| build_reserved | mo | 100000 | 7.14 | 6.78 | 7.32 | 1.05 |
| iterate | mo | 100000 | 0.73 | 1.08 | 0.79 | 0.92 |
| index | mo | 100000 | 3.24 | 3.52 | 6.29 | 0.92 |
| move | mo | 100000 | 0.08 | 0.11 | 0.06 | **1.45** |
| clear | mo | 100000 | 2.70 | 2.74 | 2.78 | 0.99 |
| insert_mid | mo | 100000 | 62059.19 | 95387.65 | 100589.48 | 0.65 |
| build | mo | 10000000 | 70.83 | 67.65 | 78.63 | 1.05 |
| build | b256 | 100000 | 167.47 | 370.66 | 408.87 | 0.45 |
| build_reserved | b256 | 100000 | 30.79 | 31.92 | 30.66 | 1.00 |
| iterate | b256 | 100000 | 5.60 | 5.55 | 5.58 | 1.01 |
| index | b256 | 100000 | 10.41 | 10.36 | 10.86 | 1.00 |
| copy | b256 | 100000 | 26.42 | 26.70 | 28.97 | 0.99 |
| move | b256 | 100000 | 0.07 | 0.09 | 0.09 | 0.80 |
| clear | b256 | 100000 | 0.00 | 0.00 | 0.00 |  |
| insert_mid | b256 | 100000 | 1499608.95 | 1472152.80 | 1441402.13 | 1.04 |
| build | b256 | 1000000 | 162.66 | 327.76 | 448.78 | 0.50 |

#### ns por operacao, map

| cenario | tipo | N | ct | std | absl | boost | ankerl | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| build | int-int | 100000 | 52.99 | 160.23 | 38.17 | 28.57 | 48.34 | **1.85** |
| hit | int-int | 100000 | 11.00 | 36.77 | 8.74 | 7.23 | 20.44 | **1.52** |
| miss | int-int | 100000 | 11.20 | 34.47 | 9.16 | 5.51 | 10.44 | **2.03** |
| erase | int-int | 100000 | 17.40 | 269.43 | 17.40 | 9.85 | 31.13 | **1.77** |
| iterate | int-int | 100000 | 7.66 | 9.39 | 2.27 | 1.68 | 0.41 | **18.92** |
| copy | int-int | 100000 | 12.25 | 114.77 | 7.39 | 0.28 | 1.26 | **43.59** |
| churn | int-int | 100000 | 37.47 | 200.92 | 76.27 | 45.76 | 78.33 | 0.82 |
| build | int-int | 10000000 | 94.92 | 562.51 | 107.51 | 105.34 | 133.35 | 0.90 |
| build | int-p16 | 100000 | 107.90 | 187.30 | 69.40 | 55.11 | 61.92 | **1.96** |
| hit | int-p16 | 100000 | 19.10 | 42.68 | 13.30 | 16.18 | 37.82 | **1.44** |
| miss | int-p16 | 100000 | 21.42 | 49.46 | 8.62 | 5.46 | 9.95 | **3.92** |
| erase | int-p16 | 100000 | 28.88 | 205.15 | 24.26 | 14.26 | 33.37 | **2.03** |
| iterate | int-p16 | 100000 | 8.35 | 19.74 | 2.23 | 1.40 | 0.52 | **16.11** |
| copy | int-p16 | 100000 | 15.66 | 151.21 | 15.17 | 3.96 | 5.30 | **3.96** |
| churn | int-p16 | 100000 | 59.91 | 209.70 | 81.55 | 61.65 | 78.20 | 0.97 |
| build | int-p16 | 1000000 | 119.61 | 581.52 | 154.19 | 129.04 | 144.09 | 0.93 |
| build | int-mo | 100000 | 146.96 | 199.44 | 107.38 | 72.11 | 81.12 | **2.04** |
| hit | int-mo | 100000 | 21.55 | 41.82 | 20.64 | 17.41 | 29.31 | **1.24** |
| miss | int-mo | 100000 | 17.45 | 37.98 | 11.37 | 5.50 | 11.12 | **3.18** |
| erase | int-mo | 100000 | 36.85 | 200.99 | 41.84 | 33.09 | 35.82 | **1.11** |
| iterate | int-mo | 100000 | 8.60 | 19.78 | 2.66 | 1.89 | 0.66 | **12.97** |
| churn | int-mo | 100000 | 75.68 | 209.85 | 94.46 | 81.64 | 78.93 | 0.96 |
| build | int-mo | 1000000 | 155.62 | 509.87 | 174.50 | 129.43 | 134.68 | **1.20** |
| build | int-b256 | 100000 | 925.94 | 383.57 | 568.78 | 471.05 | 443.44 | **2.41** |
| hit | int-b256 | 100000 | 30.14 | 55.49 | 34.85 | 32.49 | 37.89 | 0.93 |
| miss | int-b256 | 100000 | 18.62 | 46.89 | 9.34 | 5.75 | 10.27 | **3.24** |
| erase | int-b256 | 100000 | 50.71 | 419.33 | 54.65 | 35.45 | 138.93 | **1.43** |
| iterate | int-b256 | 100000 | 15.63 | 74.14 | 11.04 | 10.88 | 7.58 | **2.06** |
| copy | int-b256 | 100000 | 440.91 | 386.06 | 225.97 | 30.65 | 47.31 | **14.39** |
| churn | int-b256 | 100000 | 234.29 | 492.11 | 263.42 | 322.32 | 107.58 | **2.18** |
| build | s8-int | 100000 | 165.34 | 251.61 | 148.14 | 119.72 | 118.84 | **1.39** |
| hit | s8-int | 100000 | 81.44 | 162.71 | 101.74 | 97.72 | 136.35 | 0.83 |
| miss | s8-int | 100000 | 50.11 | 123.40 | 39.15 | 45.47 | 56.91 | **1.28** |
| erase | s8-int | 100000 | 124.98 | 405.53 | 120.40 | 113.46 | 255.98 | **1.10** |
| iterate | s8-int | 100000 | 9.91 | 35.94 | 3.65 | 3.40 | 1.82 | **5.44** |
| copy | s8-int | 100000 | 37.32 | 212.67 | 29.47 | 15.30 | 12.76 | **2.92** |
| churn | s8-int | 100000 | 129.41 | 407.49 | 231.51 | 185.83 | 121.87 | 1.06 |
| build | s8-int | 1000000 | 196.69 | 596.09 | 326.12 | 245.01 | 191.82 | 1.03 |
| build | s20-int | 100000 | 200.23 | 449.47 | 239.73 | 156.86 | 148.59 | **1.35** |
| hit | s20-int | 100000 | 112.79 | 273.76 | 214.10 | 162.01 | 229.65 | 0.70 |
| miss | s20-int | 100000 | 84.43 | 181.83 | 105.50 | 80.71 | 91.71 | 1.05 |
| erase | s20-int | 100000 | 125.32 | 543.39 | 381.98 | 358.99 | 401.32 | 0.35 |
| iterate | s20-int | 100000 | 8.75 | 53.28 | 2.92 | 2.48 | 1.10 | **7.98** |
| copy | s20-int | 100000 | 35.42 | 252.02 | 128.12 | 98.56 | 88.37 | 0.40 |
| churn | s20-int | 100000 | 208.34 | 402.54 | 330.88 | 262.20 | 135.36 | **1.54** |
| build | s40-int | 100000 | 379.64 | 478.23 | 313.64 | 278.79 | 169.79 | **2.24** |
| hit | s40-int | 100000 | 216.58 | 288.31 | 231.62 | 214.63 | 249.32 | 1.01 |
| miss | s40-int | 100000 | 166.77 | 201.43 | 120.72 | 111.89 | 108.18 | **1.54** |
| erase | s40-int | 100000 | 366.38 | 591.95 | 432.94 | 430.82 | 503.89 | 0.85 |
| iterate | s40-int | 100000 | 15.01 | 126.35 | 5.38 | 4.52 | 2.91 | **5.15** |
| copy | s40-int | 100000 | 152.31 | 278.37 | 180.16 | 200.56 | 112.56 | **1.35** |
| churn | s40-int | 100000 | 537.69 | 714.42 | 575.80 | 471.64 | 203.18 | **2.65** |
| build | s40-int | 1000000 | 797.36 | 724.56 | 696.18 | 533.58 | 243.41 | **3.28** |

#### ns por operacao, string

| cenario | tipo | N | ct | std | ct / melhor outro |
|---|---|---:|---:|---:|---:|
| create | len8 | 100000 | 7.54 | 9.81 | 0.77 |
| create | len16 | 100000 | 8.15 | 54.30 | 0.15 |
| create | len24 | 100000 | 39.21 | 64.06 | 0.61 |
| create | len40 | 100000 | 41.14 | 68.89 | 0.60 |
| eq | len40 | 100000 | 11.92 | 5.08 | **2.34** |
| lt | len40 | 100000 | 12.83 | 9.44 | **1.36** |
| concat | len30 | 100000 | 105.80 | 102.03 | 1.04 |
| copy | len8 | 100000 | 5.40 | 11.48 | 0.47 |
| copy | len16 | 100000 | 5.59 | 65.72 | 0.09 |
| copy | len24 | 100000 | 44.08 | 62.75 | 0.70 |
| copy | len40 | 100000 | 49.96 | 43.18 | **1.16** |
| append_char | char | 100000 | 2.27 | 2.58 | 0.88 |

#### ns por operacao, sort

| cenario | tipo | N | ct:sort | ct:sort_nocmp | std:sort | boost:sort | boost:spread | ct:stable | std:stable | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| - | int.random | 100000 | 64.92 | 10.46 | 64.56 | 60.10 | 27.40 | 84.30 | 82.69 | 0.38 |
| - | int.random | 10000000 | 92.91 | 18.85 | 88.24 | 99.40 | 42.17 | 134.05 | 106.21 | 0.45 |
| - | p16.random | 100000 | 67.38 | - | 74.65 | 66.42 | - | 91.07 | 82.77 | 1.01 |
| - | p16.random | 10000000 | 98.80 | - | 103.58 | 103.41 | - | 128.50 | 124.09 | 0.96 |
| - | b256.random | 100000 | 371.14 | - | 314.71 | 234.18 | - | 691.42 | 798.97 | **1.58** |
| - | str.random | 100000 | 309.15 | - | 340.72 | 244.16 | - | 284.01 | 336.74 | **1.16** |
| - | int.sorted | 100000 | 8.94 | 0.30 | 12.52 | 0.90 | 0.60 | 8.54 | 7.06 | 0.51 |
| - | int.sorted | 10000000 | 14.43 | 0.47 | 18.74 | 1.17 | 0.69 | 16.49 | 23.30 | 0.68 |
| - | p16.sorted | 100000 | 10.63 | - | 11.32 | 0.86 | - | 8.41 | 10.58 | **9.80** |
| - | p16.sorted | 10000000 | 24.58 | - | 24.03 | 2.26 | - | 44.25 | 72.55 | **10.88** |
| - | b256.sorted | 100000 | 180.40 | - | 84.69 | 15.62 | - | 357.61 | 718.45 | **11.55** |
| - | str.sorted | 100000 | 108.41 | - | 104.77 | 10.71 | - | 67.28 | 104.22 | **6.28** |
| - | int.reversed | 100000 | 37.42 | 9.30 | 8.33 | 1.26 | 10.86 | 19.15 | 10.76 | **7.36** |
| - | int.reversed | 10000000 | 53.46 | 20.52 | 22.27 | 3.04 | 17.25 | 30.23 | 23.29 | **6.76** |
| - | p16.reversed | 100000 | 38.63 | - | 8.33 | 1.26 | - | 17.55 | 19.19 | **13.92** |
| - | p16.reversed | 10000000 | 75.71 | - | 20.33 | 4.74 | - | 71.39 | 75.80 | **15.06** |
| - | b256.reversed | 100000 | 500.97 | - | 141.90 | 44.86 | - | 816.01 | 741.22 | **11.17** |
| - | str.reversed | 100000 | 334.14 | - | 88.47 | 18.63 | - | 209.72 | 123.05 | **11.25** |
| - | int.dups | 100000 | 44.34 | 11.05 | 34.84 | 18.16 | 8.78 | 54.54 | 45.93 | **1.26** |
| - | int.dups | 10000000 | 48.12 | 14.65 | 28.77 | 15.74 | 8.37 | 52.58 | 56.26 | **1.75** |
| - | p16.dups | 100000 | 38.94 | - | 37.28 | 19.24 | - | 34.11 | 39.71 | **1.77** |
| - | p16.dups | 10000000 | 86.14 | - | 36.35 | 19.89 | - | 88.02 | 104.69 | **4.33** |
| - | b256.dups | 100000 | 303.88 | - | 498.37 | 213.04 | - | 803.65 | 1047.64 | **1.43** |
| - | str.dups | 100000 | 235.79 | - | 464.69 | 210.87 | - | 231.35 | 684.50 | 1.10 |

#### ns por operacao, misc

| cenario | tipo | N | ct | std | boost | ctree | cflat | stdmap | btree | bflat | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| push_back | int | 100000 | 2.04 | 4.86 | 4.37 | - | - | - | - | - | 0.47 |
| push_front | int | 100000 | 2.33 | 2.27 | 2.82 | - | - | - | - | - | 1.03 |
| iterate | int | 100000 | 0.83 | 0.92 | 1.08 | 27.04 | 0.45 | 66.12 | 2.05 | 0.68 | **1.83** |
| index | int | 100000 | 1.54 | 3.86 | 6.37 | - | - | - | - | - | 0.40 |
| drain | int | 100000 | 1.02 | 1.63 | 1.42 | - | - | - | - | - | 0.72 |
| fifo | int | 100000 | 2.67 | 4.12 | 2.38 | - | - | - | - | - | **1.12** |
| push_back | p16 | 100000 | 9.64 | 28.12 | 28.95 | - | - | - | - | - | 0.34 |
| push_front | p16 | 100000 | 12.03 | 12.80 | 18.32 | - | - | - | - | - | 0.94 |
| iterate | p16 | 100000 | 1.15 | 0.86 | 0.99 | - | - | - | - | - | **1.34** |
| index | p16 | 100000 | 3.57 | 9.79 | 14.37 | - | - | - | - | - | 0.36 |
| drain | p16 | 100000 | 1.96 | 2.54 | 3.31 | - | - | - | - | - | 0.77 |
| fifo | p16 | 100000 | 10.08 | 11.88 | 11.36 | - | - | - | - | - | 0.89 |
| push_back | b256 | 100000 | 284.77 | 438.96 | 320.01 | - | - | - | - | - | 0.89 |
| push_front | b256 | 100000 | 543.57 | 148.27 | 158.87 | - | - | - | - | - | **3.67** |
| iterate | b256 | 100000 | 53.23 | 17.85 | 14.97 | - | - | - | - | - | **3.56** |
| index | b256 | 100000 | 100.67 | 42.41 | 45.72 | - | - | - | - | - | **2.37** |
| drain | b256 | 100000 | 39.35 | 86.43 | 71.62 | - | - | - | - | - | 0.55 |
| fifo | b256 | 100000 | 24.85 | 47.96 | 46.60 | - | - | - | - | - | 0.53 |
| build | int | 100000 | - | - | - | 383.27 | 3062.04 | 406.09 | 81.21 | 10225.56 |  |
| hit | int | 100000 | - | - | - | 146.33 | 91.64 | 192.18 | 58.48 | 137.30 |  |
| miss | int | 100000 | - | - | - | 171.10 | 91.66 | 380.24 | 63.61 | 156.15 |  |
| erase | int | 100000 | - | - | - | 169.85 | 3042.38 | 202.89 | 110.37 | 33033.43 |  |
| build | int | 1000000 | - | - | - | 2076.29 | - | 1505.95 | 336.24 | - |  |
| build | s20 | 100000 | - | - | - | 1960.07 | 155834.87 | 2482.12 | 3745.56 | - |  |
| hit | s20 | 100000 | - | - | - | 1234.44 | 541.14 | 1415.70 | 1976.34 | - |  |
| miss | s20 | 100000 | - | - | - | 1218.40 | 551.20 | 1595.37 | 3092.45 | - |  |
| erase | s20 | 100000 | - | - | - | 1167.22 | - | 1480.61 | 3082.48 | - |  |
| iterate | s20 | 100000 | - | - | - | 214.99 | - | 174.92 | 46.63 | - |  |
| pushpop | int | 100000 | 200.14 | 192.89 | - | - | - | - | - | - | 1.04 |
| construct_small | int(int) | 100000 | 4.18 | 8.44 | - | - | - | - | - | - | 0.50 |
| construct_big | int(int) | 100000 | 37.81 | 65.92 | - | - | - | - | - | - | 0.57 |
| call | int(int) | 100000 | 14.52 | 9.77 | - | - | - | - | - | - | **1.49** |
| copy | int(int) | 100000 | 45.26 | 57.47 | - | - | - | - | - | - | 0.79 |

### 3.2b Tempo da ct nesta ronda (HashMap; ns por operacao, tempo de CPU do processo, minimo de 3 execucoes alternadas)

Antes (cabecalhos de origin/main) e depois, o mesmo `cmp_map` compilado com os dois conjuntos de cabecalhos, `CMP_CPUTIME=1`, N = 1e5, `-O2`.
A maquina estava com carga de 20 a 25 durante estas celulas (antes e depois disso chegou a 250, e nessas condicoes nao se mediu mais nada), por isso **o ruido e
grande**: as celulas de `int-int`, que tem o mesmo codigo nos dois lados (o assembler de `find` e `erase` e igual), variam de -37 % a +9 %. Le-se uma diferenca de menos de 30 %
como empate, e so as grandes contam. A linha `std` e o `std::unordered_map` do binario de antes e do de depois (o mesmo codigo: a diferenca entre as duas colunas e ruido).
O tempo de `String` e dos numeros nao se mediu desta maneira (as instrucoes estao em 3.5; a seccao 5.5 tem o `==` medido em tempo de CPU da thread).

| impl | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|
| ct | build | int-b256 | 100000 | 1544.1 | 557.2 | -64% |
| ct | hit | int-b256 | 100000 | 60.6 | 90.1 | +49% |
| ct | miss | int-b256 | 100000 | 33.0 | 52.6 | +59% |
| ct | erase | int-b256 | 100000 | 79.8 | 451.3 | +466% |
| std | build | int-b256 | 100000 | 597.2 | 689.5 | +15% |
| std | hit | int-b256 | 100000 | 127.0 | 127.3 | +0% |
| ct | build | s8-int | 100000 | 297.2 | 291.7 | -2% |
| ct | hit | s8-int | 100000 | 129.9 | 137.5 | +6% |
| ct | miss | s8-int | 100000 | 98.3 | 83.1 | -16% |
| ct | erase | s8-int | 100000 | 142.6 | 138.2 | -3% |
| ct | build | s40-int | 100000 | 734.3 | 709.0 | -3% |
| ct | hit | s40-int | 100000 | 317.3 | 316.1 | -0% |
| ct | miss | s40-int | 100000 | 261.6 | 234.8 | -10% |
| ct | erase | s40-int | 100000 | 619.6 | 568.2 | -8% |
| ct | hit | int-int | 100000 | 23.4 | 22.6 | -3% |
| ct | miss | int-int | 100000 | 20.5 | 22.4 | +9% |
| ct | build | int-int | 100000 | 86.4 | 88.8 | +3% |
| ct | erase | int-int | 100000 | 50.0 | 31.6 | -37% |

O que se le: `int-b256` constroi 2,8 vezes mais depressa (1544 para 557 ns, contra 597 a 689 do `std::unordered_map`: de 2,6 vezes mais lento a empate) e o `hit` de `int-b256` fica
mais rapido que o do std (90 contra 127) mas 49 % mais lento que o de antes, o `miss` 59 % e o `erase` 5,7 vezes mais lentos: o preco da alocacao por elemento e do ponteiro a seguir. Com chaves de texto `miss` e `erase` ficam 3 a 16 %
mais rapidos e o `hit` (+6 %, 0 %) empata; com `int` empata.

### 3.3 Memoria

`layout` (tamanho do contentor vazio, slots, fator de carga e bytes vivos por elemento depois de construir N elementos; o total vem do contador de alocacoes: tamanho utilizavel mais 8 bytes de cabecalho do malloc), arvore final da ronda 2. `int-b256` mudou: as entradas de mais de 128 bytes ficam fora da tabela, que guarda ponteiros. **Ronda 3**: a chave fica na tabela e o valor num bloco de um slab (3.7.1): 294,9 / 302,6 / 293,7 bytes por elemento a N = 1e3 / 1e5 / 1e6 (as linhas de `int-b256` abaixo sao as da ronda 2).

```
layout	ct	int-int	1000	sizeof_empty=32	size=1000	slots=2048	load=0.488	bytes_per_elem=18.45	heap_bytes=18448
layout	std	int-int	1000	sizeof_empty=56	size=1000	slots=1109	load=0.902	bytes_per_elem=40.91	heap_bytes=40912
layout	absl	int-int	1000	sizeof_empty=40	size=1000	slots=2047	load=0.489	bytes_per_elem=18.45	heap_bytes=18448
layout	boost	int-int	1000	sizeof_empty=48	size=1000	slots=1919	load=0.521	bytes_per_elem=17.42	heap_bytes=17424
layout	ankerl	int-int	1000	sizeof_empty=64	size=1000	slots=2048	load=0.488	bytes_per_elem=24.61	heap_bytes=24608
layout	ct	int-int	100000	sizeof_empty=32	size=100000	slots=262144	load=0.381	bytes_per_elem=23.63	heap_bytes=2363384
layout	std	int-int	100000	sizeof_empty=56	size=100000	slots=172933	load=0.578	bytes_per_elem=45.85	heap_bytes=4584520
layout	absl	int-int	100000	sizeof_empty=40	size=100000	slots=131071	load=0.763	bytes_per_elem=11.84	heap_bytes=1183736
layout	boost	int-int	100000	sizeof_empty=48	size=100000	slots=122879	load=0.814	bytes_per_elem=11.18	heap_bytes=1118200
layout	ankerl	int-int	100000	sizeof_empty=64	size=100000	slots=131072	load=0.763	bytes_per_elem=21.05	heap_bytes=2105328
layout	ct	int-int	1000000	sizeof_empty=32	size=1000000	slots=2097152	load=0.477	bytes_per_elem=18.88	heap_bytes=18878456
layout	std	int-int	1000000	sizeof_empty=56	size=1000000	slots=1447153	load=0.691	bytes_per_elem=43.58	heap_bytes=43579464
layout	absl	int-int	1000000	sizeof_empty=40	size=1000000	slots=2097151	load=0.477	bytes_per_elem=18.88	heap_bytes=18878456
layout	boost	int-int	1000000	sizeof_empty=48	size=1000000	slots=1966079	load=0.509	bytes_per_elem=17.83	heap_bytes=17829880
layout	ankerl	int-int	1000000	sizeof_empty=64	size=1000000	slots=2097152	load=0.477	bytes_per_elem=25.17	heap_bytes=25174000
layout	ct	int-p16	1000	sizeof_empty=32	size=1000	slots=2048	load=0.488	bytes_per_elem=51.22	heap_bytes=51216
layout	std	int-p16	1000	sizeof_empty=56	size=1000	slots=1109	load=0.902	bytes_per_elem=56.90	heap_bytes=56896
layout	absl	int-p16	1000	sizeof_empty=40	size=1000	slots=2047	load=0.489	bytes_per_elem=51.20	heap_bytes=51200
layout	boost	int-p16	1000	sizeof_empty=48	size=1000	slots=1919	load=0.521	bytes_per_elem=48.13	heap_bytes=48128
layout	ankerl	int-p16	1000	sizeof_empty=64	size=1000	slots=2048	load=0.488	bytes_per_elem=40.99	heap_bytes=40992
layout	ct	int-p16	100000	sizeof_empty=32	size=100000	slots=262144	load=0.381	bytes_per_elem=65.58	heap_bytes=6557688
layout	std	int-p16	100000	sizeof_empty=56	size=100000	slots=172933	load=0.578	bytes_per_elem=61.84	heap_bytes=6184472
layout	absl	int-p16	100000	sizeof_empty=40	size=100000	slots=131071	load=0.763	bytes_per_elem=32.81	heap_bytes=3280888
layout	boost	int-p16	100000	sizeof_empty=48	size=100000	slots=122879	load=0.814	bytes_per_elem=30.84	heap_bytes=3084280
layout	ankerl	int-p16	100000	sizeof_empty=64	size=100000	slots=131072	load=0.763	bytes_per_elem=42.02	heap_bytes=4202480
layout	ct	int-p16	1000000	sizeof_empty=32	size=1000000	slots=2097152	load=0.477	bytes_per_elem=52.43	heap_bytes=52432888
layout	std	int-p16	1000000	sizeof_empty=56	size=1000000	slots=1447153	load=0.691	bytes_per_elem=59.58	heap_bytes=59579416
layout	absl	int-p16	1000000	sizeof_empty=40	size=1000000	slots=2097151	load=0.477	bytes_per_elem=52.43	heap_bytes=52432888
layout	boost	int-p16	1000000	sizeof_empty=48	size=1000000	slots=1966079	load=0.509	bytes_per_elem=49.29	heap_bytes=49287160
layout	ankerl	int-p16	1000000	sizeof_empty=64	size=1000000	slots=2097152	load=0.477	bytes_per_elem=41.95	heap_bytes=41951216
layout	ct	int-b256	1000	sizeof_empty=32	size=1000	slots=2048	load=0.488	bytes_per_elem=290.45	heap_bytes=290448
layout	std	int-b256	1000	sizeof_empty=56	size=1000	slots=1109	load=0.902	bytes_per_elem=296.90	heap_bytes=296896
layout	absl	int-b256	1000	sizeof_empty=40	size=1000	slots=2047	load=0.489	bytes_per_elem=544.76	heap_bytes=544760
layout	boost	int-b256	1000	sizeof_empty=48	size=1000	slots=1919	load=0.521	bytes_per_elem=511.99	heap_bytes=511992
layout	ankerl	int-b256	1000	sizeof_empty=64	size=1000	slots=2048	load=0.488	bytes_per_elem=290.82	heap_bytes=290824
layout	ct	int-b256	100000	sizeof_empty=32	size=100000	slots=262144	load=0.381	bytes_per_elem=295.63	heap_bytes=29563384
layout	std	int-b256	100000	sizeof_empty=56	size=100000	slots=172933	load=0.578	bytes_per_elem=301.84	heap_bytes=30184456
layout	absl	int-b256	100000	sizeof_empty=40	size=100000	slots=131071	load=0.763	bytes_per_elem=347.34	heap_bytes=34734072
layout	boost	int-b256	100000	sizeof_empty=48	size=100000	slots=122879	load=0.814	bytes_per_elem=325.71	heap_bytes=32571384
layout	ankerl	int-b256	100000	sizeof_empty=64	size=100000	slots=131072	load=0.763	bytes_per_elem=356.56	heap_bytes=35655688
layout	ct	int-b256	1000000	sizeof_empty=32	size=1000000	slots=2097152	load=0.477	bytes_per_elem=290.88	heap_bytes=290878456
layout	std	int-b256	1000000	sizeof_empty=56	size=1000000	slots=1447153	load=0.691	bytes_per_elem=299.58	heap_bytes=299579400
layout	absl	int-b256	1000000	sizeof_empty=40	size=1000000	slots=2097151	load=0.477	bytes_per_elem=555.75	heap_bytes=555745272
layout	boost	int-b256	1000000	sizeof_empty=48	size=1000000	slots=1966079	load=0.509	bytes_per_elem=521.14	heap_bytes=521142264
layout	ankerl	int-b256	1000000	sizeof_empty=64	size=1000000	slots=2097152	load=0.477	bytes_per_elem=293.61	heap_bytes=293605384
layout	ct	s8-int	1000	sizeof_empty=32	size=1000	slots=2048	load=0.488	bytes_per_elem=67.60	heap_bytes=67600
layout	std	s8-int	1000	sizeof_empty=56	size=1000	slots=1109	load=0.902	bytes_per_elem=72.90	heap_bytes=72896
layout	absl	s8-int	1000	sizeof_empty=40	size=1000	slots=2047	load=0.489	bytes_per_elem=83.95	heap_bytes=83952
layout	boost	s8-int	1000	sizeof_empty=48	size=1000	slots=1919	load=0.521	bytes_per_elem=78.85	heap_bytes=78848
layout	ankerl	s8-int	1000	sizeof_empty=64	size=1000	slots=2048	load=0.488	bytes_per_elem=57.38	heap_bytes=57376
layout	ct	s8-int	100000	sizeof_empty=32	size=100000	slots=262144	load=0.381	bytes_per_elem=86.55	heap_bytes=8654840
layout	std	s8-int	100000	sizeof_empty=56	size=100000	slots=172933	load=0.578	bytes_per_elem=77.84	heap_bytes=7784472
layout	absl	s8-int	100000	sizeof_empty=40	size=100000	slots=131071	load=0.763	bytes_per_elem=53.74	heap_bytes=5373944
layout	boost	s8-int	100000	sizeof_empty=48	size=100000	slots=122879	load=0.814	bytes_per_elem=50.50	heap_bytes=5050360
layout	ankerl	s8-int	100000	sizeof_empty=64	size=100000	slots=131072	load=0.763	bytes_per_elem=62.96	heap_bytes=6295560
layout	ct	s8-int	1000000	sizeof_empty=32	size=1000000	slots=2097152	load=0.477	bytes_per_elem=69.21	heap_bytes=69210104
layout	std	s8-int	1000000	sizeof_empty=56	size=1000000	slots=1447153	load=0.691	bytes_per_elem=75.58	heap_bytes=75579416
layout	absl	s8-int	1000000	sizeof_empty=40	size=1000000	slots=2097151	load=0.477	bytes_per_elem=85.98	heap_bytes=85983224
layout	boost	s8-int	1000000	sizeof_empty=48	size=1000000	slots=1966079	load=0.509	bytes_per_elem=80.74	heap_bytes=80744440
layout	ankerl	s8-int	1000000	sizeof_empty=64	size=1000000	slots=2097152	load=0.477	bytes_per_elem=58.72	heap_bytes=58724360
layout	ct	s20-int	1000	sizeof_empty=32	size=1000	slots=2048	load=0.488	bytes_per_elem=67.60	heap_bytes=67600
layout	std	s20-int	1000	sizeof_empty=56	size=1000	slots=1109	load=0.902	bytes_per_elem=104.91	heap_bytes=104912
layout	absl	s20-int	1000	sizeof_empty=40	size=1000	slots=2047	load=0.489	bytes_per_elem=115.97	heap_bytes=115968
layout	boost	s20-int	1000	sizeof_empty=48	size=1000	slots=1919	load=0.521	bytes_per_elem=110.86	heap_bytes=110864
layout	ankerl	s20-int	1000	sizeof_empty=64	size=1000	slots=2048	load=0.488	bytes_per_elem=89.46	heap_bytes=89456
layout	ct	s20-int	100000	sizeof_empty=32	size=100000	slots=262144	load=0.381	bytes_per_elem=86.55	heap_bytes=8654840
layout	std	s20-int	100000	sizeof_empty=56	size=100000	slots=172933	load=0.578	bytes_per_elem=109.85	heap_bytes=10984520
layout	absl	s20-int	100000	sizeof_empty=40	size=100000	slots=131071	load=0.763	bytes_per_elem=85.74	heap_bytes=8573960
layout	boost	s20-int	100000	sizeof_empty=48	size=100000	slots=122879	load=0.814	bytes_per_elem=82.50	heap_bytes=8250376
layout	ankerl	s20-int	100000	sizeof_empty=64	size=100000	slots=131072	load=0.763	bytes_per_elem=94.96	heap_bytes=9495720
layout	ct	s20-int	1000000	sizeof_empty=32	size=1000000	slots=2097152	load=0.477	bytes_per_elem=69.21	heap_bytes=69210104
layout	std	s20-int	1000000	sizeof_empty=56	size=1000000	slots=1447153	load=0.691	bytes_per_elem=107.58	heap_bytes=107579464
layout	absl	s20-int	1000000	sizeof_empty=40	size=1000000	slots=2097151	load=0.477	bytes_per_elem=117.98	heap_bytes=117983240
layout	boost	s20-int	1000000	sizeof_empty=48	size=1000000	slots=1966079	load=0.509	bytes_per_elem=112.74	heap_bytes=112744456
layout	ankerl	s20-int	1000000	sizeof_empty=64	size=1000000	slots=2097152	load=0.477	bytes_per_elem=90.72	heap_bytes=90724568
layout	ct	s40-int	1000	sizeof_empty=32	size=1000	slots=2048	load=0.488	bytes_per_elem=131.66	heap_bytes=131664
layout	std	s40-int	1000	sizeof_empty=56	size=1000	slots=1109	load=0.902	bytes_per_elem=136.90	heap_bytes=136896
layout	absl	s40-int	1000	sizeof_empty=40	size=1000	slots=2047	load=0.489	bytes_per_elem=147.97	heap_bytes=147968
layout	boost	s40-int	1000	sizeof_empty=48	size=1000	slots=1919	load=0.521	bytes_per_elem=142.88	heap_bytes=142880
layout	ankerl	s40-int	1000	sizeof_empty=64	size=1000	slots=2048	load=0.488	bytes_per_elem=121.50	heap_bytes=121504
layout	ct	s40-int	100000	sizeof_empty=32	size=100000	slots=262144	load=0.381	bytes_per_elem=150.55	heap_bytes=15054936
layout	std	s40-int	100000	sizeof_empty=56	size=100000	slots=172933	load=0.578	bytes_per_elem=141.84	heap_bytes=14184472
layout	absl	s40-int	100000	sizeof_empty=40	size=100000	slots=131071	load=0.763	bytes_per_elem=117.74	heap_bytes=11773960
layout	boost	s40-int	100000	sizeof_empty=48	size=100000	slots=122879	load=0.814	bytes_per_elem=114.50	heap_bytes=11450392
layout	ankerl	s40-int	100000	sizeof_empty=64	size=100000	slots=131072	load=0.763	bytes_per_elem=126.96	heap_bytes=12695816
layout	ct	s40-int	1000000	sizeof_empty=32	size=1000000	slots=2097152	load=0.477	bytes_per_elem=133.21	heap_bytes=133210200
layout	std	s40-int	1000000	sizeof_empty=56	size=1000000	slots=1447153	load=0.691	bytes_per_elem=139.58	heap_bytes=139579416
layout	absl	s40-int	1000000	sizeof_empty=40	size=1000000	slots=2097151	load=0.477	bytes_per_elem=149.98	heap_bytes=149983240
layout	boost	s40-int	1000000	sizeof_empty=48	size=1000000	slots=1966079	load=0.509	bytes_per_elem=144.74	heap_bytes=144744472
layout	ankerl	s40-int	1000000	sizeof_empty=64	size=1000000	slots=2097152	load=0.477	bytes_per_elem=122.72	heap_bytes=122724680
growth	ct	int	cap: 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536	reallocs=18	final_cap=1048576	final_size=1000000
growth	std	int	cap: 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192	reallocs=21	final_cap=1048576	final_size=1000000
growth	boost	int	cap: 1 2 3 4 6 9 14 22 35 56 89 142 227 363	reallocs=31	final_cap=1068729	final_size=1000000
growth	ct	p16	cap: 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536	reallocs=18	final_cap=1048576	final_size=1000000
growth	std	p16	cap: 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192	reallocs=21	final_cap=1048576	final_size=1000000
growth	boost	p16	cap: 1 2 3 4 6 9 14 22 35 56 89 142 227 363	reallocs=31	final_cap=1068729	final_size=1000000
growth	ct	b256	cap: 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536	reallocs=18	final_cap=1048576	final_size=1000000
growth	std	b256	cap: 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192	reallocs=21	final_cap=1048576	final_size=1000000
growth	boost	b256	cap: 1 2 3 4 6 9 14 22 35 56 89 142 227 363	reallocs=31	final_cap=1068729	final_size=1000000
layout	ctree	int	100000	sizeof_empty=104	bytes_per_elem=41.22	allocs=251
layout	cflat	int	100000	sizeof_empty=24	bytes_per_elem=10.53	allocs=15
layout	stdmap	int	100000	sizeof_empty=48	bytes_per_elem=48.00	allocs=100000
layout	btree	int	100000	sizeof_empty=24	bytes_per_elem=11.06	allocs=3943
layout	bflat	int	100000	sizeof_empty=24	bytes_per_elem=8.19	allocs=26
layout	ctree	s20	100000	sizeof_empty=104	bytes_per_elem=65.29	allocs=398
layout	cflat	s20	100000	sizeof_empty=24	bytes_per_elem=41.98	allocs=15
layout	stdmap	s20	100000	sizeof_empty=48	bytes_per_elem=112.00	allocs=200000
layout	btree	s20	100000	sizeof_empty=24	bytes_per_elem=91.06	allocs=121064
layout	bflat	s20	100000	sizeof_empty=24	bytes_per_elem=72.80	allocs=100026
```

Crescimento do `Vector` (capacidades por que passa ao fazer `push_back` ate 1e6): ct 8, 16, 32, ... (fator 2, 18 realocacoes, primeiro bloco de 8 elementos); `std::vector` 1, 2, 4, ... (fator 2, 21 realocacoes); `boost::container::vector` 1, 2, 3, 4, 6, 9, 14, ... (fator 1,5 a 1,6, 31 realocacoes). `sizeof` do contentor vazio: `Vector` 24 em todos (tres ponteiros), `HashMap` da ct 32 contra 56 do `std::unordered_map`, 40 do absl, 48 do boost, 64 da ankerl (com `CT_DEBUG_ITERATORS=1` o `HashMap` ganha 8 bytes; na ronda 3 uma `HashMap` com valores grandes leva ainda um slab de 48 bytes: 80 no total); `String` 24 (SSO de 23) contra 32 (SSO de 15) do libstdc++; `TreeMap` 104 (112 na ronda 3: o `Pool` guarda o ultimo pedaco), `BTreeMap` 80, `FlatMap` 24, contra 48 do `std::map`.

Alocacoes para construir 1e5 `int` por `push_back`: ct 15, std 18, boost 26; um `unordered_map<int,int>` de 1e5 faz 100014 contra 15 da ct, e o pico de memoria de uma construcao de `Vector<int>` e 524 KB (ct, que usa `realloc`) contra 786 KB (std).

#### Memoria na construcao (bytes vivos por elemento, alocacoes por construcao, pico por elemento; a ct de N = 1e5 e desta ronda, as outras linhas da ronda 1)

| familia | cenario | tipo | N | impl | vivo B/elem | pico B/elem | alocacoes | RSS KiB |
|---|---|---|---:|---|---:|---:|---:|---:|
| vector | build | int | 1000 | ct | 4.11 | 4.11 | 8 | 7088 |
| vector | build | int | 100000 | ct | 5.28 | 5.28 | 15 | 44816 |
| vector | build_reserved | int | 1000 | ct | 4.02 | 4.02 | 1 | 7276 |
| vector | build_reserved | int | 100000 | ct | 4.00 | 4.00 | 1 | 7136 |
| vector | build | int | 10000000 | ct | 6.71 | 6.71 | 22 | 43244 |
| vector | build_reserved | int | 10000000 | ct | 4.00 | 4.00 | 1 | 43028 |
| vector | build | p16 | 1000 | ct | 16.40 | 16.40 | 8 | 6892 |
| vector | build | p16 | 100000 | ct | 21.01 | 21.01 | 15 | 44380 |
| vector | build_reserved | p16 | 1000 | ct | 16.02 | 16.02 | 1 | 7120 |
| vector | build_reserved | p16 | 100000 | ct | 16.00 | 16.00 | 1 | 7104 |
| vector | build | p16 | 10000000 | ct | 26.84 | 26.84 | 22 | 160344 |
| vector | build_reserved | p16 | 10000000 | ct | 16.00 | 16.00 | 1 | 160004 |
| vector | build | mo | 1000 | ct | 24.59 | 36.90 | 8 | 7108 |
| vector | build | mo | 100000 | ct | 31.50 | 47.27 | 15 | 44808 |
| vector | build_reserved | mo | 1000 | ct | 24.02 | 24.02 | 1 | 7012 |
| vector | build_reserved | mo | 100000 | ct | 24.00 | 24.00 | 1 | 7124 |
| vector | build | mo | 10000000 | ct | 40.27 | 60.40 | 22 | 397320 |
| vector | build_reserved | mo | 10000000 | ct | 24.00 | 24.00 | 1 | 238348 |
| vector | build | b256 | 1000 | ct | 262.16 | 262.16 | 8 | 7148 |
| vector | build | b256 | 100000 | ct | 335.59 | 335.59 | 15 | 28916 |
| vector | build_reserved | b256 | 1000 | ct | 256.02 | 256.02 | 1 | 7212 |
| vector | build_reserved | b256 | 100000 | ct | 256.00 | 256.00 | 1 | 29080 |
| vector | build | b256 | 1000000 | ct | 268.44 | 268.44 | 18 | 253864 |
| vector | build_reserved | b256 | 1000000 | ct | 256.00 | 256.00 | 1 | 254016 |
| map | build | int-int | 100000 | ct | 23.63 | 35.47 | 15 | 45352 |
| map | build | int-p16 | 100000 | ct | 65.54 | 98.30 | 15 | 17320 |
| map | build | int-mo | 100000 | ct | 86.51 | 129.76 | 15 | 21452 |
| map | build | int-b256 | 100000 | ct | 295.63 | 302.86 | 100015 | 68000 |
| map | build | s8-int | 100000 | ct | 86.55 | 129.84 | 15 | 54072 |
| map | build | s20-int | 100000 | ct | 86.55 | 129.84 | 15 | 53448 |
| map | build | s40-int | 100000 | ct | 150.55 | 192.76 | 100015 | 66144 |
| string | append_char | char | 100000 | ct | 1.88 | 1.88 | 13 | 44904 |
| misc | push_back | int | 1000 | ct | 4.11 | 4.11 | 8 | 7260 |
| misc | push_back | int | 100000 | ct | 5.28 | 5.28 | 15 | 44792 |
| misc | push_back | p16 | 1000 | ct | 16.40 | 16.40 | 8 | 7120 |
| misc | push_back | p16 | 100000 | ct | 21.01 | 21.01 | 15 | 44768 |
| misc | push_back | b256 | 1000 | ct | 262.16 | 262.16 | 8 | 6796 |
| misc | push_back | b256 | 100000 | ct | 335.59 | 335.59 | 15 | 29252 |
| misc | push_back | int | 1000000 | ct | 4.19 | 4.19 | 18 | 7928 |
| misc | build | int | 1000 | ctree | 50.53 | 50.53 | 4 | 6744 |
| misc | build | int | 1000 | cflat | 8.21 | 8.21 | 8 | 7092 |
| misc | build | int | 100000 | ctree | 41.22 | 41.22 | 251 | 8616 |
| misc | build | int | 100000 | cflat | 10.49 | 10.49 | 15 | 7132 |
| misc | build | int | 1000000 | ctree | 41.15 | 41.15 | 2455 | 51848 |
| misc | build | s20 | 1000 | ctree | 66.83 | 66.83 | 5 | 7220 |
| misc | build | s20 | 1000 | cflat | 32.78 | 49.18 | 8 | 7212 |
| misc | build | s20 | 100000 | ctree | 65.29 | 65.29 | 398 | 15376 |
| misc | build | int | 10000 | ctree | 42.07 | 42.07 | 28 | 44920 |
| misc | build | int | 10000 | cflat | 13.52 | 13.52 | 12 | 44808 |

### 3.4 Custo de compilacao (uma unidade com 10 tipos por contentor)

| biblioteca | modo | tempo s | memoria pico KiB | objeto B |
|---|---|---:|---:|---:|
| ct | syntax | 1.23 | 87124 | 0 |
| ct | obj | 10.05 | 210708 | 279040 |
| std | syntax | 3.11 | 183504 | 0 |
| std | obj | 15.80 | 332388 | 392688 |
| absl | syntax | 3.92 | 320060 | 0 |
| absl | obj | 12.59 | 445188 | 1212704 |
| boost | syntax | 3.68 | 328652 | 0 |
| boost | obj | 35.43 | 485604 | 751488 |
| ct (origin/main) | syntax | 1.39 | 86772 | 0 |
| ct (origin/main) | obj | 11.90 | 206328 | 268688 |

Tempo em segundos de relogio com a maquina carregada (por isso mais alto que numa maquina livre, onde a unidade da ct fez 4,0 s e a do std 6,4 s, e a razao e a mesma), memoria de pico do compilador, tamanho do ficheiro objeto. `syntax` e `-fsyntax-only` (so analise e instanciacao), `obj` e `-c`. A unidade instancia `Vector` e `Deque` com 10 tipos, `HashMap<int, T>` e o mapa ordenado com 10 tipos, o conjunto de hash com 6 tipos, mais `Function` e a fila de prioridade; as colunas `absl` e `boost` so tem os seus mapas e vetores. `std` nao tem mapa de vetor plano.


Ronda 2 (a mesma unidade, as quatro bibliotecas outra vez, com a ct de agora, que tem o `numconv.hpp`, o `HashMap` e o `HashSet` novos e o `Vector` com duas funcoes a mais):

| biblioteca | modo | tempo s | memoria pico KiB | objeto B |
|---|---|---:|---:|---:|
| ct | syntax | 2.70 | 91940 | 0 |
| ct | obj | 25.68 | 225468 | 290712 |
| std | syntax | 8.67 | 183856 | 0 |
| std | obj | 36.98 | 332888 | 392688 |
| absl | syntax | 12.26 | 319764 | 0 |
| absl | obj | 30.51 | 445144 | 1212704 |
| boost | syntax | 6.68 | 328404 | 0 |
| boost | obj | 34.69 | 486020 | 751488 |

A maquina estava a carga de 50 a 250 (outros trabalhos), por isso os segundos nao se comparam com os de cima; a razao da ct para o `std` foi de 0,31 em `syntax` (0,40 na ronda 1) e de 0,69 em `obj` (0,64 na ronda 1), e o objeto da ct passou de 279040 para 290712 bytes (+4 %): o `numconv.hpp` (as tabelas de 683 potencias de 5 de 128 bits) e o caminho novo do `HashMap` custam compilacao.

### 3.5 Antes e depois (so ct, ronda 2: de origin/main 7fef995 a este ramo)

Instrucoes (callgrind) e bytes pedidos ao malloc, o que e deterministico; so aparecem as celulas que mudaram 0,5 % ou mais. O tempo
da ct desta ronda so se mediu na `HashMap` (3.2b). Fora do que esta aqui (todas as familias, N = 1e5) nada mudou: `Vector`, `Deque`, `sort`, `Function`,
`TreeMap` e `FlatMap` com as mesmas instrucoes. A matriz da `HashMap` e a de `String` a N = 1e3 e 1e5 inteiras (todos os tipos
e cenarios, antes e depois) esta em `docs/comparacao-tabelas.md`.

#### ct: instrucoes por operacao, antes e depois das correcoes

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|
| map | build | s8-int | 100000 | 391.77 | 332.12 | -15.2% |
| map | hit | s8-int | 100000 | 122.66 | 125.04 | +1.9% |
| map | miss | s8-int | 100000 | 97.26 | 83.13 | -14.5% |
| map | erase | s8-int | 100000 | 153.35 | 139.35 | -9.1% |
| map | copy | s8-int | 100000 | 60.73 | 59.97 | -1.2% |
| map | build | s40-int | 100000 | 910.18 | 882.00 | -3.1% |
| map | hit | s40-int | 100000 | 160.64 | 165.03 | +2.7% |
| map | miss | s40-int | 100000 | 134.74 | 124.88 | -7.3% |
| map | erase | s40-int | 100000 | 360.26 | 329.18 | -8.6% |
| string | append_char | char | 100000 | 24.06 | 20.07 | -16.6% |
| string | eq | len40 | 100000 | 58.63 | 53.63 | -8.5% |
| string | concat | len30 | 100000 | 629.02 | 392.02 | -37.7% |
| string | int2s | full | 100000 | 185.62 | 126.32 | -31.9% |
| string | s2int | full | 100000 | 175.26 | 90.03 | -48.6% |
| string | dbl2s | d | 100000 | 2572.24 | 306.51 | -88.1% |
| string | s2f32 | f | 100000 | 659.31 | 132.27 | -79.9% |
| misc | pushpop | int | 100000 | 358.44 | 272.74 | -23.9% |

#### ct: alocacoes por execucao, antes e depois

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|

#### ct: bytes pedidos ao malloc por execucao, antes e depois

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|
| string | concat | len30 | 100000 | 4700000.00 | 3100000.00 | -34.0% |

#### ct: bytes vivos no fim da regiao medida, antes e depois

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|

### 3.6 `-O3` (instrucoes por operacao, N = 1e5, um subconjunto)

Mesmo compilador e opcoes (`g++ 13.3 -std=c++17 -O3 -DNDEBUG`), 72 cenarios: o `HashMap` com chaves `int` e de 8 caracteres, `Vector`, `String`, `sort` com comparador, `Deque`, `PriorityQueue` e os numeros. A razao da ct para o melhor dos outros a `-O3`
contra a `-O2` (as mesmas colunas nos dois): **mudam de lado de 1** `build_reserved` de `int` do `Vector` (1,09 para 1,00, passa a empatar), e perde onde a `-O2` ganhava: `build` de `int-int` (0,94 para 1,15), `build`, `hit` e `miss` de `s8-int` (0,90 para 1,19, 0,94 para 1,08 e 0,88 para 1,11); e a ct perde vantagem, sem mudar de lado de 1, em `erase` de `int-int` e de `s8-int` (0,80 para 0,86 e 0,79 para 0,94), `create` de `len8` (0,71 para 1,00) e `int2s` (0,86 para 0,99). O `push_back` de `P16` do `Deque` ganha menos a `-O3` (0,41 para 0,58). A ordenacao
(`sort` com comparador aleatorio de `int` 1,27 para 1,31, `P16` 1,38) e `push_front` (1,62 e 1,49) ficam como estavam. A razao nao e so a da ct: tambem o `-O3` muda o codigo das outras bibliotecas (a tabela mostra as instrucoes por operacao de todas).

#### Ir por operacao com -O3, vector

| cenario | tipo | N | ct | std | boost | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|
| build | int | 100000 | 12.4 | 16.3 | 18.9 | 0.76 |
| build_reserved | int | 100000 | 11.0 | 11.1 | - | 1.00 |

#### Ir por operacao com -O3, map

| cenario | tipo | N | ct | std | absl | boost | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|
| build | int-int | 100000 | 130.5 | 582.8 | 142.7 | 114.0 | **1.15** |
| hit | int-int | 100000 | 24.9 | 30.4 | 50.5 | 42.5 | 0.82 |
| miss | int-int | 100000 | 24.5 | 24.1 | 46.4 | 38.0 | 1.01 |
| erase | int-int | 100000 | 44.9 | 204.7 | 71.2 | 52.3 | 0.86 |
| build | s8-int | 100000 | 329.5 | 710.6 | 344.4 | 277.5 | **1.19** |
| hit | s8-int | 100000 | 125.1 | 119.4 | 115.5 | 140.8 | 1.08 |
| miss | s8-int | 100000 | 83.1 | 74.9 | 81.2 | 95.7 | **1.11** |
| erase | s8-int | 100000 | 144.9 | 333.1 | 172.1 | 154.2 | 0.94 |

#### Ir por operacao com -O3, string

| cenario | tipo | N | ct | std | charconv | fmt | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|
| create | len8 | 100000 | 51.0 | 51.0 | - | - | 1.00 |
| create | len16 | 100000 | 49.0 | 302.1 | - | - | 0.16 |
| create | len24 | 100000 | 262.0 | 302.1 | - | - | 0.87 |
| create | len40 | 100000 | 263.0 | 303.1 | - | - | 0.87 |
| eq | len40 | 100000 | 50.6 | 41.6 | - | - | **1.22** |
| concat | len30 | 100000 | 389.0 | 516.1 | - | - | 0.75 |
| int2s | full | 100000 | 124.3 | - | 185.0 | 125.0 | 0.99 |
| dbl2s | d | 100000 | 231.0 | - | 828.1 | 1090.2 | 0.28 |

#### Ir por operacao com -O3, sort

| cenario | tipo | N | ct:sort | std:sort | ct:stable | std:stable | boost:sort | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| - | int.random | 100000 | 163.2 | 124.1 | 202.9 | 182.5 | - | **1.31** |
| - | p16.random | 100000 | 167.1 | 131.3 | - | - | 121.0 | **1.38** |

#### Ir por operacao com -O3, misc

| cenario | tipo | N | ct | std | ct / melhor outro |
|---|---|---:|---:|---:|---:|
| push_back | int | 100000 | 15.4 | 16.5 | 0.93 |
| push_front | int | 100000 | 21.5 | 13.3 | **1.62** |
| push_back | p16 | 100000 | 18.4 | 31.9 | 0.58 |
| push_front | p16 | 100000 | 40.2 | 27.1 | **1.48** |
| pushpop | int | 100000 | 271.8 | 308.8 | 0.88 |

A mesma matriz a -O2 esta na seccao 3.1; a ordem entre a ct e as outras muda onde a razao da ultima coluna passa de um lado de 1 para o outro (por exemplo, compare-se `hit` e `miss` de `int-int` entre as duas).

### 3.7 Ronda 3

Instrucoes (callgrind) e contagens de alocacoes, bytes e mispredicoes de salto (`--branch-sim=yes`), que sao deterministicas. A maquina estava
partilhada e foi-se tornando mais pesada durante a ronda (a carga de 3 a 13, e no fim o dono pediu prioridade baixa para tudo), por isso o tempo de
CPU que aqui aparece e o minimo de 3 execucoes alternadas e **vale como indicacao**, nao como medida; onde decide e a contagem de instrucoes, de
alocacoes, de bytes e de saltos falhados. O "antes" de cada tabela e o cabecalho de origin/main de 63793ca compilado **com o mesmo
`bench/compare` que o "depois"** (uma unidade de compilacao maior muda as decisoes de inlining do GCC: sem isto a mesma `HashMap<int, P16>`
dava mais 1 instrucao no `hit` e mais 8 no `iterate` sem que o codigo da biblioteca mudasse; ver 6.15).

#### 3.7.1 `HashMap` com entradas grandes: a chave fica na tabela, o valor num bloco de um slab

A ronda 2 pos as entradas de mais de 128 bytes fora da tabela, uma alocacao por entrada, e pagou com `hit` +49 %, `erase` 5,7 vezes mais lento
e `build`, `erase` e `copy` com 2 a 10 vezes as instrucoes. Agora o slot guarda a **chave** e uma referencia ao valor, e o valor vive num bloco de
um **slab por mapa** (pedacos de 4 KiB, uma pilha de ponteiros para os blocos livres): um `hit` ou um `miss` compara a chave no slot e nao
vai ao bloco, apagar empilha o ponteiro (sem tocar no bloco), copiar e uma passagem com um pedaco do tamanho certo, e uma chave grande (mais de
32 bytes) continua a por a entrada inteira num bloco do mesmo slab. Instrucoes por operacao, `int-b256`, N = 1e5 (a layout de antes da ronda 2, a da ronda 2 e a desta):

| cenario | antes da ronda 2 (inline) | ronda 2 | ronda 3 | ronda 3 / inline | alvo |
|---|---:|---:|---:|---:|---|
| build | 351.4 | 788.1 | 320.6 | 0.91 | <= ronda 2: sim |
| hit | 31.1 | 36.7 | 30.8 | 0.99 | dentro de 5 %: sim |
| miss | 27.7 | 31.0 | 26.9 | 0.97 | dentro de 5 %: sim |
| erase | 57.3 | 291.3 | 70.7 | 1.23 | dentro de 1,5 vezes: sim |
| copy | 58.0 | 565.7 | 69.6 | 1.20 | dentro de 1,5 vezes: sim |
| iterate | 21.4 | 19.4 | 18.5 | 0.86 | |
| churn | 302.3 | 496.1 | 291.5 | 0.96 | |

Alocacoes por construcao de 1e5 entradas: 15 (inline), 100015 (ronda 2), **6265** (agora); copia: 1, 100001, **2**; 1e6 passos de `churn`: 0, 1000000, **1**.
Em N = 1e3 as mesmas celulas: `build` 770,4 para 305,3, `hit` 42,1 para 35,8, `miss` 43,6 para 37,7, `erase` 303,5 para 91,8, `copy` 559,6 para 65,4 (ronda 2 para ronda 3).

**Bytes por elemento** (`layout`, contador de alocacoes, com os 8 bytes de cabecalho do malloc): inline 544,8 / 694,7 / 555,8; ronda 2 290,5 / 295,6 / 290,9; ronda 3
**294,9 / 302,6 / 293,7** (N = 1e3 / 1e5 / 1e6). **O alvo "bytes por elemento a baixo ou igual aos da ronda 2" nao se atingiu**: mais 1,5 % a mais 2,4 %. A causa:
o slot passa de 9 para 17 bytes (a chave de 4 bytes mais a referencia de 8, alinhada a 8, mais a meta), o que a 38 % de carga (N = 1e5, 262144 slots) custa 44,6
bytes por elemento contra 23,6, e o que se poupa e o cabecalho do malloc de cada entrada (264 bytes pedidos eram 272 no malloc, agora o bloco e de 256 e o pedaco
de 4 KiB custa 2 bytes por bloco). Um slot de 13 bytes (chave e ponteiro empacotados) nao serve: o `Entry &` que os iteradores devolvem tem de ter `.key` e `.value`
como membros, e um membro `V &` pede alinhamento de 8. Ainda assim a memoria fica 57 % abaixo da de antes da ronda 2 (695 para 303).

**Tempo** (CPU, minimo de 3 alternados, N = 1e5 | 1e6, ns por operacao; ruido de 10 a 30 %): `build` 904 | 932 (inline), 312 | 463 (ronda 2), **233 | 276**; `hit` 30,0 | 61,6, 42,3 | 86,0, **37,1 | 70,2**;
`miss` 18,6 | 66,6, 23,9 | 65,3, **12,9 | 53,6**; `erase` 51,5 | 99,5, 262,0 | 409,8, **36,0 | 78,7**; `copy` 438, 328, **79,8**; `churn` 262, 167, **134**; `iterate` 14,6, 15,9, **17,4**.
O que fica por recuperar e o `hit` (+24 % contra o inline a 1e5, +14 % a 1e6: uma terceira linha de cache, a do bloco, que o chamador le logo a seguir) e o `iterate` (+20 %: os valores
estao na ordem de insercao e a iteracao anda pela ordem dos slots).

##### O valor de `CT_HASH_INLINE_MAX` (varrimento de 32 a 1024 bytes de valor)

`int-b32` ... `int-b1024` (`bench/compare/cmp_map.cpp`), chave `int` (a entrada tem 8 + tamanho do valor bytes), N = 1e5, a tabela inline (`CT_HASH_INLINE_MAX=100000`)
contra o valor fora (`CT_HASH_INLINE_MAX=0`). Razao fora / inline, tempo de CPU (menos de 1 e melhor para o valor fora):

| operacao | 32 | 64 | 96 | 128 | 192 | 256 | 512 | 1024 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| build | 0,51 | 0,46 | 0,34 | 0,31 | 0,27 | 0,26 | 0,23 | 0,23 |
| hit | 1,44 | 1,24 | 1,18 | 1,28 | 1,24 | 1,22 | 1,00 | 1,11 |
| miss | 0,92 | 0,71 | 0,68 | 0,63 | 0,76 | 0,79 | 0,44 | 0,68 |
| erase | 1,24 | 0,80 | 0,83 | 0,80 | 0,60 | 0,66 | 0,67 | 0,54 |
| iterate | 1,91 | 1,51 | 1,24 | 1,26 | 1,25 | 1,33 | 1,24 | 1,23 |
| copy | 1,31 | 1,14 | 1,09 | 0,22 | 0,21 | 0,18 | 0,44 | 0,52 |
| bytes por elemento (1e5) | 0,72 | 0,57 | 0,51 | 0,48 | 0,45 | 0,44 | 0,41 | 0,40 |

A 1e6 (so 64, 128 e 256): `build` 0,40 / 0,35 / 0,33, `hit` 1,13 / 1,01 / 1,24, `miss` 0,86 / 0,87 / 0,88, `erase` 1,08 / 0,87 / 0,79, `iterate` 1,72 / 1,42 / 1,49, `copy` 0,84 / 0,85 / 0,70.

Instrucoes por operacao, mesmo varrimento, valor fora / inline (a tabela inline e a de `CT_HASH_INLINE_MAX=100000`); `hit`, `miss`, `build`, `iterate` e `copy` medidos com os blocos livres numa lista dentro do proprio bloco, o `erase` e o `churn` com a pilha de ponteiros do codigo final (so nos tamanhos de 64, 128, 256 e 1024 bytes):

| operacao | b32 | b64 | b96 | b128 | b192 | b256 | b512 | b1024 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| build | 1,05 | 1,03 | 0,97 | 0,94 | 0,94 | 0,91 | 0,86 | 0,79 |
| hit | 1,08 | 1,08 | 1,03 | 0,99 | 1,03 | 0,99 | 0,99 | 0,99 |
| miss | 1,03 | 1,03 | 1,00 | 0,97 | 1,00 | 0,97 | 0,97 | 0,97 |
| iterate | 1,11 | 1,11 | 1,03 | 0,97 | 1,03 | 0,97 | 0,97 | 0,97 |
| copy | 1,50 | 1,44 | 1,39 | 1,35 | 1,29 | 1,18 | 1,15 | 1,09 |
| erase |  | 1,40 |  | 1,29 |  | 1,23 |  | 0,99 |
| churn |  | 1,13 |  | 1,03 |  | 0,96 |  | 0,91 |

A leitura: o valor fora ganha sempre no `build` (2 a 4 vezes) e na memoria (menos 28 % a menos 60 %) e no `miss` (de 64 bytes para cima); o `erase` e o `copy`
passam a ganhar a partir de 64 e de 128 bytes; o `hit` e o `iterate` perdem quase em todos os tamanhos (de 0 a 44 % e de 23 a 91 %; mais nos valores pequenos).
A curva nao tem um joelho unico: a 128 a troca passa a ser favoravel em 5 das 6 operacoes (so o `hit` e o `iterate` ficam atras) e a memoria cai a metade, e abaixo de 96 o `copy` e
o `erase` ainda perdem. **O valor por omissao fica em 128** (entradas de mais de 128 bytes, isto e, valores de 128 bytes ou mais com chave `int`): e o ponto onde o
`copy` deixa de perder, e quem quiser o `hit` e a iteracao mais depressa a custa de memoria define `CT_HASH_INLINE_MAX` maior.

#### 3.7.2 `BTreeMap` e `BTreeSet` (`include/ct/btreemap.hpp`, `btreeset.hpp`, `detail/btree.hpp`)

Uma B+ arvore: os itens so nas folhas, os nos internos guardam copias das chaves separadoras, as folhas ligam-se para a frente (a iteracao e um varrimento de memoria contigua), os nos (512 bytes por omissao, 62
itens `int`->`int` por folha, 15 itens `String`->`int`) saem de um slab (`detail/slab.hpp`: sem cabecalho de malloc, um malloc por 8 nos). Pesquisa binaria sem saltos dentro do no. Numa folha cheia o novo
item vai primeiro para o vizinho da esquerda ou da direita se tiver lugar (a arvore enche-se melhor que com a divisao a meio) e so depois se divide; inserir por ordem crescente divide a ultima folha deixando as outras cheias; apagar
empresta ou funde com um vizinho (a fusao so se cabe numa folha); `assign_sorted` constroi os nos cheios de uma entrada estritamente crescente em O(n) (sem ser crescente cai em `put` por elemento); a copia mantem a forma. A API e a do `TreeMap`
(`put`, `find`, `get`, `contains`, `operator[]`, `erase`, `clear`, `begin`/`end`, `lower_bound_it`, `validate`) mais `lower_bound`, `upper_bound`, `first`, `last`, `erase_range(lo, hi)`, `erase_from(lo)`, `erase(primeiro, ultimo)`, `assign_sorted`, `swap`.
As garantias sao **mais fracas** (dito no cabecalho): inserir e apagar **invalidam** todos os iteradores, ponteiros e referencias para itens (os itens movem-se entre nos); com `CT_DEBUG_ITERATORS=1` (explicita, nao vem de `NDEBUG`) um iterador usado depois de a arvore mudar aborta. `K` tem de ser copiavel.
Por isso o `TreeMap` (que promete ponteiros estaveis) fica como esta.

O tamanho do no e a pesquisa escolheram-se por medida (instrucoes, mispredicoes e falhas da D1 de 3e5 pesquisas de `int`, `hit`, cachegrind):

| variante | instrucoes por pesquisa | saltos falhados | falhas D1 | bytes por elemento (1e4 / 1e6) |
|---|---:|---:|---:|---:|
| ct, nos de 256, pesquisa binaria | 386 | 1,57 | 6,4 | 11,15 / 10,91 |
| ct, nos de 256, contar quantas chaves sao menores | 650 | 5,0 | 8,1 | |
| ct, nos de 256, internos de 512 | 361 | 1,4 | 6,0 | |
| **ct, nos de 512, pesquisa binaria (a que ficou)** | 358 | 1,0 | 6,3 | **10,32 / 9,99** |
| abseil `btree_map` | 371 | 5,2 | 6,8 | 11,10 / 11,10 |

(a contagem linear fez mais instrucoes e os mesmos saltos falhados que a pesquisa linear com saida antecipada do abseil, e ficou 6 % mais depressa em tempo mas com o dobro das instrucoes; os nos internos mais largos nao ganharam nada medido). Os nos de 512 bytes fecham o alvo de memoria (abaixo do abseil), os de 256 ficavam 0,5 % acima.

Bytes por elemento (`layout`, contador de alocacoes; o abseil e o `std::map` com `std::string`, a ct com `ct::String`, por isso o texto de 20 caracteres vive na heap no abseil e no SSO na ct):

| tipo | N | cbtree (ct) | abseil btree | TreeMap (ct) | std::map |
|---|---:|---:|---:|---:|---:|
| int | 10000 | 10,32 | 11,10 | 42,07 | 48,00 |
| int | 1000000 | 9,99 | 11,10 | 41,15 | 48,00 |
| s8 | 10000 | 44,58 | 59,11 | 66,80 | 80,00 |
| s8 | 1000000 | 44,59 | 58,83 | 65,23 | 80,00 |
| s20 | 10000 | 44,58 | 91,11 | 66,80 | 112,00 |
| s20 | 1000000 | 44,59 | 90,83 | 65,23 | 112,00 |

Instrucoes por operacao, `int` e texto, N = 1e4 (`cmp_misc ordered`, as outras colunas da ronda 1/2; abseil e `std::map`):

| tipo | cenario | N | cbtree | abseil | TreeMap | std::map | cbtree / abseil |
|---|---|---:|---:|---:|---:|---:|---:|
| int | build | 10000 | 429,8 | 443,7 | 397,3 | 628,5 | 0,97 |
| int | hit | 10000 | 263,2 | 269,8 | 87,2 | 143,8 | 0,98 |
| int | miss | 10000 | 261,1 | 262,2 | 92,2 | 141,8 | 1,00 |
| int | erase | 10000 | 271,8 | 499,1 | 310,0 | 378,4 | 0,54 |
| int | iterate | 10000 | 6,4 | 18,7 | 19,9 | 23,1 | 0,34 |
| int | range | 10000 | 273,9 | 488,8 | 294,9 | 321,0 | 0,56 |
| int | bulk | 10000 | 30,4 | 165,7 | 491,0 | 724,3 | 0,18 |
| s8 | build | 10000 | 1476,3 | 1209,6 | 1181,1 | 1333,9 | 1,22 |
| s8 | hit | 10000 | 1172,9 | 693,5 | 881,5 | 653,5 | 1,69 |
| s8 | miss | 10000 | 1173,4 | 731,7 | 938,6 | 644,1 | 1,60 |
| s8 | erase | 10000 | 1203,8 | 1083,4 | 965,5 | 1078,6 | 1,11 |
| s8 | iterate | 10000 | 7,7 | 25,2 | 19,4 | 23,1 | 0,31 |
| s8 | range | 10000 | 1184,4 | 981,3 | 804,6 | 804,3 | 1,21 |
| s8 | bulk | 10000 | 120,5 | 550,7 | 2119,7 | 1716,0 | 0,22 |
| s20 | build | 10000 | 1480,2 | 1535,2 | 1184,8 | 1753,6 | 0,96 |
| s20 | hit | 10000 | 1177,6 | 695,9 | 885,2 | 656,9 | 1,69 |
| s20 | miss | 10000 | 1178,0 | 735,6 | 942,6 | 648,0 | 1,60 |
| s20 | erase | 10000 | 1208,0 | 1187,9 | 969,6 | 1234,5 | 1,02 |
| s20 | iterate | 10000 | 7,7 | 25,2 | 19,4 | 23,1 | 0,31 |
| s20 | range | 10000 | 1188,7 | 985,2 | 807,3 | 807,8 | 1,21 |
| s20 | bulk | 10000 | 120,6 | 1610,9 | 2130,8 | 2554,5 | 0,07 |

Tempo de CPU em ns por operacao (minimo de 3 alternados; a maquina com carga de 5 a 9 e prioridade baixa: **indicativo, ruido de 10 a 30 %**); `cbtree` e a ct, a ultima coluna a ct a dividir pelo abseil (alvo: ate 1,2):

| tipo | cenario | N | cbtree | abseil | TreeMap | std::map | cbtree / abseil |
|---|---|---:|---:|---:|---:|---:|---:|
| int | build | 10000 | 75,62 | 66,10 | 134,67 | 138,26 | 1,14 |
| int | hit | 10000 | 54,99 | 47,03 | 68,51 | 75,80 | 1,17 |
| int | miss | 10000 | 54,07 | 46,41 | 78,55 | 78,95 | 1,16 |
| int | erase | 10000 | 71,28 | 91,18 | 99,31 | 132,44 | 0,78 |
| int | iterate | 10000 | 0,83 | 1,64 | 9,81 | 12,26 | 0,50 |
| int | range | 10000 | 60,01 | 71,85 | 175,42 | 187,05 | 0,84 |
| int | bulk | 10000 | 2,35 | 26,96 | 69,37 | 127,62 | 0,09 |
| int | build | 1000000 | 215,99 | 188,30 | 788,95 | 963,14 | 1,15 |
| int | hit | 1000000 | 209,27 | 187,39 | 545,78 | 759,70 | 1,12 |
| int | miss | 1000000 | 209,07 | 185,74 | 812,57 | 861,67 | 1,13 |
| int | erase | 1000000 | 180,85 | 221,76 | 691,56 | 832,93 | 0,82 |
| int | iterate | 1000000 | 2,80 | 5,84 | 118,34 | 120,46 | 0,48 |
| int | range | 1000000 | 305,25 | 335,50 | 1374,36 | 1781,85 | 0,91 |
| int | bulk | 1000000 | 14,55 | 24,10 | 91,66 | 184,35 | 0,60 |
| s8 | build | 10000 | 203,18 | 208,57 | 204,56 | 233,14 | 0,97 |
| s8 | hit | 10000 | 184,47 | 149,45 | 154,90 | 148,85 | 1,23 |
| s8 | miss | 10000 | 175,92 | 162,16 | 170,30 | 155,40 | 1,08 |
| s8 | erase | 10000 | 188,85 | 193,47 | 166,49 | 196,19 | 0,98 |
| s8 | iterate | 10000 | 0,70 | 3,19 | 9,64 | 11,65 | 0,22 |
| s8 | range | 10000 | 167,11 | 192,29 | 215,80 | 240,05 | 0,87 |
| s8 | bulk | 10000 | 38,00 | 51,77 | 208,81 | 192,19 | 0,73 |
| s8 | build | 1000000 | 642,32 | 677,73 | 1112,95 | 1360,99 | 0,95 |
| s8 | hit | 1000000 | 685,23 | 681,77 | 904,25 | 1171,37 | 1,01 |
| s8 | miss | 1000000 | 650,49 | 691,54 | 1116,28 | 1125,33 | 0,94 |
| s8 | erase | 1000000 | 663,38 | 846,25 | 1047,06 | 1314,33 | 0,78 |
| s8 | iterate | 1000000 | 11,30 | 28,23 | 123,75 | 125,79 | 0,40 |
| s8 | range | 1000000 | 742,90 | 921,38 | 2105,30 | 2285,93 | 0,81 |
| s8 | bulk | 1000000 | 79,79 | 129,92 | 455,10 | 337,53 | 0,61 |

Contra o `TreeMap` o `BTreeMap` faz 3 vezes as instrucoes no `hit` de `int` (263 contra 87, a pesquisa binaria de 62 itens por no contra um ponteiro por no) e e 1,2 a 3,9 vezes mais depressa em tempo, porque a arvore rubro-negra falha a cache em cada no e a B+ em tres ou quatro.

`bulk` e construir de um intervalo ordenado (`assign_sorted` contra o construtor de intervalo do abseil e do `std::map`, e `put` por elemento no `TreeMap`), `range` e `lower_bound` mais 8 vizinhos. Em `int`, a 1e4 e a 1e6, o `build`, o `hit` e o `miss` ficam a 1,12 a 1,17 vezes o tempo do abseil (dentro do alvo de 1,2,
mas sem margem), o `erase` a 0,78 a 0,82, a iteracao a 0,5, o `range` a 0,84 a 0,91 e o `bulk` a 0,09 a 0,6. Em instrucoes `int` a 1e4: `hit` 263 contra 270, `erase` 272 contra 499, iterar 6,4 contra 18,7, `bulk` 30 contra 166. **Com chaves de texto o abseil ganha no `hit` e no `miss` em instrucoes (1,7 vezes)**:

O `hit` e o `miss` de texto fazem 1173 instrucoes contra 693 a 732 do abseil (1,6 a 1,7 vezes): 15 a 21 comparacoes de `ct::String` por pesquisa (a pesquisa binaria de 62 ou 15 itens por no, tres niveis), cada uma a 56 a 67 instrucoes contra 46 do `std::string` no abseil. Em tempo o `s8` a 1e4 fica a 1,23 (`hit`) e 1,08 (`miss`) do abseil e a 0,97 no `build`, 0,98 no `erase` e 0,22 na iteracao: a ct guarda a chave inline no no (SSO) e o abseil tem de ir buscar o `std::string` de cada comparacao.

#### 3.7.3 `ct::sort` e `ct::stable_sort`

`ct::sort` com comparador passa a particionar por blocos (BlockQuicksort: blocos de 64 elementos, os desvios dos elementos do lado errado guardam-se com um incremento em vez
de um salto e trocam-se aos pares) para tipos trivialmente copiaveis de ate 32 bytes; a mediana passa a ser de nove amostras acima de 128 elementos; os iguais ao pivo vao todos
para a esquerda de uma vez quando o pivo nao e maior que o elemento antes do intervalo (o `partition_left` do pdqsort); a particao le sempre dentro do intervalo (blocos de
tamanho fixo), por isso as guardas contra um comparador mau ficam. Fica a deteccao de entrada ordenada ou invertida de antes (agora tambem em `stable_sort`), o limite de
profundidade com heapsort, e o `std::sort`-like Hoare com guardas para os tipos grandes e os que nao sao trivialmente copiaveis (`B256`, `String`). `stable_sort` de tipos pequenos
funde de um vetor para o outro (buffer de n elementos, antes n/2) e escolhe o menor sem salto quando, nos 32 primeiros elementos da fusao, o lado muda mais de 8 vezes (dados ao acaso);
em blocos (repetidos, inversoes) fica o salto, que ai e previsivel.

Instrucoes por elemento, N = 1e5, ct antes e depois (`-O2`); as colunas das outras bibliotecas sao as de 3.1 (e a da EASTL, de 3.7.7):

| padrao | algoritmo | antes | depois | variacao | std | boost pdqsort | EASTL |
|---|---|---:|---:|---:|---:|---:|---:|
| int.random | sort | 163,2 | 208,8 | +27,9 % | 128,2 | 118,0 | 125,1 |
| p16.random | sort | 167,1 | 189,6 | +13,5 % | 135,9 | 121,2 | 135,5 |
| int.dups | sort | 134,8 | 58,4 | -56,7 % | 110,3 | 32,5 | |
| p16.dups | sort | 134,8 | 49,2 | -63,5 % | 125,3 | 32,3 | |
| int.sorted | sort | 7,0 | 7,0 | 0 | 117,2 | 11,0 | 71,5 |
| int.reversed | sort | 10,0 | 10,0 | 0 | 88,1 | 18,5 | |
| int.random | stable | 221,6 | 276,6 | +24,8 % | 193,4 | | 198,9 |
| p16.random | stable | 264,7 | 319,4 | +20,7 % | 206,6 | | 196,1 |
| int.dups | stable | 214,0 | 222,0 | +3,7 % | 189,7 | | |
| p16.dups | stable | 257,5 | 223,0 | -13,4 % | 203,8 | | |
| int.sorted | stable | 92,0 | 7,0 | -92,4 % | 119,4 | | 7,1 |
| int.reversed | stable | 201,4 | 11,0 | -94,5 % | 132,5 | | |
| p16.reversed | stable | 293,7 | 11,0 | -96,3 % | 205,4 | | |

**As instrucoes sobem em dados ao acaso** (+28 % e +14 % no `sort`, +25 % e +21 % no `stable`, que ja eram a perda) e isso nao se evitou: a particao sem salto faz mais trabalho por elemento
(guardar o desvio, somar o resultado da comparacao, trocar depois) do que o salto que a substitui. O que ela compra e o salto que a particao normal falha em metade das comparacoes.
Saltos falhados por elemento (`--branch-sim=yes`, N = 1e5, `int.random`): `sort` **6,97 para 1,96** (std 7,42, boost 7,12); `stable` **7,08 para 1,88** (std 7,94). Tempo de CPU (ns por elemento,
N = 1e6, minimo de 3 alternados, ruido de 5 a 10 %, a maquina com carga de 4): `sort` `int.random` **59,5 para 24,2** (std 57,1, boost 57,2, EASTL 54,8), `p16.random` 62,1 para 25,3 (std 61,0);
`stable` `int.random` 64,8 para 55,7 (std 67,9) e `p16.random` 75,6 para 64,4 (std 82,1); `int.dups` `sort` 21,5 para 4,1 (std 20,8) e `stable`
26,9 para 29,4 (+9 %, std 30,7); `int.sorted` e `reversed` de `stable` 9,4 para 0,43 e 11,6 para 0,81. **`ct::sort` com comparador passa de empate com o `std::sort` a 2,4 vezes mais depressa em
dados ao acaso de tipos pequenos, com mais instrucoes**; se a metrica fosse so a contagem de instrucoes seria uma regressao, e por isso fica dito aqui e na seccao 5.7. `stable_sort` gasta
agora n elementos de buffer em vez de n/2 (a 1e5 `int`: 400000 bytes pedidos em vez de 200004) para tipos pequenos.
Sem regressao nos casos de antes: ordenada 7 e invertida 10 instrucoes (`sort`), os testes `EdgeSort.*` (repetidos abaixo de 2,2 milhoes de comparacoes, ordenada em n comparacoes, comparador `<=` dentro dos limites) passam;
`tests/test_sort_blocks.cpp` percorre todos os padroes em tamanhos em volta dos limites de bloco (127 a 129, 191 a 193, 319 a 321, ...), a estabilidade, os comparadores maus com ASan e os tipos que nao vao pelos blocos.

#### 3.7.4 Json, Ini, Xml e TextReader sobre `numconv.hpp`

`Json` (leitura e escrita), `Ini` (`get_double`, `set`), `Xml` (`attr_double`, `attr_int`, `attr_uint`) e `TextReader::read_double` deixam o `strtod`, o `snprintf` e o tratamento do locale: leem com
`parse_double` (arredondamento correcto, nenhum locale) e escrevem com o texto mais curto que volta ao mesmo `double` (`write_shortest_at_least`, o mesmo `write_shortest` com o limite do expoente
posicional em 10^max(15, algarismos), que e o que o `%.{15,16,17}g` fazia; fica em `detail/numconv_g.hpp`). Saem o `json_dtoa_fast` (o caminho de `long double` so de x86), o caminho lento
com `snprintf`, `json_strtod_locale`, `ini_decimal_point`, `xml_decimal_point` e `xml_strtod_locale`, e a baseline do nostd perde 17 pares (806 para 789). Um documento com 100000 numeros
(`jbench`: decimais curtos, mantissas de 17 algarismos, expoentes), ns por numero e instrucoes por numero, antes e depois:

| operacao | antes | depois |
|---|---:|---:|
| `Json::dump` | 324 ns, 2922 instrucoes | **82 ns, 705 instrucoes** |
| `Json::parse` | 230 ns, 1067 instrucoes | **148 ns, 559 instrucoes** |

Nao havia nenhuma alternativa de leitura de JSON ja descarregada (nem simdjson nem a de `std`) para comparar; fica o antes e o depois.

**O que muda no que se escreve** (diferencial contra o codigo de antes, 2000000 de `double` de oito classes: padroes de bits ao acaso, subnormais, decimais curtos, potencias de 2 e vizinhas, inteiros grandes):
52902 dos 1999880 valores (2,6 %) saem com texto diferente, e **todos** estao num de tres casos, sem nenhum valor mais longo, mais afastado ou so de formato diferente: 14115 saem mais **curtos** (`8.863094000000001e-13`
onde se escrevia `8.8630940000000015e-13`; subnormais como `1.3328149133403e-310` e `5e-324` onde se escrevia `4.94065645841247e-324`), 9978 do mesmo tamanho mas **mais proximos** do valor
(`1.5080921830664333e-308` onde se escrevia `...334e-308`: o caminho de `long double` de x86 do Json de antes arredondava mal o ultimo algarismo de uns 0,5 % dos valores) e 2358 **empates exactos** em que passa a sair o algarismo
par como o `printf` (`1489.7146606445312` e nao `...313`; `2.9802322387695312e-08`). Nao muda: os inteiros de `double` (`100.0`, `1e+15`), o `-0.0`, o `.0` que marca um real, o limite
do expoente (`1e+15` continua a ser escrito com expoente, `123456789012345.0` sem), `Json(NaN)` e `inf` escreverem `null`. `Ini::set(double)` escreve `0.3333333333333333` (16 algarismos) onde escrevia
`0.33333333333333331` (17: o `%.15g` nao voltava ao mesmo valor e passava ao `%.17g`). **Leitura**: nenhuma diferenca. Os quatro leitores (Json, Ini, Xml, TextReader) deram bit a bit o mesmo resultado que
antes em 2000000 de textos ao acaso (1 a 24 algarismos, sinal, ponto, expoente, lixo no fim, hexadecimais, `inf`, `nan(abc)`, espacos) no locale "C"; no locale `pt_BR` (300000 textos) ha seis diferencas, todas
em `Xml::attr_double` com texto que nao e um numero (devolvia 0 e agora devolve o valor por omissao, como no locale "C": era um defeito do ramo da virgula, ver 6.13). Fuzz de ida e volta do Json (`CT_JSON_ITERS=10000000`, 3,8 s,
`tests/test_parser_numbers.cpp`, o `ctest` corre 300000): **1e7 valores, 0 falhas** (os mesmos bits ao voltar a ler, texto com a gramatica de numero do JSON, os mesmos algarismos que o `write_shortest`).

#### 3.7.5 `Pool`, `Span`, `Deque`, `String`

- **`Pool`**: a deteccao do duplo `destroy` fazia uma pesquisa binaria no indice de pedacos a cada `allocate`, `create`, `destroy` e `deallocate`. Fica a verificacao toda (nao passou a so de depuracao) mas
  o ultimo pedaco por onde passou um ponteiro fica em cache (um ponteiro a mais por `Pool`: o `TreeMap` vazio passa de 104 para 112 bytes), e uma chamada que cai nesse pedaco (criar, apagar e criar em sequencia quase sempre cai) so faz uma subtracao e uma comparacao.
  Ciclo de criar 1e5, apagar metade, criar outra metade (`cmp_extra pool`): **717,3 para 173,5 instrucoes** (`new` e `delete` 582,5). O `TreeMap`, que e feito sobre a `Pool`, melhora sem mudar: `build` de `int`
  397,3 para 331,6 (-17 %), `erase` 310,0 para 194,0 (-37 %), de `s20` 1184,8 para 1094,4 e 969,6 para 839,4; `hit` e `miss` iguais; `FlatMap` igual.
  `Pool.InterleavedChunksKeepTheStateChecksExact` (criar e apagar saltando entre pedacos distantes, duplo `destroy` depois de a cache mudar).
- **`Span::subspan`**: tirado o ramo do offset zero (a verificacao de limites fica uma so): 6,3 para 6,2 instrucoes por elemento (ponteiro cru 3,5). **Nao se fecha**: a diferenca nao e a verificacao (um teste, cerca de 1 instrucao por
  `subspan`, 0,06 por elemento neste cenario) mas o `for (int x : t)` sobre um tamanho que o compilador nao conhece (`min(16, resto)`), que gasta 5 por elemento, e o ciclo do ponteiro cru sobre 16 fixos que o compilador desenrola
  em 3,5. So um `subspan` de tamanho em tempo de compilacao o resolvia, e isso e uma API nova que ninguem pediu.
- **`Deque::push_front`**: nao se mexeu. O caminho rapido tem 15 instrucoes contra 13 do `std::deque`; as 21,5 medidas incluem 6,4 por elemento que sao o `memcpy` do `realloc` quando a fila cresce (o callgrind conta o `rep movsb` byte a byte: 29,8 % das
  instrucoes do cenario estao na `__memcpy_avx_unaligned_erms`), que o `std::deque` nunca paga porque nao copia os elementos.
- **`String::==` e `<`**: tentou-se testar o SSO das duas uma so vez quando ambas estao na heap e escrever o `<` sem o resultado de tres vias: `==` de 40 caracteres 53,6 para 49,6 (std 45,6), `<` 59,5 para 57,6 (std 56,2). **Desfez-se**: o codigo
  maior fazia o GCC deixar de inlinar `HashMap::probe_dist` (chaves de 8 caracteres: `hit` 125,1 para 157,5, `miss` 83,1 para 120,7) e subia as instrucoes do `TreeMap` (+12 % no `hit` de `s20`), do `BTreeMap` (+9 % no `hit` de `s8`) e de `stable_sort` de texto (+7 %).
  O que sobra de diferenca para o `std::string` (4 instrucoes no `==`) e decodificar o SSO dos dois lados; o `std::string` guarda sempre o ponteiro, e isso nao se consegue sem mudar a representacao.

#### 3.7.6 `HashMap` com chaves `int`: saltos falhados no `miss`

`miss` de `int-int` a N = 1e5: 0,84 saltos falhados por operacao (abseil 0,40, boost 0,18), `hit` 0,36. Prototipo (so na copia do cabecalho, nao entra) de uma sondagem que olha dois slots por volta com **uma so saida**
(`s0 = meta[i]==0 | chave[i]==k`, `s1` igual para `i+1`, `if (s0 | s1) return`): com `||` curto-circuito o GCC deixou `hit` 24,9 para 23,9 e `miss` 24,5 para 23,0 instrucoes mas **os saltos falhados ficaram em 0,84**
(o compilador volta a pôr um salto por teste); com `|` sobre os dois testes, `hit` **sobe** 24,9 para 28,3 e `miss` 24,5 para 29,2, `erase` 42,9 para 51,5, `build` 129,5 para 146,4, e os saltos falhados continuam em 0,84. **Nao se fez**:
nao ha reducao de mispredicoes sem pagar instrucoes no `hit`, e a causa (a primeira sondagem a 38 % de carga e um cara ou coroa: 0,38 por operacao so desta) so desaparece com uma comparacao de varios slots de uma vez (SSE2/SWAR na meta), que
precisa de uma meta com 8 bytes de folga no fim e de um layout novo da tabela.

#### 3.7.7 EASTL e RE2

**EASTL** (`eastl::vector`, `eastl::hash_map`, `eastl::sort` e `merge_sort`, as fontes da EASTL e a EABase descarregadas com `git clone --depth 1`, compiladas em menos de um minuto; entram em `cmp_vector`, `cmp_map`, `cmp_sort` e em `tools/bench_compare.sh fetch`).
Instrucoes por operacao | tempo de CPU em ns (N = 1e5, minimo de 3, com a maquina carregada), ct, `std`, EASTL:

| cenario | tipo | ct | std | EASTL |
|---|---|---|---|---|
| map build | int-int | 129,5 \| 56,0 | 583,8 \| 201,3 | 555,4 \| 144,6 |
| map hit | int-int | 24,9 \| 8,1 | 29,1 \| 39,2 | **19,0** \| 23,2 |
| map miss | int-int | 24,5 \| 10,7 | 23,5 \| 32,8 | **15,5** \| 23,7 |
| map erase | int-int | 42,9 \| 17,6 | 213,4 \| 182,9 | 203,7 \| 82,7 |
| map iterate | int-int | 15,1 \| 2,9 | 5,0 \| 8,0 | 14,7 \| 10,7 |
| map copy | int-int | 23,0 \| 8,8 | 453,5 \| 143,7 | 462,6 \| 137,8 |
| map build | s8-int | 332,1 \| 169,4 | 735,1 \| 266,9 | 788,0 \| 200,8 |
| map hit | s8-int | 125,1 \| 110,8 | 133,4 \| 181,0 | 144,1 \| 174,4 |
| map miss | s8-int | 83,1 \| 92,9 | 94,0 \| 193,8 | 112,7 \| 139,0 |
| map erase | s8-int | 139,4 \| 128,8 | 328,1 \| 478,9 | 377,3 \| 336,1 |
| map build 1e6 | int-int (so ns) | 80,1 | 601,1 | 418,0 |
| map hit 1e6 | int-int (so ns) | 33,5 | 83,7 | 53,4 |
| vector build | int | 13,4 \| 0,75 | 16,3 \| 4,6 | 18,7 \| 4,9 |
| vector build | p16 | 15,4 \| 6,5 | 24,7 \| 25,0 | 19,7 \| 25,0 |
| vector build_reserved | int | 12,0 \| 0,75 | 11,1 \| 0,75 | 11,1 \| 0,75 |
| vector iterate e copy | int e p16 | iguais | iguais | iguais |
| sort | int.random | 208,8 \| 24,9 ns (1e6) | 128,3 \| 57,9 | 125,1 \| 54,8 |
| stable | int.random | 276,7 \| 58,6 | 193,4 \| 70,2 | 198,9 \| 71,9 |
| sort | p16.random | 189,6 \| 27,1 | 135,9 \| 61,4 | 135,5 \| 59,3 |
| stable | p16.random | 319,4 \| 62,7 | 206,6 \| 83,1 | 196,1 \| 68,9 |
| sort | int.sorted | 7,0 \| 0,31 | 117,2 \| 8,6 | 71,5 \| 5,8 |
| stable | int.sorted | 7,0 \| 0,54 | 119,5 \| 8,5 | 7,1 \| 0,99 |

A `hash_map` da EASTL (encadeada, nos) **ganha a ct no `hit` e no `miss` de `int-int` em instrucoes** (19,0 e 15,5 contra 24,9 e 24,5: faz menos trabalho por pesquisa, a custa de um no por elemento) e perde por muito em tudo o resto
(`erase` 5 vezes, `copy` 20 vezes, `build` 4 vezes; a 1e6 em tempo 5,2 e 1,6 vezes). O `vector` e igual ao do `std`. A `sort` da EASTL esta ao nivel do `std::sort` (e a da ct, com mais instrucoes e metade do tempo, ver 3.7.3).
Fora do que se mediu: `eastl::string`, `fixed_vector` e os mapas ordenados (a EASTL tem `rbtree`/`map`, sem arvore B).

**RE2** (o pacote do Ubuntu 24.04, `libre2.so.10` ja instalado e os cabecalhos de `libre2-dev` extraidos) e `std::regex` contra a `ct::Regex`, as mesmas expressoes e o mesmo texto de `bench/bench_regex.cpp` (2,0 MB de registos),
tempo de CPU em ms, minimo de 5 (`bench/compare/cmp_regex.cpp`, `tools/bench_compare.sh regex`); a razao e `ct` a dividir pelo outro:

| cenario | ct | std::regex | RE2 | ct / std | ct / RE2 |
|---|---:|---:|---:|---:|---:|
| literal ausente, procura | 0,229 | 28,234 | 0,130 | 0,01 | 1,76 |
| literal `status=500`, todas | 1,658 | 31,562 | 1,076 | 0,05 | 1,54 |
| ip `[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+`, todas | 18,889 | 49,844 | 5,804 | 0,38 | **3,25** |
| email `[\w.]+@[\w.]+\.com`, todas | 39,748 | 114,275 | 5,736 | 0,35 | **6,93** |
| `latency=(\d+)\.(\d+)`, todas | 3,649 | 31,317 | 3,544 | 0,12 | 1,03 |
| 4 maquinas em alternativa, todas | 2,219 | 29,643 | 3,092 | 0,07 | 0,72 |
| inicio de linha `\n2026-\d\d-\d\d`, todas | 1,512 | 27,199 | 2,897 | 0,06 | 0,52 |
| `user=(.+?)@` (preguicoso), todas | 3,977 | 30,472 | 4,660 | 0,13 | 0,85 |
| `[a-z]+=\d+`, todas | 21,596 | 51,400 | 11,318 | 0,42 | 1,91 |
| `HOST=ALPHA` sem distinguir maiusculas, todas | 2,397 | 32,932 | 1,676 | 0,07 | 1,43 |
| `\s+` por ' ' (substituir) | 9,469 | 52,665 | 27,915 | 0,18 | **0,34** |
| `(\w+)@(\w+)` por `\2:\1` | 64,083 | 120,549 | 9,757 | 0,53 | **6,57** |
| fullmatch de 100000 textos curtos | 16,279 | 24,938 | 6,735 | 0,65 | 2,42 |
| `(x+x+)+y` em 30 x (x1000; std so com 18 x) | 3,533 | 11039,559 | 0,090 | 0,00 | **39,4** |
| compilar o regex do email x2000 | 2,528 | 78,953 | 14,341 | 0,03 | **0,18** |

A `ct::Regex` e mais depressa que o `std::regex` em tudo (de 1,2 a 100 vezes) e anda entre 0,2 e 7 vezes o tempo do RE2: ganha na substituicao de `\s+` e na compilacao, empata ou ganha nas procuras de alternativas e de inicio
de linha, e perde 3 a 7 vezes nas classes de caracteres sobre texto grande (`[\w.]+`) e na substituicao com grupos, onde o RE2 corre um automato deterministico, e 39 vezes no padrao patologico (a `ct` tem protecao
contra o retrocesso exponencial mas nao um DFA). As procuras da primeira ocorrencia (que acham logo no principio do texto) nao medem nada e ficam fora desta tabela.

## 4. Tempo antes e depois (so ct)

O tempo da ct desta ronda esta so na seccao 3.2b (a `HashMap`, tempo de CPU, 3 execucoes alternadas, maquina carregada) e na 5.5 (`String ==`). As celulas da ronda 1 (a primeira
passagem, com a maquina a carga de 4 a 12) ficam na `docs/comparacao-tabelas.md`.

## 5. Onde a ct ainda perde, a causa e o que se fez

Cada ponto diz o que se mediu, a causa (com a prova), o que se fez nesta ronda e o que fica. As instrucoes sao do callgrind
(N = 1e5, `-O2`, g++ 13.3), por operacao; os bytes sao os do contador de alocacoes.

**5.1 `HashMap` com chaves `int`: `miss`, `hit` e `erase` em tabelas de tamanho medio contra boost e absl.** Nao se mudou.
A causa que a ronda 1 deu (a sondagem le a chave ao slot) nao e a certa: com `valgrind --tool=callgrind --cache-sim=yes` (L1 de
32 KiB, LL de 8 MiB, como o Ryzen 5 5500U) o `miss` de `int-int` a N = 1e5 da 2,43 falhas da D1 por operacao na ct, 2,11 no absl e 2,00
no boost, e zero do LL nos tres (a tabela cabe), mas com `--branch-sim=yes` da **0,84 mispredicoes de salto por operacao na ct contra
0,40 no absl e 0,18 no boost**: a 38 % de carga o slot esta ocupado ou livre ao acaso e o ciclo de sondagem fecha com um salto
que falha; as tabelas SIMD compram um grupo de 16 etiquetas de uma vez e o salto passa a previsivel. A 1e7 a ct ja ganha a todas.
O que se tentou: (a) subir a carga maxima de 75 para 87,5 % com a sondagem linear de sempre (o que o ponto 5.2 pedia): `miss`
de `int-int` a N = 1e5 de 24,5 para 71,7 instrucoes (`hit` 24,9 para 33,0, `erase` 41,9 para 85,1), a 114000 elementos (87 %
de carga) de 26,0 para 189,8, e com chaves de 8 caracteres de 97,3 para 486,6 e 111,3 para 1453,5: as sondagens deixam de ser curtas, que
era o que o ponto pedia que se medisse; (b) etiquetas na meta so para chaves de texto, que e onde a ronda 1
mostrou que pagam (feito, ver 5.2b); (c) um layout para inteiros que nao leia o slot num `miss` sem acrescentar instrucoes ao `hit`: nao existe com a
chave e o valor no mesmo slot e sem etiqueta (a etiqueta de 7 bits que a ronda 1 mediu acrescenta 8 instrucoes ao `hit`); o
caminho seria um grupo de 16 metas comparado de uma vez com SSE2 (o que o absl e o boost fazem, a 41 a 48 instrucoes por `hit`
contra 25 da ct): troca o `miss` de 2 vezes de tempo por um `hit` de 1,7 a 1,9 vezes as instrucoes. Fica como esta.
**Ronda 3**: provou-se com um prototipo (3.7.6) que uma sondagem de dois slots com uma so saida nao baixa os 0,84 saltos falhados do `miss` (o GCC volta a por um salto por teste) e que, escrita sem curto-circuito, soma 14 % ao `hit` (24,9 para 28,3) e 19 % ao `miss`. Fica.

**5.2 `HashMap`: bytes por elemento.** Feito para valores grandes, nao para `int-int`. **Ronda 3**: a troca da ronda 2 (abaixo) foi desfeita no que custava (a chave volta ao slot e o valor vai para um slab por mapa, ver 3.7.1): `int-b256` a N = 1e5, instrucoes `build` 788 para 321, `hit` 36,7 para 30,8, `miss` 31,0 para 26,9, `erase` 291 para 71, `copy` 566 para 70; bytes 294,9 / 302,6 / 293,7 (a ronda 2: 290,5 / 295,6 / 290,9, alvo falhado por 1,5 a 2,4 % porque o slot passa de 9 para 17 bytes). O que segue e a ronda 2, para comparar.
- Valores de 256 bytes (`int-b256`, N = 1e5): **694,7 para 295,6 bytes por elemento** (std 301,8, abseil 347,3, boost 325,7,
  ankerl 356,6); N = 1e6: 555,8 para 290,9 (std 299,6, ankerl 293,6). As entradas com mais de 128 bytes ficam fora da tabela
  (cada uma no seu bloco, a tabela guarda ponteiros: 9 bytes por slot em vez de 265). Preco: uma alocacao por elemento, e as instrucoes
  por operacao sobem (N = 1e5, antes para depois; std, absl, boost): `build` 351 para 788 (824, 433, 328), `erase` 57 para 291 (307, 78, 54),
  `copy` 58 para 566 (596, 87, 31), `hit` 31,1 para 36,7 (31,8, 53,6, 44,6), `miss` 27,7 para 31,0 (23,4, 48,1, 38,0), `churn` 302 para 496; so
  `iterate` desce, 21,4 para 19,4. Fica acima do `std` no `hit` e no `miss` e abaixo nos outros, e muito acima das tabelas planas (que gastam
  347 e 326 bytes por elemento para copiarem o valor para dentro da tabela). Uma referencia a um valor passa a sobreviver ao rehash. **Tempo** (3.2b, CPU, minimo de 3 alternados, ruido
  de ate 30 %): `build` 1544 para 557 ns (o `std::unordered_map` 597 a 689: de 2,6 vezes mais lento a empate), `hit` 61 para 90 (+49 %; std 127), `miss` 33 para 53 (+59 %),
  `erase` 80 para 451 (5,7 vezes). E uma troca de memoria e de `build` por `hit`, `miss` e `erase`, e e uma decisao do dono: `CT_HASH_INLINE_MAX` (128 por omissao) muda o limite
  das entradas fora da tabela, e com um valor enorme a tabela volta a ser a de antes.
- `int-int`: 23,6 bytes por elemento a N = 1e5 contra 11,8 do abseil e 11,2 do boost: so se fecha com carga maxima acima de
  76 %, e isso nao e possivel sem perder (ver 5.1). Fica.
- Chaves de texto: a distancia ao slot de origem na meta (ver abaixo) nao muda os bytes.

**5.2b `HashMap` e `HashSet` com chaves que nao sao inteiros, enumeracoes nem ponteiros.** Feito. Cada slot ocupado guarda na meta 0x81 mais
a distancia ao slot de origem (0xFF para 126 ou mais): a sondagem so compara chaves onde a distancia bate (um `miss` quase
nunca compara strings) e a remocao desloca os seguintes sem recalcular o hash de cada um. Instrucoes por operacao, N = 1e5:
`s8-int` build 391,8 para 332,1 (-15 %), miss 97,3 para 83,1 (-15 %), erase 153,4 para 139,4 (-9 %), `churn` 351,6 para 277,2 (-21 %); `s20-int` build 527,5
para 466,9, miss 142,1 para 127,3, erase 222,7 para 186,8, `churn` 510,3 para 365,5; `s40-int` build 910,2 para 882,0, miss 134,7 para 124,9, erase
360,3 para 329,2, `churn` 747,4 para 606,3; `hit` **sobe** 122,7 para 125,0 e 160,6 para 165,0 (2 a 3 %: o ciclo faz duas comparacoes e um
contador a mais). Iterar e copiar ficam iguais (18,8 e 60,0). Os inteiros, enumeracoes e ponteiros usam o ciclo de sempre e o
assembler de `find` e `erase` de `HashMap<int,int>` e igual ao de antes (conferido byte a byte): `int-int`, `int-p16` e `int-mo` com
as mesmas instrucoes nos dois tamanhos medidos (a unica diferenca: `copy` de `int-p16` a N = 1e3, 19,2 para 19,3). Entre as chaves de texto o unico recuo medido e o `hit`; fica documentado e e a troca que
a ronda 1 recusou nos inteiros e aqui se aceita porque o saldo (miss, build, erase, churn) e de 9 a 29 % a favor.

**5.3 Iterar um `HashMap`.** **Ronda 3**: o grupo completo de 8 metas e uma so carga de 64 bits (antes um `memcpy` de tamanho variavel que o GCC expandia em saltos e cujo codigo mudava com o inlining): 17,4 para 15,1 instrucoes por elemento em `int-int`, 25,6 para 17,1 em `int-p16`, 18,8 para 16,8 com chaves de texto. A estrutura nao mudou: 17,4 instrucoes por elemento a N = 1e5 com `int` (18,8 com chaves de texto; ankerl 5,0, absl 14,0, boost 16,9). Um vetor
denso de entradas mais uma tabela de indices (o que a ankerl faz) daria 5, mas a remocao passaria a trocar com o ultimo
(move um elemento que nao e o apagado), uma referencia a um valor so sobreviveria sem put nem erase de OUTRO elemento, e a ordem
de iteracao passaria a ser a de insercao com a remocao a baralha-la: muda o que o `HashMap` documenta e o que `erase` faz, por isso nao se fez. Melhorar o
varrimento por grupos (de 8 metas por palavra de 64 bits para 16 com `movemask`) nao se mediu.

**5.4 Formatar e ler numeros.** Feito (e o maior ganho da ronda 2). **Ronda 3**: o Json, o Ini, o Xml e o TextReader passam a usar o mesmo `numconv.hpp` (3.7.4): `Json::dump` 324 para 82 ns por numero, `parse` 230 para 148; 2,6 % dos textos de reais mudam, sempre para mais curtos ou mais proximos; 1e7 valores de ida e volta sem falhas. `ct/numconv.hpp` (sem locale, sem a biblioteca standard):
texto mais curto de `double` e `float` que volta ao mesmo valor (Schubfach, tabelas de potencias de 5 de 128 bits geradas por
`tests/gen_numconv_tables.py`), leitura com arredondamento correcto (Clinger, Eisel-Lemire e, nos casos duvidosos, decimal de
precisao arbitraria; hexadecimais, inf, nan), `%.*g` bit a bit igual ao `snprintf` por uma conta de 192 bits com margem de 2^-56
(os empates e os casos que a conta nao decide caem no `snprintf`). Instrucoes por chamada, N = 1e5 (ct antes, depois, melhor dos outros):
`number(double)` (`%.6g`) 2572 para 307 (to_chars 849, fmt 1036); `to_float` 659 para 133 (from_chars 185); `to_double` de 6 algarismos
134 (from_chars 187), de ate 17 algarismos 240 (290) e de expoente espalhado 326 (350); texto mais curto de `double` 477 (to_chars 929, fmt 1212,
`%.17g` 4038) e 454 (838, 1281) com expoente espalhado; `to_int` 175 para 90 (from_chars 141); `number` de inteiro 186 para 126 (fmt 147).
Correccao (seccao 7): diferencial contra o `printf` e o `strtod` do sistema (ver 7), o locale nao conta. **Mudancas que se notam**: `String::to_int` fora do
intervalo satura em `INT_MIN` e `INT_MAX` (3000000000 dava -1294967296) e `String::to_int(int &out)` diz se coube; `String::to_float` e `to_double` nao
aceitam a virgula do locale (so '.'); `append_number(double, precision)` escreve o mesmo texto de sempre.

**5.5 `String`: `append_char`, `==`, `<`, concatenacao.** Feito em grande parte. **Ronda 3**: um caminho rapido para as duas na heap deu `==` 53,6 para 49,6 e `<` 59,5 para 57,6 instrucoes mas fez o GCC deixar de inlinar `probe_dist` (chaves de 8 caracteres no `HashMap`: `hit` 125 para 158) e subiu o `TreeMap`, o `BTreeMap` e a ordenacao de texto de 7 a 12 %: desfez-se (3.7.5). Fica o que a ronda 2 fez. `operator==`, `compare` e `operator+` lem o ponteiro e o tamanho
com um so teste do SSO (`String::raw_parts`) e `==` deixou de testar `n == 0` antes do `memcmp`; `push_back` tem o caso normal sem
`size()`, `capacity()`, `data()` e `set_size()`; a concatenacao (`String::concat`) junta com capacidade exacta, sem `reserve` nem `append`.
Instrucoes (ct antes, depois; std): `==` de 40 caracteres 58,6 para 53,6 (45,6); `<` 68,5 para 59,5 (56,2); `append_char` 24,1 para
20,1 (19,3); `a + b + c` 629 para 392 (516). **Tempo** (minimo de 9 rondas alternadas, tempo de CPU da thread, maquina a carga 25 a 60):
`==` de 8 caracteres 10,5 para 8,4 ns (std 7,4); de 40 caracteres 9,8 contra 7,6: a diferenca de 2,3 vezes da ronda 1 nao se reproduziu
(1,3 vezes). A causa que se achou: as 8 instrucoes a mais de decodificar dois SSO (o `std::string` guarda sempre o ponteiro) e um
`n == 0 ||` que o compilador deixava antes do `memcmp`; o `memcmp` e o mesmo (nos dois o mesmo `memcmp@plt`), e o que sobra de tempo nao
se explicou. Uma dica de ramo para o caso da heap (`CT_UNLIKELY` no teste do SSO) deu uma diferenca dentro do ruido, e nao ficou.

**5.6 `Deque` e `PriorityQueue`.** `PriorityQueue`: feito, 358 para 273 instrucoes por elemento (std 309), com a tecnica do buraco (um move por nivel em vez de
um swap, o `pop` desce ate a folha e sobe). `Deque::push_front`: **ronda 3**: analisado, nao se mexeu: o caminho rapido tem 15 instrucoes contra 13 e as 21,5 incluem 6,4 que sao o `memcpy` do `realloc` contado byte a byte pelo callgrind (3.7.5). Na ronda 2 nao se tentou (21,5 contra 13,3 do `std::deque`: a ct dobra um anel de potencia de 2 e le
`head_`, `cap_` e `size_` a cada `push_front`, o `std::deque` decrementa um ponteiro).

**5.7 `sort` com comparador em entrada aleatoria.** **Ronda 3**: feito, com a particao por blocos (3.7.3): `int` aleatorio a 1e6 **59,5 para 24,2 ns por elemento** (std 57,1), saltos falhados 6,97 para 1,96 por elemento, **instrucoes 163 para 209 (+28 %)**; `stable_sort` 64,8 para 55,7 ns (std 67,9), instrucoes 222 para 277. As instrucoes sobem porque a particao sem salto faz mais trabalho por elemento; decidiu-se pelo tempo e pelos saltos, e e uma regressao na metrica das instrucoes que o dono deve saber. `stable_sort` ganha a passagem unica para ordenada e invertida (92 e 201 para 7 e 11 instrucoes). Na ronda 2 nao se tentou (163 instrucoes por elemento em `int` aleatorio contra 128 do
`std::sort` e 118 do pdqsort do boost; 167 contra 136 e 121 em `P16`). O caminho (proposta, nao medida) seria a particao por blocos de 64 (BlockQuicksort/pdqsort),
em que os indices dos elementos a trocar se juntam sem ramos e que le sempre dentro do intervalo, por isso as guardas que protegem um comparador
mau (`<=`) ficariam de graca; so serve com comparador barato e tipo trivial (e e esse o caso que perde).

**5.8 `Vector`.** Igual. Novo: `resize_for_overwrite(n)` e `append_uninitialized(n)` (ver 6): crescer sem preencher os elementos novos de um tipo que se
constroi sem escrever nada (`int`: 9,0 para 5,0 instrucoes por elemento num ciclo de redimensionar e escrever tudo, como o `default_init` do boost; `P16`: 9,5 para 7,0).

**5.9 `TreeMap` a 1e6 elementos.** **Ronda 3**: feito, `BTreeMap` e `BTreeSet` (3.7.2); o `TreeMap` fica como esta. Na ronda 2 nao se tentou. Os iteradores e os ponteiros `Entry *` de `TreeMap` estao documentados como estaveis (a insercao e a remocao nao movem
elementos), e uma btree move elementos entre nos; por isso o caminho e um `BTreeMap` e um `BTreeSet` a parte, com as garantias mais fracas ditas no cabecalho
(a insercao e a remocao invalidam iteradores e referencias; a iteracao e a pesquisa por intervalo ficam), nos de 256 bytes com 15 chaves,
`lower_bound`, `erase` com fusao e emprestimo. Estimativa: umas 600 linhas, mais os testes de ordem, de remocao em todos os casos e diferenciais contra o `TreeMap`.

**5.10 `String` com chave `s40` no `HashMap`.** `build` a 1e6 mede, com a distancia na meta, menos 15 % de instrucoes a 1e5 (ver 5.2b); o resto (mover o par a cada
rehash) fica.

**5.11 Casos novos que a ronda 2 mediu (seccao 3.1, familia `extra`; ct contra o equivalente da biblioteca standard, instrucoes por operacao).**
`Variant` (`std::variant`): construir 68,7 contra 70,4, visitar 17,0 contra 18,4, atribuir por copia 34,1 contra 36,3: empata ou ganha. `Arena`
(`std::pmr::monotonic_buffer_resource`): `allocate(64)` 28,0 contra 27,1 (empate) e uma lista de nos 32,0 contra 33,1. Fila circular
(`Queue` sobre `Deque` contra `std::queue`): 18,0 contra 22,6, ganha. `Span`: somar tudo 5,0 contra 5,0 de um ponteiro cru; **`subspan` em blocos de 16 perde, 6,4
contra 3,5** (a verificacao de limites de `subspan` por chamada; **ronda 3**: nao se fecha, a diferenca e o ciclo de tamanho desconhecido e nao a verificacao, 3.7.5). **`Pool` perde, 718 contra 583 de `new` e `delete`** (**ronda 3: 174**, a deteccao do duplo `destroy` fica toda mas o ultimo pedaco fica em cache, 3.7.5) num ciclo de criar N, apagar metade e criar
outra metade: cada `allocate`, `create`, `destroy` e `deallocate` acha o bloco do slot por busca binaria num indice de blocos (`state(p)`, varias vezes por `create` e por `destroy`) para detectar o duplo `destroy`, e e essa seguranca que custa; nao se
mexeu (mudar a deteccao muda o que a `Pool` garante).

## 6. Defeitos e melhorias feitas (antes e depois)

| # | o que | antes | depois | prova |
|---|---|---|---|---|
| 1 | `HashSet` com capacidade 2 (`reserve(1)` ou a copia de um conjunto de 1 elemento) ficava 100 % cheio ao 2.o `insert`; a chave em falta nunca parava a sondagem | o 3.o `insert` nunca acaba | acaba | `EdgeHashMap.SetWithCapacityTwoDoesNotFillUpAndSpin` (o filho tem alarme de 5 s): antes "died but not with expected exit code" apos 5001 ms; depois passa em 0 ms. O `HashMap` ja tinha a guarda; faltava no `HashSet`. |
| 2 | `HeapAlloc` guardava 8 bytes de cabecalho antes de cada bloco (para o `free` de blocos alinhados) | 1000 blocos de 24 bytes ocupam 47 B cada | 32 B cada (so o malloc); `String` de 24 a 40 caracteres: 33 para 25 bytes pedidos, 48 para 32 no malloc | `EdgeHeapAlloc.NoPerAllocationHeaderOnPosix` (mallinfo2): antes 47, depois 31 a 32; ver 3.5 (bytes pedidos -12 a -24 %). Em POSIX o alinhamento acima de 16 usa `posix_memalign`, que `free` solta; em Windows fica o cabecalho. |
| 3 | `introsort`: particao de Lomuto: os repetidos iam todos para o mesmo lado e uma entrada invertida trocava quase tudo | 100000 `int` com 16 valores: 3,49 milhoes de comparacoes e cai no heapsort; invertida 3,37 milhoes; instrucoes por elemento 241 (dups) e 288 (invertida) | 1,46 milhoes e 1,44 milhoes; 135 e 10 instrucoes por elemento | `EdgeSort.ManyDuplicatesStayNearNLogN` (< 2,2 milhoes), `SortedAndReversedInputCostOnePass` (<= 100000 e 200000): ambos falharam antes (3,49 M, 3,37 M, 1,29 M) e passam. Tempo de `P16` a 1e6: aleatorio 88,8 para 70,4 ns, invertido 60,7 para 24,1, repetidos 63,7 para 33,8. Mais um teste com comparador `<=` (nao le fora do intervalo, corre com ASan). |
| 4 | Iterar um `HashMap`/`HashSet` passava slot a slot pelos buracos; copiar refazia hash e sondagem de cada elemento | iterar 23 instrucoes por elemento (1e5, `int`), copiar 59 | 17 e 23 | ver 3.5: iterar -25 %, copiar -61 % (`int`), -58 % (`s8`), -21 % (`s40`); o tempo esta na seccao 4. `EdgeHashMap.IterationVisitsExactlyTheOccupiedSlots`, `IterationAtTinyAndExactCapacities`, `CopyAndAssignKeepEveryEntry`. A copia por indice so se usa na copia construida (o hasher e o mesmo) e quando a capacidade que fica e a da origem. |
| 5 | `String::to_int` e `String::number` | `strtol` e um algarismo por passo | analise propria com o mesmo resultado que `static_cast<int>(strtol(...))`; dois algarismos por passo | `EdgeNumbers.StringToIntMatchesStrtolOnRandomText` (1e6 textos, igual a `strtol`), `IntegerToTextMatchesPrintfOnRandomValues` (5e5 valores, igual a `printf`); ver 3.5: `s2int` -40 % de instrucoes, `int2s` -2 %. |
| 6 | `String::to_int` fora do intervalo do `int` | `static_cast<int>(strtol(...))`: 3000000000 dava -1294967296, 4294967296 dava 0, -3000000000 dava 1294967296 | satura em `INT_MIN` e `INT_MAX`; `String::to_int(int &out)` devolve false se o texto nao for so espacos, sinal e algarismos ou se nao couber | `NumconvString.ToIntSaturates`, `ToIntChecked` e `ToIntMatchesSaturatedStrtolOnRandomText` (400000 textos, igual ao `strtol` preso ao intervalo): os que mudam de comportamento falham com o cabecalho de origin/main (3000000000 -> -1294967296) e passam; `EdgeNumbers.StringToIntMatchesStrtolOnRandomText` ganhou a mesma conta. |
| 7 | apagar ou inserir num `HashMap`/`HashSet` durante um ciclo `for` (salta ou repete elementos, ou le uma tabela que mudou) | nao se detectava | `erase_if(pred)` visita cada elemento uma vez; em depuracao (`CT_DEBUG_ITERATORS`) o iterador aborta se for usado depois de a tabela mudar | `HashEraseIf.*` (4) e `HashDebugIterators.*` (3, EXPECT_DEATH com apagar e inserir durante o ciclo, depois de `clear`, com `const_iterator`). |
| 8 | `String::to_float` e `String::number(double)` dependiam do locale (so tratados com uma copia do texto) | `snprintf` e `strtof` | `ct/numconv.hpp` nao tem locale: o separador e sempre '.' | `NumconvLocale.DecimalPointDoesNotDependOnLocale` (pt_BR, pt_PT; `1,5` le 1), diferencial com o programa em pt_BR (o oracle corre num locale "C" com `uselocale`). `Hardening.NumerosDeVirgulaFlutuanteNaoDependemDoLocale` mudou: `String("1,5").to_float()` era 1,5 em pt_PT e agora e 1. |
| 9 | o codigo novo: o produto de 126 bits do Schubfach com g = floor + 1 dava resto nos limites de intervalo que caem num decimal curto | `0x4350000000000001` escrevia `1.801439850948199e+16`, que le o double seguinte (114 falhas em 200000 passos do diferencial, de ida e volta e de "nao e o mais curto") | o arredondamento para impar so olha os 63 bits de cima do resto (como o do JDK), de modo que o excesso do teto desaparece quando o produto exacto e inteiro | `NumconvShortest.IntervalBoundariesOnShortDecimals` e o diferencial: falhava (ida e volta e "nao e o mais curto") antes e passa. |
| 10 | o codigo novo: a posicao do ponto do decimal exacto contava so os 800 algarismos guardados | uma entrada de mais de 800 algarismos dava um valor 10^k fora | conta os algarismos todos, guardados ou nao | `NumconvParse.LongInputs` (2000 textos de 20 a 920 algarismos contra o `strtod`) falhava (caminho exacto) e passa. |
| 11 | `HashMap<int, B256>`: 695 bytes por elemento a N = 1e5 (a tabela a 38 % de carga com slots de 265 bytes) | 69,5 MB | 29,6 MB (so se paga uma alocacao por elemento) | `HashLayoutBig.BytesPerElementAreCloseToTheEntrySize`: o limite de 290 falha com o cabecalho de origin/main (694,7) e passa (295,6). |

| 12 | `CT_DEBUG_ITERATORS` ligada pela ausencia de `NDEBUG` (defeito da ronda 2: o ABI dependia do `NDEBUG`) | o `HashMap` e o `HashSet` ganhavam 8 bytes (o contador de versao) e iteradores maiores em toda a unidade sem `NDEBUG`; duas unidades do mesmo programa com `NDEBUG` diferente (uma biblioteca em Release ligada a uma aplicacao em Debug, um teste com `#undef NDEBUG`) discordavam do tamanho e dos membros do mesmo tipo e corrompiam memoria em silencio (partiu 11 testes da biblioteca grafica) | a macro e explicita (`CT_DEBUG_ITERATORS=1`, nunca por omissao nem por `NDEBUG`) e os tipos que o layout ou o comportamento inline faz depender de uma macro levam um namespace inline com o valor no nome (`ct::abi_dbgiter::HashMap`, `abi_hashinline_N`, `abi_tls_mbed`, `abi_re_check_caps`): uma ligacao com unidades que discordam falha por referencia por definir | `tests/abi_a.cpp` (NDEBUG), `abi_b.cpp` (-UNDEBUG) e `abi_main.cpp`: tamanho, alinhamento e posicao de cada membro de `HashMap`, `HashSet`, `String`, `Vector`, `Variant`, `Function`, `TreeMap`, `Deque` iguais nas tres unidades e um `Box` preenchido numa le-se na outra; com os cabecalhos de 63793ca o teste falha (`Box` 448 contra 496 bytes, `HashMap` 32 contra 40) e rebenta; `abi_mixed_a.cpp` + `abi_mixed_b.cpp` (uma com `-DCT_DEBUG_ITERATORS=1`) falham na ligacao (etapa `abi` do `check.sh`, ou `ctest -R ct_abi_mixed`) |
| 13 | `Xml::attr_double` com virgula decimal no locale (`pt_BR`, `pt_PT`) | texto que nao e numero devolvia 0 (o ramo da virgula nao via o valor por omissao) | devolve o valor por omissao, como no locale "C"; o `strtod` sai | `XmlNumbers.AttributeDoublesIntsAndLocale`; diferencial contra o codigo de antes: 6 diferencas em 300000 textos em `pt_BR`, todas em `Xml::attr_double` com texto invalido |
| 14 | o caminho de `long double` do Json (`json_dtoa_fast`, so em x86) escrevia o ultimo algarismo mal em cerca de 0,5 % dos reais | `1.5080921830664334e-308` (o decimal de 17 algarismos mais proximo e `...333`), `1489.7146606445313` (empate exacto: o par e `...312`) | o texto mais curto e mais proximo, empates ao par | 52902 dos 1999880 valores mudam, todos para mais curtos (14115), mais proximos (9978) ou empate ao par (2358): 3.7.4; `JsonNumbers.DumpIsTheShortestRoundTripText` |
| 15 | o codigo de `HashMap<int, P16>` e `String::to_double` mudava de instrucoes sem mudar nada na biblioteca | `hit` de `int-p16` mais 1, `iterate` mais 8, `s2f64` mais 5 a 14 instrucoes: uma unidade de compilacao maior (ou um refactor de uma funcao ao lado) muda as decisoes do GCC | o "antes" mede-se com o mesmo `bench/compare` (seccao 2); o `iterate` ganha uma carga so por grupo de 8 metas; o `write_shortest_at_least` foi para um cabecalho a parte (`detail/numconv_g.hpp`) em vez de mexer no `numconv.hpp` | o assembler de `find`, `erase` e `iterate` isolados de `HashMap<int, P16>` e igual byte a byte; `s2f64` 237 e 320 instrucoes em ambos |

**Auditoria das macros que mudam layout ou comportamento inline** (todos os `#if` dos cabecalhos): `CT_DEBUG_ITERATORS` (layout de `HashMap`, `HashSet` e dos iteradores), `CT_HASH_INLINE_MAX` (layout de `HashMap`; o valor tem de ser um literal inteiro e, se a aplicacao o definir, entra no nome do tipo), `CT_TLS_MBEDTLS` e `CT_TLS_DEFAULT_MBEDTLS` (layout e construtor de `TlsLibrary`) e `CT_RE_CHECK_CAPS` (um `__builtin_trap` no motor de `Regex`): as quatro explicitas, desligadas por omissao e com o namespace inline. Nenhuma depende de `NDEBUG`, de `assert` (os cabecalhos nao usam `assert`), de `__OPTIMIZE__` nem de `_GLIBCXX_DEBUG` (nao aparecem). Ficam fora porque so seguem a plataforma ou o compilador e nao sao escolhas da aplicacao: `_WIN32`, `__APPLE__`, `__ANDROID__`, `__EMSCRIPTEN__`, `__GNUC__`, `_MSC_VER`, `__BYTE_ORDER__` (o `String` recusa-se a compilar em big-endian), `__SIZEOF_INT128__`, `MSG_NOSIGNAL`, `SO_NOSIGPIPE`, `__cpp_inline_variables`;
`CT_COND_MONOTONIC` (`thread.hpp`, derivada da plataforma: escolhe o relogio da `CondVar`, nao o layout), `CT_NUMCONV_NO_CLINGER` (`numconv.hpp`, derivada de `__i386__` sem `__SSE2_MATH__`: tira um atalho que da o mesmo resultado, nao o layout) e `CT_RECTPACKER_IMPLEMENTATION` ("defina numa unidade so": decide quem define funcoes nao inline, nao o layout).

O que se tentou e se desfez: etiquetas de 7 bits na meta do `HashMap`/`HashSet` para chaves inteiras (ronda 1, 5.1) e a carga maxima de 87,5 % com sondagem linear (ronda 2, 5.1): um prototipo a deixou o `miss` de `int-int` a 71,7 instrucoes (24,5 antes). Nao entram nesta lista as variacoes que
vem so de o compilador ter escolhido outro codigo para o mesmo codigo da ct (5.7, 5.8).

## 7. Casos limite (parte 2): o que `tests/test_edge.cpp` cobre

O ficheiro nao usa a biblioteca standard (so o gtest e cabecalhos C) e corre em `tools/check.sh` (release e ASan + UBSan, com
`detect_leaks`).

- **Vazio e um elemento**: `HashMap`, `HashSet` (iterar, procurar, apagar, copiar, limpar).
- **Auto-atribuicao e auto-insercao**: `push_back(v[i])`, `insert(begin, v.back())`, `emplace_back(v[i])`, `resize(n, v[i])`,
  `assign(3, v[i])` com `Vector<String>` em cada capacidade de 1 a 70 (a realocacao nao pode deixar a referencia solta);
  `String` com `append(s)`, `+=` de si, `insert(0, s)`, `insert/append/assign` de um ponteiro para dentro de si, `s = s`, com comprimentos de 0 a 80
  (passa o limite do SSO de 23); `operator=` por copia e por move de si em `Vector`, `Deque`, `HashMap`, `HashSet`, `TreeMap`,
  `FlatMap`, `String`. `Vector::assign(first, last)` com iteradores para dentro do proprio vetor nao se testa: como no
  `std::vector`, e uso invalido.
- **Mover e reutilizar**: depois de mover, `HashMap`, `HashSet`, `Vector`, `Deque`, `String` ficam vazios e aceitam novos elementos.
- **Apagar enquanto se percorre**: `Vector::erase` devolve o iterador seguinte (testado a apagar durante a iteracao); em `HashMap` e
  `HashSet` a remocao desloca os elementos seguintes para tras e por isso **apagar o elemento actual durante um ciclo
  `for` e invalido** (o ciclo pode saltar ou repetir elementos): o padrao correcto e `erase_if(pred)` (visita cada elemento uma
  vez, `HashEraseIf.*`) ou juntar as chaves e apagar depois, que e o que o teste faz. Inserir durante a iteracao pode fazer rehash
  e invalida os iteradores e as referencias. **A regra esta dita nos cabecalhos** (`hashmap.hpp` e `hashset.hpp`, ao pe de `begin`)
  e, com `CT_DEBUG_ITERATORS=1` (**explicito**: nao depende de `NDEBUG`, porque a macro muda o tamanho da tabela e duas unidades de um programa
  com `NDEBUG` diferente discordavam do layout; ver 6.12 e `tests/abi_*.cpp`), **o iterador aborta se for usado depois de a tabela mudar** (`HashDebugIterators.*`, na etapa asan do `check.sh`); sem ela
  nao ha verificacao nenhuma e o iterador e o de sempre.
- **Crescimento nas fronteiras**: capacidades de 1 a 70 no `Vector`, a fronteira 23/24 do SSO do `String` (`is_small()` muda exatamente
  ai), tabelas de 2, 4 e 8 slots (grupo parcial do iterador) e `reserve(n)` de 0 a 70 em `HashMap` e `HashSet`.
- **Tamanhos perto do maximo**: `reserve(max_size())`, `resize(max_size())`, `append(max_size(), c)`, `Deque::reserve(max_size())`,
  `HashMap::reserve(SIZE_MAX / 2 + 1)` abortam com mensagem (nao dao a volta); `HeapAlloc::allocate` com alinhamento invalido ou tamanho
  enorme tambem. Estes testes usam `EXPECT_DEATH`.
- **Falha de alocacao injetada em cada ponto**: **nao feito, por desenho**: a ct nao tem caminho de erro de alocacao (`HeapAlloc`
  chama `abort` com "sem memoria", como diz o README: falha rapida, sem excecoes), por isso nao ha estado a manter valido depois da
  falha. Fica testado que o tamanho absurdo aborta.
- **Construcoes e destruicoes**: um tipo que conta `Counted` (cada construcao tem exatamente uma destruicao) em `Vector` (push, emplace, insert,
  erase, resize, assign, copia, move, swap, shrink), `Deque` (push de ambos os lados, resize), `HashMap` (put com substituicao, erase, copia, clear),
  `TreeMap` e `FlatMap` (com `validate()` da arvore), e um tipo so move em `Vector`, `Deque`, `HashMap`, `TreeMap`: o contador de vivos
  bate com `size()` em cada passo e e zero no fim.
- **Alinhamento e elementos enormes**: `alignas(64)` e `alignas(32)` em `Vector`, `Deque`, `HashMap`, `TreeMap`, `Arena`, `Pool` apos varias
  realocacoes; elementos de 4 KiB em `Vector` e `Deque`. Elementos de tamanho zero nao existem em C++ (um `struct` vazio tem 1 byte).
- **Tabelas de hash**: todas as chaves a colidir (`Hash` que devolve uma constante, 1500 chaves); 1e6 de `put`/`erase` com no maximo 1000 vivos sem
  a tabela crescer (a remocao por deslocamento nao deixa marcadores); iteracao em todas as densidades.
- **Strings**: bytes NUL no meio (`find`, `rfind`, `compare`, `hash`, `substr`, `split`, `to_int` para no NUL), bytes que nao sao UTF-8
  (`\xff`, sobrelongo `\xc0\x80`, substituto `\xed\xa0\x80`) como bytes, os 256 valores de byte, comprimentos de 0 a 70.
- **Numeros nos limites**, contra valores conhecidos: `to_int` com `INT_MIN`, `INT_MAX`, vazio, so espacos, sinal, `0x10`, `1e3`, lixo antes e depois;
  `to_float` com denormais (`1e-45` = bits 1, `1e-46` = 0, `1.17549435e-38`), `FLT_MAX`, `1e39` = infinito, `inf`, `nan`, `-0`, `16777217`;
  `number` com `INT64_MIN`, `UINT64_MAX`, `1e300`, `-0.0`, `0.1` com 17 digitos; `Json` com `INT64_MAX`/`MIN`, `UINT64_MAX` e o seguinte (passam a real),
  `5e-324` (bits 1), `DBL_MIN`, `DBL_MAX`, `1e309`, `-0`, e 19 formas invalidas (`01`, `+1`, `.5`, `1.`, `0x10`, `NaN`, `Infinity`, ...);
  `Ini::get_int` recusa o que nao cabe. **Nao se mudou** `String::to_int` com um valor fora do intervalo do `int`
  (da a volta, ver docs/SUSPEITAS.md): precisa de uma decisao do dono e o teste de `to_int` compara com `strtol` precisamente para nao a mudar.
- **Texto de numeros (`ct/numconv.hpp`, ronda 2)**: `tests/test_numconv.cpp` (19 testes) e o diferencial longo `tests/numconv_diff.cpp`, contra o `printf` e o
  `strtod` do sistema (o oracle corre sempre no locale "C", com a ct num locale de virgula quando se pede). Cada iteracao experimenta um `double` ao acaso (expoente
  uniforme, subnormais, quocientes, decimais curtos, inteiros grandes) e um `float` ao acaso por todos os caminhos: o texto mais curto volta ao mesmo valor, nenhum decimal mais curto
  volta (confere-se D-1, D e D+1 do `%.*e` com um algarismo a menos) e entre os da mesma largura e o mais proximo; `%.*g` de 1 a 17 algarismos igual ao `snprintf`; a leitura de
  `%.17g`, `%.*g`, `%.*e`, `%a`, `%.*a`, de textos ao acaso de ate 40 algarismos com expoente de -350 a 350 e de hexadecimais ao acaso igual ao `strtod` e ao `strtof` em bits e no fim do
  texto, e o caminho exacto (decimal de precisao arbitraria) sozinho tambem; de 1 em 4 iteracoes os limites de intervalo que caem em decimais curtos e os pontos medios exactos entre
  dois doubles vizinhos escritos com ate 780 algarismos (empates e a vizinhanca). Mais todas as potencias de 2 e de 10 com duas vizinhas de cada lado. Corridas desta ronda:
  **102 milhoes de iteracoes** em 5 corridas de semente diferente (A em C e B em pt_BR pararam aos 15 milhoes cada, com o oracle de antes; D em C, E em pt_BR e F em C correram 24 milhoes cada, com o oracle corrigido), **3 falhas, todas do oracle** (nas corridas A e B): tres hexadecimais subnormais de
  mais de 53 bits em que o `strtod` da glibc arredonda mal (`0X5EdD31.8F9BE4E6p-1045`, `0XcDc.Bf866f417D4p-1035`, `0X98A382a242e7eCp-1078`; aritmetica exacta em Python da a razao
  a `ct::parse_double`); com o oracle corrigido (salta o valor nesses casos) 72 milhoes de iteracoes sem falhas; e 1 140 850 687 dos 2^32 `float` (26,6 %) escritos e lidos de volta sem
  falhas (parou por falta de tempo, a maquina estava a carga 250). Alem do que o `ctest` corre (200000 iteracoes por teste, `CT_NUMCONV_ITERS` muda-o).
- **Diferenciais com 1e6 de operacoes e semente fixa**: `Vector` contra um vetor de capacidade fixa (push, pop, insert, erase de
  intervalos, resize, assign, copia, move, swap, erase durante o ciclo), `Deque` contra um modelo com deslocamento (push e pop dos dois lados, resize, copia),
  `String` contra um vetor de `char` (push, append, insert, erase, resize, `append(s)`, copia, `substr`, `find`), `TreeMap` e `FlatMap` contra
  um vetor direto de 2048 chaves (ordem ascendente na iteracao, `validate()`), `HashMap` e `HashSet` contra um vetor direto de 4096 chaves
  (com copias, atribuicoes e moves pelo caminho).

- **Ronda 3**: `tests/test_btree.cpp` (23 testes: o diferencial de 1e6 operacoes contra o `TreeMap` com contadores de cada caminho de dividir, emprestar e fundir, em nos minimos; chaves de texto; ordem crescente e decrescente; `lower_bound` e `upper_bound` exactos;
  `erase_range`, `erase_from` e por iteradores; `assign_sorted` ordenado, desordenado e com repetidos; construcoes e destruicoes, valores so move, sobre-alinhados e de 4 KiB, auto-referencia do valor e da chave, auto-atribuicao e auto-movimento, bytes por elemento e alocacoes; o iterador de depuracao),
  `tests/test_sort_blocks.cpp` (8: todos os padroes e tamanhos em volta dos limites dos blocos, estabilidade, comparadores maus com ASan, repetidos em tempo quase linear, ordenada e invertida numa passagem), `tests/test_parser_numbers.cpp` (6: o texto do Json, o diferencial de ida e volta de 1e7 valores, Ini, Xml com locale, TextReader),
  `HashLayoutModes.*` e `HashLayoutBig` (as tres disposicoes da tabela: valores so move, sobre-alinhados, chave grande, diferencial contra um vetor directo, contagem de construcoes, reutilizacao dos blocos) e `Pool.InterleavedChunksKeepTheStateChecksExact`.

**Resultado da ronda 1**: um defeito real (6.1) e as melhorias de eficiencia de 6.2 a 6.5. Todos os outros testes passaram sem mexer em codigo.
Tudo corre com ASan + UBSan. A ronda 2 acrescentou 45 testes (seccao 6, linhas 6 a 11, e `test_numconv`, `test_string_ops`, `test_pq_hole`, `test_hash_iter`, `test_hash_layout`, `test_vector_overwrite`) e o diferencial longo de `numconv.hpp`.

## 8. O que ficou por fazer

Da ronda 3 (cada ponto diz porque, ver a seccao 5 e a 3.7):

- **Bytes por elemento da `HashMap` com valores grandes** ficam 1,5 a 2,4 % acima dos da ronda 2 (294,9 / 302,6 / 293,7 contra 290,5 / 295,6 / 290,9): o slot de 17 bytes (chave e referencia) so desce a 13 com uma representacao em que o `Entry &` do iterador deixa de existir.
  O `hit` fica 14 a 24 % acima do da tabela inline (um terceiro acesso, o do bloco) e o `iterate` 20 %.
- **`.value` de uma `const Entry &` nao e const** com valores grandes (docs/SUSPEITAS.md): precisa de uma decisao do dono.
- **`BTreeMap` com chaves de texto** gasta 1,7 vezes as instrucoes do `btree_map` do abseil no `hit` e no `miss` (a pesquisa binaria faz 15 a 21 comparacoes de `ct::String` por pesquisa e a comparacao da ct custa 56 a 67 instrucoes contra 46 do `std::string`);
  o `String::==` e o `<` ficam a 4 instrucoes do `std::string` (o `std::string` guarda sempre o ponteiro). Um prefixo de 8 bytes da chave nos nos internos, ou uma comparacao de tres vias, reduziam-no; nao se fez. O `BTreeMap` nao tem iteradores para tras (as folhas ligam so para a frente),
  nem `erase(iterator)` isolado, nem pesquisa transparente (`find` com `StringView`).
- **`ct::sort` com comparador em dados ao acaso** gasta mais instrucoes (+28 %) para ganhar 2,4 vezes em tempo: uma particao por blocos com menos instrucoes por elemento nao se tentou; e `stable_sort` de tipos pequenos pede n elementos de memoria em vez de n/2.
- **`HashMap` com chaves `int`: saltos falhados no `miss`** (0,84 por operacao contra 0,40 do abseil): so se resolve com uma comparacao de varios slots de uma vez, que pede uma meta com 8 bytes de folga e um layout novo (3.7.6).
- **`Deque::push_front`, `Span::subspan` e `String::==`**: analisados, ficam (3.7.5).
- **O tempo de CPU da ronda 3** e indicativo (a maquina esteve a carga de 3 a 13 e, no fim, com prioridade baixa): falta repeti-lo com a maquina livre, e o tempo de `String`, dos numeros e da ordenacao com mais de 3 execucoes.
- **EASTL**: so se mediu `vector`, `hash_map`, `sort` e `merge_sort`; falta `eastl::string`, `fixed_vector`, `eastl::map`. **RE2**: o `bench/bench_regex.cpp` continua so contra o `std::regex`; o `cmp_regex` e que tem o RE2.
- `Ini::get_int`, `TextReader::read_int` e os inteiros do Xml continuam a usar `strtoll` ou a conta propria (nao tem locale nem arredondamento): os reais dos quatro leitores ja passaram para o `numconv.hpp`.

Das rondas 1 e 2 que a 3 nao tocou:

- **Carga maxima de 87,5 % no `HashMap`** (5.1, 5.2): medida com um prototipo e recusada (o `miss` de `int-int` passa de 24,5 para 71,7 instrucoes). Os bytes por elemento de `int-int` ficam em 23,6 (absl 11,8).
- **Iterar um `HashMap` a 5 instrucoes por elemento** (5.3): pede armazenamento denso, que muda o `erase` e as referencias; nao se fez (agora 15 instrucoes).
- Falha de alocacao (nao ha caminho de erro, ver 7); `Vector` de 1 elemento com capacidade 8; `reserve` do `String` a dobrar a partir do SSO; `-march=native`.
- Nao ha vector com armazenamento interno (small vector) na ct para medir. `Span` mede-se so contra um ponteiro cru (o `std::span` e C++20 e esta bateria compila em C++17).

## 9. Como repetir

```sh
tools/bench_compare.sh fetch      # descarrega abseil, boost, fmt, valgrind e a ankerl para build-compare/third (nada e instalado)
tools/bench_compare.sh build      # CMake com -DCT_BUILD_COMPARE=ON (desligado por omissao), g++ -O2
tools/bench_compare.sh time       # tempo, alocacoes, bytes, RSS: build-compare/out/time.tsv
tools/bench_compare.sh ir         # instrucoes com callgrind: build-compare/out/ir.tsv
tools/bench_compare.sh layout     # sizeof vazio, carga, bytes por elemento, crescimento
tools/bench_compare.sh compile    # tempo e memoria do compilador, tamanho do objeto
tools/bench_compare.sh tables     # junta tudo em build-compare/out/tables.md (tools/bench_tables.py)
```

Os numeros da ronda 2 repetem-se assim (g++ 13.3, `-O2`; `CT_COMPARE_OPT=-O3` para a seccao 3.6):

```sh
CT_COMPARE_BUILD=/tmp/antes CT_COMPARE_OPT="-O2" tools/bench_compare.sh build   # com os cabecalhos de origin/main (7fef995) em include/
CMP_CPUTIME=1 /tmp/antes/cmp_map ct build int-b256 100000                      # tempo de CPU em vez do relogio
cmake -S . -B build && cmake --build build --target ct_numconv_diff              # o diferencial longo de numconv.hpp
build/ct_numconv_diff 50000000 1                                                # 5e7 doubles ao acaso, semente 1
build/ct_numconv_diff 24000000 2 pt_BR.utf8                                     # o mesmo com a ct num locale de virgula
build/ct_numconv_diff --float-all 1 0                                           # os 2^32 floats, texto mais curto
```


Ronda 3: `tools/bench_compare.sh fetch` traz tambem a EASTL e a EABase (`git clone --depth 1`) e `build` compila as fontes da EASTL; `tools/bench_compare.sh regex` corre `cmp_regex` (ct, `std::regex` e RE2). O varrimento de `CT_HASH_INLINE_MAX`:

```sh
CT_COMPARE_BUILD=/tmp/in  CT_COMPARE_OPT="-O2 -DCT_HASH_INLINE_MAX=100000" tools/bench_compare.sh build   # a tabela inline
CT_COMPARE_BUILD=/tmp/out CT_COMPARE_OPT="-O2 -DCT_HASH_INLINE_MAX=0"      tools/bench_compare.sh build   # o valor sempre fora
for t in b32 b64 b96 b128 b192 b256 b512 b1024; do for s in build hit miss erase iterate copy; do
  CMP_CPUTIME=1 /tmp/in/cmp_map ct $s int-$t 100000; CMP_CPUTIME=1 /tmp/out/cmp_map ct $s int-$t 100000; done; done
CMP_CALLGRIND=1 valgrind --tool=callgrind --collect-atstart=no /tmp/in/cmp_map ct hit int-b256 100000   # instrucoes; --branch-sim=yes para os saltos
CT_JSON_ITERS=10000000 build/ct_tests --gtest_filter='JsonNumbers.RoundTrip*'                           # 1e7 valores de ida e volta do Json
```

`CT_COMPARE_QUICK=1` so faz N = 1e3 e 1e5; `CT_COMPARE_ONLY=map` so a familia `map` na etapa `time`; `CT_COMPARE_THIRD`,
`CT_COMPARE_OUT`, `CT_COMPARE_BUILD` mudam as pastas. Todos os comandos correm num scope do systemd com `MemoryMax=4G` e a
compilacao com `-j3`. Para repetir o "antes" compila-se `origin/main` numa pasta e passa-se em `CT_COMPARE_BUILD`.

