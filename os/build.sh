set -e

export LC_ALL=C
export PREFIX=/usr

export MAKEFLAGS="-j$(nproc)"
export FORCE_UNSAFE_CONFIGURE=1

export M4=/usr/bin/m4
export SED=/usr/bin/sed
export PERL=/usr/bin/perl

export DESTDIR=/tmp/recipe_staging
rm -rf $DESTDIR
mkdir -p $DESTDIR

bash $1 
