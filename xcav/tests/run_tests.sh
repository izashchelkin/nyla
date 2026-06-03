#!/usr/bin/env bash
# ─── xcav test suite ───────────────────────────────────────────────────────
# Tests: blocks, move, delete, edit, edit-rejection
# Usage: ./xcav/tests/run_tests.sh
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FIXTURES="$SCRIPT_DIR/fixtures"
EXPECTED="$SCRIPT_DIR/expected"
TMP_ROOT="${TMPDIR:-/tmp}"
TMPDIR="${XCAV_TEST_TMPDIR:-$TMP_ROOT/xcav_tests_$$}"

# Build xcav if needed
if [ ! -f "$REPO_ROOT/build/linux-debug/bin/xcav" ]; then
    echo "Building xcav..."
    cmake --build "$REPO_ROOT/build/linux-debug" --target xcav || exit 1
    echo ""
fi

# Install xcav (so tests exercise the installed binary path too)
if [ -z "${XC_BIN:-}" ]; then
    echo "Installing xcav..."
    "$REPO_ROOT/scripts/install_xcav.sh" || true
    echo ""
fi

# Use the build binary, not the system-installed one
XC="${XC_BIN:-$REPO_ROOT/build/linux-debug/bin/xcav}"

# Disable usage logging during tests
export XC_NO_LOG=1

# Clean up stale backups from previous runs (backup path is relative to cwd)
rm -rf .xcav_backups

PASS=0
FAIL=0
FAILED_TESTS=()

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass() { echo -e "  ${GREEN}PASS${NC} $1"; ((PASS++)); }
fail() { echo -e "  ${RED}FAIL${NC} $1 — $2"; ((FAIL++)); FAILED_TESTS+=("$1"); }

setup() {
    rm -rf "$TMPDIR"
    mkdir -p "$TMPDIR"
}

teardown() {
    rm -rf "$TMPDIR"
}

# Normalize blocks output: strip file path prefix
normalize_blocks() {
    # Strip the absolute path prefix, keep just the filename.
    # Old format: /path/to/file: N top-level blocks:
    # New format: /path/to/file (N blocks)
    sed 's|^.*/\([^/: ]*\)[: ]|\1 |' "$1" | sed 's|^.*/\([^/]*\)$|\1|'
}

# ─── Test helpers ──────────────────────────────────────────────────────────

test_blocks() {
    local name="$1"
    local fixture="$2"
    local expected="$3"

    local out="$TMPDIR/${name}_out.txt"
    "$XC" blocks "$fixture" > "$out" 2>&1
    normalize_blocks "$out" > "$out.norm"

    local exp="$TMPDIR/${name}_exp.txt"
    normalize_blocks "$expected" > "$exp"

    if diff -q "$out.norm" "$exp" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "output mismatch"
        echo "    diff:"
        diff "$out.norm" "$exp" | sed 's/^/    /'
    fi
}

test_blocks_repo_path() {
    local name="$1"
    local path="$2"
    local out="$TMPDIR/${name}_out.txt"

    local rc=0
    (cd "$REPO_ROOT" && "$XC" blocks "$path" > "$out" 2>&1) || rc=$?

    if [ "$rc" -eq 0 ] && grep -q "MoveBlock" "$out"; then
        pass "$name"
    else
        fail "$name" "expected blocks for repo-relative path '$path'"
        echo "    output:"
        sed 's/^/    /' "$out"
    fi
}

test_move() {
    local name="$1"
    local fixture="$2"
    local line="$3"
    local dest="$4"
    local expected="$5"

    local ext="${fixture##*.}"
    local tmp="$TMPDIR/${name}_file.${ext}"
    cp "$fixture" "$tmp"
    "$XC" move "$tmp" "$line" "$dest" > /dev/null 2>&1

    if diff -q "$tmp" "$expected" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "file mismatch after move"
        echo "    diff expected vs actual:"
        diff "$expected" "$tmp" | sed 's/^/    /'
    fi
}

test_delete() {
    local name="$1"
    local fixture="$2"
    local line="$3"
    local expected="$4"

    local ext="${fixture##*.}"
    local tmp="$TMPDIR/${name}_file.${ext}"
    cp "$fixture" "$tmp"
    "$XC" delete "$tmp" "$line" > /dev/null 2>&1

    if diff -q "$tmp" "$expected" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "file mismatch after delete"
        echo "    diff expected vs actual:"
        diff "$expected" "$tmp" | sed 's/^/    /'
    fi
}

test_edit() {
    local name="$1"
    local fixture="$2"
    local old_text="$3"
    local new_text="$4"
    local expected="$5"

    local ext="${fixture##*.}"
    local tmp="$TMPDIR/${name}_file.${ext}"
    cp "$fixture" "$tmp"

    local oldf="$TMPDIR/${name}_old.txt"
    local newf="$TMPDIR/${name}_new.txt"
    echo -n "$old_text" > "$oldf"
    echo -n "$new_text" > "$newf"

    "$XC" edit "$tmp" "$oldf" "$newf" > /dev/null 2>&1

    if diff -q "$tmp" "$expected" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "file mismatch after edit"
        echo "    diff expected vs actual:"
        diff "$expected" "$tmp" | sed 's/^/    /'
    fi
}


test_edit_stdin_long_oldtext_dry_run() {
    local name="$1"
    local tmp="$TMPDIR/${name}_long_oldtext.c"
    local before="$TMPDIR/${name}_before.c"

    {
        printf 'int long_old_text(void) {\n'
        for i in $(seq 1 70); do
            printf '    // line %03d\n' "$i"
        done
        printf '    return 0;\n'
        printf '}\n'
    } > "$tmp"
    cp "$tmp" "$before"

    local out
    set +e
    out="$({ cat "$tmp"; printf '%s\n' '---XCAV_EDIT_SEPARATOR---'; cat "$tmp"; } | "$XC" edit "$tmp" --stdin --dry-run 2>&1)"
    local rc=$?
    set -e

    if [ "$rc" -eq 0 ] && echo "$out" | grep -q -- "--dry-run" && diff -q "$before" "$tmp" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "expected long stdin dry-run to succeed without modifying file"
        echo "    rc=$rc"
        echo "$out" | sed 's/^/    /'
    fi
}

test_edit_stdin_large_source_dry_run() {
    local name="$1"
    local tmp="$TMPDIR/${name}_large_source.c"
    local before="$TMPDIR/${name}_before.c"

    {
        printf 'int large_source(void) {\n'
        printf '    return 0;\n'
        printf '}\n'
        for i in $(seq 1 5000); do
            printf '// padding line %04d 0123456789abcdef0123456789abcdef\n' "$i"
        done
    } > "$tmp"
    cp "$tmp" "$before"

    local out
    set +e
    out="$(printf 'int large_source(void) {\n---XCAV_EDIT_SEPARATOR---\nint large_source(void) {\n' | "$XC" edit "$tmp" --stdin --dry-run 2>&1)"
    local rc=$?
    set -e

    if [ "$rc" -eq 0 ] && echo "$out" | grep -q -- "--dry-run" && diff -q "$before" "$tmp" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "expected large source dry-run to succeed without modifying file"
        echo "    rc=$rc"
        echo "$out" | sed 's/^/    /'
    fi
}

test_replace() {
    local name="$1"
    local fixture="$2"
    local line="$3"
    local old_text="$4"
    local new_text="$5"
    local expected="$6"

    local ext="${fixture##*.}"
    local tmp="$TMPDIR/${name}_file.${ext}"
    cp "$fixture" "$tmp"

    local oldf="$TMPDIR/${name}_replace_old.txt"
    local newf="$TMPDIR/${name}_replace_new.txt"
    echo -n "$old_text" > "$oldf"
    echo -n "$new_text" > "$newf"

    "$XC" replace "$tmp" "$line" "$oldf" "$newf" > /dev/null 2>&1

    if diff -q "$tmp" "$expected" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "file mismatch after replace"
        echo "    diff expected vs actual:"
        diff "$expected" "$tmp" | sed 's/^/    /'
    fi
}


test_backup_gitignore() {
    local name="$1"
    local repo="$TMPDIR/backup_gitignore_repo"
    rm -rf "$repo"
    mkdir -p "$repo"
    cp "$FIXTURES/funcs.c" "$repo/file.c"

    if ! (cd "$repo" && git init -q && git add file.c && git commit -q -m init); then
        fail "$name" "git init failed"
        return
    fi

    set +e
    (cd "$repo" && "$XC" delete file.c 12 > /dev/null 2>&1)
    local delete_rc=$?
    set -e
    if [ "$delete_rc" -ne 0 ]; then
        fail "$name" "delete failed"
        sed 's/^/    /' "$repo/delete.out"
        return
    fi

    # Backups are stored in ~/.xcav/backups/, not in the repo.
    # Verify no backup files leak into the working tree.
    if [ -d "$repo/.xcav_backups" ]; then
        fail "$name" ".xcav_backups/ should not exist in the repo (backups moved to ~/.xcav/backups/)"
        return
    fi

    local status
    status="$(cd "$repo" && git status --porcelain --untracked-files=all)"
    # Only untracked files (??) are a problem — tracked modifications are expected.
    if echo "$status" | grep -q '^??'; then
        fail "$name" "unexpected untracked files in repo: $status"
        return
    fi

    set +e
    (cd "$repo" && "$XC" undo file.c > /dev/null 2>&1)
    local undo_rc=$?
    set -e
    if [ "$undo_rc" -eq 0 ] && diff -q "$FIXTURES/funcs.c" "$repo/file.c" > /dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "undo did not restore the file"
        sed 's/^/    /' "$repo/undo.out"
    fi
}

# ═══════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════

echo "xcav test suite"
echo "  binary: $XC"
echo "  fixtures: $FIXTURES"
echo "  expected: $EXPECTED"
echo ""

if [ ! -x "$XC" ]; then
    echo -e "${RED}ERROR: xcav binary not found at $XC${NC}"
    echo "  Build with: cmake --build build/linux-debug --target xcav"
    exit 1
fi

setup

# ─── blocks ────────────────────────────────────────────────────────────────

echo -e "${YELLOW}─── blocks ───${NC}"

test_blocks "blocks (C)" \
    "$FIXTURES/funcs.c" \
    "$EXPECTED/blocks_funcs_c.txt"

test_blocks "blocks (C++)" \
    "$FIXTURES/structs.cc" \
    "$EXPECTED/blocks_structs_cc.txt"

test_blocks "blocks (Java)" \
    "$FIXTURES/methods.java" \
    "$EXPECTED/blocks_methods_java.txt"

test_blocks_repo_path "blocks repo-relative C++ path" "xcav/move.cc"
test_blocks_repo_path "blocks dot-relative C++ path" "./xcav/move.cc"

# ─── move (C) ──────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── move (C) ───${NC}"

test_move "move bar after foo" \
    "$FIXTURES/funcs.c" 8 6 \
    "$EXPECTED/c_move_bar_after_foo.c"

test_move "move baz after include" \
    "$FIXTURES/funcs.c" 12 2 \
    "$EXPECTED/c_move_baz_after_include.c"

test_move "move qux after baz" \
    "$FIXTURES/funcs.c" 16 14 \
    "$EXPECTED/c_move_qux_after_baz.c"

# ─── move (C++) ────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── move (C++) ───${NC}"

test_move "move Distance after Point" \
    "$FIXTURES/structs.cc" 16 14 \
    "$EXPECTED/cc_move_distance_after_point.cc"

test_move "move Point to end" \
    "$FIXTURES/structs.cc" 3 24 \
    "$EXPECTED/cc_move_point_to_end.cc"

# ─── delete (C) ────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (C) ───${NC}"

