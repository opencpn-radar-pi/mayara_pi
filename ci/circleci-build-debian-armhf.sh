#!/usr/bin/env bash

#
# Cross-compile the Debian/Ubuntu armhf (32-bit ARM) artifact on an x86_64
# runner: multiarch dev libraries + crossbuild-essential-armhf, no QEMU
# emulation of the actual build (only wx-config, invoked once by cmake to
# read target compiler/linker flags, runs under qemu-user-static/binfmt).
#
set -xe

sudo dpkg --add-architecture armhf

sudo apt-get -qq update
sudo apt-get install -y --no-install-recommends \
    devscripts equivs crossbuild-essential-armhf qemu-user-static

# Install extra build libs
ME=$(echo ${0##*/} | sed 's/\.sh//g')
EXTRA_LIBS=./ci/extras/extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    while read -r line; do
        sudo apt-get install $line
    done < "$EXTRA_LIBS"
fi
EXTRA_LIBS=./ci/extras/${ME}_extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    while read -r line; do
        sudo apt-get install $line
    done < "$EXTRA_LIBS"
fi

pwd

git submodule update --init opencpn-libs

# Foreign-arch Build-Depends (":armhf" library packages); host-arch tools
# (cmake, debhelper, ...) are left alone -- apt resolves both correctly.
sudo apt-get build-dep -y -a armhf ./ci/control

rm -rf build && mkdir build && cd build

tag=$(git tag --contains HEAD)
current_branch=$(git branch --show-current)

if [ -n "$tag" ] || [ "$current_branch" = "master" ]; then
  BUILD_TYPE=Release
else
  BUILD_TYPE=RelWithDebInfo
fi

cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_TOOLCHAIN_FILE=$(pwd)/../cmake/debian-armhf-toolchain.cmake ..

make -j2
make package
ls -l
