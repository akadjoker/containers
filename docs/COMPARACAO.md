# Comparacao da ct com a biblioteca standard e com outras

Medicoes de velocidade, memoria e custo de compilacao dos contentores e utilitarios da `ct` contra `std` e contra as
alternativas conhecidas, mais os casos limite que se testaram a seguir. Tudo o que esta aqui foi corrido de verdade, com
`tools/bench_compare.sh` (ver o fim); o que nao foi corrido esta dito.

## 1. Resumo

- **Onde a ct ganha**: `Vector` sem reserve (2 a 4 vezes mais depressa que `std::vector` em tipos triviais a 1e5 e 1e7, porque
  cresce com `realloc`, que o glibc faz em sitio ou com `mremap`), `HashMap` contra `std::unordered_map` em procura (1,2 a 3,3
  vezes), remocao (1,6 a 15), copia (ate 9,7) e construcao com valores pequenos (ate 5,9), `HashMap` nas instrucoes por
  operacao de `hit`, `miss` e `build` com chaves `int` (menos que absl, boost e ankerl), `HashMap` a 1e7 elementos (mais depressa que
  todas em `build` e `hit`), `TreeMap` nas instrucoes (`hit` 85 contra 146 do `std::map`), `String` ate 23 caracteres (cabe no SSO de
  24 bytes da ct e nao no de 32 do libstdc++: criar 1,3 a 9,5 vezes mais depressa e copiar ate 11,7; so copiar 40 caracteres, heap nos dois, perde 14 %), `String::append`, `Deque` em
  `push_back`, acesso por indice e fila, `Function` a construir e copiar, compilacao (a unidade de teste compila em dois tercos do
  tempo e com dois tercos da memoria do equivalente em `std`).
- **Onde empata**: leitura sequencial e acesso aleatorio de `Vector`, `String::find`, `String` a concatenar, `sort` com comparador
  (`ct::sort` e `std::sort` no tempo em `int` aleatorio (65 contra 65 ns a 1e5; mas 25 % mais instrucoes); ordenado e invertido a ct ganha depois da correcao), `TreeMap`
  contra `std::map` ate 1e5.
- **Onde ainda perde** (seccao 5, com causa e proposta): `HashMap` com chaves `int` e tabelas de tamanho medio contra boost e absl
  (`miss` 2 vezes, `build` 1,8) e com valores de 256 bytes (2,4 vezes mais lento que `std::unordered_map` a 1e5: a tabela
  fica a 38 % de carga e gasta 700 B por elemento); bytes por elemento do `HashMap` (carga maxima de 75 %); `String ==` e `<` de 40
  caracteres (1,3 a 2,3 vezes no tempo; as instrucoes dizem 28 % mais); `TreeMap` a 1e6 elementos (`build` +38 %, `miss` +43 %
  contra `std::map`, apesar de menos instrucoes) e muito atras da `btree` do abseil; `Deque::push_front` e `PriorityQueue`; `ct::sort` com comparador em entrada aleatoria (25 % mais instrucoes que `std::sort`, o preco de nao ler fora do
  intervalo com um comparador mau); formatar
  `double` (`snprintf`, 3 vezes mais instrucoes que `to_chars` e fmt) e ler `float` (`strtof`, 3,6 vezes); iterar um `HashMap`
  (17 contra 5 instrucoes da ankerl).
- **Corrigido nesta ronda** (seccao 6, cada um com antes e depois): `HashSet` com capacidade 2 ficava 100 % cheio e o
  terceiro `insert` nunca acabava (defeito real); `HeapAlloc` gastava 8 bytes por alocacao; o `introsort` caia no heapsort com
  repetidos e gastava 3 vezes mais em entradas invertidas, e ordenada ou invertida custa agora uma passagem; iterar um
  `HashMap`/`HashSet` custava os buracos da tabela; copiar um `HashMap` refazia o hash de tudo; `String::to_int` custa
  menos 40 % das instrucoes.

## 2. Metodo

**Maquina**: AMD Ryzen 5 5500U (6 nucleos, 12 fios, 8 MiB de L3), 18 GB de RAM, Linux 7.0 (Ubuntu 24.04), glibc 2.39.
A maquina estava partilhada com outros trabalhos durante todas as medicoes (carga de 4 a 12 numa maquina de 12 fios).
**Por isso se confia nas instrucoes e nas contagens de alocacoes e nao no relogio**: o relogio e a melhor de 3 a 7
repeticoes e ainda tem ruido de 10 a 30 %. Quando o tempo e a instrucao discordam, a seccao 5 diz qual e a causa.

**Compilador**: g++ 13.3.0, `-std=c++17 -O2` (sem `-march=native`; C++17 so porque `std::string_view`, `to_chars` e os
tipos das outras bibliotecas o pedem, a ct e C++14). `-O3` nao se corre nesta ronda. Mesmo compilador e mesmas
opcoes para todas as bibliotecas.

**Bibliotecas**: libstdc++ 13.3; abseil 20220623.1 (`flat_hash_map`, `btree_map`; os cabecalhos do pacote Ubuntu e as
bibliotecas ja instaladas); Boost 1.83 (`unordered_flat_map`, `container::vector`, `deque`, `flat_map`, `sort::pdqsort`,
`spreadsort`); ankerl::unordered_dense 4.5.0 (um so cabecalho, descarregado da versao oficial); fmt 9.1.0 (modo so de
cabecalhos); valgrind 3.22. Os pacotes Ubuntu de abseil, Boost, fmt e valgrind foram descarregados com `apt-get download` e
extraidos para uma pasta propria (`dpkg -x`), sem instalar nada no sistema; o repositorio nao os guarda
(`tools/bench_compare.sh fetch` repete o passo). **Nao corridos**: EASTL (precisa de dois repositorios e de compilar a
biblioteca), RE2 e `std::regex` (a ct tem `bench/bench_regex.cpp` contra `std::regex`; nao se repetiu aqui), `std::variant`,
`std::pmr` e os alocadores (`Arena`, `Pool`) ficaram de fora desta ronda; `Variant` nao se mediu.

**Programas** (`bench/compare`, so compilam com `-DCT_BUILD_COMPARE=ON`, que vem desligado; esta pasta e a excecao
declarada ao "sem std": inclui a biblioteca standard e as outras, e `tools/check.sh` nostd nao a olha):

- `cmp_vector`, `cmp_map`, `cmp_string`, `cmp_sort`, `cmp_misc` (deque, mapas ordenados, `PriorityQueue`, `Function`);
  `compile_unit.cpp` para o custo de compilacao.