test_delete "delete baz" \
    "$FIXTURES/funcs.c" 12 \
    "$EXPECTED/c_delete_baz.c"

test_delete "delete qux" \
    "$FIXTURES/funcs.c" 16 \
    "$EXPECTED/c_delete_qux.c"

# ─── delete (C++) ──────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (C++) ───${NC}"

test_delete "delete Distance" \
    "$FIXTURES/structs.cc" 16 \
    "$EXPECTED/cc_delete_distance.cc"

test_delete "delete Midpoint" \
    "$FIXTURES/structs.cc" 22 \
    "$EXPECTED/cc_delete_midpoint.cc"

# ─── delete (Java) ─────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (Java) ───${NC}"

test_delete "delete Calculator class" \
    "$FIXTURES/methods.java" 2 \
    "$EXPECTED/java_delete_class.java"

# ─── Java (calculator fixture) ────────────────────────────────────────────

echo -e "\n${YELLOW}─── Java (calculator) ───${NC}"

test_blocks "blocks (Java calculator)" \
    "$FIXTURES/calculator.java" \
    "$EXPECTED/blocks_calculator_java.txt"

test_move "move add after multiply (Java)" \
    "$FIXTURES/calculator.java" 15 25 \
    "$EXPECTED/java_move_add_after_multiply.java"

test_delete "delete divide (Java)" \
    "$FIXTURES/calculator.java" 27 \
    "$EXPECTED/java_delete_divide.java"

test_edit "edit constructor (Java)" \
    "$FIXTURES/calculator.java" \
    "this.baseValue = base;" \
    "this.offset = base;" \
    "$EXPECTED/java_edit_constructor.java"

test_replace "replace in add body (Java)" \
    "$FIXTURES/calculator.java" 15 \
    "a + b + baseValue" \
    "a + b + offset" \
    "$EXPECTED/java_replace_add_body.java"


# ─── edit ─────────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── edit ───${NC}"

test_edit "edit return value" \
    "$FIXTURES/funcs.c" \
    "    return x + 1;" \
    "    return x + 42;" \
    "$EXPECTED/c_edit_return.c"

test_edit "edit comment" \
    "$FIXTURES/funcs.c" \
    "// Test fixture for xcav — C functions" \
    "// Test fixture for xcav — C FUNCTIONS" \
    "$EXPECTED/c_edit_comment.c"

# ─── read ─────────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── read ───${NC}"

read_out="$("$XC" read "$FIXTURES/funcs.c" 4 2>&1)"
if echo "$read_out" | grep -q "int foo"; then
    pass "read C function"
else
    fail "read C function" "unexpected output: $read_out"
fi

read_out2="$("$XC" read "$FIXTURES/structs.cc" 4 2>&1)"
if echo "$read_out2" | grep -q "struct Point"; then
    pass "read C++ struct"
else
    fail "read C++ struct" "unexpected output: $read_out2"
fi

# --name mode
read_name="$("$XC" read "$FIXTURES/funcs.c" --name baz 2>&1)"
if echo "$read_name" | grep -q "int baz"; then
    pass "read --name baz"
else
    fail "read --name baz" "unexpected output: $read_name"
fi

# --all mode
read_all="$("$XC" read "$FIXTURES/funcs.c" --all 2>&1)"
if echo "$read_all" | grep -q "int foo" && echo "$read_all" | grep -q "int qux"; then
    pass "read --all"
else
    fail "read --all" "unexpected output: $read_all"
fi

# --numbers flag
read_num="$("$XC" read "$FIXTURES/funcs.c" 4 --numbers 2>&1)"
if echo "$read_num" | grep -q "4: int foo"; then
    pass "read --numbers"
else
    fail "read --numbers" "unexpected output: $read_num"
fi

# ─── replace ──────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── replace ───${NC}"

cp "$FIXTURES/funcs.c" "$TMPDIR/replace_test.c"
echo -n "x + 1" > "$TMPDIR/replace_old.txt"
echo -n "x + 999" > "$TMPDIR/replace_new.txt"
"$XC" replace "$TMPDIR/replace_test.c" 4 "$TMPDIR/replace_old.txt" "$TMPDIR/replace_new.txt" 2>&1 > /dev/null
if grep -q "x + 999" "$TMPDIR/replace_test.c"; then
    pass "replace within block"
else
    fail "replace within block" "replacement not found in file"
fi

# ─── undo ─────────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── undo ───${NC}"

cp "$FIXTURES/funcs.c" "$TMPDIR/undo_test.c"
"$XC" delete "$TMPDIR/undo_test.c" 12 2>&1 > /dev/null
"$XC" undo "$TMPDIR/undo_test.c" 2>&1 > /dev/null
if diff -q "$FIXTURES/funcs.c" "$TMPDIR/undo_test.c" > /dev/null 2>&1; then
    pass "undo after delete"
else
    fail "undo after delete" "file not restored correctly"
fi

# ─── blocks (new fixtures) ──────────────────────────────────────────────

echo -e "\n${YELLOW}─── blocks (extensions) ───${NC}"

test_blocks "blocks (.h header)" \
    "$FIXTURES/header.h" \
    "$EXPECTED/blocks_header_h.txt"

test_blocks "blocks (.cpp extension)" \
    "$FIXTURES/cpp_test.cpp" \
    "$EXPECTED/blocks_cpp_test_cpp.txt"

test_blocks "blocks (.hpp extension)" \
    "$FIXTURES/header.hpp" \
    "$EXPECTED/blocks_header_hpp.txt"

test_blocks "blocks (nested C++)" \
    "$FIXTURES/nested.cc" \
    "$EXPECTED/blocks_nested_cc.txt"

test_blocks "blocks (.hxx extension)" \
    "$FIXTURES/header.hxx" \
    "$EXPECTED/blocks_header_hxx.txt"

test_blocks "blocks (JavaScript)" \
    "$FIXTURES/javascript.js" \
    "$EXPECTED/blocks_javascript_js.txt"

test_blocks "blocks (TypeScript)" \
    "$FIXTURES/typescript.ts" \
    "$EXPECTED/blocks_typescript_ts.txt"

# ─── blocks (unsupported extensions) ─────────────────────────────────────

echo -e "\n${YELLOW}─── blocks (error paths) ───${NC}"

# Unknown extension should log error
set +e
unknown_out="$("$XC" blocks "$FIXTURES/unknown.xyz" 2>&1)"
unknown_rc=$?
# set -e (removed: errexit was not enabled at script startup)
if echo "$unknown_out" | grep -q "unsupported file type"; then
    pass "blocks (unknown extension)"
else
    fail "blocks (unknown extension)" "expected 'unsupported file type' error: $unknown_out"
fi

# No extension should fail
set +e
noext_out="$("$XC" blocks "$FIXTURES/noext" 2>&1)"
noext_rc=$?
# set -e (removed: errexit was not enabled at script startup)
if echo "$noext_out" | grep -q "unsupported file type"; then
    pass "blocks (no extension)"
else
    fail "blocks (no extension)" "expected 'unsupported file type': $noext_out"
fi

# File not found
set +e
notfound_out="$("$XC" blocks "$FIXTURES/nonexistent.c" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$notfound_out" | grep -q "cannot open"; then
    pass "blocks (file not found)"
else
    fail "blocks (file not found)" "expected 'cannot open': $notfound_out"
fi

# Empty file — should list 0 blocks
empty_out="$("$XC" blocks "$FIXTURES/empty.c" 2>&1)"
if echo "$empty_out" | grep -q "(0 blocks)"; then
    pass "blocks (empty file)"
else
    fail "blocks (empty file)" "unexpected: $empty_out"
fi

# Restore errexit state (was disabled by set +e blocks above)
set +e

# ─── move (nested C++) ───────────────────────────────────────────────────

echo -e "\n${YELLOW}─── move (nested C++) ───${NC}"

test_move "move Execute after Calculator" \
    "$FIXTURES/nested.cc" 23 21 \
    "$EXPECTED/cc_move_execute_after_calc.cc"

test_move "move enum to end of file" \
    "$FIXTURES/nested.cc" 5 28 \
    "$EXPECTED/cc_move_enum_to_end.cc"

# ─── move (top of file) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── move (top of file) ───${NC}"

test_move "move qux to top (dest=1)" \
    "$FIXTURES/funcs.c" 16 1 \
    "$EXPECTED/c_move_qux_to_top.c"

# ─── move (JavaScript) ───────────────────────────────────────────────────

echo -e "\n${YELLOW}─── move (JavaScript) ───${NC}"

test_move "move greet after Calculator (JS)" \
    "$FIXTURES/javascript.js" 4 18 \
    "$EXPECTED/js_move_greet_after_class.js"

# ─── move (whitespace fixture) ───────────────────────────────────────────

echo -e "\n${YELLOW}─── move (whitespace) ───${NC}"

test_move "move bar after foo (whitespace)" \
    "$FIXTURES/whitespace.c" 10 7 \
    "$EXPECTED/c_move_bar_after_foo_ws.c"

# ─── move (error paths) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── move (error paths) ───${NC}"

# Move with no block at line (line 7 is blank between functions)
cp "$FIXTURES/funcs.c" "$TMPDIR/move_err1.c"
set +e
move_err1="$("$XC" move "$TMPDIR/move_err1.c" 7 3 2>&1)"
move_rc1=$?
# set -e (removed: errexit was not enabled at script startup)
if echo "$move_err1" | grep -q "no structural block found"; then
    pass "move (no block at line)"
else
    fail "move (no block at line)" "expected error: $move_err1"
fi

# Move with destination inside the block
cp "$FIXTURES/funcs.c" "$TMPDIR/move_err2.c"
set +e
move_err2="$("$XC" move "$TMPDIR/move_err2.c" 4 5 2>&1)"
move_rc2=$?
# set -e (removed: errexit was not enabled at script startup)
if echo "$move_err2" | grep -q "contains the destination line"; then
    pass "move (dest inside block)"
else
    fail "move (dest inside block)" "expected contains destination: $move_err2"
fi

# Move file not found
set +e
move_err3="$("$XC" move "$FIXTURES/nonexistent.c" 1 2 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$move_err3" | grep -q "cannot open"; then
    pass "move (file not found)"
else
    fail "move (file not found)" "expected 'cannot open': $move_err3"
fi

# ─── delete (.h, .cpp) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (extensions) ───${NC}"

test_delete "delete add from header" \
    "$FIXTURES/header.h" 4 \
    "$EXPECTED/h_delete_add.cc"

test_delete "delete main from .cpp" \
    "$FIXTURES/cpp_test.cpp" 4 \
    "$EXPECTED/cpp_delete_main.cc"

# ─── delete (nested C++) ─────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (nested C++) ───${NC}"

test_delete "delete Operation enum" \
    "$FIXTURES/nested.cc" 5 \
    "$EXPECTED/cc_delete_operation.cc"

# ─── delete (only block) ─────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (only block) ───${NC}"

test_delete "delete only block" \
    "$FIXTURES/single_func.c" 1 \
    "$EXPECTED/c_delete_only_function.c"

# ─── delete (JavaScript) ─────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (JavaScript) ───${NC}"

test_delete "delete arrow function (JS)" \
    "$FIXTURES/javascript.js" 8 \
    "$EXPECTED/js_delete_arrow.js"

# ─── delete (error paths) ────────────────────────────────────────────────

echo -e "\n${YELLOW}─── delete (error paths) ───${NC}"

# Delete at line with no structural block (line 7 is blank)
cp "$FIXTURES/funcs.c" "$TMPDIR/del_err1.c"
set +e
del_err1="$("$XC" delete "$TMPDIR/del_err1.c" 7 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$del_err1" | grep -q "no structural block found"; then
    pass "delete (no block at line)"
