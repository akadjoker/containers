#!/bin/bash
# Compara a ct com a biblioteca standard e com outras (abseil, boost, ankerl, fmt), ver docs/COMPARACAO.md.
#
#   tools/bench_compare.sh fetch      descarrega as bibliotecas que faltam para $CT_COMPARE_THIRD (nada e instalado no sistema)
#   tools/bench_compare.sh build      compila bench/compare (opcao CMake CT_BUILD_COMPARE, desligada por omissao)
#   tools/bench_compare.sh time       tempo por operacao, alocacoes, bytes e RSS (uma linha por celula, ficheiro $OUT/time.tsv)
#   tools/bench_compare.sh ir         instrucoes por operacao com callgrind (so se houver valgrind; ficheiro $OUT/ir.tsv)
#   tools/bench_compare.sh layout     tamanho do contentor vazio, fator de carga, bytes por elemento, crescimento
#   tools/bench_compare.sh compile    tempo e memoria do compilador e tamanho do objeto, por biblioteca
#   tools/bench_compare.sh regex      ct::Regex contra std::regex e RE2 (tempo de CPU, ficheiro $OUT/regex.txt; RE2 so se houver <re2/re2.h>)
#   tools/bench_compare.sh tables     junta os ficheiros em tabelas markdown ($OUT/tables.md)
#   tools/bench_compare.sh all        build time ir layout compile tables
#
# Variaveis: CT_COMPARE_THIRD (pasta das bibliotecas, omissao <repo>/build-compare/third), CT_COMPARE_OUT (resultados,
# omissao <repo>/build-compare/out), CT_COMPARE_QUICK=1 (so N=1e3 e 1e5), CT_COMPARE_OPT (omissao -O2),
# CT_COMPARE_BUILD (pasta de build, omissao <out>/../build), CT_COMPARE_ONLY (so uma familia na etapa time), CT_COMPARE_IMPLS (so estas implementacoes, ex. "ct ctree cflat"), CT_COMPARE_VALGRIND (caminho do valgrind; omissao o do sistema, se houver; VALGRIND_LIB tambem se for preciso).
# Todos os comandos correm num scope do systemd com MemoryMax=4G e a compilacao usa -j3: a maquina e partilhada.
#
# Nota: esta pasta (bench/compare) inclui a biblioteca standard e as outras de proposito; e a excecao declarada ao
# "sem std" e a etapa nostd de tools/check.sh nao a olha.

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
THIRD=${CT_COMPARE_THIRD:-$ROOT/build-compare/third}
OUT=${CT_COMPARE_OUT:-$ROOT/build-compare/out}
BUILD=${CT_COMPARE_BUILD:-$OUT/../build}
OPT=${CT_COMPARE_OPT:--O2}
QUICK=${CT_COMPARE_QUICK:-0}
VG=${CT_COMPARE_VALGRIND:-$(command -v valgrind 2>/dev/null || true)}
if [ -z "$VG" ] && [ -x "$THIRD/root-valgrind/usr/bin/valgrind.bin" ]; then
    VG=$THIRD/root-valgrind/usr/bin/valgrind.bin
    export VALGRIND_LIB=$THIRD/root-valgrind/usr/libexec/valgrind
fi
mkdir -p "$OUT"

cap() { systemd-run --user --scope --quiet -p MemoryMax=4G -p MemorySwapMax=0 -- "$@"; }

if [ "$QUICK" = 1 ]; then NS="1000 100000"; else NS="1000 100000 10000000"; fi

