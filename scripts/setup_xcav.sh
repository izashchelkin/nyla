#!/bin/bash
# Clone vendored tree-sitter dependencies for xcav.
# Run once after fresh clone: ./scripts/setup_xcav.sh

set -e
cd "$(dirname "$0")/.."

echo "=== Cloning tree-sitter dependencies into vendor/ ==="

clone_dep() {
    local name=$1 url=$2
    if [ -d "vendor/$name" ]; then
        echo "  vendor/$name — already exists, skipping"
    else
        echo "  vendor/$name — cloning..."
        git clone --depth 1 "$url" "vendor/$name"
    fi
}

clone_dep tree-sitter    https://github.com/tree-sitter/tree-sitter
clone_dep tree-sitter-c  https://github.com/tree-sitter/tree-sitter-c
clone_dep tree-sitter-cpp https://github.com/tree-sitter/tree-sitter-cpp
clone_dep tree-sitter-java https://github.com/tree-sitter/tree-sitter-java

echo "=== Done. xcav dependencies ready. ==="
echo "Build: cmake --preset linux-debug && cmake --build build/linux-debug --target xcav"
