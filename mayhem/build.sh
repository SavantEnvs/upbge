#!/usr/bin/env bash
#
# mayhem/build.sh — builds the UPBGE fuzz harnesses and the selftest oracle.
#
# UPBGE (a Blender fork) needs a ~hundred-library dependency stack to build as a whole, and none of it
# is needed to reach the code the harnesses target. So this compiles, straight from upstream's own
# sources, the self-contained subset the targets use: the PLY importer, the pylike expression
# evaluator, the DNA/SDNA decoder, and the few blenlib/guardedalloc/clog files under them. Every
# object is built with -ffunction-sections and linked with --gc-sections, which discards the
# functions that reference the rest of Blender's kernel (never called from these entry points).
# Offline: the only external libraries are apt's libfmt/libzstd (installed in the Dockerfile).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# Relax only UBSan's alignment check: the importer and DNA code read byte-packed buffers through
# float*/double*/int32_t* casts (benign on x86) and each one would otherwise be a fatal defect.
SANITIZER_FLAGS="$SANITIZER_FLAGS -fno-sanitize=alignment"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
B="$SRC/build/mayhem"
rm -rf "$B"
mkdir -p "$B/fuzz" "$B/standalone" "$B/test"

INC=""
for d in source/blender/blentranslation intern/profile intern/libc_compat \
         source/blender/io/ply/importer source/blender/io/ply source/blender/io/ply/intern \
         source/blender/io/common source/blender/blenlib source/blender/blenkernel \
         source/blender/makesdna source/blender/makesrna source/blender/imbuf \
         source/blender/depsgraph source/blender/blenloader source/blender/functions \
         source/blender/gpu source/blender/windowmanager intern/guardedalloc intern/clog \
         intern/atomic intern/utfconv intern/string extern/fast_float source/blender \
         intern/mantaflow/extern source/blender/animrig source/blender/nodes source/blender/bmesh; do
  INC="$INC -I$SRC/$d"
done

CXXFLAGS_COMMON="-std=c++20 -funsigned-char -w -O1 -ffunction-sections -DWITH_DNA_GHASH $INC"

LIB_SOURCES="
intern/clog/clog.cc
intern/guardedalloc/intern/mallocn.cc
intern/guardedalloc/intern/mallocn_lockfree_impl.cc
intern/guardedalloc/intern/mallocn_guarded_impl.cc
intern/guardedalloc/intern/memory_usage.cc
intern/guardedalloc/intern/leak_detector.cc
source/blender/blenlib/intern/BLI_assert.cc
source/blender/blenlib/intern/endian_switch.cc
source/blender/blenlib/intern/fileops_c.cc
source/blender/blenlib/intern/path_utils.cc
source/blender/blenlib/intern/system.cc
source/blender/blenlib/intern/expr_pylike_eval.cc
source/blender/makesdna/intern/dna_genfile.cc
source/blender/makesdna/intern/dna_utils.cc
source/blender/io/ply/importer/ply_import.cc
source/blender/io/ply/importer/ply_import_data.cc
source/blender/io/ply/importer/ply_import_buffer.cc
mayhem/stubs.cc
"
HARNESSES="fuzz_ply fuzz_expr_pylike fuzz_sdna"

# compile_set <objdir> <flags...>: compile LIB_SOURCES, MAYHEM_JOBS at a time; fail if any compile fails.
compile_set() {
  local dir="$1"; shift
  local src pid failed=0 pids=()
  for src in $LIB_SOURCES; do
    while [ "${#pids[@]}" -ge "$MAYHEM_JOBS" ]; do
      wait "${pids[0]}" || failed=1
      pids=("${pids[@]:1}")
    done
    "$CXX" "$@" -c "$SRC/$src" -o "$dir/$(basename "$src").o" &
    pids+=("$!")
  done
  for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
  return "$failed"
}

# 1) Fuzz objects: sanitized + SanCov-instrumented (fuzzer-no-link, even when $SANITIZER_FLAGS is empty).
FUZZ_FLAGS="$CXXFLAGS_COMMON $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS"
compile_set "$B/fuzz" $FUZZ_FLAGS
for h in $HARNESSES; do
  "$CXX" $FUZZ_FLAGS -c "$SRC/mayhem/harnesses/$h.cc" -o "$B/fuzz/harness_$h.o"
done
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/fuzz/lsan_off.o"

# 2) Standalone (run-once, no libFuzzer runtime) objects: same flags without SanCov.
STANDALONE_FLAGS="$CXXFLAGS_COMMON $SANITIZER_FLAGS $DEBUG_FLAGS"
compile_set "$B/standalone" $STANDALONE_FLAGS
for h in $HARNESSES; do
  "$CXX" $STANDALONE_FLAGS -c "$SRC/mayhem/harnesses/$h.cc" -o "$B/standalone/harness_$h.o"
done
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/standalone/lsan_off.o"
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone/standalone_main.o"

LINK_COMMON="-fuse-ld=lld -Wl,--gc-sections -lfmt"
for h in $HARNESSES; do
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
      $(ls "$B"/fuzz/*.o | grep -v "/harness_") "$B/fuzz/harness_$h.o" $LINK_COMMON -o "$SRC/$h"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS \
      $(ls "$B"/standalone/*.o | grep -v "/harness_") "$B/standalone/harness_$h.o" $LINK_COMMON \
      -o "$SRC/$h-standalone"
done

# 3) The selftest oracle: the same Blender sources with NORMAL flags (no sanitizers, no SanCov).
TEST_FLAGS="$CXXFLAGS_COMMON $COVERAGE_FLAGS"
compile_set "$B/test" $TEST_FLAGS
"$CXX" $TEST_FLAGS -c "$SRC/mayhem/selftest/selftest.cc" -o "$B/test/selftest_main.o"
"$CXX" $COVERAGE_FLAGS $(ls "$B"/test/*.o) $LINK_COMMON -o "$SRC/upbge_selftest"
