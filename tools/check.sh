#!/bin/sh
# Tudo o que um revisor precisa, num comando:
#
#   tools/check.sh              todas as etapas
#   tools/check.sh --quick      sem os testes de JsRegex e de Unicode (metade do tempo) nas etapas com sanitizers
#   tools/check.sh --deep       fuzz com muito mais operacoes por semente (CT_FUZZ_ITERS=100000)
#   tools/check.sh nostd asan   so essas etapas
#
# Etapas:
#   nostd    nenhum std:: nem cabecalho C++ novo em include/, tests/ e bench/ (compara com tools/nostd.baseline;
#            tools/check.sh --update-nostd reescreve a baseline: so para tirar usos, nunca para juntar)
#   headers  cada cabecalho compila sozinho com -Wall -Wextra -Wpedantic -Werror (alvo ct_headers)
#   release  testes em Release
#   asan     testes com ASan + UBSan (UBSan fatal, float-cast-overflow incluido) e o ct_torture
#   tsan     os testes que usam threads com TSan (precisa de `setarch -R` neste kernel)
#   parse    fuzz de mutacao dos parsers (Json, Xml, Ini, Soup, Http, Regex, JsRegex, Unicode) com ASan + UBSan
#
# Escreve uma linha por etapa e acaba com codigo diferente de zero se alguma falhar. O log de cada etapa fica em
# $CT_CHECK_DIR/logs. Variaveis:
#   CT_CHECK_DIR     onde ficam as pastas de build (omissao: <repo>/build-check, ja ignorada pelo git)
#   CT_GTEST_SRC     pasta com o codigo do GoogleTest 1.14 (omissao: o CMake faz o download)
#   CT_CHECK_CXX     compilador (omissao: o c++ do sistema)
#   CT_CHECK_JOBS    trabalhos paralelos do build (omissao: nproc)
#   CT_CHECK_NETWORK 1 inclui os testes que falam com servidores TLS reais (omissao: fora)

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR=${CT_CHECK_DIR:-$ROOT/build-check}
JOBS=${CT_CHECK_JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )}
CXX=${CT_CHECK_CXX:-}
LOGS=$DIR/logs
QUICK=0
DEEP=0
STAGES=""

for arg in "$@"; do
    case $arg in
    --quick) QUICK=1 ;;
    --deep) DEEP=1 ;;
    --update-nostd) UPDATE_NOSTD=1 ;;
    -h | --help)
        sed -n '2,/^set -u/p' "$0" | sed '$d'
        exit 0
        ;;
    -*)
        echo "opcao desconhecida: $arg" >&2
        exit 2
        ;;
    *) STAGES="$STAGES $arg" ;;
    esac
done
UPDATE_NOSTD=${UPDATE_NOSTD:-0}
if [ -z "$STAGES" ]; then
    if [ "$UPDATE_NOSTD" = 1 ]; then STAGES="nostd"; else STAGES="nostd headers release asan tsan parse"; fi
fi

mkdir -p "$LOGS"

FAILED=0
SUMMARY=""

now() { date +%s; }

record() { # nome estado detalhe
    line=$(printf '%-8s %-5s %s' "$1" "$2" "$3")
    echo "$line"
    SUMMARY="$SUMMARY
$line"
    [ "$2" = PASS ] || FAILED=1
}

cmake_gtest_arg() {
    if [ -n "${CT_GTEST_SRC:-}" ]; then
        echo "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$CT_GTEST_SRC"
    fi
}

cxx_arg() {
    if [ -n "$CXX" ]; then
        echo "-DCMAKE_CXX_COMPILER=$CXX"
    fi
}

# configure <pasta> <tipo> <flags de compilacao> <flags de ligacao> [emulador]
# O emulador e o que o CMake poe antes do ct_tests quando o corre para descobrir os testes (gtest_discover_tests):
# com TSan o executavel morre com "unexpected memory mapping" se o ASLR cair mal, e `setarch -R` evita-o.
configure() {
    cmake -S "$ROOT" -B "$1" -DCMAKE_BUILD_TYPE="$2" -DCT_BUILD_BENCH=OFF \
        -DCMAKE_CXX_FLAGS="$3" -DCMAKE_EXE_LINKER_FLAGS="$4" \
        ${5:+-DCMAKE_CROSSCOMPILING_EMULATOR="$5"} \
        $(cmake_gtest_arg) $(cxx_arg)
}

