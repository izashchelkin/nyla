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

    if ! (cd "$repo" && git init -q); then
        fail "$name" "git init failed"
        return
    fi

    set +e
    (cd "$repo" && "$XC" delete file.c 12 > delete.out 2>&1)
    local delete_rc=$?
    set -e
    if [ "$delete_rc" -ne 0 ]; then
        fail "$name" "delete failed"
        sed 's/^/    /' "$repo/delete.out"
        return
    fi

    if [ ! -f "$repo/.xcav_backups/.gitignore" ]; then
        fail "$name" ".xcav_backups/.gitignore was not created"
        return
    fi

    local status
    status="$(cd "$repo" && git status --porcelain --untracked-files=all .xcav_backups)"
    if echo "$status" | grep -qE '/[0-9][0-9][0-9]$'; then
        fail "$name" "backup payload is visible to git: $status"
        return
    fi
    if ! echo "$status" | grep -qF "?? .xcav_backups/.gitignore"; then
        fail "$name" "expected only .xcav_backups/.gitignore to be visible: $status"
        return
    fi

    set +e
    (cd "$repo" && "$XC" undo file.c > undo.out 2>&1)
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
set -e
if echo "$unknown_out" | grep -q "unsupported file type"; then
    pass "blocks (unknown extension)"
else
    fail "blocks (unknown extension)" "expected 'unsupported file type' error: $unknown_out"
fi

# No extension should fail
set +e
noext_out="$("$XC" blocks "$FIXTURES/noext" 2>&1)"
noext_rc=$?
set -e
if echo "$noext_out" | grep -q "unsupported file type"; then
    pass "blocks (no extension)"
else
    fail "blocks (no extension)" "expected 'unsupported file type': $noext_out"
fi

# File not found
set +e
notfound_out="$("$XC" blocks "$FIXTURES/nonexistent.c" 2>&1)"
set -e
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
set -e
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
set -e
if echo "$move_err2" | grep -q "contains the destination line"; then
    pass "move (dest inside block)"
else
    fail "move (dest inside block)" "expected contains destination: $move_err2"
fi

# Move file not found
set +e
move_err3="$("$XC" move "$FIXTURES/nonexistent.c" 1 2 2>&1)"
set -e
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
set -e
if echo "$del_err1" | grep -q "no structural block found"; then
    pass "delete (no block at line)"
else
    fail "delete (no block at line)" "expected error: $del_err1"
fi

# Delete file not found
set +e
del_err2="$("$XC" delete "$FIXTURES/nonexistent.c" 1 2>&1)"
set -e
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
set -e
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
set -e
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
set -e
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
set -e
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
set -e
if echo "$stdin_err" | grep -q "separator line not found"; then
    pass "edit (stdin no separator)"
else
    fail "edit (stdin no separator)" "expected separator error: $stdin_err"
fi

test_edit_stdin_long_oldtext_dry_run "edit stdin dry-run long oldText"
test_edit_stdin_large_source_dry_run "edit stdin dry-run large source"


# ─── read (extended) ─────────────────────────────────────────────────────

echo -e "\n${YELLOW}─── read (extended) ───${NC}"

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
set -e
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
set -e
if echo "$read_noblock" | grep -q "no block found"; then
    pass "read (no block at line)"
else
    fail "read (no block at line)" "expected error: $read_noblock"
fi

# read file not found
set +e
read_nofile="$("$XC" read "$FIXTURES/nonexistent.c" 1 2>&1)"
set -e
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
set -e
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
set -e
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
set -e
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
set -e
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
set -e
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