# ---------------------------------------------------------------------------------------------------------------------
stage_fetch() {
    mkdir -p "$THIRD"
    for p in libabsl-dev libboost1.83-dev libre2-dev libfmt-dev valgrind; do
        d=$THIRD/deb-$p
        if [ ! -d "$THIRD/root-$p" ]; then
            mkdir -p "$d" "$THIRD/root-$p"
            (cd "$d" && apt-get download "$p") || { echo "falhou $p" >&2; continue; }
            dpkg -x "$d"/*.deb "$THIRD/root-$p"
        fi
    done
    if [ ! -f "$THIRD/ankerl/ankerl/unordered_dense.h" ]; then
        mkdir -p "$THIRD/ankerl/ankerl"
        curl -sSfL -o "$THIRD/ankerl/ankerl/unordered_dense.h" \
            https://raw.githubusercontent.com/martinus/unordered_dense/v4.5.0/include/ankerl/unordered_dense.h
    fi
    echo "bibliotecas em $THIRD"
}

compare_includes() {
    inc=""
    for d in "$THIRD"/root-*/usr/include "$THIRD"/ankerl; do [ -d "$d" ] && inc="$inc;$d"; done
    echo "${inc#;}"
}

compare_libs() {
    libs="-Wl,--no-as-needed"
    for l in hash city low_level_hash raw_hash_set hashtablez_sampler base throw_delegate int128 spinlock_wait \
        raw_logging_internal synchronization malloc_internal stacktrace symbolize debugging_internal demangle_internal \
        time time_zone exponential_biased graphcycles_internal strings strings_internal; do
        f=$(ls /usr/lib/x86_64-linux-gnu/libabsl_$l.so.* 2>/dev/null | head -1)
        [ -n "$f" ] && libs="$libs;$f"
    done
    f=$(ls /usr/lib/x86_64-linux-gnu/libre2.so.* 2>/dev/null | head -1)
    [ -n "$f" ] && libs="$libs;$f"
    echo "$libs"
}

stage_build() {
    cmake -S "$ROOT" -B "$BUILD" -DCT_BUILD_COMPARE=ON -DCT_BUILD_TESTS=OFF -DCT_BUILD_BENCH=OFF \
        -DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="$OPT -DNDEBUG" \
        "-DCT_COMPARE_INCLUDES=$(compare_includes)" "-DCT_COMPARE_LIBS=$(compare_libs)" >"$OUT/cmake.log" 2>&1 || {
        echo "cmake falhou, ver $OUT/cmake.log" >&2
        return 1
    }
    cap cmake --build "$BUILD" -j3 >"$OUT/build.log" 2>&1 || { echo "build falhou, ver $OUT/build.log" >&2; return 1; }
    echo "compilado em $BUILD"
}

# ---------------------------------------------------------------------------------------------------------------------
# Cada linha de matriz: familia|impls|cenarios|tipos|Ns   (Ns vazio = a lista por omissao)
VEC_IMPLS="ct std boost"
MAP_IMPLS="ct std absl boost ankerl"