- Cada processo faz um cenario (`impl cenario tipo N`) e imprime uma linha: ns por operacao (melhor de 3 a 7 repeticoes),
  operacoes, alocacoes, bytes pedidos, pico e bytes vivos no fim, RSS maximo, `sizeof` e um somatorio de verificacao.
  Todos os cenarios para todas as bibliotecas dao o mesmo somatorio (e o programa de conferencia): o trabalho e igual.
- As alocacoes contam-se interpondo `malloc`, `calloc`, `realloc`, `memalign`, `posix_memalign` e `free` (glibc): bytes
  pedidos, numero de chamadas e bytes vivos = tamanho utilizavel mais 8 bytes de cabecalho do glibc, que e o que
  a memoria custa de verdade. Vale para todas as bibliotecas, porque todas acabam em `malloc`.
- As instrucoes vem de `valgrind --tool=callgrind`, ligando a coleccao so dentro da regiao medida
  (`CALLGRIND_TOGGLE_COLLECT`), uma execucao, N = 1e5, a dividir pelo numero de operacoes. `perf` nao se usou.
- Tipos: `int`, `P16` (16 bytes triviais), `B256` (256 bytes triviais), `MO` (so move, destrutor nao trivial); chaves
  `int`, `s8` (8 caracteres, SSO nos dois lados), `s20` (SSO da ct, heap do libstdc++), `s40` (heap nos dois). Chaves
  distintas e de aspecto aleatorio (uma permutacao de `uint32`). N de 1e3, 1e5 e 1e7; `B256` e as chaves de texto so ate
  1e5 ou 1e6, por memoria e por tempo.
- Cada biblioteca usa o seu hasher por omissao (`ct::Hash`, `std::hash`, `absl::Hash`, `boost::hash`, o wyhash da ankerl);
  `std::hash<int>` e a identidade.

**O que nao e igual** (por isso um empate ou uma vitoria nao e sempre um merito): `HashMap` da ct e enderecamento aberto
com sondagem linear e remocao por deslocamento (sem marcadores), as referencias nao sobrevivem a `put`; `std::unordered_map`
garante estabilidade de referencias (e por isso aloca um no por elemento); `absl` e `boost` sao tabelas SIMD com
etiquetas; ankerl guarda os valores densos num vetor e por isso itera mais depressa e usa menos memoria por valor; `Vector`
da ct so move por copia de bytes quando o tipo e trivialmente copiavel e usa `realloc` (o `std::vector` nunca pode);
`sort` de `int` sem comparador da ct e um radix (O(n), nao uma ordenacao por comparacao) e as outras colunas de `sort`
com comparador sao comparacoes; `TreeMap` e uma arvore rubro-negra com um `Pool` por tras; a ct aborta em vez de lancar
excecoes, e `std` e as outras tem de se preparar para elas.


## 3. Resultados

### 3.1 Instrucoes por operacao (callgrind, N = 1e5, arvore final)

Menos e melhor. A ultima coluna e `ct` a dividir pelo melhor dos outros; a negrito quando `ct` perde mais de 10 %. No `sort` as colunas sao `impl:algoritmo`.


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

#### Ir por operacao, map

| cenario | tipo | N | ct | std | absl | boost | ankerl | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| build | int-int | 100000 | 129.5 | 583.8 | 223.8 | 137.9 | 199.0 | 0.94 |
| hit | int-int | 100000 | 24.9 | 30.4 | 47.6 | 41.5 | 49.2 | 0.82 |
| miss | int-int | 100000 | 24.5 | 24.1 | 45.7 | 37.9 | 48.3 | 1.01 |
| erase | int-int | 100000 | 41.9 | 213.4 | 78.4 | 52.3 | 110.1 | 0.80 |
| iterate | int-int | 100000 | 17.4 | 5.0 | 14.0 | 16.9 | 5.0 | **3.47** |
| copy | int-int | 100000 | 23.0 | 453.5 | 53.5 | 12.5 | 12.7 | **1.84** |
| build | s8-int | 100000 | 391.8 | 735.1 | 536.3 | 368.6 | 375.4 | 1.06 |
| hit | s8-int | 100000 | 122.7 | 133.3 | 149.6 | 149.0 | 151.1 | 0.92 |
| miss | s8-int | 100000 | 97.3 | 94.0 | 101.7 | 109.5 | 124.5 | 1.03 |
| erase | s8-int | 100000 | 153.4 | 328.1 | 176.0 | 198.5 | 308.5 | 0.87 |
| iterate | s8-int | 100000 | 18.8 | 5.0 | 14.2 | 17.9 | 5.0 | **3.74** |
| copy | s8-int | 100000 | 60.7 | 516.5 | 160.2 | 88.4 | 54.5 | **1.11** |
| build | s40-int | 100000 | 910.2 | 1200.5 | 1098.3 | 849.7 | 817.5 | **1.11** |
| hit | s40-int | 100000 | 160.6 | 178.8 | 221.9 | 182.2 | 170.2 | 0.94 |
| miss | s40-int | 100000 | 134.7 | 142.0 | 175.7 | 144.8 | 146.1 | 0.95 |
| erase | s40-int | 100000 | 360.3 | 519.4 | 400.5 | 377.8 | 475.1 | 0.95 |
| iterate | s40-int | 100000 | 18.8 | 5.0 | 14.2 | 17.9 | 5.0 | **3.74** |
| copy | s40-int | 100000 | 483.8 | 937.5 | 655.2 | 507.5 | 475.8 | 1.02 |

#### Ir por operacao, string

| cenario | tipo | N | ct | std | charconv | libc | fmt | ct / melhor outro |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| create | len8 | 100000 | 52.0 | 73.0 | - | - | - | 0.71 |
| create | len16 | 100000 | 50.0 | 323.0 | - | - | - | 0.15 |
| create | len24 | 100000 | 262.0 | 323.0 | - | - | - | 0.81 |
| create | len40 | 100000 | 263.0 | 324.0 | - | - | - | 0.81 |
| append_char | char | 100000 | 24.1 | 19.3 | - | - | - | **1.25** |
| eq | len40 | 100000 | 58.6 | 45.6 | - | - | - | **1.28** |
| concat | len30 | 100000 | 629.0 | 516.1 | - | - | - | **1.22** |
| int2s | full | 100000 | 185.6 | 218.9 | 230.5 | 665.8 | 146.5 | **1.27** |
| s2int | full | 100000 | 175.3 | 306.2 | 141.3 | 290.2 | - | **1.24** |
| dbl2s | d | 100000 | 2572.2 | - | 849.0 | 2450.2 | 1036.1 | **3.03** |
| s2f32 | f | 100000 | 659.3 | 549.3 | 184.5 | 537.3 | - | **3.57** |

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
| pushpop | int | 100000 | 358.4 | 308.8 | - | - | - | - | - | - | **1.16** |
| construct_small | int(int) | 100000 | 28.0 | 27.0 | - | - | - | - | - | - | 1.04 |
| construct_big | int(int) | 100000 | 269.0 | 297.0 | - | - | - | - | - | - | 0.91 |
| call | int(int) | 100000 | 18.0 | 17.0 | - | - | - | - | - | - | 1.06 |
| copy | int(int) | 100000 | 298.0 | 326.0 | - | - | - | - | - | - | 0.91 |

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

