set -e

export MANIFEST=$NYLAROOT/pkg/manifests/$1
echo > $MANIFEST

(cd /tmp/recipe_staging && find . -type f | sed 's|^\./||' | sort) > $MANIFEST

export PKGDB=$NYLAROOT/pkg/db
comm -12 <(cat $MANIFEST) <(cat $PKGDB) | grep . && (printf "\n\nFile conflicts detected!\n\n\n"; exit 1)

echo Copying package files...
rsync -av /tmp/recipe_staging/ $NYLAROOT/

# commit database
cat $PKGDB $MANIFEST > ${PKGDB}.tmp
sort ${PKGDB}.tmp > $PKGDB
rm ${PKGDB}.tmp
