#!/bin/sh
bazel build //nyla/wm --compilation_mode=dbg && mv -f ~/wm ~/wmold ; cp ./bazel-out/k8-dbg/bin/nyla/wm/wm ~/wm
