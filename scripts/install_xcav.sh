#!/usr/bin/env bash
set -euo pipefail

cmake --build build/linux-release --target xcav
sudo cmake --install build/linux-release
