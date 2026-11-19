# Suspeitas

Problemas que a segunda ronda de caca a bugs viu e **nao corrigiu**, ou que nao conseguiu provar. Cada entrada diz
onde esta, o que se tentou e porque ficou. Quando se resolver uma, apaga-se daqui no mesmo commit que a resolve.

Os numeros de linha sao os da arvore em que isto foi escrito; procure pelo nome da funcao se tiverem andado.

## Provadas, mas nao corrigidas (precisam de uma decisao)

### Json::operator== nao e reflexivo com NaN

- `Json(NAN) == Json(NAN)` da falso e o `dump()` escreve `null`. Um documento com NaN nao e igual a si mesmo. E o que o
  IEEE manda para `double`; nao se mexeu.

## Nao provadas

### O strtod e o strtof da glibc arredondam mal alguns hexadecimais subnormais

- Com mais de 53 bits de mantissa e um resultado subnormal: `0X5EdD31.8F9BE4E6p-1045`, `0XcDc.Bf866f417D4p-1035` e
  `0X98A382a242e7eCp-1078` em double (o `strtod` da ...c, ...e e ...e; o certo e o valor mais um, confirmado com aritmetica exacta
  em Python) e `0x1.8b9ff5p-128` em float (...fe, o certo e ...ff). A `ct::parse_double` e a `parse_float` dao o certo. Quem
  compare a `ct` com o `strtod` nesses hexadecimais tem de contar com isto: `tests/numconv_check.hpp` salta o valor
  nesses casos e compara so o fim do texto.

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