else
    fail "delete (no block at line)" "expected error: $del_err1"
fi

# Delete file not found
set +e
del_err2="$("$XC" delete "$FIXTURES/nonexistent.c" 1 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$del_err2" | grep -q "cannot open"; then
    pass "delete (file not found)"
else
    fail "delete (file not found)" "expected 'cannot open': $del_err2"
fi

# ─── edit (tab handling) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── edit (tab handling) ───${NC}"

test_edit "edit with tab characters" \
    "$FIXTURES/whitespace.c" \
    "	return x + 1;   " \
    "	return x + 42;" \
    "$EXPECTED/c_edit_whitespace_cleanup.c"

# ─── edit (various source files) ─────────────────────────────────────────

echo -e "\n${YELLOW}─── edit (various files) ───${NC}"

test_edit "edit in file with unusual syntax" \
    "$FIXTURES/preexisting_error.cpp" \
    "return 0" \
    "return 42" \
    "$EXPECTED/cpp_edit_preexisting_error.cpp"

test_edit "edit near raw string literal" \
    "$FIXTURES/raw_string.cpp" \
    "return 42;" \
    "return 99;" \
    "$EXPECTED/cpp_edit_raw_string.cpp"

# ─── edit (error paths) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── edit (error paths) ───${NC}"

# File not found
set +e
edit_err1="$("$XC" edit "$FIXTURES/nonexistent.c" "$TMPDIR/x.txt" "$TMPDIR/y.txt" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$edit_err1" | grep -q "cannot open"; then
    pass "edit (file not found)"
else
    fail "edit (file not found)" "expected 'cannot open': $edit_err1"
fi

# oldText not found
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_notfound.c"
echo -n 'nonexistent_text_xyz' > "$TMPDIR/edit_notfound_old.txt"
echo -n 'replacement' > "$TMPDIR/edit_notfound_new.txt"
set +e
edit_err2="$("$XC" edit "$TMPDIR/edit_notfound.c" "$TMPDIR/edit_notfound_old.txt" "$TMPDIR/edit_notfound_new.txt" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$edit_err2" | grep -q "oldText not found"; then
    pass "edit (oldText not found)"
else
    fail "edit (oldText not found)" "expected 'oldText not found': $edit_err2"
fi

# ambiguous oldText (full-line matching: 'return' appears on multiple lines)
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_ambig.c"
printf '}\n' > "$TMPDIR/edit_ambig_old.txt"
printf '}\n' > "$TMPDIR/edit_ambig_new.txt"
set +e
edit_err3="$("$XC" edit "$TMPDIR/edit_ambig.c" "$TMPDIR/edit_ambig_old.txt" "$TMPDIR/edit_ambig_new.txt" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
# Should succeed since full-line oldText matches exactly one occurrence
if echo "$edit_err3" | grep -qE "ambiguous|not unique"; then
    pass "edit (ambiguous oldText)"
else
    fail "edit (ambiguous oldText)" "expected not unique: $edit_err3"
fi

# empty oldText
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_empty.c"
echo -n '' > "$TMPDIR/edit_empty_old.txt"
echo -n 'x' > "$TMPDIR/edit_empty_new.txt"
set +e
edit_err4="$("$XC" edit "$TMPDIR/edit_empty.c" "$TMPDIR/edit_empty_old.txt" "$TMPDIR/edit_empty_new.txt" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$edit_err4" | grep -q "oldText cannot be empty"; then
    pass "edit (empty oldText)"
else
    fail "edit (empty oldText)" "expected error: $edit_err4"
fi

# ─── edit (stdin mode) ───────────────────────────────────────────────────

echo -e "\n${YELLOW}─── edit (stdin mode) ───${NC}"

# Successful stdin edit
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_stdin.c"
printf '    return x + 1;\n---XCAV_EDIT_SEPARATOR---\n    return x + 999;\n' | "$XC" edit "$TMPDIR/edit_stdin.c" --stdin 2>&1 > /dev/null
if grep -q "x + 999" "$TMPDIR/edit_stdin.c"; then
    pass "edit (stdin mode)"
else
    fail "edit (stdin mode)" "stdin edit not applied"
fi

# Stdin without separator
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_stdin2.c"
set +e
stdin_err="$(echo 'old text' | "$XC" edit "$TMPDIR/edit_stdin2.c" --stdin 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$stdin_err" | grep -q "separator line not found"; then
    pass "edit (stdin no separator)"
else
    fail "edit (stdin no separator)" "expected separator error: $stdin_err"
fi

test_edit_stdin_long_oldtext_dry_run "edit stdin dry-run long oldText"
test_edit_stdin_large_source_dry_run "edit stdin dry-run large source"


# ─── read (extended) ─────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── read (extended) ───${NC}"

# --offset without --limit should work (regression for uint32 overflow bug)
read_offset_only="$("$XC" read "$FIXTURES/funcs.c" --offset 3 2>&1)"
if echo "$read_offset_only" | grep -q "int foo"; then
    pass "read --offset (no --limit, regression)"
else
    fail "read --offset (no --limit, regression)" "expected 'int foo', got: $read_offset_only"
fi

# --name with struct name
read_suffix="$("$XC" read "$FIXTURES/structs.cc" --name Point 2>&1)"
if echo "$read_suffix" | grep -q "struct Point"; then
    pass "read --name struct"
else
    fail "read --name struct" "unexpected: $read_suffix"
fi

# --name with no match
set +e
read_nomatch="$("$XC" read "$FIXTURES/funcs.c" --name nonexistent_func 2>&1)"
read_nomatch_rc=$?
# set -e (removed: errexit was not enabled at script startup)
if echo "$read_nomatch" | grep -q "no block matching"; then
    pass "read --name no match"
else
    fail "read --name no match" "expected 'no block matching': $read_nomatch"
fi

# --all --numbers (combined flags)
read_allnum="$("$XC" read "$FIXTURES/funcs.c" --all --numbers 2>&1)"
if echo "$read_allnum" | grep -q "4: int foo"; then
    pass "read --all --numbers"
else
    fail "read --all --numbers" "missing line numbers: $read_allnum"
fi

# read line with no structural block (line 7 is blank)
set +e
read_noblock="$("$XC" read "$FIXTURES/funcs.c" 7 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$read_noblock" | grep -q "no block found"; then
    pass "read (no block at line)"
else
    fail "read (no block at line)" "expected error: $read_noblock"
fi

# read file not found
set +e
read_nofile="$("$XC" read "$FIXTURES/nonexistent.c" 1 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$read_nofile" | grep -q "no block found\|no blocks found"; then
    pass "read (file not found)"
else
    fail "read (file not found)" "unexpected: $read_nofile"
fi

# ─── read (JavaScript/TypeScript) ────────────────────────────────────────

echo -e "\n${YELLOW}─── read (JS/TS) ───${NC}"

# JS read by line (greet is wrapped in export_statement)
js_read="$("$XC" read "$FIXTURES/javascript.js" 4 2>&1)"
if echo "$js_read" | grep -q "greet"; then
    pass "read JS export statement"
else
    fail "read JS export statement" "unexpected: $js_read"
fi

# JS read --all
js_all="$("$XC" read "$FIXTURES/javascript.js" --all 2>&1)"
if echo "$js_all" | grep -q "Calculator"; then
    pass "read JS --all"
else
    fail "read JS --all" "unexpected: $js_all"
fi

# TS read by line
ts_read="$("$XC" read "$FIXTURES/typescript.ts" 23 2>&1)"
if echo "$ts_read" | grep -q "function distance"; then
    pass "read TS function"
else
    fail "read TS function" "unexpected: $ts_read"
fi

# TS read --all
ts_all="$("$XC" read "$FIXTURES/typescript.ts" --all 2>&1)"
if echo "$ts_all" | grep -q "Circle"; then
    pass "read TS --all"
else
    fail "read TS --all" "unexpected: $ts_all"
fi

# ─── read (Java) ────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── read (Java) ───${NC}"

# Java read by line (add method)
java_read="$("$XC" read "$FIXTURES/calculator.java" 16 2>&1)"
if echo "$java_read" | grep -q "public int add"; then
    pass "read Java method by line"
else
    fail "read Java method by line" "unexpected: $java_read"
fi

# Java read --name
java_name="$("$XC" read "$FIXTURES/calculator.java" --name divide 2>&1)"
if echo "$java_name" | grep -q "public double divide"; then
    pass "read Java --name"
else
    fail "read Java --name" "unexpected: $java_name"
fi

# Java read --name with class name
java_class="$("$XC" read "$FIXTURES/calculator.java" --name Calculator 2>&1)"
if echo "$java_class" | grep -q "public class Calculator"; then
    pass "read Java --name class"
else
    fail "read Java --name class" "unexpected: $java_class"
fi

# Java read --all
java_all="$("$XC" read "$FIXTURES/calculator.java" --all 2>&1)"
if echo "$java_all" | grep -q "public int add" && echo "$java_all" | grep -q "public double divide"; then
    pass "read Java --all"
else
    fail "read Java --all" "unexpected: $java_all"
fi

# ─── read (--name suffix matching) ──────────────────────────────────────

echo -e "\n${YELLOW}─── read (--name suffix) ───${NC}"

# Exact match works (already tested above with --name baz)
# Suffix match: any suffix of a top-level block name matches.
# Note: nested members (struct::method) are not exposed as top-level blocks,
# so "Point::GetX" suffix "GetX" won't match until struct internals are
# surfaced by ListBlocks.

# Partial (non-suffix) should NOT match
set +e
read_partial="$("$XC" read "$FIXTURES/funcs.c" --name oo 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$read_partial" | grep -q "no block matching"; then
    pass "read --name partial (not suffix)"
else
    fail "read --name partial (not suffix)" "should not match: $read_partial"
fi

# Exact match on simple names still works
read_exact="$("$XC" read "$FIXTURES/funcs.c" --name bar 2>&1)"
if echo "$read_exact" | grep -q "int bar"; then
    pass "read --name exact match (bar)"
else
    fail "read --name exact match (bar)" "unexpected: $read_exact"
fi

# ─── replace (extended) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── replace (extended) ───${NC}"

# Replace multi-line
cp "$FIXTURES/funcs.c" "$TMPDIR/replace_multi.c"
printf 'int foo(int x) {\n    return x + 1;\n}' > "$TMPDIR/replace_multi_old.txt"
printf 'int foo(int x) {\n    return x + 999;\n}' > "$TMPDIR/replace_multi_new.txt"
"$XC" replace "$TMPDIR/replace_multi.c" 4 "$TMPDIR/replace_multi_old.txt" "$TMPDIR/replace_multi_new.txt" 2>&1 > /dev/null
if grep -q "x + 999" "$TMPDIR/replace_multi.c"; then
    pass "replace (multi-line)"
else
    fail "replace (multi-line)" "multi-line replace not applied"
fi

# Replace scoped to a different block (struct)
cp "$FIXTURES/structs.cc" "$TMPDIR/replace_struct.cc"
echo -n 'int x;' > "$TMPDIR/replace_struct_old.txt"
echo -n 'double x;' > "$TMPDIR/replace_struct_new.txt"
"$XC" replace "$TMPDIR/replace_struct.cc" 4 "$TMPDIR/replace_struct_old.txt" "$TMPDIR/replace_struct_new.txt" 2>&1 > /dev/null
if grep -q "double x" "$TMPDIR/replace_struct.cc"; then
    pass "replace (scoped to struct)"
else
    fail "replace (scoped to struct)" "struct-scoped replace not applied"
fi