### 3.3 Memoria

`layout` (tamanho do contentor vazio, slots, fator de carga e bytes vivos por elemento depois de construir N elementos; o total vem do contador de alocacoes: tamanho utilizavel mais 8 bytes de cabecalho do malloc), arvore final:

```
ct int-int 100000  sizeof_empty=32  size=100000  slots=262144  load=0.381  bytes_per_elem=23.63  heap_bytes=2363384
std int-int 100000  sizeof_empty=56  size=100000  slots=172933  load=0.578  bytes_per_elem=45.85  heap_bytes=4584520
absl int-int 100000  sizeof_empty=40  size=100000  slots=131071  load=0.763  bytes_per_elem=11.84  heap_bytes=1183736
boost int-int 100000  sizeof_empty=48  size=100000  slots=122879  load=0.814  bytes_per_elem=11.18  heap_bytes=1118200
ankerl int-int 100000  sizeof_empty=64  size=100000  slots=131072  load=0.763  bytes_per_elem=21.05  heap_bytes=2105328
ct int-int 1000000  sizeof_empty=32  size=1000000  slots=2097152  load=0.477  bytes_per_elem=18.88  heap_bytes=18878456
std int-int 1000000  sizeof_empty=56  size=1000000  slots=1447153  load=0.691  bytes_per_elem=43.58  heap_bytes=43579464
absl int-int 1000000  sizeof_empty=40  size=1000000  slots=2097151  load=0.477  bytes_per_elem=18.88  heap_bytes=18878456
boost int-int 1000000  sizeof_empty=48  size=1000000  slots=1966079  load=0.509  bytes_per_elem=17.83  heap_bytes=17829880
ankerl int-int 1000000  sizeof_empty=64  size=1000000  slots=2097152  load=0.477  bytes_per_elem=25.17  heap_bytes=25174000
ct int-b256 100000  sizeof_empty=32  size=100000  slots=262144  load=0.381  bytes_per_elem=694.72  heap_bytes=69472248
std int-b256 100000  sizeof_empty=56  size=100000  slots=172933  load=0.578  bytes_per_elem=301.84  heap_bytes=30184456
absl int-b256 100000  sizeof_empty=40  size=100000  slots=131071  load=0.763  bytes_per_elem=347.34  heap_bytes=34734072
boost int-b256 100000  sizeof_empty=48  size=100000  slots=122879  load=0.814  bytes_per_elem=325.71  heap_bytes=32571384
ankerl int-b256 100000  sizeof_empty=64  size=100000  slots=131072  load=0.763  bytes_per_elem=356.56  heap_bytes=35655688
ct int-b256 1000000  sizeof_empty=32  size=1000000  slots=2097152  load=0.477  bytes_per_elem=555.75  heap_bytes=555749368
std int-b256 1000000  sizeof_empty=56  size=1000000  slots=1447153  load=0.691  bytes_per_elem=299.58  heap_bytes=299579400
absl int-b256 1000000  sizeof_empty=40  size=1000000  slots=2097151  load=0.477  bytes_per_elem=555.75  heap_bytes=555745272
boost int-b256 1000000  sizeof_empty=48  size=1000000  slots=1966079  load=0.509  bytes_per_elem=521.14  heap_bytes=521142264
ankerl int-b256 1000000  sizeof_empty=64  size=1000000  slots=2097152  load=0.477  bytes_per_elem=293.61  heap_bytes=293605384
ct s8-int 100000  sizeof_empty=32  size=100000  slots=262144  load=0.381  bytes_per_elem=86.55  heap_bytes=8654840
std s8-int 100000  sizeof_empty=56  size=100000  slots=172933  load=0.578  bytes_per_elem=77.84  heap_bytes=7784472
absl s8-int 100000  sizeof_empty=40  size=100000  slots=131071  load=0.763  bytes_per_elem=53.74  heap_bytes=5373944
boost s8-int 100000  sizeof_empty=48  size=100000  slots=122879  load=0.814  bytes_per_elem=50.50  heap_bytes=5050360
ankerl s8-int 100000  sizeof_empty=64  size=100000  slots=131072  load=0.763  bytes_per_elem=62.96  heap_bytes=6295560
ct s8-int 1000000  sizeof_empty=32  size=1000000  slots=2097152  load=0.477  bytes_per_elem=69.21  heap_bytes=69210104
std s8-int 1000000  sizeof_empty=56  size=1000000  slots=1447153  load=0.691  bytes_per_elem=75.58  heap_bytes=75579416
absl s8-int 1000000  sizeof_empty=40  size=1000000  slots=2097151  load=0.477  bytes_per_elem=85.98  heap_bytes=85983224
boost s8-int 1000000  sizeof_empty=48  size=1000000  slots=1966079  load=0.509  bytes_per_elem=80.74  heap_bytes=80744440
ankerl s8-int 1000000  sizeof_empty=64  size=1000000  slots=2097152  load=0.477  bytes_per_elem=58.72  heap_bytes=58724360
ct s40-int 100000  sizeof_empty=32  size=100000  slots=262144  load=0.381  bytes_per_elem=150.55  heap_bytes=15054936
std s40-int 100000  sizeof_empty=56  size=100000  slots=172933  load=0.578  bytes_per_elem=141.84  heap_bytes=14184472
absl s40-int 100000  sizeof_empty=40  size=100000  slots=131071  load=0.763  bytes_per_elem=117.74  heap_bytes=11773960
boost s40-int 100000  sizeof_empty=48  size=100000  slots=122879  load=0.814  bytes_per_elem=114.50  heap_bytes=11450392
ankerl s40-int 100000  sizeof_empty=64  size=100000  slots=131072  load=0.763  bytes_per_elem=126.96  heap_bytes=12695816
ct s40-int 1000000  sizeof_empty=32  size=1000000  slots=2097152  load=0.477  bytes_per_elem=133.21  heap_bytes=133210200
std s40-int 1000000  sizeof_empty=56  size=1000000  slots=1447153  load=0.691  bytes_per_elem=139.58  heap_bytes=139579416
absl s40-int 1000000  sizeof_empty=40  size=1000000  slots=2097151  load=0.477  bytes_per_elem=149.98  heap_bytes=149983240
boost s40-int 1000000  sizeof_empty=48  size=1000000  slots=1966079  load=0.509  bytes_per_elem=144.74  heap_bytes=144744472
ankerl s40-int 1000000  sizeof_empty=64  size=1000000  slots=2097152  load=0.477  bytes_per_elem=122.72  heap_bytes=122724680
ctree int 100000  sizeof_empty=104  bytes_per_elem=41.22  allocs=251
cflat int 100000  sizeof_empty=24  bytes_per_elem=10.53  allocs=15
stdmap int 100000  sizeof_empty=48  bytes_per_elem=48.00  allocs=100000
btree int 100000  sizeof_empty=24  bytes_per_elem=11.06  allocs=3943
bflat int 100000  sizeof_empty=24  bytes_per_elem=8.19  allocs=26
ctree s20 100000  sizeof_empty=104  bytes_per_elem=65.29  allocs=398
cflat s20 100000  sizeof_empty=24  bytes_per_elem=41.98  allocs=15
stdmap s20 100000  sizeof_empty=48  bytes_per_elem=112.00  allocs=200000
btree s20 100000  sizeof_empty=24  bytes_per_elem=91.06  allocs=121064
bflat s20 100000  sizeof_empty=24  bytes_per_elem=72.80  allocs=100026
```

