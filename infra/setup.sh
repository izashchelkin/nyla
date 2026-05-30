#!/bin/bash
# Setup script for ihorzash.com + git.ihorzash.com on Fedora 43+ VPS
# Run as root. Dependencies: dnf, systemd, caddy, gcc, make, git, python3.
# ─── Install packages ───
set -euo pipefail

dnf install -y caddy gcc make git autoconf automake libtool \
               openssl-devel zlib-devel fcgi-devel \
               libcurl-devel luajit-devel pkg-config

# ─── Build fcgiwrap (not in Fedora repos — tiny, no deps beyond fcgi-devel) ───
# Source tarball must be copied to /tmp first (github.com is blocked from VPS).
# scp fcgiwrap-1.1.0.tar.gz root@host:/tmp/ before running.
if [ ! -f /tmp/fcgiwrap-1.1.0.tar.gz ]; then
    echo "Missing /tmp/fcgiwrap-1.1.0.tar.gz — copy it first"
    exit 1
fi
cd /tmp && tar xzf fcgiwrap-1.1.0.tar.gz && cd fcgiwrap-1.1.0
autoreconf -i
./configure
make -j$(nproc) CFLAGS='-std=gnu99 -Wall -Wextra -pedantic -O2 -g3 -Wno-implicit-fallthrough'
make install

# ─── Clone git + cgit sources ───
# git
cd /tmp && rm -rf git-2.54.0
git clone --depth 1 --branch v2.54.0 git://git.kernel.org/pub/scm/git/git.git git-2.54.0
cd git-2.54.0 && make configure && ./configure && make -j$(nproc) libgit.a

# cgit
cd /tmp && rm -rf cgit-latest
git clone --depth 1 git://git.zx2c4.com/cgit cgit-latest
cd cgit-latest
rm -rf git
cp -rl /tmp/git-2.54.0 git
sed -i 's|^CGIT_SCRIPT_PATH.*|CGIT_SCRIPT_PATH = /var/www/cgit|' Makefile
sed -i 's|^CGIT_DATA_PATH.*|CGIT_DATA_PATH = /var/www/cgit|' Makefile
make -j$(nproc) NO_LUA=1 prefix=/usr
make install NO_LUA=1 prefix=/usr

# ─── Copy static assets ───
mkdir -p /var/www/cgit
cp -f cgit.css cgit.png cgit.js favicon.ico robots.txt /var/www/cgit/ 2>/dev/null || true

# ─── Deploy config files ───
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cp "$SCRIPT_DIR/Caddyfile" /etc/caddy/Caddyfile
cp "$SCRIPT_DIR/cgitrc" /etc/cgitrc
cp "$SCRIPT_DIR/cgit-bridge.py" /usr/local/bin/cgit-bridge
chmod +x /usr/local/bin/cgit-bridge
cp "$SCRIPT_DIR/cgit-bridge.service" /etc/systemd/system/cgit-bridge.service
cp "$SCRIPT_DIR/index.html" /var/www/ihorzash.com/index.html

# ─── Permissions ───
chown -R caddy:caddy /var/www/cgit /var/www/ihorzash.com
chmod go+x /home/ihorz
chmod -R o+rX /home/ihorz/nyla

# ─── Start services ───
systemctl daemon-reload
systemctl enable --now cgit-bridge
systemctl enable --now caddy
caddy reload 2>/dev/null || systemctl reload caddy

echo "Done. Check: https://ihorzash.com + https://git.ihorzash.com"