# Filtro do GoogleTest: positivos, depois "-" e os negativos (separados por ":").
make_filter() { # positivos negativos...
    pos=$1
    shift
    neg=""
    for n in "$@"; do
        [ -n "$n" ] && neg="$neg${neg:+:}$n"
    done
    if [ -n "$neg" ]; then echo "$pos-$neg"; else echo "$pos"; fi
}

network_exclusion() {
    if [ "${CT_CHECK_NETWORK:-0}" = 1 ]; then echo ""; else echo "*Network*"; fi
}

# Nomes dos suites que ficam no ficheiros de testes dados, para o filtro do TSan.
suites_of() {
    for f in "$@"; do
        sed -n -E 's/^TEST(_F|_P)?\(([A-Za-z0-9_]+),.*/\2/p' "$ROOT/tests/$f"
    done | sort -u | tr '\n' ':' | sed 's/:$//'
}

# ---------------------------------------------------------------------------------------------------------------------

stage_nostd() {
    cur=$DIR/nostd.current
    base=$ROOT/tools/nostd.baseline
    (
        cd "$ROOT" || exit 1
        for f in $(find include tests bench -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' \) | sort); do
            # cabecalhos que nao sao de C (sem .h) e cada std::nome; um par (ficheiro, simbolo) por linha
            grep -hoE '^[[:space:]]*#[[:space:]]*include[[:space:]]*<[A-Za-z_]+>' "$f" 2>/dev/null |
                sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*/include /' | sort -u |
                sed "s|^|$f	|"
            grep -hoE 'std::[A-Za-z_][A-Za-z_0-9]*' "$f" 2>/dev/null | sort -u | sed "s|^|$f	|"
        done
    ) >"$cur"
    if [ "$UPDATE_NOSTD" = 1 ]; then
        cp "$cur" "$base"
        record nostd PASS "baseline reescrita: $(wc -l <"$base") pares"
        return
    fi
    if [ ! -f "$base" ]; then
        record nostd FAIL "falta tools/nostd.baseline (tools/check.sh --update-nostd)"
        return
    fi
    sort "$base" >"$DIR/nostd.base.sorted"
    sort "$cur" >"$DIR/nostd.cur.sorted"
    new=$(comm -13 "$DIR/nostd.base.sorted" "$DIR/nostd.cur.sorted")
    gone=$(comm -23 "$DIR/nostd.base.sorted" "$DIR/nostd.cur.sorted" | wc -l)
    if [ -n "$new" ]; then
        printf '%s\n' "$new" | sed 's/^/    novo uso de std: /' >&2
        record nostd FAIL "$(printf '%s\n' "$new" | wc -l) usos novos de std (ficheiro, simbolo) fora da baseline"
    else
        record nostd PASS "sem usos novos de std ($(wc -l <"$cur") pares na baseline, $gone ja saiu: pode reduzir-se)"
    fi
}

stage_headers() {
    d=$DIR/headers
    t0=$(now)
    if { configure "$d" Release "-Wall -Wextra -Wpedantic -Werror" "" &&
        cmake --build "$d" --target ct_headers -j"$JOBS"; } >"$LOGS/headers.log" 2>&1; then
        record headers PASS "cada cabecalho compila sozinho ($(($(now) - t0))s)"
    else
        record headers FAIL "ver $LOGS/headers.log"
    fi
}

run_gtest() { # log binario filtro
    "$2" --gtest_filter="$3" >"$1" 2>&1
}

count_tests() { # log
    sed -n -E 's/^\[==========\] ([0-9]+) tests? from .* ran\..*/\1/p' "$1" | tail -1
}

stage_release() {
    d=$DIR/release
    t0=$(now)
    if ! { configure "$d" Release "" "" && cmake --build "$d" --target ct_tests -j"$JOBS"; } >"$LOGS/release-build.log" 2>&1; then
        record release FAIL "build falhou, ver $LOGS/release-build.log"
        return
    fi
    if (cd "$ROOT" && run_gtest "$LOGS/release.log" "$d/ct_tests" "$(make_filter '*' "$(network_exclusion)")"); then
        record release PASS "$(count_tests "$LOGS/release.log") testes ($(($(now) - t0))s)"
    else
        record release FAIL "ver $LOGS/release.log: $(grep -c '^\[  FAILED  \]' "$LOGS/release.log") falhas"
    fi
}

sanitizer_filter() {
    if [ "$QUICK" = 1 ]; then
        make_filter '*' "$(network_exclusion)" 'JsRegex*' 'Unicode*'
    else
        make_filter '*' "$(network_exclusion)"
    fi
}

stage_asan() {
    d=$DIR/asan
    t0=$(now)
    san="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
    if ! { configure "$d" Debug "$san" "-fsanitize=address,undefined" &&
        cmake --build "$d" --target ct_tests ct_torture ct_parsefuzz -j"$JOBS"; } >"$LOGS/asan-build.log" 2>&1; then
        record asan FAIL "build falhou, ver $LOGS/asan-build.log"
        return
    fi
    iters=""
    [ "$DEEP" = 1 ] && iters="CT_FUZZ_ITERS=100000"
    if (cd "$ROOT" && env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 $iters \
        "$d/ct_tests" --gtest_filter="$(sanitizer_filter)" >"$LOGS/asan.log" 2>&1); then
        ok=1
    else
        ok=0
    fi
    tort=ok
    for seed in 1 17 12345; do
        env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$d/ct_torture" "$seed" 10000 >>"$LOGS/asan-torture.log" 2>&1 || tort=FALHOU
    done
    if [ $ok = 1 ] && [ "$tort" = ok ]; then
        record asan PASS "$(count_tests "$LOGS/asan.log") testes + torture ($(($(now) - t0))s)"
    else
        record asan FAIL "testes: $([ $ok = 1 ] && echo ok || echo FALHARAM, ver $LOGS/asan.log); torture: $tort"
    fi
}

stage_tsan() {
    d=$DIR/tsan
    t0=$(now)
    emulator=""
    command -v setarch >/dev/null 2>&1 && emulator="setarch;-R"
    if ! { configure "$d" Debug "-fsanitize=thread -O1 -g -fno-omit-frame-pointer" "-fsanitize=thread" "$emulator" &&
        cmake --build "$d" --target ct_tests -j"$JOBS"; } >"$LOGS/tsan-build.log" 2>&1; then
        record tsan FAIL "build falhou, ver $LOGS/tsan-build.log"
        return
    fi
    suites=$(suites_of test_thread.cpp test_hardening.cpp test_socket.cpp test_http.cpp test_tls.cpp test_stream.cpp)
    filter=$(make_filter "$(printf '%s' "$suites" | sed 's/:/.*:/g; s/$/.*/')" "$(network_exclusion)")
    wrap=""
    command -v setarch >/dev/null 2>&1 && wrap="setarch -R"
    if (cd "$ROOT" && env TSAN_OPTIONS=halt_on_error=1 $wrap "$d/ct_tests" --gtest_filter="$filter" >"$LOGS/tsan.log" 2>&1); then
        record tsan PASS "$(count_tests "$LOGS/tsan.log") testes com threads ($(($(now) - t0))s)"
    else
        record tsan FAIL "ver $LOGS/tsan.log"
    fi
}

stage_parse() {
    d=$DIR/asan
    t0=$(now)
    if [ ! -x "$d/ct_parsefuzz" ]; then
        record parse FAIL "falta $d/ct_parsefuzz (corra a etapa asan primeiro)"
        return
    fi
    n=3000
    [ "$DEEP" = 1 ] && n=100000
    if env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$d/ct_parsefuzz" all "$n" 1 >"$LOGS/parse.log" 2>&1; then
        record parse PASS "8 parsers x $n entradas estragadas ($(($(now) - t0))s)"
    else
        record parse FAIL "ver $LOGS/parse.log"
    fi
}

for s in $STAGES; do
    case $s in
    nostd) stage_nostd ;;
    headers) stage_headers ;;
    release) stage_release ;;
    asan) stage_asan ;;
    tsan) stage_tsan ;;
    parse) stage_parse ;;
    *)
        echo "etapa desconhecida: $s" >&2
        exit 2
        ;;
    esac
done

echo "----"
if [ $FAILED = 0 ]; then
    echo "check: tudo passou"
else
    echo "check: FALHOU"
fi
exit $FAILED