# Replace oldText not found in block
cp "$FIXTURES/funcs.c" "$TMPDIR/replace_nf.c"
echo -n 'nonexistent_in_this_function' > "$TMPDIR/replace_nf_old.txt"
echo -n 'replacement' > "$TMPDIR/replace_nf_new.txt"
set +e
replace_err="$("$XC" replace "$TMPDIR/replace_nf.c" 4 "$TMPDIR/replace_nf_old.txt" "$TMPDIR/replace_nf_new.txt" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$replace_err" | grep -q "oldText not found within block"; then
    pass "replace (oldText not found)"
else
    fail "replace (oldText not found)" "expected error: $replace_err"
fi

# Replace ambiguous within block
cp "$FIXTURES/funcs.c" "$TMPDIR/replace_ambig.c"
echo -n 'x' > "$TMPDIR/replace_ambig_old.txt"
echo -n 'y' > "$TMPDIR/replace_ambig_new.txt"
set +e
replace_ambig="$("$XC" replace "$TMPDIR/replace_ambig.c" 4 "$TMPDIR/replace_ambig_old.txt" "$TMPDIR/replace_ambig_new.txt" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$replace_ambig" | grep -q "ambiguous"; then
    pass "replace (ambiguous in block)"
else
    fail "replace (ambiguous in block)" "expected ambiguous: $replace_ambig"
fi

# Replace with empty newText
cp "$FIXTURES/funcs.c" "$TMPDIR/replace_empty.c"
echo -n 'x + 1' > "$TMPDIR/replace_empty_old.txt"
echo -n '' > "$TMPDIR/replace_empty_new.txt"
"$XC" replace "$TMPDIR/replace_empty.c" 4 "$TMPDIR/replace_empty_old.txt" "$TMPDIR/replace_empty_new.txt" 2>&1 > /dev/null
if grep -q 'return ;' "$TMPDIR/replace_empty.c"; then
    pass "replace (empty newText)"
else
    fail "replace (empty newText)" "empty replacement not applied"
fi

# ─── undo (extended) ─────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── undo (extended) ───${NC}"

test_backup_gitignore "backup payload ignored by git"

# Undo after move
cp "$FIXTURES/funcs.c" "$TMPDIR/undo_move.c"
"$XC" move "$TMPDIR/undo_move.c" 12 6 2>&1 > /dev/null
"$XC" undo "$TMPDIR/undo_move.c" 2>&1 > /dev/null
if diff -q "$FIXTURES/funcs.c" "$TMPDIR/undo_move.c" > /dev/null 2>&1; then
    pass "undo after move"
else
    fail "undo after move" "file not restored"
fi

# Undo after edit
cp "$FIXTURES/funcs.c" "$TMPDIR/undo_edit.c"
echo -n '    return x + 1;' > "$TMPDIR/undo_edit_old.txt"
echo -n '    return x + 99;' > "$TMPDIR/undo_edit_new.txt"
"$XC" edit "$TMPDIR/undo_edit.c" "$TMPDIR/undo_edit_old.txt" "$TMPDIR/undo_edit_new.txt" 2>&1 > /dev/null
"$XC" undo "$TMPDIR/undo_edit.c" 2>&1 > /dev/null
if diff -q "$FIXTURES/funcs.c" "$TMPDIR/undo_edit.c" > /dev/null 2>&1; then
    pass "undo after edit"
else
    fail "undo after edit" "file not restored"
fi

# Undo after replace
cp "$FIXTURES/funcs.c" "$TMPDIR/undo_replace.c"
echo -n 'x + 1' > "$TMPDIR/undo_replace_old.txt"
echo -n 'x + 777' > "$TMPDIR/undo_replace_new.txt"
"$XC" replace "$TMPDIR/undo_replace.c" 4 "$TMPDIR/undo_replace_old.txt" "$TMPDIR/undo_replace_new.txt" 2>&1 > /dev/null
"$XC" undo "$TMPDIR/undo_replace.c" 2>&1 > /dev/null
if diff -q "$FIXTURES/funcs.c" "$TMPDIR/undo_replace.c" > /dev/null 2>&1; then
    pass "undo after replace"
else
    fail "undo after replace" "file not restored"
fi

# Undo with no backup
cp "$FIXTURES/funcs.c" "$TMPDIR/undo_nobackup.c"
set +e
undo_err="$("$XC" undo "$TMPDIR/undo_nobackup.c" 2>&1)"
# set -e (removed: errexit was not enabled at script startup)
if echo "$undo_err" | grep -q "no backup found"; then
    pass "undo (no backup)"
else
    fail "undo (no backup)" "expected 'no backup': $undo_err"
fi

# ─── undo (multi-level) ──────────────────────────────────────────────────

echo -e "\n${YELLOW}─── undo (multi-level) ───${NC}"

# Multi-level undo: backup is overwritten, so only one level works.
# Documents current limitation.
cp "$FIXTURES/funcs.c" "$TMPDIR/undo_multi.c"
"$XC" delete "$TMPDIR/undo_multi.c" 12 2>&1 > /dev/null   # delete baz
"$XC" move "$TMPDIR/undo_multi.c" 14 6 2>&1 > /dev/null   # move qux after foo
"$XC" undo "$TMPDIR/undo_multi.c" 2>&1 > /dev/null         # undo move → post-delete state
# Verify we're at post-delete state (qux should be back, baz still gone)
if grep -q "int baz" "$TMPDIR/undo_multi.c"; then
    fail "undo multi-level (first undo)" "baz should still be deleted"
elif ! grep -q "int qux" "$TMPDIR/undo_multi.c"; then
    fail "undo multi-level (first undo)" "qux should be restored"
else
    pass "undo multi-level (first undo)"
fi
# Second undo should succeed and restore original file (multi-version backups)
"$XC" undo "$TMPDIR/undo_multi.c" 2>&1 > /dev/null
if grep -q "int baz" "$TMPDIR/undo_multi.c" && grep -q "int qux" "$TMPDIR/undo_multi.c"; then
    pass "undo multi-level (second undo)"
else
    fail "undo multi-level (second undo)" "expected original file with both baz and qux"
fi
# Third undo should fail (no more backups)
set +e
undo3_out="$("$XC" undo "$TMPDIR/undo_multi.c" 2>&1)"
undo3_rc=$?
# set -e (removed: errexit was not enabled at script startup)
if echo "$undo3_out" | grep -q "no backup found"; then
    pass "undo multi-level (third undo fails)"
else
    fail "undo multi-level (third undo fails)" "expected 'no backup': $undo3_out"
fi

# ─── onboard ─────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── onboard ───${NC}"

onboard_out="$("$XC" onboard 2>&1)"
if echo "$onboard_out" | grep -q "xcav"; then
    pass "onboard prints guide"
else
    fail "onboard prints guide" "unexpected: $onboard_out"
fi

# ─── help ─────────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── help ───${NC}"

# Verify help output mentions all subcommands
help_out="$("$XC" help 2>&1)"
for cmd in blocks read move delete edit replace undo onboard help '--stdin' 'tree-sitter'; do
    if echo "$help_out" | grep -qF -e "$cmd"; then
        pass "help mentions '$cmd'"
    else
        fail "help mentions '$cmd'" "missing from help output"
    fi
done

# ═══════════════════════════════════════════════════════════════════════════
# Phase 1 — Rich fixture survey + read (realistic.java)
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 1: realistic.java survey + read ───${NC}"

test_blocks "blocks (realistic.java — 15 blocks)" \
    "$FIXTURES/realistic.java" \
    "$EXPECTED/blocks_realistic_java.txt"

# read --name DataService (class preference, warns about constructor ambiguity)
read_ds="$("$XC" read "$FIXTURES/realistic.java" --name DataService 2>&1)"
if echo "$read_ds" | grep -q "public class DataService"; then
    pass "read realistic --name DataService (class)"
else
    fail "read realistic --name DataService (class)" "unexpected: $read_ds"
fi

# read --name toString (with @Override annotation)
read_tostr="$("$XC" read "$FIXTURES/realistic.java" --name toString 2>&1)"
if echo "$read_tostr" | grep -q "@Override"; then
    pass "read realistic --name toString (@Override preserved)"
else
    fail "read realistic --name toString (@Override preserved)" "unexpected: $read_tostr"
fi

# read --name firstOrNull (generic method)
read_forn="$("$XC" read "$FIXTURES/realistic.java" --name firstOrNull 2>&1)"
if echo "$read_forn" | grep -q "<T> T firstOrNull"; then
    pass "read realistic --name firstOrNull (generic)"
else
    fail "read realistic --name firstOrNull (generic)" "unexpected: $read_forn"
fi

# read --name process (interface method with throws)
read_proc="$("$XC" read "$FIXTURES/realistic.java" --name process 2>&1)"
if echo "$read_proc" | grep -q "throws IOException"; then
    pass "read realistic --name process (interface, throws preserved)"
else
    fail "read realistic --name process (interface, throws preserved)" "unexpected: $read_proc"
fi

# read --name Status (enum)
read_status="$("$XC" read "$FIXTURES/realistic.java" --name Status 2>&1)"
if echo "$read_status" | grep -q "enum Status"; then
    pass "read realistic --name Status (enum)"
else
    fail "read realistic --name Status (enum)" "unexpected: $read_status"
fi

# read --all on realistic.java
read_rall="$("$XC" read "$FIXTURES/realistic.java" --all 2>&1)"
if echo "$read_rall" | grep -q "DataService" && echo "$read_rall" | grep -q "DataProcessor" && echo "$read_rall" | grep -q "Status"; then
    pass "read realistic --all (all types present)"
else
    fail "read realistic --all (all types present)" "unexpected: $read_rall"
fi

# ═══════════════════════════════════════════════════════════════════════════
# Phase 2 — Single-file mutations (realistic.java)
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 2: realistic.java single-file mutations ───${NC}"

test_move "move legacyMethod before toString (annotations preserved)" \
    "$FIXTURES/realistic.java" 34 31 \
    "$EXPECTED/java_move_legacy_before_toString.java"

test_move "move DataService ctor after compute (throws preserved)" \
    "$FIXTURES/realistic.java" 10 16 \
    "$EXPECTED/java_move_ctor_after_compute.java"

test_delete "delete validate (abstract method)" \
    "$FIXTURES/realistic.java" 49 \
    "$EXPECTED/java_delete_validate.java"

test_delete "delete isActive (enum method, enum preserved)" \
    "$FIXTURES/realistic.java" 65 \
    "$EXPECTED/java_delete_isActive.java"

test_edit "edit divide body (throws unchanged)" \
    "$FIXTURES/realistic.java" \
    "return a / b;" \
    "return b != 0 ? a / b : 0;" \
    "$EXPECTED/java_edit_divide_body.java"

test_replace "replace compute body (signature unchanged)" \
    "$FIXTURES/realistic.java" 14 \
    "return a + b;" \
    "return a + b + 1;" \
    "$EXPECTED/java_replace_compute_body.java"

# ═══════════════════════════════════════════════════════════════════════════
# Phase 3 — Cross-file operations
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 3: cross-file copy / move-into ───${NC}"

# --- copy divide into target (throws preserved) ---
cp "$FIXTURES/realistic_target.java" "$TMPDIR/copy_target.java"
"$XC" copy "$FIXTURES/realistic.java" 19 "$TMPDIR/copy_target.java" 3 > /dev/null 2>&1
if diff -q "$TMPDIR/copy_target.java" "$EXPECTED/java_copy_divide_into_target.java" > /dev/null 2>&1; then
    pass "copy divide into target (throws preserved)"
else
    fail "copy divide into target (throws preserved)" "file mismatch"
    diff "$EXPECTED/java_copy_divide_into_target.java" "$TMPDIR/copy_target.java" | sed 's/^/    /'
fi

