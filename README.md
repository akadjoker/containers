# ct — fast containers (C++14, header-only)
 

## Estado

| Container  | Notas |
|---|---|
| `ct::Vector<T, Alloc>`  | realloc/memcpy para tipos triviais, resize sem inicialização, allocator policy via EBO |
| `ct::Arena` + `ArenaAlloc`  | bump por blocos, try_expand (crescimento in-place), reset() recicla por frame |
| `ct::Pool<T>`  | free list tipada p/ objetos de jogo; API crua sem ctors; |
| `ct::String`  | 24 B com SSO de 23 (std: 32 B, SSO 15); find/split/trim/number/hash; x15-20 na zona 16-23 chars |
| `ct::HashMap<K,V>`  | open-addressing linear, erase backward-shift; x2-13 vs unordered_map |
| `ct::HashSet<K>`  | mesma técnica; x5.4 vs unordered_set |
| `ct::FlatMap<K,V>`  | ordenado (Vector + binary search); iteração x40-50 vs std::map |
| `ct::TreeMap<K,V>`  | red-black tree com nós no Pool; insert x1.4, churn x1.2 vs std::map |
| `ct::Deque<T, Alloc>`  | ring buffer pow2 (wrap = AND); pontas x1.9-2.4, FIFO x2, acesso x1.8 vs std::deque; spans lineares p/ iteração vectorizada (x2.6-3) |
| `ct::Stack<T>`  | adaptor LIFO sobre Vector, overhead zero; x1.2-3.3 vs std::stack (default deque), x1.2-2.9 vs vector-backed |
| `ct::Queue<T>`  | adaptor FIFO sobre Deque; x1.2-2.1 vs std::queue; com reserve() e clear() |
| `ct::Array<T,N>`  | array fixo (sizeof == N*sizeof(T), trivial/standard-layout); acesso constexpr, at() com fatal, fill via memset, ==/< via memcmp só onde é seguro |
| `ct::Span<T>` / `ct::StringView`  | vistas sem dono (2 palavras); ligam Vector/Array/String/arrays C sem copiar; `split_once`/`trimmed` parseiam configs sem alocar |
| `ct::Unique<T>` / `Rc<T>` / `Weak<T>`  | RAII sem atomics; `make_rc` faz uma só alocação com o bloco colado ao objeto; copiar x2.6 vs `shared_ptr` |
| `ct::SlotMap<T>` + `Handle<T>`  | handles com geração sobre array denso; handle morto é detetado em vez de dar ponteiro pendurado; iteração x15.8 e lookup x1.9 vs unordered_map |
| `ct::Json`  | RFC 8259 completo sobre String+Vector; parse x2.5-3.4 e lookup x9.7 vs nlohmann; 32 B por valor; 0,14 s de compilação vs 0,93 s |
| `ct::Xml`  | subconjunto pragmático p/ ler/escrever XML tipo Tiled TMX/TSX; sem namespaces/DTD/XPath (fora de escopo, de propósito); parse + dump(indent) |
| `ct::Function<R(Args...)>`  | callback type-erased tipo std::function; SBO de 3 ponteiros sem alocar, só alvos grandes vão ao heap; copiável se o alvo for |
| `ct::Variant<Ts...>`  | união com tag, conjunto fechado de tipos; sem alocar (armazenamento = o maior dos Ts); get/get_if/is/visit |
| `ct::sort`  | radix O(n) p/ números (x2.5-4.5 vs std::sort) + introsort genérico (x1.15) |