matrix() {
    local fam=$1
    case $fam in
    vector)
        for t in int p16 mo b256; do
            local ns="1000 100000" big="10000000"
            [ "$QUICK" = 1 ] && big=""
            [ "$t" = b256 ] && big="1000000"
            [ "$t" = b256 ] && [ "$QUICK" = 1 ] && big=""
            for s in build build_reserved iterate index copy move clear insert_mid resize_fill resize_overwrite; do
                for n in $ns; do for i in $VEC_IMPLS; do echo "cmp_vector $i $s $t $n"; done; done
            done
            # de 1e6 para cima so os cenarios que contam (insert no meio e O(N) por operacao e mede a memoria)
            for s in build build_reserved iterate copy; do
                for n in $big; do for i in $VEC_IMPLS; do echo "cmp_vector $i $s $t $n"; done; done
            done
        done ;;
    map)
        for t in int-int int-p16 int-mo int-b256 s8-int s20-int s40-int; do
            local ns="1000 100000" big=""
            case $t in int-int) big="10000000" ;; int-p16 | int-mo | s8-int | s40-int) big="1000000" ;; esac
            [ "$QUICK" = 1 ] && big=""
            for s in build hit miss erase iterate copy churn; do
                for n in $ns; do for i in $MAP_IMPLS; do echo "cmp_map $i $s $t $n"; done; done
            done
            for s in build hit erase; do
                for n in $big; do for i in $MAP_IMPLS; do echo "cmp_map $i $s $t $n"; done; done
            done
        done ;;
    string)
        for n in 100000 1000000; do
            for i in ct std; do
                for t in len8 len16 len24 len40; do echo "cmp_string $i create $t $n"; echo "cmp_string $i copy $t $((n / 10))"; done
                echo "cmp_string $i eq len40 $n"; echo "cmp_string $i lt len40 $n"; echo "cmp_string $i concat len30 $n"
            done
        done
        for n in $NS; do
            [ "$n" = 10000000 ] && n=10000000
            for i in ct std; do echo "cmp_string $i append_char char $n"; echo "cmp_string $i append_piece piece16 $((n / 4))"; done
        done
        for n in 1000000 10000000; do for i in ct std; do echo "cmp_string $i find needle8 $n"; done; done
        for i in ct std charconv libc fmt; do
            for t in full small; do echo "cmp_string $i int2s $t 1000000"; echo "cmp_string $i s2int $t 1000000"; done
            echo "cmp_string $i dbl2s d 1000000"; echo "cmp_string $i s2f32 f 1000000"
        done
        for i in ct charconv libc fmt; do for t in d wide; do echo "cmp_string $i d2short $t 1000000"; done; done
        for i in ct std charconv libc; do for t in f long wide; do echo "cmp_string $i s2f64 $t 1000000"; done; done ;;
    sort)
        for p in random sorted reversed dups; do
            for n in $NS; do
                for i in "ct sort" "ct sort_nocmp" "std sort" "boost sort" "boost spread" "ct stable" "std stable"; do echo "cmp_sort $i int.$p $n"; done
            done
            for n in $(echo $NS | sed 's/10000000/10000000/'); do
                for i in "ct sort" "std sort" "boost sort" "ct stable" "std stable"; do echo "cmp_sort $i p16.$p $n"; done
            done
            for n in 1000 100000; do
                for i in "ct sort" "std sort" "boost sort" "ct stable" "std stable"; do echo "cmp_sort $i b256.$p $n"; echo "cmp_sort $i str.$p $n"; done
            done
        done ;;
    misc)
        for t in int p16 b256; do
            for n in 1000 100000; do
                for i in ct std boost; do for s in push_back push_front iterate index drain fifo; do echo "cmp_misc deque $i $s $t $n"; done; done
            done
        done
        for i in ct std boost; do for s in push_back push_front iterate drain fifo; do echo "cmp_misc deque $i $s int 1000000"; done; done
        for t in int s20; do
            for n in 1000 100000 1000000; do
                [ "$n" = 1000000 ] && [ "$t" = s20 ] && continue
                # inserir e apagar a meio de um vetor ordenado de Strings e O(N^2) moves: 1e5 leva minutos
                for i in ctree cflat stdmap btree bflat; do
                    [ "$n" = 1000000 ] && { [ "$i" = cflat ] || [ "$i" = bflat ]; } && continue
                    [ "$n" = 100000 ] && [ "$t" = s20 ] && { [ "$i" = cflat ] || [ "$i" = bflat ]; } && continue
                    for s in build hit miss erase iterate; do echo "cmp_misc ordered $i $s $t $n"; done
                done
            done
        done
        for n in 1000 100000; do for i in ct std; do echo "cmp_misc pq $i pushpop int $n"; for s in construct_small construct_big call copy; do echo "cmp_misc function $i $s x $n"; done; done; done ;;
    extra)
        for n in 100000 1000000; do
            for i in ct std; do
                for s in construct visit copy_assign; do echo "cmp_extra variant $i $s $n"; done
                for s in alloc64 create_nodes; do echo "cmp_extra arena $i $s $n"; done
                echo "cmp_extra pool $i cycle $n"; echo "cmp_extra ring $i fifo $n"
            done
            for i in ct raw; do for s in sum sub; do echo "cmp_extra span $i $s $n"; done; done
        done ;;
    esac
}

