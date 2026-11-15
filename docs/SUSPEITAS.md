# Suspeitas

Problemas que a segunda ronda de caca a bugs viu e **nao corrigiu**, ou que nao conseguiu provar. Cada entrada diz
onde esta, o que se tentou e porque ficou. Quando se resolver uma, apaga-se daqui no mesmo commit que a resolve.

Os numeros de linha sao os da arvore em que isto foi escrito; procure pelo nome da funcao se tiverem andado.

## Provadas, mas nao corrigidas (precisam de uma decisao)

### String::find_last_of(set, pos) trata pos como exclusivo

- `include/ct/string.hpp`, `String::find_last_of`.
- `std::string::find_last_of(set, pos)` procura em `[0, pos]`; esta procura em `[0, pos)`. Num diferencial contra
  `std::string` com 300000 combinacoes aleatorias, `find_last_of` e a unica funcao de `String` ou `StringView` que
  difere, e so com `pos` explicito: `String("abcabc").find_last_of("c", 2)` da `npos` (std: 2),
  `find_last_of("a", 0)` da `npos` (std: 0). Com `pos` por omissao (npos) e igual.
- `tests/test_string.cpp` (`EXPECT_EQ(String("a.b.c").find_last_of(".", 3), 1u)`) fixa o comportamento actual como se
  fosse de proposito, e o codigo dos outros tres projectos pode depender dele. Mudar e alterar comportamento visivel.
  `Fuzz2.StringSets` tambem o fixa.
- Decisao: ou se mantem e se escreve no README que `pos` e exclusivo, ou se alinha com o std e se mudam os dois testes.

### Variant: atribuir a partir de um membro do que o proprio Variant guarda

- `include/ct/variant.hpp`, `operator=(const Variant &)` e `operator=(Variant &&)`.
- Destroem a alternativa actual antes de copiar/mover a origem. Se a origem vive dentro da alternativa actual, e uso
  depois de libertar. O caso natural e percorrer uma lista: com `struct No { Variant<int, Rc<No>> next; };`,
  `cursor = cursor.get<Rc<No>>()->next;` e `cursor = ct::move(cursor.get<Rc<No>>()->next);` dao
  heap-use-after-free sob ASan (e o segundo da lixo em Release).
- O `std::variant` do libstdc++ faz o mesmo (destroi e depois constroi), por isso nao e uma divergencia do std; o
  `Json` e o `Xml`, que sao arvores, foram corrigidos nesta ronda.
- Corrige-se copiando/movendo a origem para um Variant local antes de destruir, so quando a alternativa actual tem
  destrutor (as triviais nao podem ser donas de nada). Foi escrito, testado (a lista acima, copia e move, mais um
  `Unique<No>`) e medido com um grupo `variant` de `bench/bench_more.cpp`, 9 rondas a alternar, minimos: atribuir
  escalares igual (15.0 ms contra 15.0), atribuir Strings por copia **+5% a +9%** (40.2 contra 43.8), atribuir por move
  com Strings **+99%** (80.4 contra 160.2: o move passa a ser dois). Como o dono nao aceita nada mais lento, ficou de
  fora. A correccao e pequena: um `replace_with(Variant &)` privado (destroi, constroi por move a partir do local) e uma
  `VariantOps::trivially_destructible(idx)`.

### Json::operator== nao e reflexivo com NaN

- `Json(NAN) == Json(NAN)` da falso e o `dump()` escreve `null`. Um documento com NaN nao e igual a si mesmo. E o que o
  IEEE manda para `double`; nao se mexeu.

### String::to_int e a conversao silenciosa

- `include/ct/string.hpp`, `to_int`: `static_cast<int>(std::strtol(...))`. `String("3000000000").to_int()` da
  -1294967296 (o long cabe, o int nao) sem erro nenhum, e `to_int` e `to_float` nao tem como dizer que a string nao era
  um numero (devolvem 0). Nao ha canal de erro para mudar sem mudar a assinatura; o `Ini::get_int` e o
  `TextReader::read_int` recusam o fora do intervalo.

## Nao provadas

### Json: o caminho lento de json_append_double nao e exercitado

- `include/ct/json.hpp`, `json_append_double`, tudo o que vem depois de `#if defined(CT_JSON_FAST_DTOA)`
  (snprintf com %.15g, %.16g, %.17g e a leitura de volta).
