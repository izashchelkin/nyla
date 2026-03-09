set -ex

bash pkgbuild.sh "$1"
bash pkginstall.sh "$1"