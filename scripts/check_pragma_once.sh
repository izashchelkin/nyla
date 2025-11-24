#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-.}"

if [[ ! -d "$ROOT_DIR" ]]; then
  echo "Error: '$ROOT_DIR' is not a directory" >&2
  exit 2
fi

status=0

while IFS= read -r -d '' f; do
  if ! grep -q '^#pragma once' "$f"; then
    echo "Missing #pragma once: $f" >&2
    status=1
  fi
done < <(find "$ROOT_DIR" \( -name '*.h' -o -name '*.hpp' -o -name '*.hh' -o -name '*.hxx' \) -print0)

exit "$status"