Crescimento do `Vector` (capacidades por que passa ao fazer `push_back` ate 1e6): ct 8, 16, 32, ... (fator 2, 18 realocacoes, primeiro bloco de 8 elementos); `std::vector` 1, 2, 4, ... (fator 2, 21 realocacoes); `boost::container::vector` 1, 2, 3, 4, 6, 9, 14, ... (fator 1,5 a 1,6, 31 realocacoes). `sizeof` do contentor vazio: `Vector` 24 em todos (tres ponteiros), `HashMap` da ct 32 contra 56 do `std::unordered_map`, 40 do absl, 48 do boost, 64 da ankerl; `String` 24 (SSO de 23) contra 32 (SSO de 15) do libstdc++; `TreeMap` 104, `FlatMap` 24, contra 48 do `std::map`.

Alocacoes para construir 1e5 `int` por `push_back`: ct 15, std 18, boost 26; um `unordered_map<int,int>` de 1e5 faz 100014 contra 15 da ct, e o pico de memoria de uma construcao de `Vector<int>` e 524 KB (ct, que usa `realloc`) contra 786 KB (std).


#### Memoria na construcao (bytes vivos por elemento, alocacoes por construcao, pico por elemento)

| familia | cenario | tipo | N | impl | vivo B/elem | pico B/elem | alocacoes | RSS KiB |
|---|---|---|---:|---|---:|---:|---:|---:|
| vector | build | int | 100000 | ct | 5.28 | 5.28 | 15 | 44528 |
| vector | build | int | 100000 | std | 5.28 | 7.95 | 18 | 44592 |
| vector | build | int | 100000 | boost | 4.10 | 6.68 | 26 | 44784 |
| vector | build_reserved | int | 100000 | ct | 4.01 | 4.01 | 1 | 44520 |
| vector | build_reserved | int | 100000 | std | 4.01 | 4.01 | 1 | 44616 |
| vector | build_reserved | int | 100000 | boost | 4.01 | 4.01 | 1 | 44688 |
| vector | build | p16 | 100000 | ct | 21.01 | 21.01 | 15 | 44804 |
| vector | build | p16 | 100000 | std | 21.01 | 31.54 | 18 | 44796 |
| vector | build | p16 | 100000 | boost | 16.34 | 26.54 | 26 | 44496 |
| vector | build_reserved | p16 | 100000 | ct | 16.02 | 16.02 | 1 | 44716 |
| vector | build_reserved | p16 | 100000 | std | 16.02 | 16.02 | 1 | 44544 |
| vector | build_reserved | p16 | 100000 | boost | 16.02 | 16.02 | 1 | 44664 |
| vector | build | mo | 100000 | ct | 31.50 | 47.27 | 15 | 44672 |
| vector | build | mo | 100000 | std | 31.50 | 47.27 | 18 | 44580 |
| vector | build | mo | 100000 | boost | 24.49 | 39.81 | 26 | 44300 |
| vector | build_reserved | mo | 100000 | ct | 24.00 | 24.00 | 1 | 44844 |
| vector | build_reserved | mo | 100000 | std | 24.00 | 24.00 | 1 | 44848 |
| vector | build_reserved | mo | 100000 | boost | 24.00 | 24.00 | 1 | 44392 |
| map | build | int-int | 100000 | ct | 23.63 | 35.47 | 15 | 44844 |
| map | build | int-int | 100000 | std | 45.85 | 47.96 | 100014 | 45052 |
| map | build | int-int | 100000 | absl | 11.84 | 17.78 | 17 | 44744 |
| map | build | int-int | 100000 | boost | 11.18 | 16.79 | 13 | 44452 |
| map | build | int-int | 100000 | ankerl | 21.05 | 26.34 | 34 | 44716 |
| map | build | s8-int | 100000 | ct | 86.55 | 129.84 | 15 | 53480 |
| map | build | s8-int | 100000 | std | 77.84 | 77.84 | 100014 | 48828 |
| map | build | s8-int | 100000 | absl | 53.74 | 80.61 | 17 | 49548 |
| map | build | s8-int | 100000 | boost | 50.50 | 75.78 | 13 | 48840 |
| map | build | s8-int | 100000 | ankerl | 62.96 | 89.21 | 34 | 47444 |
| map | build | s40-int | 100000 | ct | 150.55 | 192.76 | 100015 | 65496 |
| map | build | s40-int | 100000 | std | 141.84 | 141.84 | 200014 | 61092 |
| map | build | s40-int | 100000 | absl | 117.74 | 117.74 | 100017 | 58708 |
| map | build | s40-int | 100000 | boost | 114.50 | 114.50 | 100013 | 58224 |
| map | build | s40-int | 100000 | ankerl | 126.96 | 131.16 | 100034 | 58328 |
| string | append_char | char | 100000 | ct | 1.88 | 1.88 | 13 | 44824 |
| string | append_char | char | 100000 | std | 1.23 | 1.84 | 13 | 44468 |
| misc | push_back | int | 100000 | ct | 5.28 | 5.28 | 15 | 44420 |
| misc | push_back | int | 100000 | std | 4.23 | 4.23 | 790 | 44780 |
| misc | push_back | int | 100000 | boost | 4.23 | 4.23 | 790 | 44904 |
| misc | push_back | p16 | 100000 | ct | 21.01 | 21.01 | 15 | 44520 |
| misc | push_back | p16 | 100000 | std | 16.91 | 16.91 | 3136 | 44452 |
| misc | push_back | p16 | 100000 | boost | 16.91 | 16.91 | 3136 | 44680 |
| misc | build | int | 10000 | ctree | 42.07 | 42.07 | 28 | 44416 |
| misc | build | int | 10000 | cflat | 13.52 | 13.52 | 12 | 44492 |
| misc | build | int | 10000 | stdmap | 48.00 | 48.00 | 10000 | 44536 |
| misc | build | int | 10000 | btree | 11.10 | 11.10 | 400 | 44472 |
| misc | build | int | 10000 | bflat | 12.44 | 20.22 | 22 | 44736 |

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

