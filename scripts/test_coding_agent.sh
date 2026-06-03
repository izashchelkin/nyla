#!/bin/bash
# ─── Test the coding agent with local Ollama ─────────────────────────────────
# Ensures the Ollama server is running, then runs the agent with a test prompt.
# Usage: ./scripts/test_coding_agent.sh [prompt]
#   Default prompt: "Say hello in one short sentence."
#
# Environment variables:
#   CODING_AGENT_MODEL — override the model name (default: qwen2.5-coder:1.5b)
#   CODING_AGENT_USE_STUB — set to 1 to use the stub provider (no LLM needed)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO_ROOT/build/linux-debug/bin/coding_agent"
PROMPT="${1:-Say hello in one short sentence.}"

# ─── Check prerequisites ────────────────────────────────────────────────────

if [[ ! -f "$BIN" ]]; then
    echo "❌ coding_agent binary not found. Build it first:"
    echo "   cmake --build build/linux-debug --target coding_agent"
    exit 1
fi

# ─── Ollama server check ────────────────────────────────────────────────────

if ! curl -s http://localhost:11434/api/tags > /dev/null 2>&1; then
    echo "⚠️  Ollama server not running. Starting ollama serve..."
    ollama serve &>/tmp/ollama-server.log &
    OLLAMA_PID=$!
    sleep 2
    if ! curl -s http://localhost:11434/api/tags > /dev/null 2>&1; then
        echo "❌ Failed to start Ollama server. Check /tmp/ollama-server.log"
        exit 1
    fi
    echo "✅ Ollama server started (pid $OLLAMA_PID)"
else
    echo "✅ Ollama server is running"
fi

# Check if the model is available
MODEL="${CODING_AGENT_MODEL:-qwen2.5-coder:1.5b}"
if ! ollama list 2>/dev/null | grep -q "$MODEL"; then
    echo "📥 Pulling model: $MODEL ..."
    ollama pull "$MODEL"
    echo "✅ Model pulled: $MODEL"
fi

# ─── Run the agent ──────────────────────────────────────────────────────────

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🧪 Testing coding agent with Ollama ($MODEL)"
echo "   Prompt: $PROMPT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Send the prompt + exit via stdin, capture output
printf '%s\nexit\n' "$PROMPT" | timeout 60 "$BIN" 2>&1

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ Test complete"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
