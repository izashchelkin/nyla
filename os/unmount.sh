set -euo pipefail

ROOT=/nyla

if [[ $EUID -ne 0 ]]; then
  echo "Run as root" >&2
  exit 1
fi

findmnt -rn -o TARGET \
  | awk -v root="$ROOT" '$0 == root || index($0, root "/") == 1 { print length($0), $0 }' \
  | sort -rn \
  | cut -d' ' -f2- \
  | while read -r target; do
      umount "$target" 2>/dev/null || umount -l "$target"
    done