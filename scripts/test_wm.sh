#!/bin/bash
# Build and run WM integration tests (headless, Xvfb :98).
# Usage: ./scripts/test_wm.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"

echo "=== Building WM and tests ==="
cmake --build build/linux-debug --target wm wm_test

echo ""
echo "=== Running WM integration tests ==="
./build/linux-debug/bin/wm_test