- Neste Linux o caminho rapido (long double de 64 bits de mantissa) serve todos os valores, por isso a cobertura nunca
  entra no caminho lento, que e o que corre no MSVC e no Apple arm64 (long double igual a double). Compilando uma copia
  de `json.hpp` com `CT_JSON_FAST_DTOA` desligado e escrevendo e lendo de volta 1000000 de doubles aleatorios (padroes
  de bits com expoente aleatorio, denormais incluidos) nao houve nenhuma diferenca de bits. Nao ha teste no repo
  porque duas definicoes diferentes da mesma funcao inline no mesmo executavel sao uma violacao da ODR: precisava de um
  executavel a parte compilado com uma macro que nao existe.
- Tambem nunca corre `json_strtod_locale` com um numero de mais de 63 caracteres nos testes de antes; um diferencial
  contra `strtod` com 1000000 de numeros de ate 120 digitos, com expoente e sinal, nao deu nenhuma diferenca.

### Json aceita bytes que nao sao UTF-8

- `parse_string_raw` copia os bytes a partir de 0x20 sem validar UTF-8 (rejeita surrogates soltos nos escapes `\u`).
  Parece de proposito (a ida e volta de bytes quaisquer funciona, o fuzz de ida e volta usa-o); fica aqui para que
  conste.

### BinaryReader::string/bytes deixam o prefixo lido quando falham

- `include/ct/binary.hpp`, `sized`: le por blocos que dobram (para nao alocar o que um prefixo mentiroso pede) e, se
  falhar a meio, volta ao tamanho do ultimo bloco bom e devolve false. `out` fica com os bytes ja lidos. Nao ha erro,
  so que `out` nao fica vazio. Nao se mexeu.

### FileStream em modo ReadWrite sem seek entre ler e escrever

- `include/ct/stream.hpp`, `FileStream::read` e `write`. O C exige um `fseek` ou `fflush` entre uma leitura e uma
  escrita no mesmo `FILE` aberto em "r+b"; o glibc perdoa, outros nao. Nao se reproduziu nada aqui.

### Regex: retrocesso exponencial

- `include/ct/regex.hpp`. O `Regex` tem memoizacao que o torna linear em padroes como `(a+)+$` e `(a|aa)+$` (medido:
  n=2000 e n=3000 em menos de 1 ms), mas a memoizacao desliga-se quando o programa tem repeticoes contadas (`{3}`),
  lookaround, retroreferencias ou grupos atomicos, e nem `(a*)*$` ela apanha: n=22 demora 0.8 s, n=24 3.2 s, n=26
  12.8 s (o `re` do Python: 0.47 s, 7.6 s para n=22 e 26). `(a|b|)*` repetido N vezes seguido de `c` num texto
  `abab...` de 60 caracteres cresce com n elevado a N: N=3 0.26 s, N=4 7.0 s (Python 0.15 s e 4.0 s). E a complexidade
  do retrocesso e e igual a do Python, nao um bug; fica aqui porque o `ct_parsefuzz` apanhou um destes e quem compila
  padroes de utilizadores pode ficar preso. O `JsRegex` tem `set_limits(max_steps, max_stack)`; o `Regex` nao tem limite
  de passos.

## Metodo

- A cobertura por cabecalho (`llvm-cov`) nao conta funcoes de templates que nenhum teste instancia: uma funcao nunca
  chamada nao aparece como "por cobrir", simplesmente nao aparece.
- A lista de funcoes e linhas por cobrir que guiou a ronda foi tirada de `clang++ -fprofile-instr-generate
  -fcoverage-mapping` com `llvm-cov export -format=lcov`.
- Corrigidas e com teste nesta ronda, para nao as repetir aqui: `Vector::assign(n, v[i])`, atribuir por move um filho
  ao `Json` e ao `Xml`, `Atomic` (overflow com sinal e `fetch_add` de ponteiros), `parallel_for` com chunk enorme,
  `Function` com ponteiro nulo, `BinaryWriter::string` com mais de 4 GiB, memcmp/memcpy de nulo com tamanho 0,
  referencias numericas do Soup, `read_int` com overflow, `read_all` com um erro antigo, `sort` de Deque e de iteradores
  inversos, numeros de virgula flutuante em locales com virgula.