# Os argumentos de cmp_misc levam a familia primeiro: o formato das linhas e <exe> <args...>, o harness sabe os seus.
run_family() { # familia ficheiro
    local fam=$1 file=$2
    local total
    total=$(matrix "$fam" | wc -l)
    local k=0
    matrix "$fam" | while read -r exe args; do
        k=$((k + 1))
        if [ -n "${CT_COMPARE_IMPLS:-}" ]; then
            keep=0
            for w in $CT_COMPARE_IMPLS; do case " $args " in *" $w "*) keep=1 ;; esac; done
            [ $keep = 1 ] || continue
        fi
        # cmp_sort: "ct sort int.random N" -> impl algoritmo tipo N (ja assim); cmp_misc: familia impl cenario tipo N
        cap "$BUILD/$exe" $args 2>>"$OUT/errors.log" | sed "s/^/$fam\t/" >>"$file" || echo "FALHOU: $exe $args" >>"$OUT/errors.log"
    done
}

stage_time() {
    : >"$OUT/time.tsv"
    for f in vector map string sort misc extra; do
        [ -n "${CT_COMPARE_ONLY:-}" ] && [ "$CT_COMPARE_ONLY" != "$f" ] && continue
        echo "tempo: $f" >&2
        run_family "$f" "$OUT/time.tsv"
    done
    echo "$OUT/time.tsv: $(wc -l <"$OUT/time.tsv") linhas"
}

# Instrucoes: N = 1e5 (1e4 para os lentos), uma execucao de cada, so a regiao medida conta.
ir_one() { # exe args...
    local exe=$1
    shift
    local cgout=$OUT/cg.out
    rm -f "$cgout"
    local line
    line=$(CMP_CALLGRIND=1 cap "$VG" --tool=callgrind --collect-atstart=no --callgrind-out-file="$cgout" "$BUILD/$exe" "$@" 2>/dev/null) || return 1
    local tot
    tot=$(grep -m1 '^totals:' "$cgout" | awk '{print $2}')
    [ -z "$tot" ] && return 1
    [ -z "$line" ] && return 0
    printf '%s\t%s\t%s\n' "${exe#cmp_}" "$line" "$tot"
}

ir_matrix() {
    for n in 100000; do
        for t in int p16 mo; do for s in build build_reserved iterate copy insert_mid resize_fill resize_overwrite; do for i in $VEC_IMPLS; do echo "cmp_vector $i $s $t $n"; done; done; done
        for t in int-int s8-int s20-int s40-int int-b256; do for s in build hit miss erase iterate copy; do for i in $MAP_IMPLS; do echo "cmp_map $i $s $t $n"; done; done; done
        for i in ct std; do
            for t in len8 len16 len24 len40; do echo "cmp_string $i create $t $n"; done
            echo "cmp_string $i append_char char $n"; echo "cmp_string $i eq len40 $n"; echo "cmp_string $i concat len30 $n"
        done
        for i in ct std charconv libc fmt; do echo "cmp_string $i int2s full $n"; echo "cmp_string $i s2int full $n"; echo "cmp_string $i dbl2s d $n"; echo "cmp_string $i s2f32 f $n"; done
        for i in ct charconv libc fmt; do for t in d wide; do echo "cmp_string $i d2short $t $n"; done; done
        for i in ct std charconv libc; do for t in f long wide; do echo "cmp_string $i s2f64 $t $n"; done; done
        for p in random sorted reversed dups; do
            for i in "ct sort" "ct sort_nocmp" "std sort" "boost sort" "boost spread" "ct stable" "std stable"; do echo "cmp_sort $i int.$p $n"; done
            for i in "ct sort" "std sort" "boost sort" "ct stable" "std stable"; do echo "cmp_sort $i p16.$p $n"; done
        done
        for t in int p16; do for i in ct std boost; do for s in push_back push_front iterate drain fifo; do echo "cmp_misc deque $i $s $t $n"; done; done; done
        for i in ctree cflat stdmap btree bflat; do for s in build hit erase; do echo "cmp_misc ordered $i $s int 10000"; done; done
        for i in ct std; do echo "cmp_misc pq $i pushpop int $n"; for s in construct_small construct_big call copy; do echo "cmp_misc function $i $s x $n"; done; done
        for i in ct std; do
            for s in construct visit copy_assign; do echo "cmp_extra variant $i $s $n"; done
            for s in alloc64 create_nodes; do echo "cmp_extra arena $i $s $n"; done
            echo "cmp_extra pool $i cycle $n"; echo "cmp_extra ring $i fifo $n"
        done
        for i in ct raw; do for s in sum sub; do echo "cmp_extra span $i $s $n"; done; done
    done
}

