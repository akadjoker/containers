# Registo de bugs

Este ficheiro acompanha os bugs encontrados durante a auditoria. Cada entrada
fica aqui mesmo depois de corrigida, com o teste de regressão que impede o seu
regresso.

## Corrigidos

| Área | Problema | Correção | Teste de regressão |
|---|---|---|---|
| `Vector` | `emplace_back` e `emplace` podiam receber uma referência a um elemento do próprio vector e invalidá-la durante `grow()` ou ao abrir o intervalo para inserção. | Materializar o novo `T` antes de qualquer deslocação de armazenamento. | `VectorInsertErase.EmplaceWithElementReferenceSurvivesRelocation` |
| `Deque` | `emplace_back` e `emplace_front` podiam invalidar um argumento que referia um elemento interno durante crescimento. | Materializar o novo `T` antes de `grow()`. | `DequeWrap.EmplaceWithElementReferenceSurvivesGrowth` |
| `HashMap` | `put(entry.key, entry.value)` crescia antes de procurar a chave; com referências internas podia rehash, ler memória inválida e inserir uma duplicada. `operator[]` tinha o mesmo rehash desnecessário para chave existente. | Procurar primeiro; para inserções que exigem rehash, estabilizar chave e valor antes de mover as entradas. | `HashMap.PutWithEntryReferencesDoesNotRehashOrDangle` |
| `HashSet` | `insert(*set.begin())` podia rehash antes de descobrir que a chave já existia, invalidando a referência. | Procurar primeiro; estabilizar a chave antes de rehash numa inserção nova. | `HashSet.InsertWithInternalKeyDoesNotRehash` |
| `Json` | As APIs que recebiam `ct::String` convertiam a chave para `c_str()`, truncando chaves com NUL embutido em `find`, `contains`, `operator[]` e `set`. | Sobrecargas que comparam o comprimento completo de `String`. | `Json.StringKeysWithEmbeddedNulKeepTheirFullLength` |
| `Span` | Operações vazias podiam fazer aritmética sobre `nullptr` (`end`, `last`, `subspan` e o construtor por par de ponteiros). | Preservar diretamente o ponteiro nulo quando o intervalo é vazio. | `Span.EmptySubviewsDoNotDoNullPointerArithmetic` |
| `Vector` / `SlotMap` | Copiar `Vector` vazio e obter `SlotMap::end()` vazio faziam aritmética `nullptr + 0`. | Usar diretamente os iteradores armazenados/nulos no caso vazio. | `VectorCopyMove.EmptyCopyKeepsNullIterators`, `SlotMap.EmptyIterationUsesNullIteratorsSafely` |

## Validação executada

- Testes específicos dos contentores alterados, incluindo fuzzing já existente.
- Casos de regressão executados com AddressSanitizer e UBSan.
- `ct_torture` sob AddressSanitizer e UBSan: seeds `1`, `17`, `12345` e
  `3735928559`, com 20 000 iterações cada.

## Verificação de desempenho

As correções que mexem em capacidade só materializam argumentos no ramo frio de
crescimento. Os caminhos `push_*` de `Vector`/`Deque` não foram alterados.

Comparação Release (`-O3 -march=native`) com o `HEAD` limpo, no mesmo host; os
valores são o melhor de sete repetições e por isso pequenas diferenças são ruído
normal de frequência/temperatura:

| Caso | HEAD limpo | Auditoria | Leitura |
|---|---:|---:|---|
| `Vector` push 2M `int` sem `reserve` | 1,701 ms | 1,668 ms | sem regressão |
| `Deque` push_back 2M `int` | 1,209 ms | 1,201 ms | sem regressão |
| `Deque` FIFO 8M operações | 5,935 ms | 5,946 ms | sem regressão |
| `HashMap` put 1M | 64,723 ms | 63,345 ms | sem regressão |
| `HashMap` put 1M reservado | 24,097 ms | 25,614 ms | +6,3%; o único desvio observável |
| `HashSet` insert 1M + contains 1M | 37,603 ms | 35,392 ms | dentro do ruído |

O `HashMap` continua a vencer `std::unordered_map` por cerca de 11x na inserção
reservada. A verificação de chaves internas só corre no crescimento, mas o ramo
extra ainda merece ser acompanhado em máquinas de CI com frequência fixa.

## Próxima área a auditar

Segurança perante falhas de construção/alocação: os contentores privilegiam um
modelo sem exceções, mas tipos de utilizador que lançam durante cópia/movimento
merecem uma política explícita e testes dedicados se essa utilização estiver no
escopo da biblioteca.

## Automação

O workflow [`.github/workflows/ci.yml`](.github/workflows/ci.yml) executa em
cada `push` e pull request: testes Debug, testes com ASan/UBSan e três seeds de
`ct_torture`. Não publica artefactos nem corre benchmarks, para manter o uso de
GitHub Actions baixo e os resultados de performance fora de máquinas partilhadas.