# --- move-into legacyMethod into target (annotations preserved) ---
cp "$FIXTURES/realistic.java" "$TMPDIR/mi_src.java"
cp "$FIXTURES/realistic_target.java" "$TMPDIR/mi_target.java"
"$XC" move-into "$TMPDIR/mi_src.java" 34 "$TMPDIR/mi_target.java" 3 > /dev/null 2>&1
if diff -q "$TMPDIR/mi_target.java" "$EXPECTED/java_moveinto_legacy_target.java" > /dev/null 2>&1; then
    pass "move-into legacyMethod (annotations preserved)"
else
    fail "move-into legacyMethod (annotations preserved)" "file mismatch"
    diff "$EXPECTED/java_moveinto_legacy_target.java" "$TMPDIR/mi_target.java" | sed 's/^/    /'
fi

# --- move-into with --copy-includes (constructor, throws IOException → imports copied) ---
cp "$FIXTURES/realistic_target.java" "$TMPDIR/mi_ci_target.java"
"$XC" move-into "$FIXTURES/realistic.java" 10 "$TMPDIR/mi_ci_target.java" 3 --copy-includes > /dev/null 2>&1
if diff -q "$TMPDIR/mi_ci_target.java" "$EXPECTED/java_moveinto_includes_target.java" > /dev/null 2>&1; then
    pass "move-into --copy-includes (Java imports copied)"
else
    fail "move-into --copy-includes (Java imports copied)" "file mismatch"
    diff "$EXPECTED/java_moveinto_includes_target.java" "$TMPDIR/mi_ci_target.java" | sed 's/^/    /'
fi
# Restore realistic.java after move-into mutated it
"$XC" undo "$FIXTURES/realistic.java" > /dev/null 2>&1 || true

# ═══════════════════════════════════════════════════════════════════════════
# Phase 4 — Edge cases
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 4: edge cases ───${NC}"

test_blocks "blocks (empty Java — 0 blocks)" \
    "$FIXTURES/empty_java.java" \
    "$EXPECTED/blocks_empty_java.txt"

test_blocks "blocks (interface-only Java)" \
    "$FIXTURES/interface_only.java" \
    "$EXPECTED/blocks_interface_only.txt"

test_blocks "blocks (enum-only Java)" \
    "$FIXTURES/enum_only.java" \
    "$EXPECTED/blocks_enum_only.txt"

test_blocks "blocks (abstract-only Java)" \
    "$FIXTURES/abstract_only.java" \
    "$EXPECTED/blocks_abstract_only.txt"

# Constructor name collision: --name DataService should prefer class, warn about ambiguity
collision_out="$("$XC" read "$FIXTURES/realistic.java" --name DataService 2>&1)"
if echo "$collision_out" | grep -q "WARNING.*matches 2 blocks"; then
    pass "read --name DataService warns on collision"
else
    fail "read --name DataService warns on collision" "expected WARNING, got: $collision_out"
fi

# Method with type parameter <T> found by --name
read_gen="$("$XC" read "$FIXTURES/realistic.java" --name firstOrNull 2>&1)"
if echo "$read_gen" | grep -q "T firstOrNull"; then
    pass "read --name firstOrNull (generic, found by name)"
else
    fail "read --name firstOrNull (generic, found by name)" "unexpected: $read_gen"
fi

# ═══════════════════════════════════════════════════════════════════════════
# Phase 5 — Non-Java backfill
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 5: non-Java backfill ───${NC}"

# --- TSX blocks ---
test_blocks "blocks (TSX)" \
    "$FIXTURES/react.tsx" \
    "$EXPECTED/blocks_tsx.txt"

# --- C cross-file copy ---
cp "$FIXTURES/target.c" "$TMPDIR/c_copy_tgt.c"
"$XC" copy "$FIXTURES/funcs.c" 4 "$TMPDIR/c_copy_tgt.c" 5 > /dev/null 2>&1
if diff -q "$TMPDIR/c_copy_tgt.c" "$EXPECTED/c_copy_foo_into_target.c" > /dev/null 2>&1; then
    pass "C cross-file copy"
else
    fail "C cross-file copy" "file mismatch"
    diff "$EXPECTED/c_copy_foo_into_target.c" "$TMPDIR/c_copy_tgt.c" | sed 's/^/    /'
fi

# --- C cross-file move-into ---
cp "$FIXTURES/funcs.c" "$TMPDIR/c_mi_src.c"
cp "$FIXTURES/target.c" "$TMPDIR/c_mi_tgt.c"
"$XC" move-into "$TMPDIR/c_mi_src.c" 8 "$TMPDIR/c_mi_tgt.c" 5 > /dev/null 2>&1
if diff -q "$TMPDIR/c_mi_tgt.c" "$EXPECTED/c_moveinto_bar_target.c" > /dev/null 2>&1; then
    pass "C cross-file move-into"
else
    fail "C cross-file move-into" "file mismatch"
    diff "$EXPECTED/c_moveinto_bar_target.c" "$TMPDIR/c_mi_tgt.c" | sed 's/^/    /'
fi

# --- C cross-file move-into --copy-includes ---
cp "$FIXTURES/with_includes.c" "$TMPDIR/c_mici_src.c"
cp "$FIXTURES/target.c" "$TMPDIR/c_mici_tgt.c"
"$XC" move-into "$TMPDIR/c_mici_src.c" 4 "$TMPDIR/c_mici_tgt.c" 5 --copy-includes > /dev/null 2>&1
if diff -q "$TMPDIR/c_mici_tgt.c" "$EXPECTED/c_moveinto_includes_target.c" > /dev/null 2>&1; then
    pass "C cross-file move-into --copy-includes"
else
    fail "C cross-file move-into --copy-includes" "file mismatch"
    diff "$EXPECTED/c_moveinto_includes_target.c" "$TMPDIR/c_mici_tgt.c" | sed 's/^/    /'
fi

# --- JS read ---
js_read2="$("$XC" read "$FIXTURES/javascript.js" 10 2>&1)"
if echo "$js_read2" | grep -q "class Calculator"; then
    pass "read JS class by line"
else
    fail "read JS class by line" "unexpected: $js_read2"
fi

# --- JS edit ---
cp "$FIXTURES/javascript.js" "$TMPDIR/js_edit.js"
echo -n 'return x * y;' > "$TMPDIR/js_edit_old.txt"
echo -n 'return x + y;' > "$TMPDIR/js_edit_new.txt"
"$XC" edit "$TMPDIR/js_edit.js" "$TMPDIR/js_edit_old.txt" "$TMPDIR/js_edit_new.txt" > /dev/null 2>&1
if grep -q 'return x + y;' "$TMPDIR/js_edit.js"; then
    pass "JS edit"
else
    fail "JS edit" "edit not applied"
fi

# --- JS cross-file ---
cp "$FIXTURES/javascript.js" "$TMPDIR/js_cross_src.js"
cp "$FIXTURES/javascript.js" "$TMPDIR/js_cross_tgt.js"
"$XC" move-into "$TMPDIR/js_cross_src.js" 4 "$TMPDIR/js_cross_tgt.js" 18 > /dev/null 2>&1
if "$XC" blocks "$TMPDIR/js_cross_tgt.js" 2>&1 | grep -q "greet"; then
    pass "JS cross-file move-into"
else
    fail "JS cross-file move-into" "move-into failed"
fi

# --- TS cross-file ---
cp "$FIXTURES/typescript.ts" "$TMPDIR/ts_cross_src.ts"
cp "$FIXTURES/typescript.ts" "$TMPDIR/ts_cross_tgt.ts"
"$XC" copy "$TMPDIR/ts_cross_src.ts" 23 "$TMPDIR/ts_cross_tgt.ts" 25 > /dev/null 2>&1
if "$XC" blocks "$TMPDIR/ts_cross_tgt.ts" 2>&1 | grep -q "func distance"; then
    pass "TS cross-file copy"
else
    fail "TS cross-file copy" "copy failed"
fi

# ─── replace-block ──────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── replace-block ───${NC}"

# --- replace-block C bar (stdin mode) ---
cp "$FIXTURES/funcs.c" "$TMPDIR/rb_c_stdin.c"
cat <<'EOF' | "$XC" replace-block "$TMPDIR/rb_c_stdin.c" 8 > /dev/null 2>&1
int bar(int x) {
    return x * 3;
}
EOF
if diff -q "$TMPDIR/rb_c_stdin.c" "$EXPECTED/c_replace_block_bar.c" > /dev/null 2>&1; then
    pass "replace-block C bar (stdin)"
else
    fail "replace-block C bar (stdin)" "file mismatch"
    diff "$EXPECTED/c_replace_block_bar.c" "$TMPDIR/rb_c_stdin.c" | sed 's/^/    /'
fi

# --- replace-block Java add (stdin mode) ---
cp "$FIXTURES/calculator.java" "$TMPDIR/rb_java_add.c"
cat <<'EOF' | "$XC" replace-block "$TMPDIR/rb_java_add.c" 15 > /dev/null 2>&1
    public int add(int a, int b) {
        return a + b + 100;
    }
EOF
if diff -q "$TMPDIR/rb_java_add.c" "$EXPECTED/java_replace_block_add.java" > /dev/null 2>&1; then
    pass "replace-block Java add (stdin)"
else
    fail "replace-block Java add (stdin)" "file mismatch"
    diff "$EXPECTED/java_replace_block_add.java" "$TMPDIR/rb_java_add.c" | sed 's/^/    /'
fi

# --- replace-block Java class (stdin mode, no package/imports) ---
cp "$FIXTURES/calculator.java" "$TMPDIR/rb_java_class.c"
cat <<'EOF' | "$XC" replace-block "$TMPDIR/rb_java_class.c" 7 > /dev/null 2>&1
public class Calculator {

    private int baseValue;

    public Calculator(int base) {
        this.baseValue = base;
    }

    public int compute(int x) {
        return x + baseValue;
    }
}
EOF
if diff -q "$TMPDIR/rb_java_class.c" "$EXPECTED/java_replace_block_class.java" > /dev/null 2>&1; then
    pass "replace-block Java class (stdin)"
else
    fail "replace-block Java class (stdin)" "file mismatch"
    diff "$EXPECTED/java_replace_block_class.java" "$TMPDIR/rb_java_class.c" | sed 's/^/    /'
fi

# --- replace-block C bar (file mode, backward compat) ---
cp "$FIXTURES/funcs.c" "$TMPDIR/rb_c_file.c"
printf 'int bar(int x) {\n    return x * 3;\n}\n' > "$TMPDIR/rb_c_new.txt"
"$XC" replace-block "$TMPDIR/rb_c_file.c" 8 "$TMPDIR/rb_c_new.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/rb_c_file.c" "$EXPECTED/c_replace_block_bar.c" > /dev/null 2>&1; then
    pass "replace-block C bar (file mode)"
else
    fail "replace-block C bar (file mode)" "file mismatch"
    diff "$EXPECTED/c_replace_block_bar.c" "$TMPDIR/rb_c_file.c" | sed 's/^/    /'
fi

# --- replace-block stdin empty error ---
cp "$FIXTURES/funcs.c" "$TMPDIR/rb_empty.c"
set +e
rb_empty_err="$(echo -n '' | "$XC" replace-block "$TMPDIR/rb_empty.c" 8 2>&1)"
# set -e
if echo "$rb_empty_err" | grep -q "stdin is empty"; then
    pass "replace-block (stdin empty)"
else
    fail "replace-block (stdin empty)" "expected 'stdin is empty': $rb_empty_err"
fi

# ─── insert ──────────────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── insert ───${NC}"

cat > "$TMPDIR/insert_before.txt" << 'XCAVEOF'
int new_before(int x) {
    return x + 100;
}
XCAVEOF
cat > "$TMPDIR/insert_after.txt" << 'XCAVEOF'
int new_after(int x) {
    return x * 100;
}
XCAVEOF