stage_ir() {
    if [ -z "$VG" ]; then echo "sem valgrind: etapa ir saltada (CT_COMPARE_VALGRIND ou ./bench_compare.sh fetch)" >&2; return 0; fi
    : >"$OUT/ir.tsv"
    ir_matrix | while read -r exe args; do
        ir_one "$exe" $args >>"$OUT/ir.tsv" || echo "FALHOU ir: $exe $args" >>"$OUT/errors.log"
    done
    echo "$OUT/ir.tsv: $(wc -l <"$OUT/ir.tsv") linhas"
}

stage_layout() {
    : >"$OUT/layout.txt"
    for t in int-int int-p16 int-b256 s8-int s20-int s40-int; do
        for n in 1000 100000 1000000; do for i in $MAP_IMPLS; do cap "$BUILD/cmp_map" $i layout $t $n >>"$OUT/layout.txt"; done; done
    done
    for t in int p16 b256; do for i in $VEC_IMPLS; do cap "$BUILD/cmp_vector" $i growth $t 1000000 >>"$OUT/layout.txt"; done; done
    for t in int s20; do for i in ctree cflat stdmap btree bflat; do cap "$BUILD/cmp_misc" ordered $i layout $t 100000 >>"$OUT/layout.txt"; done; done
    echo "$OUT/layout.txt: $(wc -l <"$OUT/layout.txt") linhas"
}

stage_compile() {
    : >"$OUT/compile.tsv"
    inc=$(compare_includes | tr ';' '\n' | sed 's/^/-I/' | tr '\n' ' ')
    for lib in ct std absl boost; do
        for kind in syntax obj; do
            flag="-fsyntax-only"
            [ "$kind" = obj ] && flag="-c -o $OUT/compile_$lib.o"
            /usr/bin/time -f "%e\t%M" -o "$OUT/time.tmp" systemd-run --user --scope --quiet -p MemoryMax=4G -p MemorySwapMax=0 -- \
                c++ -std=c++17 $OPT -DCMP_LIB_$(echo $lib | tr a-z A-Z)=1 -I"$ROOT/include" $inc $flag "$ROOT/bench/compare/compile_unit.cpp" 2>>"$OUT/errors.log"
            size=0
            [ "$kind" = obj ] && size=$(stat -c %s "$OUT/compile_$lib.o")
            printf '%s\t%s\t%s\t%s\n' "$lib" "$kind" "$(cat "$OUT/time.tmp")" "$size" >>"$OUT/compile.tsv"
        done
    done
    echo "$OUT/compile.tsv"
}

stage_regex() {
    cap "$BUILD/cmp_regex" >"$OUT/regex.txt" 2>>"$OUT/errors.log" && echo "$OUT/regex.txt"
}

stage_tables() { python3 -I "$ROOT/tools/bench_tables.py" "$OUT" >"$OUT/tables.md" && echo "$OUT/tables.md"; }

stages=${*:-all}
[ "$stages" = all ] && stages="build time ir layout compile regex tables"
for s in $stages; do
    case $s in
    fetch) stage_fetch ;;
    build) stage_build ;;
    time) stage_time ;;
    ir) stage_ir ;;
    layout) stage_layout ;;
    compile) stage_compile ;;
    regex) stage_regex ;;
    tables) stage_tables ;;
    *) echo "etapa desconhecida: $s" >&2; exit 2 ;;
    esac
done
