#!/usr/bin/env bash

OLD_VERSION="$1"
NEW_VERSION="$2"

USAGE=$(cat <<-END
    	Usage: $0 OLD_VERSION NEW_VERSION
END
     )

if [[ -z $OLD_VERSION || -z $NEW_VERSION ]]
then
    echo $USAGE
    exit 1
fi

NEW_PACKAGE_DIR="$HOME/hibp_$NEW_VERSION-1_amd64"

sed -i "s/${OLD_VERSION//./\\.}/${NEW_VERSION//./\\.}/g" DEBIAN/control README.md

rm -rf $NEW_PACKAGE_DIR
mkdir -p $NEW_PACKAGE_DIR/DEBIAN/
cp DEBIAN/control $NEW_PACKAGE_DIR/DEBIAN/

git pull &&
    ./build.sh -c gcc -b release --purge --nopch --install --install-prefix=$NEW_PACKAGE_DIR/usr/local &&
    cd $NEW_PACKAGE_DIR &&
    mkdir debian &&
    touch debian/control &&
    DEPS="$(dpkg-shlibdeps -O usr/local/bin/hibp-* 2>/dev/null)" &&
    echo "Depends: ${DEPS/shlibs:Depends=/}" >> DEBIAN/control &&
    rm -rf debian/control &&
    cd $HOME &&
    dpkg-deb --build --root-owner-group $NEW_PACKAGE_DIR &&
    cd hibp

echo "package built, if all works nicely do this:"
echo
echo "git commit -m\"releasing new version $NEW_VERSION\"" DEBIAN/control README.md
echo "git tag v$NEW_VERSION"
echo "git push"
echo "git push origin --tags"
echo
echo "then create a new release on github and upload the .deb"