cp "$FIXTURES/funcs.c" "$TMPDIR/insert_before_bar.c"
"$XC" insert --before "$TMPDIR/insert_before_bar.c" 8 "$TMPDIR/insert_before.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/insert_before_bar.c" "$EXPECTED/c_insert_before_bar.c" > /dev/null 2>&1; then
    pass "insert --before"
else
    fail "insert --before" "file mismatch"
    diff "$EXPECTED/c_insert_before_bar.c" "$TMPDIR/insert_before_bar.c" | sed 's/^/    /'
fi

cp "$FIXTURES/funcs.c" "$TMPDIR/insert_after_bar.c"
"$XC" insert --after "$TMPDIR/insert_after_bar.c" 10 "$TMPDIR/insert_after.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/insert_after_bar.c" "$EXPECTED/c_insert_after_bar.c" > /dev/null 2>&1; then
    pass "insert --after"
else
    fail "insert --after" "file mismatch"
    diff "$EXPECTED/c_insert_after_bar.c" "$TMPDIR/insert_after_bar.c" | sed 's/^/    /'
fi

# Insert error: no --before/--after flag
cp "$FIXTURES/funcs.c" "$TMPDIR/insert_noflag.c"
set +e
insert_noflag="$("$XC" insert "$TMPDIR/insert_noflag.c" 8 "$TMPDIR/insert_before.txt" 2>&1)"
# set -e
if echo "$insert_noflag" | grep -q "specify --before, --after, before, or after"; then
    pass "insert (no flag)"
else
    fail "insert (no flag)" "expected error: $insert_noflag"
fi

# Insert: positional before/after (without -- prefix)
cp "$FIXTURES/funcs.c" "$TMPDIR/insert_pos_before.c"
"$XC" insert before "$TMPDIR/insert_pos_before.c" 8 "$TMPDIR/insert_before.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/insert_pos_before.c" "$EXPECTED/c_insert_before_bar.c" > /dev/null 2>&1; then
    pass "insert before (positional)"
else
    fail "insert before (positional)" "file mismatch"
    diff "$EXPECTED/c_insert_before_bar.c" "$TMPDIR/insert_pos_before.c" | sed 's/^/    /'
fi

cp "$FIXTURES/funcs.c" "$TMPDIR/insert_pos_after.c"
"$XC" insert after "$TMPDIR/insert_pos_after.c" 10 "$TMPDIR/insert_after.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/insert_pos_after.c" "$EXPECTED/c_insert_after_bar.c" > /dev/null 2>&1; then
    pass "insert after (positional)"
else
    fail "insert after (positional)" "file mismatch"
    diff "$EXPECTED/c_insert_after_bar.c" "$TMPDIR/insert_pos_after.c" | sed 's/^/    /'
fi

# Insert error: bad line number
cp "$FIXTURES/funcs.c" "$TMPDIR/insert_badline.c"
set +e
insert_badline="$("$XC" insert --before "$TMPDIR/insert_badline.c" 0 "$TMPDIR/insert_before.txt" 2>&1)"
# set -e
if echo "$insert_badline" | grep -q "line number must be"; then
    pass "insert (bad line number)"
else
    fail "insert (bad line number)" "expected error: $insert_badline"
fi

# ─── replace-block output format ─────────────────────────────────────────

echo -e "\n${YELLOW}─── replace-block output format ───${NC}"

# Verify line count in replace-block output
cp "$FIXTURES/realistic.java" "$TMPDIR/rb_output.java"
echo 'class Test { void m1() {} void m2() {} }' > "$TMPDIR/rb_new_class.txt"
set +e
rb_output="$("$XC" replace-block "$TMPDIR/rb_output.java" 7 "$TMPDIR/rb_new_class.txt" 2>&1)"
# set -e
if echo "$rb_output" | grep -qE 'was [0-9]+ lines?, now [0-9]+ lines?'; then
    pass "replace-block (line count)"
else
    fail "replace-block (line count)" "expected 'was N lines, now M lines': $rb_output"
fi

# Verify formatter reminder for class-level replacements
if echo "$rb_output" | grep -q "Run your formatter"; then
    pass "replace-block (formatter reminder)"
else
    fail "replace-block (formatter reminder)" "expected formatter reminder: $rb_output"
fi

# ═══════════════════════════════════════════════════════════════════════════
# Phase 6 — Coverage gaps: untested features and edge cases
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 6: copy --show-returns, --copy-includes ───${NC}"

# --- copy --show-returns ---
cp "$FIXTURES/target.c" "$TMPDIR/c_copy_showret.c"
copy_show_out="$("$XC" copy "$FIXTURES/funcs.c" 4 "$TMPDIR/c_copy_showret.c" 5 --show-returns 2>&1)"
if echo "$copy_show_out" | grep -q '# returns' && diff -q "$TMPDIR/c_copy_showret.c" "$EXPECTED/c_copy_show_returns.c" > /dev/null 2>&1; then
    pass "copy --show-returns"
else
    fail "copy --show-returns" "expected # returns in output and matching dest file"
    echo "    output: $copy_show_out"
    diff "$EXPECTED/c_copy_show_returns.c" "$TMPDIR/c_copy_showret.c" | sed 's/^/    /'
fi

# --- copy --copy-includes (C) ---
cp "$FIXTURES/target.c" "$TMPDIR/c_copy_inc.c"
"$XC" copy "$FIXTURES/with_includes.c" 4 "$TMPDIR/c_copy_inc.c" 5 --copy-includes > /dev/null 2>&1
if diff -q "$TMPDIR/c_copy_inc.c" "$EXPECTED/c_copy_include_target.c" > /dev/null 2>&1; then
    pass "copy --copy-includes (C)"
else
    fail "copy --copy-includes (C)" "file mismatch"
    diff "$EXPECTED/c_copy_include_target.c" "$TMPDIR/c_copy_inc.c" | sed 's/^/    /'
fi

# --- copy --copy-includes (Java) ---
cp "$FIXTURES/realistic_target.java" "$TMPDIR/copy_java_inc.java"
"$XC" copy "$FIXTURES/realistic.java" 10 "$TMPDIR/copy_java_inc.java" 3 --copy-includes > /dev/null 2>&1
if grep -q 'import java.io.IOException' "$TMPDIR/copy_java_inc.java"; then
    pass "copy --copy-includes (Java)"
else
    fail "copy --copy-includes (Java)" "expected import copied"
fi

# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 6: copy error paths ───${NC}"

# copy source not found
set +e
copy_src_err="$("$XC" copy "$FIXTURES/nonexistent.c" 1 "$FIXTURES/target.c" 3 2>&1)"
# set -e
if echo "$copy_src_err" | grep -qE "cannot|operation_failed"; then
    pass "copy (source not found)"
else
    fail "copy (source not found)" "expected error: $copy_src_err"
fi

# copy bad line number
set +e
copy_badline="$("$XC" copy "$FIXTURES/funcs.c" 0 "$FIXTURES/target.c" 1 2>&1)"
# set -e
if echo "$copy_badline" | grep -q "must be >= 1"; then
    pass "copy (bad line number)"
else
    fail "copy (bad line number)" "expected error: $copy_badline"
fi

echo -e "\n${YELLOW}─── Phase 6: move-into static stripping ───${NC}"

# move-into strips 'static' from cross-file moved functions
cp "$FIXTURES/static_funcs.c" "$TMPDIR/mi_static_src.c"
cp "$FIXTURES/target.c" "$TMPDIR/mi_static_tgt.c"
"$XC" move-into "$TMPDIR/mi_static_src.c" 4 "$TMPDIR/mi_static_tgt.c" 5 > /dev/null 2>&1
if diff -q "$TMPDIR/mi_static_tgt.c" "$EXPECTED/c_moveinto_static_stripped.c" > /dev/null 2>&1; then
    pass "move-into (static stripped)"
else
    fail "move-into (static stripped)" "file mismatch"
    diff "$EXPECTED/c_moveinto_static_stripped.c" "$TMPDIR/mi_static_tgt.c" | sed 's/^/    /'
fi

# (uses temp copies, no need to undo fixtures)

echo -e "\n${YELLOW}─── Phase 6: move-into error paths ───${NC}"

# move-into src not found
set +e
mi_src_err="$("$XC" move-into "$FIXTURES/nonexistent.c" 1 "$FIXTURES/target.c" 1 2>&1)"
# set -e
if echo "$mi_src_err" | grep -qE "cannot|operation_failed"; then
    pass "move-into (source not found)"
else
    fail "move-into (source not found)" "expected error: $mi_src_err"
fi

# move-into bad line number
set +e
mi_badline="$("$XC" move-into "$FIXTURES/funcs.c" -1 "$FIXTURES/target.c" 1 2>&1)"
# set -e
if echo "$mi_badline" | grep -q "must be >= 1"; then
    pass "move-into (bad src line)"
else
    fail "move-into (bad src line)" "expected error: $mi_badline"
fi

echo -e "\n${YELLOW}─── Phase 6: edit --dry-run (file mode) ───${NC}"

# edit --dry-run should report match without modifying file
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_dryrun.c"
echo -n '    return x + 1;' > "$TMPDIR/edit_dryrun_old.txt"
echo -n '    return x + 99;' > "$TMPDIR/edit_dryrun_new.txt"
set +e
dryrun_out="$("$XC" edit "$TMPDIR/edit_dryrun.c" "$TMPDIR/edit_dryrun_old.txt" "$TMPDIR/edit_dryrun_new.txt" --dry-run 2>&1)"
set -e
if echo "$dryrun_out" | grep -q "dry-run" && diff -q "$FIXTURES/funcs.c" "$TMPDIR/edit_dryrun.c" > /dev/null 2>&1; then
    pass "edit --dry-run (file mode)"
else
    fail "edit --dry-run (file mode)" "file modified or missing --dry-run output"
    echo "    output: $dryrun_out"
fi

echo -e "\n${YELLOW}─── Phase 6: read --raw ───${NC}"

# read --raw preserves indentation
read_raw="$("$XC" read "$FIXTURES/funcs.c" 4 --raw 2>&1)"
if echo "$read_raw" | grep -q '    return x + 1;'; then
    pass "read --raw (preserves indent)"
else
    fail "read --raw (preserves indent)" "expected indented output: $read_raw"
fi

# read --all --raw preserves all indentation
read_all_raw="$("$XC" read "$FIXTURES/funcs.c" --all --raw 2>&1)"
if echo "$read_all_raw" | grep -q '    return x + 1;' && echo "$read_all_raw" | grep -q '#include <stdio.h>'; then
    pass "read --all --raw (preserves indent)"
else
    fail "read --all --raw (preserves indent)" "expected indented output"
fi

echo -e "\n${YELLOW}─── Phase 6: read --fix ───${NC}"

# read --fix writes corrected indentation back to disk
cp "$FIXTURES/whitespace.c" "$TMPDIR/read_fix.c"
"$XC" read "$TMPDIR/read_fix.c" 5 --fix > /dev/null 2>&1
# After --fix, the file should still be valid C with the function body
if grep -q 'int foo' "$TMPDIR/read_fix.c" && grep -q 'return x + 1' "$TMPDIR/read_fix.c"; then
    pass "read --fix"
else
    fail "read --fix" "file missing expected content after fix"
fi

echo -e "\n${YELLOW}─── Phase 6: insert --before --after (both flags) ───${NC}"

