#!/bin/bash
# e2e integration tests for coding_agent against Ollama
# Requires: ollama serve running, qwen3:4b model available
# Usage: ./scripts/test_ca.sh

set -euo pipefail
cd "$(dirname "$0")/.."

CA_BIN="./build/linux-debug/bin/coding_agent"
TIMEOUT=60
PASS=0
FAIL=0

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; RESET='\033[0m'
pass() { echo -e "  ${GREEN}PASS${RESET} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}FAIL${RESET} $1: $2"; FAIL=$((FAIL+1)); }
skip() { echo -e "  ${YELLOW}SKIP${RESET} $1: $2"; }

# Check prerequisites
if ! curl -s http://localhost:11434/api/tags >/dev/null 2>&1; then
    echo "Ollama not running - all tests skipped"; exit 0
fi
if ! curl -s http://localhost:11434/api/tags | grep -q 'qwen3:4b'; then
    echo "qwen3:4b not available - all tests skipped"; exit 0
fi
if [ ! -x "$CA_BIN" ]; then
    echo "coding_agent binary not found at $CA_BIN"; exit 1
fi

echo "Running coding_agent e2e tests with qwen3:4b..."
echo ""

run_test() {
    local name="$1" prompt="$2" pattern="$3"
    local output
    if output=$(echo "$prompt" | timeout "$TIMEOUT" "$CA_BIN" 2>&1 | sed 's/\x1b\[[0-9;]*m//g'); then
        if echo "$output" | grep -q "$pattern"; then
            pass "$name"
        else
            fail "$name" "pattern not found"
            echo "    Output: $(echo "$output" | tr '\n' ' ' | head -c 200)"
        fi
    else
        local rc=$?
        if [ $rc -eq 124 ]; then
            fail "$name" "timed out"
        else
            fail "$name" "exit code $rc"
        fi
    fi
}

run_test "basic text response" \
    "say hello in one short sentence" \
    "[Hh]ello"

run_test "xcav_blocks tool (trigger + execute + respond)" \
    "list blocks in coding_agent/agent_loop.cc" \
    "\[xcav_blocks\] OK"

run_test "bash tool (trigger + execute + respond)" \
    "run: ls coding_agent/" \
    "\[bash\] OK"

run_test "read_file tool (trigger + execute + respond)" \
    "read the file coding_agent/TODO.md and show the first line" \
    "\[read_file\] OK"

echo ""
echo "Results: ${GREEN}$PASS passed${RESET}, ${RED}$FAIL failed${RESET}"
if [ $FAIL -gt 0 ]; then exit 1; fi
