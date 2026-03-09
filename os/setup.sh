pushd /nyla

mkdir -p dev
mount --bind /dev dev

mkdir -p proc
mount --bind /proc proc

mkdir -p sys
mount --bind /sys sys

ln -sf usr/bin bin
ln -sf usr/sbin sbin
ln -sf usr/lib lib
ln -sf usr/lib lib64
ln -sf lib lib64

mount -t tmpfs -o size=2G,mode=1777 tmpfs /nyla/tmp

popd