### 3.5 Antes e depois das correcoes (so ct)

Instrucoes (callgrind) e bytes pedidos ao malloc, o que e deterministico; o tempo esta na seccao 4.


#### ct: instrucoes por operacao

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|
| vector | build_reserved | int | 100000 | 11.02 | 12.02 | +9.1% |
| vector | build_reserved | p16 | 100000 | 13.02 | 14.02 | +7.7% |
| map | iterate | int-int | 100000 | 23.37 | 17.43 | -25.4% |
| map | copy | int-int | 100000 | 59.03 | 23.00 | -61.0% |
| map | build | s8-int | 100000 | 398.85 | 391.77 | -1.8% |
| map | erase | s8-int | 100000 | 151.47 | 153.35 | +1.2% |
| map | iterate | s8-int | 100000 | 23.37 | 18.75 | -19.8% |
| map | copy | s8-int | 100000 | 145.77 | 60.73 | -58.3% |
| map | build | s40-int | 100000 | 928.27 | 910.18 | -1.9% |
| map | erase | s40-int | 100000 | 355.36 | 360.26 | +1.4% |
| map | iterate | s40-int | 100000 | 23.37 | 18.75 | -19.8% |
| map | copy | s40-int | 100000 | 612.60 | 483.75 | -21.0% |
| string | create | len8 | 100000 | 50.02 | 52.02 | +4.0% |
| string | create | len16 | 100000 | 48.02 | 50.02 | +4.2% |
| string | create | len24 | 100000 | 272.02 | 262.02 | -3.7% |
| string | create | len40 | 100000 | 273.02 | 263.02 | -3.7% |
| string | concat | len30 | 100000 | 634.02 | 629.02 | -0.8% |
| string | int2s | full | 100000 | 189.79 | 185.62 | -2.2% |
| string | s2int | full | 100000 | 291.18 | 175.26 | -39.8% |
| string | s2f32 | f | 100000 | 663.31 | 659.31 | -0.6% |
| sort | ct:sort | int.random | 100000 | 158.30 | 163.14 | +3.1% |
| sort | ct:stable | int.random | 100000 | 233.12 | 221.59 | -4.9% |
| sort | ct:sort | p16.random | 100000 | 165.43 | 167.10 | +1.0% |
| sort | ct:stable | p16.random | 100000 | 227.84 | 264.68 | +16.2% |
| sort | ct:sort | int.sorted | 100000 | 109.62 | 7.02 | -93.6% |
| sort | ct:stable | int.sorted | 100000 | 109.72 | 91.95 | -16.2% |
| sort | ct:sort | p16.sorted | 100000 | 109.79 | 7.02 | -93.6% |
| sort | ct:stable | p16.sorted | 100000 | 108.81 | 139.58 | +28.3% |
| sort | ct:sort | int.reversed | 100000 | 288.29 | 10.02 | -96.5% |
| sort | ct:stable | int.reversed | 100000 | 219.02 | 201.43 | -8.0% |
| sort | ct:sort | p16.reversed | 100000 | 290.30 | 10.02 | -96.5% |
| sort | ct:stable | p16.reversed | 100000 | 237.96 | 293.67 | +23.4% |
| sort | ct:sort | int.dups | 100000 | 240.96 | 134.81 | -44.1% |
| sort | ct:stable | int.dups | 100000 | 226.07 | 213.93 | -5.4% |
| sort | ct:sort | p16.dups | 100000 | 247.94 | 134.75 | -45.7% |
| sort | ct:stable | p16.dups | 100000 | 221.09 | 257.46 | +16.5% |
| misc | construct_big | int(int) | 100000 | 273.02 | 269.02 | -1.5% |
| misc | copy | int(int) | 100000 | 302.02 | 298.02 | -1.3% |

#### ct: bytes pedidos ao malloc por execucao

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|
| map | build | s40-int | 100000 | 22201096.00 | 21400976.00 | -3.6% |
| map | copy | s40-int | 100000 | 13550760.00 | 12750752.00 | -5.9% |
| string | create | len24 | 100000 | 3300000.00 | 2500000.00 | -24.2% |
| string | create | len40 | 100000 | 4900000.00 | 4100000.00 | -16.3% |
| string | concat | len30 | 100000 | 5500000.00 | 4700000.00 | -14.5% |
| misc | fifo | int | 100000 | 1032.00 | 992.00 | -3.9% |
| misc | fifo | p16 | 100000 | 4008.00 | 3968.00 | -1.0% |
| misc | construct_big | int(int) | 100000 | 7200000.00 | 6400000.00 | -11.1% |
| misc | copy | int(int) | 100000 | 7200000.00 | 6400000.00 | -11.1% |

#### ct: alocacoes por execucao

| familia | cenario | tipo | N | antes | depois | variacao |
|---|---|---|---:|---:|---:|---:|

## 4. Tempo antes e depois das correcoes (so ct, ns por operacao, N = 1e5)

Para cada celula correm os binarios da ct de origin/main e da arvore final alternadamente quatro vezes (a carga da maquina afeta os dois por igual) e fica o minimo de cada lado. Mesmo assim o ruido e de uns 10 %: le-se uma diferenca menor que isso como empate. Uma primeira passagem com uma execucao de cada lado (feita com a maquina a carga 14 a 27) deu alguns saltos de 2 a 5 vezes que o minimo de quatro nao confirma (por exemplo `stable_sort` de `P16`), por isso nao se usa.