Regra  para jogos: **Arena** para o que morre no fim do frame
(`reset()` recicla tudo, memória estabiliza no pico de um frame);
**Pool** para o que nasce/morre a meio do jogo (free individual via free list).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest            # testes
./ct_bench                   # benchmarks vs std
```

## Notas de performance do Deque

- Ring buffer contíguo (capacidade potência de 2, wrap com AND) em vez do
  map-de-blocos da std::deque — sem dupla indireção nem divisões.
- Trade-off deliberado: crescer **invalida ponteiros/iteradores** (a std::deque
  garante estabilidade nas pontas). Usa `reserve()` se precisares.
- Sem insert/erase no meio — é uma fila/janela, não um vector.
- Para loops quentes, `first_span()`/`second_span()` dão os (até) dois segmentos
  lineares — iterar por span vectoriza (x2.6-3 vs range-for da std::deque).

## Notas de performance do Vector

- Tipos trivialmente copiáveis crescem com `realloc` (pode crescer in-place) e
  relocalizam com `memcpy` → 5–9x mais rápido que `std::vector` em `push_back`
  sem reserve.
- `resize(n)` em tipos triviais **não inicializa** os elementos novos — usa
  `resize(n, T())` se precisares de zeros.
- Não usar `noinline` no caminho de crescimento: deixa o GCC manter `cap_` em
  registo no loop quente (lição aprendida ao ler o assembly).

## Notas do ct::Json

Valor de 32 bytes (`String` inline com SSO de 23 evita o malloc na maioria das chaves
e strings curtas; arrays e objetos são um ponteiro para um `Vector`). Sem exceções:
input malformado devolve `Json::Error` com mensagem, linha e coluna; usar o tipo errado
(pedir `items()` a um número) é `fatal()`.

- Objetos guardam a **ordem de inserção** (`Vector` de membros + procura linear) — o
  round-trip devolve o ficheiro pela mesma ordem, ao contrário do `std::map` do nlohmann,
  que reordena as chaves. Chaves repetidas ficam ambas; `find()` dá a primeira.
- Parse com limite de profundidade (`kMaxDepth`, 200) — um ficheiro com `[[[[[...` de
  fora não estoura o stack.
- Números: inteiros ficam `Int`/`Uint` (int64/uint64, sem perder precisão em ids grandes)
  e só passam a `double` se não couberem. `1.0` sai como `"1.0"` para o tipo sobreviver
  ao round-trip. Um número fora do alcance do double (`1e999`) é **erro de parse**, não
  infinito — aceitá-lo faria o dump escrever `null` e perder o valor em silêncio.
- **Não validamos UTF-8**: bytes inválidos (ficheiros antigos em latin-1) passam tal e
  qual, onde o nlohmann rejeita o ficheiro inteiro. Deliberado — não se perde um ficheiro
  de assets por causa de um `ç` mal codificado.
- dtoa próprio: gera os dígitos por escalamento em `long double` e **verifica o
  round-trip** antes de aceitar, com fallback para `snprintf`. Testado em 3,2 M de
  doubles aleatórios sem uma única falha. Independente do locale (o `strtod`/`snprintf`
  do sistema partem-se com `LC_NUMERIC` de vírgula decimal; este não).

Validação: **397 ficheiros JSON reais** (7,1 MB, 166 740 nós) comparados nó a nó com o
nlohmann — zero diferenças de valor, zero round-trips falhados. Mais 240 mil inputs de
lixo e mutações de ficheiros reais sob ASan/UBSan sem um crash nem um leak, e 3,2 M de
doubles aleatórios sem uma falha de round-trip.

Medido contra o `nlohmann::json` 3.11.3 em cenas reais do Radion (`ct_bench_json`):

| | ct | nlohmann | |
|---|---|---|---|
| parse (31 KB) | 15,9 ms | 53,3 ms | **x3.34** |
| walk completo | 3,6 ms | 9,7 ms | **x2.66** |
| lookup por chave | 0,49 ms | 4,77 ms | **x9.74** |
| construir do zero | 8,5 ms | 17,2 ms | **x2.02** |
| dump compacto | 13,2 ms | 10,2 ms | x0.77 |
| dump(4) | 15,7 ms | 13,1 ms | x0.84 |

O dump ainda perde porque o nlohmann traz o grisu2 para formatar doubles (o nosso
serializador em si é ~1,9x mais rápido; a diferença toda está no dtoa). Em compilação:
`ct/json.hpp` são 16 298 linhas pré-processadas e 0,14 s por TU, contra 98 564 linhas
e 0,93 s do `nlohmann/json.hpp`.

## Notas do ct::Xml

Nó = sempre um elemento (nome, atributos, filhos, texto). Sem exceções, como o `Json`:
`Xml::Error` para input malformado, defaults nos getters para o resto.

- Escopo deliberadamente pequeno — feito para mapas do [Tiled](https://www.mapeditor.org/)
  (`.tmx`/`.tsx`), não um parser XML genérico. Fora de escopo: namespaces, DTD/entidades
  externas (só as 5 predefinidas — `&amp; &lt; &gt; &quot; &apos;` — mais referências
  numéricas `&#N;`/`&#xN;`), XPath/XSLT/validação de schema. Um `<!DOCTYPE>` é tolerado e
  ignorado, nunca processado.
- `CDATA` entra cru (sem decode de entidades) — é o caminho comum para dados de tile em
  base64/csv. Texto e CDATA concatenam-se em `text()`; espaço em branco puro entre filhos
  (só indentação) é descartado automaticamente, mas um elemento sem filhos preserva-o tal
  e qual (`text_trimmed()` apara as pontas quando isso não importa, como no `<data>`).
- `dump(indent)` seguem a mesma convenção do `Json::dump` (`< 0` compacto, `>= 0` um
  elemento por linha); `dump_document()` antepõe a declaração `<?xml ...?>`.
- Mesma técnica do `Json` para o problema de conter-se a si próprio: `children_` é um
  `Vector<Xml>*` alocado à parte (preguiçoso — só no primeiro filho), não um valor direto.
- Validado com 48 testes GTest e 200 mil mutações aleatórias de ficheiros TMX-like sob
  ASan/UBSan, sem crash nem leak.

## Notas do ct::Function e ct::Variant

Dois primitivos genéricos, não específicos de nenhum formato de ficheiro — a diferença
entre eles é a pergunta que respondem:

- **`Function<R(Args...)>`** guarda **qualquer coisa chamável** com essa assinatura
  (função livre, lambda, functor, `std::bind`) atrás de type erasure — conjunto aberto,
  o `Function` não sabe nem quer saber o tipo concreto por baixo.
- **`Variant<Ts...>`** guarda **um valor de um conjunto fechado** de tipos conhecidos em
  compile-time — nunca aloca, o armazenamento é só o maior dos `Ts...` embutido.

```cpp
ct::Function<int(int,int)> f = [](int a, int b) { return a + b; };
f(2, 3); // 5

ct::Variant<int, double, ct::String> v = 42;
v.is<int>();       // true
v = ct::String("x");
v.visit(Printer{}); // dispatch para o operator() certo, por tipo ativo
```

- `Function` copia o alvo por SBO de 3 ponteiros (24 B) — cobre a maioria das lambdas de
  callback ("captura `this` + um par de valores"); só as maiores vão ao heap. Copiar uma
  `Function` exige que o alvo guardado também seja copiável (como o `std::function`).
  Chamar uma `Function` vazia é `fatal()`, não `std::bad_function_call`.
- `Variant` usa recursão sobre `Ts...` para destruir/copiar/mover o alternativo ativo —
  não há vtable por índice, e a lista costuma ter poucos tipos. `get<T>()` errado é
  `fatal()`; `get_if<T>()` devolve `nullptr` para o caminho em que isso é normal.
- Ambos guardam o valor via `reinterpret_cast` de um buffer de bytes — o mesmo truque que
  o `std::function`/`std::variant` da própria std usam por baixo (legal por `basic.life`).

## Notas do ct::SlotMap

O problema que resolve: guardar `Body*` ou `Entity*` e ficar com um ponteiro pendurado
quando o objeto morre. Aqui guarda-se um `Handle<T>` (8 bytes, tipado — não se troca o de
uma entidade pelo de um corpo) e o mapa deteta handles mortos:

```cpp
ct::SlotMap<Body> bodies;
auto h = bodies.insert(Body{...});
bodies.erase(h);
bodies.get(h);          // nullptr, e não lixo — o slot foi reutilizado com nova geração
for (Body &b : bodies.items()) b.integrate(dt);   // denso e contíguo
```

- Os objetos ficam num array **denso** (`erase` tapa o buraco com o último), por isso
  iterar é igual a iterar um `Vector` — nada de saltos de cache como num map de nós.
- A geração é ímpar quando o slot está vivo e par quando está livre, e sobe a cada
  `erase`: um handle antigo nunca volta a bater certo, mesmo depois de o slot ser
  reutilizado. `Handle{}` (geração 0) nunca é válido.
- **Os endereços não são estáveis** — o handle é que é. Não guardes `T*` entre frames,
  e não apagues durante uma passagem por `items()` (recolhe handles, apaga depois).
- `operator[]` é `fatal()` se o handle for inválido; `get()` devolve `nullptr` para o
  caminho em que a morte do objeto é normal.

Com 100 000 corpos de 32 B (`ct_bench_slotmap`):

| | ct::SlotMap | std::unordered_map | |
|---|---|---|---|
| iterar e integrar | 5,7 ms | 90,3 ms | **x15.8** |
| lookup por handle | 10,0 ms | 18,7 ms | **x1.87** |
| apagar metade e repor | 5,4 ms | 27,7 ms | **x5.09** |

Contra um `Vector` a seco, iterar por `items()` custa o mesmo (x1.03 — é literalmente o
mesmo array), e o acesso por handle custa **x0.41** face a indexar um índice cru: duas
leituras dependentes mais a verificação da geração. É esse o preço de não ter ponteiros
pendurados.

## Notas dos ponteiros RAII

```cpp
ct::Unique<Textura> t = ct::make_unique<Textura>("chao.png");  // 8 B, um dono
ct::Rc<Mesh> m = ct::make_rc<Mesh>(...);                       // vários donos
ct::Weak<Mesh> obs = m;                                        // observa sem segurar
if (ct::Rc<Mesh> vivo = obs.lock()) desenhar(*vivo);           // ou vem vazio
```

Contadores **normais, não atómicos** — é single-thread por desenho, como o resto da lib.
Um `Rc` nunca pode atravessar threads (duas cópias em simultâneo corrompem a contagem).

- `make_rc` faz **uma** alocação: `[Ctrl][padding][T]`, com o contador na mesma linha de
  cache do objeto. O `Ctrl` são 16 B (dois `uint32` + um ponteiro de operação), contra os
  24-32 B do bloco da std com vtable de deleter.
- O bloco sabe destruir **o tipo com que foi criado**: `Rc<Base> b = make_rc<Derivada>()`
  corre `~Derivada` mesmo sem destrutor virtual (com `unique_ptr`/`delete` isso é UB).
- `Rc` tem 16 B (ponteiro + bloco) para poder fazer upcast. Daria para 8 B calculando o
  bloco a partir do objeto, mas aí `Rc<Derivada>` → `Rc<Base>` deixava de compilar, e a
  cópia não fica mais rápida por isso — o ganho está no contador, não no tamanho.
- Ciclos de `Rc` continuam a fugir, como em qualquer refcount: o de trás tem de ser `Weak`.

Quando usar o quê, por ordem de preferência: **Arena** (morre no fim do frame) → **Pool**
ou **Unique** (um dono) → **SlotMap + Handle** (entidades/corpos, com deteção de morte) →
**Rc/Weak** só para posse mesmo partilhada com tempo de vida dinâmico (assets referenciados
por N entidades). Um `Handle` de 8 B faz o trabalho de um `weak_ptr` sem bloco de controlo
nem refcount, e ainda itera denso.

| 2 M de operações | ct | std | |
|---|---|---|---|
| copiar (refcount) | 20,0 ms | 52,4 ms | **x2.62** |
| make + destruir | 23,9 ms | 27,4 ms | x1.14 |
| `Unique` make + destruir | 19,4 ms | 23,9 ms | x1.23 |