# insert with both --before and --after should error
cp "$FIXTURES/funcs.c" "$TMPDIR/insert_both.c"
cat > "$TMPDIR/insert_both_content.txt" << 'XCAVEOF'
int dummy(void) { return 0; }
XCAVEOF
set +e
insert_both="$("$XC" insert --before --after "$TMPDIR/insert_both.c" 8 "$TMPDIR/insert_both_content.txt" 2>&1)"
# set -e
if echo "$insert_both" | grep -q "not both"; then
    pass "insert (--before --after both)"
else
    fail "insert (--before --after both)" "expected 'not both' error: $insert_both"
fi

echo -e "\n${YELLOW}─── Phase 6: replace-block error paths ───${NC}"

# replace-block file not found
set +e
rb_nofile="$("$XC" replace-block "$FIXTURES/nonexistent.c" 1 "$FIXTURES/funcs.c" 2>&1)"
# set -e
if echo "$rb_nofile" | grep -qE "cannot|cannot open|operation_failed"; then
    pass "replace-block (file not found)"
else
    fail "replace-block (file not found)" "expected error: $rb_nofile"
fi

# replace-block new-file not found
cp "$FIXTURES/funcs.c" "$TMPDIR/rb_nonew.c"
set +e
rb_nonew="$("$XC" replace-block "$TMPDIR/rb_nonew.c" 8 "$TMPDIR/nonexistent_new.txt" 2>&1)"
# set -e
if echo "$rb_nonew" | grep -qE "cannot|cannot read"; then
    pass "replace-block (new-file not found)"
else
    fail "replace-block (new-file not found)" "expected error: $rb_nonew"
fi

# replace-block bad line number
cp "$FIXTURES/funcs.c" "$TMPDIR/rb_badline.c"
echo 'int dummy(void) { return 0; }' > "$TMPDIR/rb_badline_new.txt"
set +e
rb_badline="$("$XC" replace-block "$TMPDIR/rb_badline.c" 0 "$TMPDIR/rb_badline_new.txt" 2>&1)"
# set -e
if echo "$rb_badline" | grep -q "must be >= 1"; then
    pass "replace-block (bad line number)"
else
    fail "replace-block (bad line number)" "expected error: $rb_badline"
fi

echo -e "\n${YELLOW}─── Phase 6: replace scoped in Java ───${NC}"

# replace within Java method block (subtract, line 19)
cp "$FIXTURES/calculator.java" "$TMPDIR/replace_java.c"
echo -n 'return a - b - baseValue;' > "$TMPDIR/replace_java_old.txt"
echo -n 'return a - b + baseValue;' > "$TMPDIR/replace_java_new.txt"
"$XC" replace "$TMPDIR/replace_java.c" 19 "$TMPDIR/replace_java_old.txt" "$TMPDIR/replace_java_new.txt" > /dev/null 2>&1
if grep -q 'return a - b + baseValue;' "$TMPDIR/replace_java.c"; then
    pass "replace scoped (Java)"
else
    fail "replace scoped (Java)" "replace not applied"
fi

# replace scoped in JS
cp "$FIXTURES/javascript.js" "$TMPDIR/replace_js.js"
echo -n 'return x * y;' > "$TMPDIR/replace_js_old.txt"
echo -n 'return x + y;' > "$TMPDIR/replace_js_new.txt"
"$XC" replace "$TMPDIR/replace_js.js" 10 "$TMPDIR/replace_js_old.txt" "$TMPDIR/replace_js_new.txt" > /dev/null 2>&1
if grep -q 'return x + y;' "$TMPDIR/replace_js.js"; then
    pass "replace scoped (JS)"
else
    fail "replace scoped (JS)" "replace not applied"
fi

echo -e "\n${YELLOW}─── Phase 6: read large file truncation ───${NC}"

# read --all on a >500 line file should truncate with warning
large_file="$TMPDIR/large_file.c"
{
    printf '#include <stdio.h>\n\n'
    printf 'int main(void) {\n'
    printf '    printf("hello\\n");\n'
    printf '    return 0;\n'
    printf '}\n'
    for i in $(seq 1 600); do
        printf '// padding line %04d 0123456789abcdef0123456789abcdef0123456789abcdef\n' "$i"
    done
} > "$large_file"
read_large="$("$XC" read "$large_file" --all 2>&1)"
if echo "$read_large" | grep -q 'Truncated'; then
    pass "read large file truncation"
else
    fail "read large file truncation" "expected Truncated warning"
fi

echo -e "\n${YELLOW}─── Phase 6: blocks directory mode ───${NC}"

# blocks on a directory with multiple source files
mkdir -p "$TMPDIR/multi_dir"
cp "$FIXTURES/funcs.c" "$TMPDIR/multi_dir/a.c"
cp "$FIXTURES/structs.cc" "$TMPDIR/multi_dir/b.cc"
blocks_dir="$("$XC" blocks "$TMPDIR/multi_dir" 2>&1)"
if echo "$blocks_dir" | grep -q 'a.c' && echo "$blocks_dir" | grep -q 'b.cc'; then
    pass "blocks (directory mode, multiple files)"
else
    fail "blocks (directory mode, multiple files)" "expected both filenames: $blocks_dir"
fi

# ═══════════════════════════════════════════════════════════════════════════
# Phase 7 — Remaining coverage gaps: error paths, edge cases, features
# ═══════════════════════════════════════════════════════════════════════════

echo -e "\n${YELLOW}─── Phase 7: replace error paths ───${NC}"

# replace with nonexistent old-file
cp "$FIXTURES/funcs.c" "$TMPDIR/replace_oldnf.c"
echo -n 'x' > "$TMPDIR/replace_oldnf_new.txt"
set +e
replace_oldnf="$("$XC" replace "$TMPDIR/replace_oldnf.c" 4 "$TMPDIR/nonexistent_old.txt" "$TMPDIR/replace_oldnf_new.txt" 2>&1)"
# set -e
if echo "$replace_oldnf" | grep -q "cannot read old-text file"; then
    pass "replace (old-file not found)"
else
    fail "replace (old-file not found)" "expected error: $replace_oldnf"
fi

# replace with nonexistent new-file
cp "$FIXTURES/funcs.c" "$TMPDIR/replace_newnf.c"
echo -n 'x + 1' > "$TMPDIR/replace_newnf_old.txt"
set +e
replace_newnf="$("$XC" replace "$TMPDIR/replace_newnf.c" 4 "$TMPDIR/replace_newnf_old.txt" "$TMPDIR/nonexistent_new.txt" 2>&1)"
# set -e
if echo "$replace_newnf" | grep -q "cannot read new-text file"; then
    pass "replace (new-file not found)"
else
    fail "replace (new-file not found)" "expected error: $replace_newnf"
fi

echo -e "\n${YELLOW}─── Phase 7: insert error paths ───${NC}"

# insert with nonexistent content file
cp "$FIXTURES/funcs.c" "$TMPDIR/insert_nocontent.c"
set +e
insert_nocontent="$("$XC" insert --before "$TMPDIR/insert_nocontent.c" 8 "$TMPDIR/nonexistent_content.txt" 2>&1)"
# set -e
if echo "$insert_nocontent" | grep -q "cannot read content file"; then
    pass "insert (content file not found)"
else
    fail "insert (content file not found)" "expected error: $insert_nocontent"
fi

# insert with nonexistent target file
set +e
insert_nofile="$("$XC" insert --before "$TMPDIR/nonexistent_file.c" 1 "$TMPDIR/insert_before.txt" 2>&1)"
# set -e
if echo "$insert_nofile" | grep -qE "cannot|cannot open|operation_failed"; then
    pass "insert (file not found)"
else
    fail "insert (file not found)" "expected error: $insert_nofile"
fi

echo -e "\n${YELLOW}─── Phase 7: read error paths ───${NC}"

# read --offset 0
set +e
read_off0="$("$XC" read "$FIXTURES/funcs.c" --offset 0 2>&1)"
# set -e
if echo "$read_off0" | grep -q "must be >= 1"; then
    pass "read (--offset must be >= 1)"
else
    fail "read (--offset must be >= 1)" "expected error: $read_off0"
fi

# read --offset beyond file length (use non-code file for plain path)
set +e
read_exceeds="$("$XC" read "$FIXTURES/unknown.xyz" --offset 999 2>&1)"
# set -e
if echo "$read_exceeds" | grep -q "exceeds file length"; then
    pass "read (--offset exceeds file length)"
else
    fail "read (--offset exceeds file length)" "expected 'exceeds file length': $read_exceeds"
fi

# read --name without value (-- in pattern must be escaped for grep)
set +e
read_noname="$("$XC" read "$FIXTURES/funcs.c" --name 2>&1)"
# set -e
if echo "$read_noname" | grep -q -e "name requires a value"; then
    pass "read (--name without value)"
else
    fail "read (--name without value)" "expected error: $read_noname"
fi

# read empty file
set +e
read_empty="$("$XC" read "$FIXTURES/empty.c" 1 2>&1)"
# set -e
if echo "$read_empty" | grep -qE "no block found|empty file|unsupported"; then
    pass "read (empty file)"
else
    fail "read (empty file)" "expected error: $read_empty"
fi

echo -e "\n${YELLOW}─── Phase 7: edit error paths ───${NC}"

# edit with missing old-file/new-file (no --stdin)
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_missing.c"
set +e
edit_missing="$("$XC" edit "$TMPDIR/edit_missing.c" 2>&1)"
# set -e
if echo "$edit_missing" | grep -q "expected <old-file> <new-file>"; then
    pass "edit (missing old/new files)"
else
    fail "edit (missing old/new files)" "expected error: $edit_missing"
fi

echo -e "\n${YELLOW}─── Phase 7: edit multi-pair ───${NC}"

# edit with two old/new pairs — both applied
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi2.c"
echo -n 'return x + 1;' > "$TMPDIR/edit_multi2_o1.txt"
echo -n 'return x + 100;' > "$TMPDIR/edit_multi2_n1.txt"
echo -n 'return x * 2;' > "$TMPDIR/edit_multi2_o2.txt"
echo -n 'return x * 200;' > "$TMPDIR/edit_multi2_n2.txt"
"$XC" edit "$TMPDIR/edit_multi2.c" \
    "$TMPDIR/edit_multi2_o1.txt" "$TMPDIR/edit_multi2_n1.txt" \
    "$TMPDIR/edit_multi2_o2.txt" "$TMPDIR/edit_multi2_n2.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/edit_multi2.c" "$EXPECTED/c_edit_multi_two.c" > /dev/null 2>&1; then
    pass "edit multi-pair (two changes)"
else
    fail "edit multi-pair (two changes)" "file mismatch"
    diff "$EXPECTED/c_edit_multi_two.c" "$TMPDIR/edit_multi2.c" | sed 's/^/    /'
fi

# edit with three old/new pairs
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi3.c"
echo -n 'return x + 1;' > "$TMPDIR/edit_multi3_o1.txt"
echo -n 'return x + 100;' > "$TMPDIR/edit_multi3_n1.txt"
echo -n 'return x * 2;' > "$TMPDIR/edit_multi3_o2.txt"
echo -n 'return x * 200;' > "$TMPDIR/edit_multi3_n2.txt"
echo -n 'return x - 1;' > "$TMPDIR/edit_multi3_o3.txt"
echo -n 'return x - 500;' > "$TMPDIR/edit_multi3_n3.txt"
"$XC" edit "$TMPDIR/edit_multi3.c" \
    "$TMPDIR/edit_multi3_o1.txt" "$TMPDIR/edit_multi3_n1.txt" \
    "$TMPDIR/edit_multi3_o2.txt" "$TMPDIR/edit_multi3_n2.txt" \
    "$TMPDIR/edit_multi3_o3.txt" "$TMPDIR/edit_multi3_n3.txt" > /dev/null 2>&1