| familia | celula | antes | depois | variacao |
|---|---|---:|---:|---:|
| vector | vector ct build int 100000 | 1.99 | 2.02 | +1.7% |
| vector | vector ct build p16 100000 | 10.07 | 9.25 | -8.1% |
| vector | vector ct build_reserved int 100000 | 1.75 | 1.62 | -7.3% |
| vector | vector ct build_reserved p16 100000 | 9.63 | 10.70 | +11.1% |
| map | map ct build int-int 100000 | 101.43 | 105.09 | +3.6% |
| map | map ct hit int-int 100000 | 10.00 | 13.51 | +35.1% |
| map | map ct miss int-int 100000 | 15.85 | 15.91 | +0.4% |
| map | map ct erase int-int 100000 | 26.11 | 24.48 | -6.2% |
| map | map ct iterate int-int 100000 | 10.11 | 5.33 | -47.3% |
| map | map ct copy int-int 100000 | 16.84 | 11.73 | -30.4% |
| map | map ct build s8-int 100000 | 246.22 | 241.05 | -2.1% |
| map | map ct hit s8-int 100000 | 102.02 | 105.25 | +3.2% |
| map | map ct miss s8-int 100000 | 76.36 | 117.41 | +53.8% |
| map | map ct erase s8-int 100000 | 179.11 | 174.61 | -2.5% |
| map | map ct iterate s8-int 100000 | 13.23 | 10.14 | -23.4% |
| map | map ct copy s8-int 100000 | 43.65 | 34.55 | -20.9% |
| string | string ct create len8 100000 | 6.17 | 6.39 | +3.5% |
| string | string ct create len24 100000 | 21.98 | 22.58 | +2.7% |
| string | string ct concat len30 100000 | 100.34 | 69.04 | -31.2% |
| string | string ct s2int full 100000 | 38.15 | 27.05 | -29.1% |
| string | string ct int2s full 100000 | 26.04 | 21.85 | -16.1% |
| sort | sort ct sort int.random 100000 | 73.78 | 78.42 | +6.3% |
| sort | sort ct stable int.random 100000 | 100.56 | 117.34 | +16.7% |
| sort | sort ct sort p16.random 100000 | 83.17 | 101.65 | +22.2% |
| sort | sort ct stable p16.random 100000 | 100.63 | 101.53 | +0.9% |
| sort | sort ct sort int.sorted 100000 | 15.21 | 0.45 | -97.0% |
| sort | sort ct stable int.sorted 100000 | 13.82 | 10.46 | -24.3% |
| sort | sort ct sort p16.sorted 100000 | 18.50 | 1.55 | -91.6% |
| sort | sort ct stable p16.sorted 100000 | 17.27 | 22.74 | +31.7% |
| sort | sort ct sort int.reversed 100000 | 104.76 | 1.22 | -98.8% |
| sort | sort ct stable int.reversed 100000 | 32.97 | 23.56 | -28.5% |
| sort | sort ct sort p16.reversed 100000 | 61.08 | 2.02 | -96.7% |
| sort | sort ct stable p16.reversed 100000 | 32.69 | 27.39 | -16.2% |
| sort | sort ct sort int.dups 100000 | 105.80 | 49.80 | -52.9% |
| sort | sort ct stable int.dups 100000 | 46.61 | 33.80 | -27.5% |
| sort | sort ct sort p16.dups 100000 | 53.64 | 34.72 | -35.3% |
| sort | sort ct stable p16.dups 100000 | 41.55 | 41.72 | +0.4% |
| misc | misc deque ct push_back int 100000 | 1.49 | 1.04 | -30.3% |
| misc | misc deque ct push_front int 100000 | 2.19 | 2.20 | +0.5% |
| misc | misc deque ct iterate int 100000 | 0.71 | 1.27 | +78.0% |
| misc | misc deque ct fifo int 100000 | 2.83 | 2.27 | -19.6% |
| misc | misc ordered ctree build int 100000 | 313.83 | 306.89 | -2.2% |
| misc | misc ordered ctree hit int 100000 | 164.18 | 159.10 | -3.1% |
| misc | misc ordered ctree erase int 100000 | 220.09 | 220.64 | +0.3% |
| misc | misc pq ct pushpop int 100000 | 122.41 | 121.76 | -0.5% |
| misc | misc function ct copy x 100000 | 29.09 | 33.37 | +14.7% |

## 5. Onde a ct ainda perde, a causa e o que fazer

Cada ponto diz o que se mediu, a causa (com a prova) e, quando nao se corrigiu, a proposta e o ganho esperado.

**5.1 `HashMap` com chaves `int`: tempo de `miss`, `hit` e `erase` em tabelas de tamanho medio contra boost e absl.**
Com 1e5 elementos, `miss` custa 11 ns na ct e 5,5 ns no `boost::unordered_flat_map` (2 vezes), `build` 53 contra 29 (1,8
vezes), `erase` 17 contra 10. As instrucoes nao dizem o mesmo: a ct gasta menos (`hit` 25 contra 41 de boost, `miss` 24 contra
38). A diferenca e de cache: a sondagem da ct le o byte de meta e, se o slot esta ocupado, **vai ler a chave ao slot**
(uma linha de cache de uma tabela de 2 MiB) para comparar; as tabelas SIMD guardam 7 bits do hash em cada byte de meta
e so tocam no slot quando a etiqueta bate (um `miss` nunca toca nos slots). A 1e7 a ct ja ganha a todas (`build` 95 ns contra
105 de boost, `hit` 57 contra 88): ai todas estao limitadas pela memoria.
*Prova*: um prototipo com etiquetas de 7 bits na meta (`meta = 0x80 | hash >> 57`, comparada antes da chave) baixou o `miss` de 62
para 44 ns a 1e6 e de 54 para 21 ns com chaves `s8`, e o `build` 2 a 13 %. *Porque nao entrou*: o proprio prototipo subiu as
instrucoes de `hit` com chaves `int` de 25 para 33, as de `erase` de 42 para 50 (e o `erase` de `String` de 151 para 180: a remocao
por deslocamento tem de mover a etiqueta), e o tempo de `hit` e `erase` subiu 7 a 12 % a 1e6; "nada pode ficar mais lento"
mandou voltar atras (o commit de reversao esta no historico).
*Proposta*: etiquetas so para chaves de comparacao cara (`String` e compostas), que e onde mais ganham (`miss` -31 % de instrucoes e
-61 % de tempo, `build` -27 % de instrucoes com `s8`), e deixar as chaves de tipos primitivos como estao; mais deslocar a etiqueta
no `erase` sem recalcular o hash. Ganho esperado: `miss` de chaves de texto 2 a 3 vezes mais depressa.

**5.2 `HashMap`: bytes por elemento.** Com chaves `int` e valores `int` a ct gasta 19 a 24 B por elemento (1e5 e 1e6), absl 12 a 19,
boost 11 a 18, ankerl 21 a 25, `std::unordered_map` 44 a 46 (ver 3.3). Um slot custa 9 bytes (8 do par mais 1 de meta), a carga
maxima e 75 % (absl e boost 87,5 %, ankerl 80 %) e a tabela dobra, por isso a carga fica entre 37,5 % e 75 %: 12 a 24 B por elemento conforme o
N calhe (1e5 calha mal: 38 %). Com valores de 256 bytes isto pesa: 100000 elementos ocupam 262144 slots de 265 bytes (69 MB, pico de
104 MB com a tabela antiga durante o rehash) contra 30 MB do `std::unordered_map`; `build` mede 925 ns por elemento contra 384
do std e 443 a 569 das tabelas de abseil, boost e ankerl (callgrind: a ct gasta 351 instrucoes por elemento e o std 824: o tempo e
trafego de memoria). Proposta: subir a carga maxima para 87,5 % com etiquetas (5.1), o que da 10 a 20 B; sem etiquetas
a sondagem linear com 87 % de carga fica cara.

