#!/bin/sh
# Cobertura dos testes por cabecalho, com clang (-fprofile-instr-generate -fcoverage-mapping) e llvm-cov:
#
#   tools/coverage.sh              constroi, corre ct_tests (sem os testes de rede) e escreve a tabela
#   tools/coverage.sh --functions  mais a lista das funcoes que nenhum teste chama, por cabecalho
#   tools/coverage.sh --lines core mais as linhas por correr (so cabecalhos cujo nome contem "core"; ex.: vector, json)
#
# Escreve em $CT_COV_DIR (omissao: <repo>/build-cov, ja ignorada pelo git): report.txt (a tabela completa),
# functions.txt (funcoes por chamar) e coverage.lcov. Variaveis: CT_GTEST_SRC, CT_CHECK_JOBS como em tools/check.sh;
# CT_COV_CXX (omissao: clang++), CT_COV_PROFDATA e CT_COV_COV (llvm-profdata e llvm-cov, com a versao do clang se for
# preciso, p.ex. llvm-cov-18).
#
# Nota: uma funcao de template que nenhum teste instancia nao aparece em lado nenhum, nem como "por chamar".

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR=${CT_COV_DIR:-$ROOT/build-cov}
JOBS=${CT_CHECK_JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )}
CXX=${CT_COV_CXX:-clang++}
PROFDATA=${CT_COV_PROFDATA:-llvm-profdata}
LLVMCOV=${CT_COV_COV:-llvm-cov}
FUNCTIONS=0
LINES=""

while [ $# -gt 0 ]; do
    case $1 in
    --functions) FUNCTIONS=1 ;;
    --lines)
        LINES=${2:-}
        shift
        ;;
    -h | --help)
        sed -n '2,/^set -u/p' "$0" | sed '$d'
        exit 0
        ;;
    *)
        echo "opcao desconhecida: $1" >&2
        exit 2
        ;;
    esac
    shift
done

gtest_arg=""
[ -n "${CT_GTEST_SRC:-}" ] && gtest_arg="-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$CT_GTEST_SRC"

mkdir -p "$DIR"
cmake -S "$ROOT" -B "$DIR" -DCMAKE_BUILD_TYPE=Debug -DCT_BUILD_BENCH=OFF -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -O0" $gtest_arg >"$DIR/configure.log" 2>&1 || {
    echo "configure falhou, ver $DIR/configure.log" >&2
    exit 1
}
cmake --build "$DIR" --target ct_tests -j"$JOBS" >"$DIR/build.log" 2>&1 || {
    echo "build falhou, ver $DIR/build.log" >&2
    exit 1
}

rm -f "$DIR"/*.profraw
(cd "$ROOT" && LLVM_PROFILE_FILE="$DIR/cov-%p.profraw" "$DIR/ct_tests" --gtest_filter='-*Network*' >"$DIR/run.log" 2>&1) ||
    echo "aviso: o ct_tests falhou, ver $DIR/run.log (a cobertura e a do que correu)" >&2

"$PROFDATA" merge -o "$DIR/cov.profdata" "$DIR"/*.profraw
IGNORE='(_deps|/usr/|/tests/|googletest|gtest)'

"$LLVMCOV" report "$DIR/ct_tests" -instr-profile="$DIR/cov.profdata" -ignore-filename-regex="$IGNORE" 2>/dev/null |
    sed "s|$ROOT/include/ct/||" >"$DIR/report.txt"
echo "cobertura por cabecalho (Funcoes e Ramos: % cobertos; o resto em $DIR/report.txt)"
awk 'NR == 1 { next } /^-+$/ { next } NF >= 13 && $1 !~ /^Filename/ {
    printf "%-28s funcoes %7s  linhas %7s  ramos %7s\n", $1, $7, $10, $13
}' "$DIR/report.txt"

"$LLVMCOV" export "$DIR/ct_tests" -instr-profile="$DIR/cov.profdata" -format=lcov -ignore-filename-regex="$IGNORE" \
    >"$DIR/coverage.lcov" 2>/dev/null

# funcoes por chamar: FN:linha,nome / FNDA:vezes,nome. Cada instanciacao de um template e um nome, por isso agrupa-se
# por (ficheiro, linha) e so conta como por chamar a funcao que nenhuma instanciacao chamou, como faz o llvm-cov report.
awk -v root="$ROOT/include/ct/" '
    /^SF:/ { f = substr($0, 4); sub(root, "", f); next }
    /^FN:/ { split(substr($0, 4), a, ","); file[a[2]] = f; line[a[2]] = a[1]; next }
    /^FNDA:/ { split(substr($0, 6), a, ","); calls[a[2]] += a[1]; next }
    END {
        for (n in file) { k = file[n] SUBSEP line[n]; total[k] += calls[n]; rep[k] = n }
        for (k in total) if (total[k] == 0) { split(k, p, SUBSEP); printf "%s\t%s\t%s\n", p[1], p[2], rep[k] }
    }
' "$DIR/coverage.lcov" | sort -t "$(printf '\t')" -k1,1 -k2,2n >"$DIR/functions.raw"
if command -v c++filt >/dev/null 2>&1; then
    while IFS="$(printf '\t')" read -r f l n; do
        printf '%s:%s  %s\n' "$f" "$l" "$(printf '%s' "$n" | c++filt)"
    done <"$DIR/functions.raw" >"$DIR/functions.txt"
else
    awk -F '\t' '{ printf "%s:%s  %s\n", $1, $2, $3 }' "$DIR/functions.raw" >"$DIR/functions.txt"
fi
echo "funcoes por chamar: $(wc -l <"$DIR/functions.txt") (lista em $DIR/functions.txt)"
[ "$FUNCTIONS" = 1 ] && cat "$DIR/functions.txt"

if [ -n "$LINES" ]; then
    awk -v root="$ROOT/include/ct/" -v pat="$LINES" '
        /^SF:/ { f = substr($0, 4); sub(root, "", f); show = (index(f, pat) > 0); next }
        /^DA:/ && show { split(substr($0, 4), a, ","); if (a[2] == 0) printf "%s:%s\n", f, a[1] }
    ' "$DIR/coverage.lcov" | sort -u
fi