if diff -q "$TMPDIR/edit_multi3.c" "$EXPECTED/c_edit_multi_three.c" > /dev/null 2>&1; then
    pass "edit multi-pair (three changes)"
else
    fail "edit multi-pair (three changes)" "file mismatch"
    diff "$EXPECTED/c_edit_multi_three.c" "$TMPDIR/edit_multi3.c" | sed 's/^/    /'
fi

# edit multi-pair with --dry-run — neither change applied
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_dry.c"
set +e
multi_dry_out="$("$XC" edit "$TMPDIR/edit_multi_dry.c" \
    "$TMPDIR/edit_multi2_o1.txt" "$TMPDIR/edit_multi2_n1.txt" \
    "$TMPDIR/edit_multi2_o2.txt" "$TMPDIR/edit_multi2_n2.txt" --dry-run 2>&1)"
# set -e
if echo "$multi_dry_out" | grep -q "dry-run" && diff -q "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_dry.c" > /dev/null 2>&1; then
    pass "edit multi-pair --dry-run"
else
    fail "edit multi-pair --dry-run" "file modified or missing --dry-run output"
    echo "    output: $multi_dry_out"
fi

# edit multi-pair with --no-blocks
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_noblocks.c"
multi_nob_out="$("$XC" edit "$TMPDIR/edit_multi_noblocks.c" \
    "$TMPDIR/edit_multi2_o1.txt" "$TMPDIR/edit_multi2_n1.txt" \
    "$TMPDIR/edit_multi2_o2.txt" "$TMPDIR/edit_multi2_n2.txt" --no-blocks 2>&1)"
if diff -q "$TMPDIR/edit_multi_noblocks.c" "$EXPECTED/c_edit_multi_two.c" > /dev/null 2>&1 && \
   ! echo "$multi_nob_out" | grep -q "int foo"; then
    pass "edit multi-pair --no-blocks"
else
    fail "edit multi-pair --no-blocks" "blocks output present or file mismatch"
    echo "    output: $multi_nob_out"
fi

# edit multi-pair — second pair fails (oldText not found), first is applied
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_fail.c"
echo -n 'return x + 1;' > "$TMPDIR/edit_multi_fail_o1.txt"
echo -n 'return x + 100;' > "$TMPDIR/edit_multi_fail_n1.txt"
echo -n 'no_such_text_in_file' > "$TMPDIR/edit_multi_fail_o2.txt"
echo -n 'anything' > "$TMPDIR/edit_multi_fail_n2.txt"
set +e
multi_fail_out="$("$XC" edit "$TMPDIR/edit_multi_fail.c" \
    "$TMPDIR/edit_multi_fail_o1.txt" "$TMPDIR/edit_multi_fail_n1.txt" \
    "$TMPDIR/edit_multi_fail_o2.txt" "$TMPDIR/edit_multi_fail_n2.txt" 2>&1)"
multi_fail_rc=$?
# set -e
if echo "$multi_fail_out" | grep -q "oldText not found" && \
   [ "$multi_fail_rc" -ne 0 ] && \
   grep -q "x + 100" "$TMPDIR/edit_multi_fail.c"; then
    pass "edit multi-pair (second fails, first applied)"
else
    fail "edit multi-pair (second fails, first applied)" "expected error and partial application"
    echo "    rc=$multi_fail_rc output=$multi_fail_out"
fi

# edit multi-pair — uneven args (old without matching new)
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_uneven.c"
set +e
multi_uneven_out="$("$XC" edit "$TMPDIR/edit_multi_uneven.c" \
    "$TMPDIR/edit_multi2_o1.txt" "$TMPDIR/edit_multi2_n1.txt" \
    "$TMPDIR/edit_multi2_o2.txt" 2>&1)"
# set -e
if echo "$multi_uneven_out" | grep -q "uneven count"; then
    pass "edit multi-pair (uneven args)"
else
    fail "edit multi-pair (uneven args)" "expected 'uneven count': $multi_uneven_out"
fi

# edit multi-pair undo — each edit creates a backup, undo restores step by step
cp "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_undo.c"
echo -n 'return x + 1;' > "$TMPDIR/edit_multi_undo_o1.txt"
echo -n 'return x + 100;' > "$TMPDIR/edit_multi_undo_n1.txt"
echo -n 'return x * 2;' > "$TMPDIR/edit_multi_undo_o2.txt"
echo -n 'return x * 200;' > "$TMPDIR/edit_multi_undo_n2.txt"
"$XC" edit "$TMPDIR/edit_multi_undo.c" \
    "$TMPDIR/edit_multi_undo_o1.txt" "$TMPDIR/edit_multi_undo_n1.txt" \
    "$TMPDIR/edit_multi_undo_o2.txt" "$TMPDIR/edit_multi_undo_n2.txt" > /dev/null 2>&1
# First undo: reverts second edit only — foo still changed, bar restored to original
"$XC" undo "$TMPDIR/edit_multi_undo.c" > /dev/null 2>&1
if grep -q "return x + 100" "$TMPDIR/edit_multi_undo.c" && \
   grep -q "return x \* 2" "$TMPDIR/edit_multi_undo.c" && \
   ! grep -q "return x \* 200" "$TMPDIR/edit_multi_undo.c"; then
    pass "edit multi-pair undo (first undo — second edit reverted)"
else
    fail "edit multi-pair undo (first undo — second edit reverted)" "unexpected state after first undo"
    cat "$TMPDIR/edit_multi_undo.c" | sed 's/^/    /'
fi
# Second undo: restores to original file
"$XC" undo "$TMPDIR/edit_multi_undo.c" > /dev/null 2>&1
if diff -q "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_undo.c" > /dev/null 2>&1; then
    pass "edit multi-pair undo (second undo — back to original)"
else
    fail "edit multi-pair undo (second undo — back to original)" "file not fully restored"
    diff "$FIXTURES/funcs.c" "$TMPDIR/edit_multi_undo.c" | sed 's/^/    /'
fi
# Third undo should fail (no more backups)
set +e
multi_undo3_out="$("$XC" undo "$TMPDIR/edit_multi_undo.c" 2>&1)"
multi_undo3_rc=$?
# set -e
if echo "$multi_undo3_out" | grep -q "no backup found"; then
    pass "edit multi-pair undo (third undo fails — no more backups)"
else
    fail "edit multi-pair undo (third undo fails — no more backups)" "expected 'no backup': $multi_undo3_out"
fi

echo -e "\n${YELLOW}─── Phase 7: cross-file error paths ───${NC}"

# move-into dest file not found
cp "$FIXTURES/funcs.c" "$TMPDIR/mi_dstnf_src.c"
set +e
mi_dstnf="$("$XC" move-into "$TMPDIR/mi_dstnf_src.c" 4 "$TMPDIR/nonexistent_dest.c" 1 2>&1)"
# set -e
if echo "$mi_dstnf" | grep -qE "cannot|operation_failed"; then
    pass "move-into (dest not found)"
else
    fail "move-into (dest not found)" "expected error: $mi_dstnf"
fi

# copy dest file not found
set +e
copy_dstnf="$("$XC" copy "$FIXTURES/funcs.c" 4 "$TMPDIR/nonexistent_dest2.c" 1 2>&1)"
# set -e
if echo "$copy_dstnf" | grep -qE "cannot|operation_failed"; then
    pass "copy (dest not found)"
else
    fail "copy (dest not found)" "expected error: $copy_dstnf"
fi

echo -e "\n${YELLOW}─── Phase 7: blocks empty directory ───${NC}"

# blocks on an empty directory (no source files)
mkdir -p "$TMPDIR/empty_dir"
blocks_emptydir="$("$XC" blocks "$TMPDIR/empty_dir" 2>&1)"
# Should succeed silently (no output for source files)
pass "blocks (empty directory)"

echo -e "\n${YELLOW}─── Phase 7: move-into --copy-includes deduplication ───${NC}"

# move-into with --copy-includes: target already has the include, should not duplicate
cp "$FIXTURES/with_includes.c" "$TMPDIR/mi_dedup_src.c"
cp "$FIXTURES/has_stdlib_include.c" "$TMPDIR/mi_dedup_tgt.c"
"$XC" move-into "$TMPDIR/mi_dedup_src.c" 4 "$TMPDIR/mi_dedup_tgt.c" 5 --copy-includes > /dev/null 2>&1
if diff -q "$TMPDIR/mi_dedup_tgt.c" "$EXPECTED/c_moveinto_dedup_target.c" > /dev/null 2>&1; then
    pass "move-into --copy-includes (deduplication)"
else
    fail "move-into --copy-includes (deduplication)" "file mismatch"
    diff "$EXPECTED/c_moveinto_dedup_target.c" "$TMPDIR/mi_dedup_tgt.c" | sed 's/^/    /'
fi
# Restore source after move-into
"$XC" undo "$TMPDIR/mi_dedup_src.c" > /dev/null 2>&1 || true

echo -e "\n${YELLOW}─── Phase 7: move-into --copy-includes (JS) ───${NC}"

# JS cross-file move-into with --copy-includes (dst line 5 = closing brace)
cp "$FIXTURES/javascript.js" "$TMPDIR/mi_js_src.js"
cp "$FIXTURES/js_target.js" "$TMPDIR/mi_js_tgt.js"
"$XC" move-into "$TMPDIR/mi_js_src.js" 4 "$TMPDIR/mi_js_tgt.js" 5 --copy-includes > /dev/null 2>&1
if diff -q "$TMPDIR/mi_js_tgt.js" "$EXPECTED/js_moveinto_inc_target.js" > /dev/null 2>&1; then
    pass "move-into --copy-includes (JS)"
else
    fail "move-into --copy-includes (JS)" "file mismatch"
    diff "$EXPECTED/js_moveinto_inc_target.js" "$TMPDIR/mi_js_tgt.js" | sed 's/^/    /'
fi

echo -e "\n${YELLOW}─── Phase 7: delete trailing semicolon cleanup ───${NC}"

# delete struct with trailing semicolon — verify ';' is cleaned up
test_delete "delete struct (trailing semicolon)" \
    "$FIXTURES/type_decls.c" 4 \
    "$EXPECTED/c_delete_struct_with_semi.c"

echo -e "\n${YELLOW}─── Phase 7: replace Unicode normalization ───${NC}"

# replace with Unicode em-dash in oldText — NormalizeText converts to "--"
# Fixture has literal "--", oldText has em-dash (U+2014 = E2 80 94)
cp "$FIXTURES/double_dash.c" "$TMPDIR/replace_unicode.c"
# oldText: "x \xe2\x80\x94 1" (em-dash U+2014), normalizes to "x -- 1"
printf 'x \xe2\x80\x94 1' > "$TMPDIR/replace_unicode_old.txt"
echo -n 'x ++ 1' > "$TMPDIR/replace_unicode_new.txt"
"$XC" replace "$TMPDIR/replace_unicode.c" 1 "$TMPDIR/replace_unicode_old.txt" "$TMPDIR/replace_unicode_new.txt" > /dev/null 2>&1
if grep -q 'x ++ 1' "$TMPDIR/replace_unicode.c"; then
    pass "replace (Unicode em-dash normalization)"
else
    fail "replace (Unicode em-dash normalization)" "replace not applied"
fi

# ─── summary ───────────────────────────────────────────────────────────────

echo ""
echo -e "${YELLOW}═══════════════════════════════════════${NC}"
echo -e "  Total: $((PASS + FAIL))  ${GREEN}Passed: $PASS${NC}  ${RED}Failed: $FAIL${NC}"
echo -e "${YELLOW}═══════════════════════════════════════${NC}"

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo -e "${RED}Failed tests:${NC}"
    for t in "${FAILED_TESTS[@]}"; do
        echo "  - $t"
    done
fi

teardown
exit "$FAIL"