**5.3 Iterar um `HashMap`.** Depois da correcao (iterador por grupos de 8 slots) custa 17 instrucoes por elemento a
1e5 (antes 23); ankerl, que guarda os valores densos num vetor, custa 5, e absl 14. Os slots livres que ficam pelo meio (a carga e 38 %)
continuam a custar. Proposta: um vetor denso de entradas mais uma tabela de indices (o que a ankerl faz) mudaria a estabilidade
das referencias e o custo de `erase`; nao se tentou.

**5.4 Formatar e ler numeros.** `String::number(double)` usa `snprintf("%.*g")`: 2572 instrucoes contra 849 de `to_chars` e 1036 de
fmt (3 vezes), e 2450 do `snprintf` puro (e o mesmo). `String::to_float` usa `strtof`: 659 contra 185 de `from_chars` (3,6 vezes).
`to_int` desceu de 291 para 175 (agora sem `strtol`) e `from_chars` faz 141. Inteiros para texto: 186 contra 147 de
`fmt::format_int`. Proposta: `append_number(double)` poderia usar o caminho rapido de `json_append_double` (que ja existe,
com `long double`) quando a precisao pedida e a mais curta que volta ao mesmo valor, e `to_float` um Eisel-Lemire (cerca de 150
instrucoes). Nao se fez: sao 200 linhas de codigo numerico que precisam de um diferencial de 1e8 valores contra `strtof`/`snprintf`.

**5.5 `String`: `append_char`, `==`, `<`, concatenacao.** 1,2 a 1,3 vezes as instrucoes do `std::string` (`push_back` 24 contra 19, `==` de 40
caracteres 59 contra 46, `a + b + c` 629 contra 516). A ct decide SSO ou heap em cada `size()` e `data()` (a marca esta no ultimo byte);
o `std::string` guarda o ponteiro sempre. No tempo `append_char` e a concatenacao empatam, mas `==` e `<` de 40 caracteres
sao 1,3 a 2,3 vezes mais lentos (12 contra 5 ns a 1e5; 11 contra 8 a 1e6): a diferenca de instrucoes (13) nao a explica toda e nao
se encontrou o resto (os dois chamam o mesmo `memcmp`). A concatenacao pede `reserve(30)` e o `reserve` da ct dobra a
partir do SSO (23 vezes 2 mais 1: 47 bytes, contra 31 do libstdc++, que dobra a partir de 15). Proposta: `operator+` com capacidade exata.
Ganho pequeno (uma alocacao de 48 em vez de 64 bytes).

**5.6 `Deque` e `PriorityQueue`.** `push_front` custa 21 instrucoes contra 13 do `std::deque` (a ct dobra um anel de potencia de 2
e copia; o `std::deque` aloca blocos fixos e nunca move); percorrer 10 contra 8. `PriorityQueue` faz `pushpop` com 358 contra 309
instrucoes do `std::priority_queue`: o `siftDown` troca a cada nivel em vez de descer ate a folha e subir (a tecnica do buraco do
libstdc++, menos comparacoes). Proposta para a segunda: copiar a tecnica do buraco, ganho de cerca de 15 %. No tempo `Deque`
ganha em `push_back` e `fifo`.

**5.7 `sort` com comparador em entrada aleatoria.** Depois da correcao `ct::sort` faz 163 instrucoes por elemento em `int` aleatorio
(158 antes; `std::sort` 128, pdqsort de boost 118; em `P16` aleatorio 167, 165 antes, 136 e 121): **+3 % e +1 % de
instrucoes** a mais que antes e cerca de 25 % mais que `std::sort`. E o preco das guardas `lo`/`hi` da particao, que mantem a
leitura dentro do intervalo com um comparador que nao e uma ordem fraca estrita (`<=`; a versao sem guardas, como a do libstdc++, le
fora da memoria nesse caso, e o `introsort` anterior nao lia): aceitou-se, a troca e -45 % nos repetidos e -96 % nas entradas
ordenadas ou invertidas. No relogio nao se consegue resolver nesta maquina: um micro-programa isolado a 1e6 `P16` aleatorios mede
88,8 para 70,4 ns por elemento, e a passagem alternada da seccao 4 (1e5, minimo de 4) mede +6 % em `int` e +22 % em `P16`; so se
pode dizer que a diferenca esta dentro do ruido. `stable_sort` nao mudou de codigo, mas as instrucoes de `p16` subiram 16 a
28 %: o GCC passou a ver o buffer de `malloc` sem o cabecalho e trocou o ciclo de copia por `memcpy`, e o callgrind conta cada
passo do `rep movsb` como uma instrucao (45 % do total esta no `memcpy`); isolado e a tempo de relogio, `stable_sort` de 1e6 `P16`
mede 57 contra 56 ns/elemento (aleatorio) e 37 contra 32 (ordenado) antes e depois, e a passagem alternada de `p16.random` da empate.

**5.8 `Vector`.** Empate em tudo menos em construcao sem reserve, que ganha (3 a 4 vezes a 1e5, 2 vezes a 1e7, porque
`realloc` cresce sem copiar). Tres notas: (a) `build_reserved` mede +1 instrucao por elemento depois das correcoes em `int` e
`P16` (11 para 12): e o ciclo do `push_back` que o GCC compilou com a carga do fim da capacidade a parte em vez de dentro do `cmp`
(o codigo da ct nao mudou; as duas formas tem as mesmas microoperacoes). (b) O primeiro bloco de um `Vector` tem 8 elementos: um
`Vector<B256>` de 1 elemento aloca 2 KiB (o do std aloca 256 B); cresce por 2 como o std. (c) `build` de `MO` (tipo so move com destrutor)
a 1e5: ct 53 ns, std 58, boost 15: nas instrucoes a ct e o std empatam (36 contra 34) e a boost gasta mais (52), por isso e o
comportamento de paginas do `malloc` na sequencia de tamanhos de 1,6 que a boost usa, nao codigo.

**5.9 `TreeMap` a 1e6 elementos.** Nas instrucoes ganha ao `std::map` (`hit` 85 contra 146 a 1e4, `build` 397 contra 628, `erase` 299
contra 374), mas a 1e6 elementos `build` custa 2076 ns contra 1506, `miss` 1547 contra 1079 e `hit` 1253 contra 1060: e trafego de cache
numa arvore de 40 bytes por no que o `Pool` entrega pela ordem de insercao. A `btree_map` do abseil faz o mesmo em 229 a 336 ns (4 a 6
vezes mais depressa que as duas). Iterar a arvore custa 27 ns por elemento contra 66 do `std::map` e 2 da btree. Proposta: uma btree
(chaves agrupadas por no) no lugar da rubro-negra; e uma mudanca de estrutura, nao se tentou.

