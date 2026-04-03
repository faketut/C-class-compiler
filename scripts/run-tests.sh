#!/usr/bin/env bash
# wlp4-compiler — regression suite for the corpus under this repo’s test/ directory.
# Usage: bash scripts/run_tests.sh   (from any working directory)
#
# Requires executables in $REPO_ROOT/bin only: wlp4scan, wlp4parse, wlp4type, wlp4gen,
#   linkasm, arm64emu, wlp4c, linker-striparmcom.
# Optional env: WLP4GEN_CC, ALLOC_COM, PRINT_COM
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Test corpus: only *.wlp4 / *.wlp4ti and test/expected/ (see README).
TEST_DIR="$REPO_ROOT/test"
if [[ ! -d "$TEST_DIR" ]]; then
  echo "run_tests.sh: missing test directory: $TEST_DIR" >&2
  exit 1
fi

cd "$TEST_DIR" || exit 1

PATH="$REPO_ROOT/bin:$PATH"

# Resolve tools only from repo bin/ (not PATH elsewhere).
_local_tool() {
  local p="$REPO_ROOT/bin/$1"
  [[ -x "$p" ]] && printf '%s' "$p" || printf '%s' ""
}
LINKASM="$(_local_tool linkasm)"
ARM64EMU="$(_local_tool arm64emu)"
WLP4C="$(_local_tool wlp4c)"
LINKER_STRIP="$(_local_tool linker-striparmcom)"

[[ -n "$LINKASM" && -n "$ARM64EMU" && -n "$WLP4C" ]] || {
  echo "run_tests.sh: install linkasm, arm64emu, wlp4c in $REPO_ROOT/bin" >&2
  exit 1
}

ALLOC_COM="${ALLOC_COM:-$REPO_ROOT/lib/alloc.com}"
PRINT_COM="${PRINT_COM:-$REPO_ROOT/lib/print.com}"

if [[ -n "${WLP4GEN_CC:-}" ]]; then
  :
elif [[ -f "$REPO_ROOT/src/wlp4gen.cc" ]]; then
  WLP4GEN_CC="$REPO_ROOT/src/wlp4gen.cc"
else
  echo "run_tests.sh: set WLP4GEN_CC or add src/wlp4gen.cc" >&2
  exit 1
fi

strip_emulator() {
  awk '/^Program exited normally\./ {exit} {print}'
}

# Compare x0 only (reference compiler may use a different frame layout).
compare_x0() {
  grep '^x0:' "$1" | head -1 >"$TEST_DIR/our.x0"
  grep '^x0:' "$2" | head -1 >"$TEST_DIR/ref.x0"
  diff -q "$TEST_DIR/ref.x0" "$TEST_DIR/our.x0" >/dev/null 2>&1
}

wlp4ti_pipeline() {
  wlp4scan <"$1" | wlp4parse | wlp4type
}

run_compare_ref() {
  local name="$1" src="$2" emu_mode="${3:-}"
  shift 3
  local emu_args=("$@")
  echo "  --- $name"
  if ! wlp4ti_pipeline "$TEST_DIR/$src" >"$TEST_DIR/got.wlp4ti" 2>/dev/null; then
    echo "  SKIP $name (scan/parse/type failed)"
    return 2
  fi
  if ! ./wlp4gen <"$TEST_DIR/got.wlp4ti" | "$LINKASM" >"$TEST_DIR/our.com" 2>/dev/null; then
    echo "  FAIL $name (wlp4gen/linkasm)"
    return 1
  fi
  [[ -s "$TEST_DIR/our.com" ]] || { echo "  FAIL $name (empty .com)"; return 1; }
  local emu_cmd=("$ARM64EMU")
  [[ -n "$emu_mode" ]] && emu_cmd+=("$emu_mode")
  emu_cmd+=("$TEST_DIR/our.com" "${emu_args[@]}")
  "${emu_cmd[@]}" 2>&1 | strip_emulator >"$TEST_DIR/our.out" || true
  if ! "$WLP4C" <"$TEST_DIR/$src" >"$TEST_DIR/ref.bin" 2>/dev/null; then
    echo "  SKIP $name (wlp4c)"
    return 2
  fi
  local ref_cmd=("$ARM64EMU")
  [[ -n "$emu_mode" ]] && ref_cmd+=("$emu_mode")
  ref_cmd+=("$TEST_DIR/ref.bin" "${emu_args[@]}")
  "${ref_cmd[@]}" 2>&1 | strip_emulator >"$TEST_DIR/ref.out" || true
  if compare_x0 "$TEST_DIR/our.out" "$TEST_DIR/ref.out"; then
    echo "  PASS $name (x0)"
    return 0
  fi
  echo "  FAIL $name (x0)"
  echo "    ref: $(grep '^x0:' "$TEST_DIR/ref.out" | head -1)"
  echo "    our: $(grep '^x0:' "$TEST_DIR/our.out" | head -1)"
  return 1
}

