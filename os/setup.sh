set -euo pipefail

ROOT=/nyla

if [[ $EUID -ne 0 ]]; then
  echo "Run as root" >&2
  exit 1
fi

mkdir -p \
  "$ROOT/dev" \
  "$ROOT/etc" \
  "$ROOT/proc" \
  "$ROOT/run" \
  "$ROOT/sys" \
  "$ROOT/tmp" \
  "$ROOT/usr/bin" \
  "$ROOT/usr/lib" \
  "$ROOT/var"

is_mounted() {
  mountpoint -q "$1"
}

if ! is_mounted "$ROOT/dev"; then
  mount --rbind /dev "$ROOT/dev"
  mount --make-rslave "$ROOT/dev"
fi

if ! is_mounted "$ROOT/proc"; then
  mount --rbind /proc "$ROOT/proc"
  mount --make-rslave "$ROOT/proc"
fi

if ! is_mounted "$ROOT/sys"; then
  mount --rbind /sys "$ROOT/sys"
  mount --make-rslave "$ROOT/sys"
fi

if ! is_mounted "$ROOT/run"; then
  mount --rbind /run "$ROOT/run"
  mount --make-rslave "$ROOT/run"
fi

if ! is_mounted "$ROOT/tmp"; then
  mount -t tmpfs -o mode=1777,nodev,nosuid tmpfs "$ROOT/tmp"
fi

cat > "$ROOT/etc/resolv.conf" <<'EOF'
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

ln -sfn usr/bin "$ROOT/bin"
ln -sfn usr/bin "$ROOT/sbin"
ln -sfn bin "$ROOT/usr/sbin"

ln -sfn usr/lib "$ROOT/lib"
ln -sfn usr/lib "$ROOT/lib64"
ln -sfn lib "$ROOT/usr/lib64"