**5.10 `String` com chave `s40` no `HashMap`.** `build` a 1e6: ct 797 ns contra 534 (boost) e 243 (ankerl). A ankerl guarda os pares
densos e nao move as chaves ao crescer; a ct move o par (`String` + `int`) a cada rehash. E o preco do enderecamento aberto com
as entradas dentro da tabela.

## 6. Defeitos e melhorias feitas (antes e depois)

| # | o que | antes | depois | prova |
|---|---|---|---|---|
| 1 | `HashSet` com capacidade 2 (`reserve(1)` ou a copia de um conjunto de 1 elemento) ficava 100 % cheio ao 2.o `insert`; a chave em falta nunca parava a sondagem | o 3.o `insert` nunca acaba | acaba | `EdgeHashMap.SetWithCapacityTwoDoesNotFillUpAndSpin` (o filho tem alarme de 5 s): antes "died but not with expected exit code" apos 5001 ms; depois passa em 0 ms. O `HashMap` ja tinha a guarda; faltava no `HashSet`. |
| 2 | `HeapAlloc` guardava 8 bytes de cabecalho antes de cada bloco (para o `free` de blocos alinhados) | 1000 blocos de 24 bytes ocupam 47 B cada | 32 B cada (so o malloc); `String` de 24 a 40 caracteres: 33 para 25 bytes pedidos, 48 para 32 no malloc | `EdgeHeapAlloc.NoPerAllocationHeaderOnPosix` (mallinfo2): antes 47, depois 31 a 32; ver 3.5 (bytes pedidos -12 a -24 %). Em POSIX o alinhamento acima de 16 usa `posix_memalign`, que `free` solta; em Windows fica o cabecalho. |
| 3 | `introsort`: particao de Lomuto: os repetidos iam todos para o mesmo lado e uma entrada invertida trocava quase tudo | 100000 `int` com 16 valores: 3,49 milhoes de comparacoes e cai no heapsort; invertida 3,37 milhoes; instrucoes por elemento 241 (dups) e 288 (invertida) | 1,46 milhoes e 1,44 milhoes; 135 e 10 instrucoes por elemento | `EdgeSort.ManyDuplicatesStayNearNLogN` (< 2,2 milhoes), `SortedAndReversedInputCostOnePass` (<= 100000 e 200000): ambos falharam antes (3,49 M, 3,37 M, 1,29 M) e passam. Tempo de `P16` a 1e6: aleatorio 88,8 para 70,4 ns, invertido 60,7 para 24,1, repetidos 63,7 para 33,8. Mais um teste com comparador `<=` (nao le fora do intervalo, corre com ASan). |
| 4 | Iterar um `HashMap`/`HashSet` passava slot a slot pelos buracos; copiar refazia hash e sondagem de cada elemento | iterar 23 instrucoes por elemento (1e5, `int`), copiar 59 | 17 e 23 | ver 3.5: iterar -25 %, copiar -61 % (`int`), -58 % (`s8`), -21 % (`s40`); o tempo esta na seccao 4. `EdgeHashMap.IterationVisitsExactlyTheOccupiedSlots`, `IterationAtTinyAndExactCapacities`, `CopyAndAssignKeepEveryEntry`. A copia por indice so se usa na copia construida (o hasher e o mesmo) e quando a capacidade que fica e a da origem. |
| 5 | `String::to_int` e `String::number` | `strtol` e um algarismo por passo | analise propria com o mesmo resultado que `static_cast<int>(strtol(...))`; dois algarismos por passo | `EdgeNumbers.StringToIntMatchesStrtolOnRandomText` (1e6 textos, igual a `strtol`), `IntegerToTextMatchesPrintfOnRandomValues` (5e5 valores, igual a `printf`); ver 3.5: `s2int` -40 % de instrucoes, `int2s` -2 %. |

O que se tentou e se desfez: etiquetas de 7 bits na meta do `HashMap`/`HashSet` (5.1). Nao entram nesta lista as variacoes que
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
  `HashSet` a remocao desloca os elementos seguintes para tras e por isso **apagar o elemento atual durante um ciclo
  `for` e invalido** (o ciclo pode saltar ou repetir elementos); o padrao correto e juntar as chaves e apagar depois, que e o que o teste faz.
  Inserir durante a iteracao pode fazer rehash e invalida os iteradores e as referencias. A ct nao tem forma de o detetar; fica
  documentado aqui e nao no codigo (a regra do projeto e nao ter comentarios nos cabecalhos).
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
- **Diferenciais com 1e6 de operacoes e semente fixa**: `Vector` contra um vetor de capacidade fixa (push, pop, insert, erase de
  intervalos, resize, assign, copia, move, swap, erase durante o ciclo), `Deque` contra um modelo com deslocamento (push e pop dos dois lados, resize, copia),
  `String` contra um vetor de `char` (push, append, insert, erase, resize, `append(s)`, copia, `substr`, `find`), `TreeMap` e `FlatMap` contra
  um vetor direto de 2048 chaves (ordem ascendente na iteracao, `validate()`), `HashMap` e `HashSet` contra um vetor direto de 4096 chaves
  (com copias, atribuicoes e moves pelo caminho).

**Resultado**: um defeito real (6.1) e as melhorias de eficiencia de 6.2 a 6.5. Todos os outros testes passaram sem mexer em codigo.
Tudo corre com ASan + UBSan.

## 8. O que ficou por fazer

- Etiquetas no `HashMap` so para chaves de texto (5.1) e carga de 87,5 %.
- `to_float` rapido, `append_number(double)` rapido (5.4), `PriorityQueue` com a tecnica do buraco (5.6).
- `Variant`, `Arena`, `Pool`, `Regex` contra `std::pmr`, RE2 e `std::regex`; EASTL.
- `-O3` (nao se mediu se muda a ordem); `-march=native`.
- Falha de alocacao (nao ha caminho de erro, ver 7).
- `Vector` de 1 elemento com capacidade 8; `reserve` do `String` a dobrar a partir do SSO (5.5, 5.8).
- `String::to_int` fora do intervalo (docs/SUSPEITAS.md).

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

`CT_COMPARE_QUICK=1` so faz N = 1e3 e 1e5; `CT_COMPARE_ONLY=map` so a familia `map` na etapa `time`; `CT_COMPARE_THIRD`,
`CT_COMPARE_OUT`, `CT_COMPARE_BUILD` mudam as pastas. Todos os comandos correm num scope do systemd com `MemoryMax=4G` e a
compilacao com `-j3`. Para repetir o "antes" compila-se `origin/main` numa pasta e passa-se em `CT_COMPARE_BUILD`.

