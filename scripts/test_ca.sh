#!/bin/bash
# e2e integration tests for coding_agent against ChatGPT Codex
# Requires: ChatGPT subscription token in ~/.pi/agent/auth.json
# Usage: ./scripts/test_ca.sh

set -euo pipefail
cd "$(dirname "$0")/.."

CA_BIN="./build/linux-debug/bin/coding_agent"
TIMEOUT=120
PASS=0
FAIL=0

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; RESET='\033[0m'
pass() { echo -e "  ${GREEN}PASS${RESET} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}FAIL${RESET} $1: $2"; FAIL=$((FAIL+1)); }

# Check binary exists
if [ ! -x "$CA_BIN" ]; then
    echo "coding_agent binary not found at $CA_BIN"; exit 1
fi

# ─── Credentials from pi auth ──────────────────────────────────────────────────

AUTH_FILE="$HOME/.pi/agent/auth.json"
if [ ! -f "$AUTH_FILE" ] || ! command -v python3 &>/dev/null; then
    echo "No auth.json or python3 — skipping integration tests."; exit 0
fi

OPENAI_CODEX_TOKEN=$(python3 -c "
import json, base64
with open('$AUTH_FILE') as f:
    auth = json.load(f)
print(auth.get('openai-codex', {}).get('access', ''))
")

if [ -z "$OPENAI_CODEX_TOKEN" ]; then
    echo "No openai-codex token — skipping integration tests."; exit 0
fi

OPENAI_CODEX_ACCOUNT_ID=$(python3 -c "
import json, base64
with open('$AUTH_FILE') as f:
    auth = json.load(f)
token = auth.get('openai-codex', {}).get('access', '')
if token:
    parts = token.split('.')
    if len(parts) >= 2:
        payload = json.loads(base64.urlsafe_b64decode(parts[1] + '=='))
        print(payload.get('https://api.openai.com/auth', {}).get('chatgpt_account_id', ''))
")

export OPENAI_CODEX_TOKEN OPENAI_CODEX_ACCOUNT_ID
echo "Using ChatGPT Codex (account: ${OPENAI_CODEX_ACCOUNT_ID:0:8}...)"

# ─── Helpers ───────────────────────────────────────────────────────────────────

run_test() {
    local name="$1" prompt="$2" pattern="$3"
    local output rc=0
    if output=$(echo "$prompt" | timeout "$TIMEOUT" "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g'); then
        if echo "$output" | grep -q "$pattern"; then
            pass "$name"
        else
            fail "$name" "pattern not found"
            echo "    Output: $(echo "$output" | tr '\n' ' ' | head -c 300)"
        fi
    else
        rc=$?
        if [ $rc -eq 124 ]; then
            fail "$name" "timed out"
        else
            fail "$name" "exit code $rc"
        fi
    fi
}

# ─── Error-handling tests ──────────────────────────────────────────────────────

echo "─── Error handling tests ───"
echo ""

OUTPUT=$(echo "" | timeout 5 "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true)
echo "$OUTPUT" | grep -qi "goodbye" && pass "empty input (Ctrl-D)" || fail "empty input (Ctrl-D)" ""

OUTPUT=$(echo "exit" | timeout 5 "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true)
echo "$OUTPUT" | grep -qi "goodbye" && pass "exit command" || fail "exit command" ""

OUTPUT=$(echo "quit" | timeout 5 "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true)
echo "$OUTPUT" | grep -qi "goodbye" && pass "quit command" || fail "quit command" ""

OUTPUT=$(OPENAI_CODEX_TOKEN="" OPENAI_CODEX_ACCOUNT_ID="" echo "hello" | timeout 10 "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true)
RC=$?
[ $RC -ne 139 ] && [ -n "$OUTPUT" ] && pass "no token — no crash" || fail "no token" "exit $RC"
echo "$OUTPUT" | grep -qi "token\|not set" && pass "no token — error message" || fail "no token" "no error msg"

echo ""

# ─── Integration tests ─────────────────────────────────────────────────────────

echo "─── Integration tests (gpt-5.4-mini via Codex) ───"
echo ""

# Test /model command
OUTPUT=$(echo "/model" | timeout 10 "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true)
if echo "$OUTPUT" | grep -q "Available models"; then
    pass "/model lists models"
else
    fail "/model lists models" "pattern not found"
    echo "    Output: $(echo "$OUTPUT" | tr '\n' ' ' | head -c 200)"
fi

run_test "basic text response" \
    "say hello in one short sentence" \
    "[Hh]ello"

run_test "xcav_blocks tool" \
    "list blocks in coding_agent/agent_loop.cc" \
    "\[xcav_blocks\] OK"

run_test "bash tool" \
    "run: ls coding_agent/" \
    "\[bash\] OK"

run_test "read_file tool" \
    "use the read_file tool to read coding_agent/TODO.md and show the first line" \
    "\[read_file\] OK"

run_test "multi-turn" \
    "first run xcav_blocks on coding_agent/main.cc, then tell me what you found" \
    "\[xcav_blocks\] OK"

# Tool failure fork — triggers the fork repair path when xcav_read fails.
# The fork uses its own memory-pool chunk (not a 4096-byte stack buffer).
# Verifies the process doesn't crash (SIGILL=132, SIGSEGV=139) when the
# fork formats a system message with the tool failure details.
OUTPUT=$(echo "use xcav_read on file: coding_agent/definitely_missing_file_xyz.cc" | timeout 60 "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' || true)
RC=$?
if [ $RC -ne 132 ] && [ $RC -ne 139 ] && [ -n "$OUTPUT" ]; then
    pass "tool failure fork — no crash"
else
    fail "tool failure fork" "exit $RC (132=SIGILL, 139=SIGSEGV)"
fi

echo ""
echo "Results: ${GREEN}$PASS passed${RESET}, ${RED}$FAIL failed${RESET}"
[ $FAIL -gt 0 ] && exit 1 || exit 0
