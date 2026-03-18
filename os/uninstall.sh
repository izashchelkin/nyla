set -e

export MANIFEST=$NYLAROOT/pkg/manifests/$1

export PKGDB=$NYLAROOT/pkg/db
comm -23 $PKGDB $MANIFEST > ${PKGDB}.tmp

while IFS= read -r file_path; do
    [ -z "$file_path" ] && continue
    rm -vf "$NYLAROOT/$file_path"
done < $MANIFEST

rm $MANIFEST
sort ${PKGDB}.tmp > $PKGDB