run_heap_link() {
  local name="$1" src="$2"
  shift 2
  local emu_args=("$@")
  echo "  --- $name (heap)"
  [[ -n "$LINKER_STRIP" ]] || { echo "  SKIP $name (no linker-striparmcom)"; return 2; }
  [[ -f "$ALLOC_COM" ]] || { echo "  SKIP $name (no alloc.com)"; return 2; }
  if ! wlp4ti_pipeline "$TEST_DIR/$src" >"$TEST_DIR/got.wlp4ti" 2>/dev/null; then
    echo "  SKIP $name (pipeline)"
    return 2
  fi
  ./wlp4gen <"$TEST_DIR/got.wlp4ti" | "$LINKASM" >"$TEST_DIR/our.com"
  local link=("$LINKER_STRIP" "$TEST_DIR/our.com")
  if grep -qi 'println' "$TEST_DIR/$src"; then
    [[ -f "$PRINT_COM" ]] || { echo "  SKIP $name (needs print.com for println)"; return 2; }
    link+=("$PRINT_COM")
  fi
  link+=("$ALLOC_COM")
  "${link[@]}" >"$TEST_DIR/linked.com"
  "$ARM64EMU" "$TEST_DIR/linked.com" "${emu_args[@]}" 2>&1 | strip_emulator >"$TEST_DIR/our.out" || true
  if ! "$WLP4C" -c <"$TEST_DIR/$src" >"$TEST_DIR/ref_heap.com" 2>/dev/null; then
    echo "  SKIP $name (wlp4c)"
    return 2
  fi
  local rlink=("$LINKER_STRIP" "$TEST_DIR/ref_heap.com")
  if grep -qi 'println' "$TEST_DIR/$src" && [[ -f "$PRINT_COM" ]]; then
    rlink+=("$PRINT_COM")
  fi
  rlink+=("$ALLOC_COM")
  if ! "${rlink[@]}" >"$TEST_DIR/ref_linked.com" 2>/dev/null; then
    echo "  SKIP $name (cannot link ref)"
    return 2
  fi
  "$ARM64EMU" "$TEST_DIR/ref_linked.com" "${emu_args[@]}" 2>&1 | strip_emulator >"$TEST_DIR/ref.out" || true
  if compare_x0 "$TEST_DIR/our.out" "$TEST_DIR/ref.out"; then
    echo "  PASS $name (x0)"
    return 0
  fi
  echo "  FAIL $name (x0)"
  return 1
}

PASS=0
FAIL=0
SKIP=0
bump() {
  case "$1" in
    0) PASS=$((PASS + 1)) ;;
    1) FAIL=$((FAIL + 1)) ;;
    2) SKIP=$((SKIP + 1)) ;;
  esac
}

echo "Building wlp4gen..."
g++ -std=c++17 -O2 -Wall -Wextra -o wlp4gen "$WLP4GEN_CC"

echo ""
echo "=== Procedures ==="
run_compare_ref proc procedures/proc.wlp4 "" 0 0; bump $?
run_compare_ref no_param procedures/no_param_proc.wlp4 "" 0 0; bump $?
run_compare_ref recursive procedures/recursive.wlp4 "" 5 0; bump $?
run_compare_ref two_arg procedures/two_arg_proc.wlp4 "" 3 4; bump $?

echo ""
echo "=== Pointers ==="
run_compare_ref wain_ptr pointers/wain_ptr.wlp4 "-a" 241 241 241; bump $?
run_compare_ref addr_of pointers/addr_of.wlp4 "" 0 0; bump $?
run_compare_ref deref_write pointers/deref_write.wlp4 "" 7 0; bump $?

echo ""
echo "=== Pointer arithmetic ==="
run_compare_ref ptr_ptr_sub arithmetic/ptr_ptr_sub.wlp4 "-a" 0 1 2 3 4; bump $?
run_compare_ref ptr_arith_sub arithmetic/ptr_arith_sub.wlp4 "-a" 0 1 2 3 4; bump $?

echo ""
echo "=== Pointer comparisons ==="
run_compare_ref while_ptr comparisons/while_ptr.wlp4 "-a" 0 1 2 3 4; bump $?
run_compare_ref if_cmp comparisons/if_ptr_cmp.wlp4 "-a" 0 1 2 3 4; bump $?
run_compare_ref while_scan comparisons/while_scan.wlp4 "-a" 1 2 3 4 5; bump $?

echo ""
echo "=== Heap ==="
run_heap_link alloc_basic heap/alloc_basic.wlp4 10 20; bump $?

echo ""
echo "=== Summary ==="
echo "Passed: $PASS  Failed: $FAIL  Skipped: $SKIP"
if [[ "$FAIL" -gt 0 ]]; then
  exit 1
fi
exit 0
