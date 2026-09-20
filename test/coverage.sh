#!/usr/bin/env bash
#
# mayara_pi - build the unit tests instrumented, run them, and report line
# coverage for the units under test.
#
# Driven by `make coverage` from the repository root; usable on its own as
#   test/coverage.sh <build-dir> [--html]
#
# Handles both coverage toolchains: Clang's source-based instrumentation
# (llvm-profdata + llvm-cov, what macOS gives you) and GCC's gcov, via gcovr.
set -euo pipefail

BUILD_DIR="${1:-build-coverage}"
WANT_HTML=""
[ "${2:-}" = "--html" ] && WANT_HTML=1

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cmake -B "$BUILD_DIR" -S test -DMAYARA_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
cmake --build "$BUILD_DIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

BIN="$BUILD_DIR/mayara_tests"
[ -x "$BIN" ] || { echo "coverage: $BIN was not built" >&2; exit 1; }

# The sources we actually want a number for. Coverage of doctest itself, or of
# the tests, tells us nothing.
SOURCES=(
  "$ROOT/src/RadarMessage.cpp"
  "$ROOT/src/RadarState.cpp"
  "$ROOT/src/RadarPalette.cpp"
  "$ROOT/src/RadarControls.cpp"
)

# Written by test/CMakeLists.txt when MAYARA_COVERAGE is on.
COMPILER_ID="$(cat "$BUILD_DIR/compiler-id.txt" 2>/dev/null || true)"

# `xcrun llvm-cov` on macOS, plain llvm-cov elsewhere. Both must be the pair
# that matches the compiler, or the profile format will not be understood.
llvm() {
  local tool="$1"; shift
  if command -v "xcrun" >/dev/null 2>&1 && xcrun -f "$tool" >/dev/null 2>&1; then
    xcrun "$tool" "$@"
  elif command -v "$tool" >/dev/null 2>&1; then
    "$tool" "$@"
  else
    echo "coverage: $tool not found (install LLVM, or the Xcode command line" \
         "tools on macOS)" >&2
    exit 1
  fi
}

case "$COMPILER_ID" in
  *Clang*)
    PROFRAW="$BUILD_DIR/mayara_tests.profraw"
    PROFDATA="$BUILD_DIR/mayara_tests.profdata"
    rm -f "$PROFRAW" "$PROFDATA"
    LLVM_PROFILE_FILE="$PROFRAW" "$BIN" --force-colors=false
    llvm llvm-profdata merge -sparse "$PROFRAW" -o "$PROFDATA"
    echo
    llvm llvm-cov report "$BIN" -instr-profile="$PROFDATA" "${SOURCES[@]}"
    if [ -n "$WANT_HTML" ]; then
      OUT="$BUILD_DIR/html"
      llvm llvm-cov show "$BIN" -instr-profile="$PROFDATA" \
           -format=html -output-dir="$OUT" "${SOURCES[@]}"
      echo
      echo "HTML report: $OUT/index.html"
    fi
    ;;
  GNU)
    # gcov *accumulates* into existing .gcda files rather than replacing them,
    # so a second run would still count the lines the first one hit -- a test
    # deleted since would go on showing as covered.
    find "$BUILD_DIR" -name '*.gcda' -delete
    "$BIN" --force-colors=false
    if ! command -v gcovr >/dev/null 2>&1; then
      echo "coverage: gcovr not found (pip install gcovr)" >&2
      exit 1
    fi
    echo
    GCOVR_ARGS=(--root "$ROOT" --filter "$ROOT/src/" --print-summary)
    if [ -n "$WANT_HTML" ]; then
      mkdir -p "$BUILD_DIR/html"
      GCOVR_ARGS+=(--html-details "$BUILD_DIR/html/index.html")
    fi
    gcovr "${GCOVR_ARGS[@]}" "$BUILD_DIR"
    [ -n "$WANT_HTML" ] && echo "HTML report: $BUILD_DIR/html/index.html"
    ;;
  *)
    echo "coverage: don't know how to report for compiler '$COMPILER_ID'" >&2
    exit 1
    ;;
esac
