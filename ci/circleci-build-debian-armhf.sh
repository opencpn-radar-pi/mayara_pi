#!/usr/bin/env bash

#
# Cross-compile the Debian/Ubuntu armhf (32-bit ARM) artifact on an x86_64
# runner: multiarch dev libraries + crossbuild-essential-armhf. No QEMU: the
# armhf wx-config that cmake shells out to for target compiler/linker flags
# is itself a shell script, not an ELF binary, so it just runs.
#
set -xe

# Ubuntu doesn't mirror armhf (or any non-x86 arch) on archive.ubuntu.com /
# security.ubuntu.com -- those only ever carried amd64/i386. armhf lives on
# the separate ports.ubuntu.com archive, so the default sources need
# restricting to amd64 and a matching armhf source added, or `apt-get
# update` 404s on every armhf index once the architecture is added.
suite="$OCPN_TARGET"
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
    # deb822 format (Ubuntu 24.04+, e.g. the bare GitHub runner for noble).
    sudo sed -i '/^Types: deb$/a Architectures: amd64' \
        /etc/apt/sources.list.d/ubuntu.sources
    cat <<EOF | sudo tee /etc/apt/sources.list.d/armhf.sources > /dev/null
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports
Suites: $suite $suite-updates $suite-security $suite-backports
Components: main universe restricted multiverse
Architectures: armhf
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
else
    # One-line sources.list format (Ubuntu 22.04 and earlier, e.g. the
    # ubuntu:22.04 container for jammy).
    sudo sed -i -E "s#^deb (http://(archive|security)\.ubuntu\.com\S*)#deb [arch=amd64] \1#" \
        /etc/apt/sources.list
    for c in main restricted universe multiverse; do
        for s in "$suite" "$suite-updates" "$suite-security"; do
            echo "deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports $s $c"
        done
    done | sudo tee -a /etc/apt/sources.list > /dev/null
fi

sudo dpkg --add-architecture armhf

sudo apt-get -qq update
sudo apt-get install -y --no-install-recommends \
    devscripts equivs crossbuild-essential-armhf

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

# `apt-get build-dep -a` only accepts a directory containing debian/control,
# not an arbitrary control file path -- stage a copy there. python3-pip is
# dropped: it's "Architecture: all" but (unlike python3-setuptools) lacks a
# "Multi-Arch: foreign" tag, so cross build-dep resolution looks for a
# nonexistent "python3-pip:armhf" and fails the whole transaction; it isn't
# actually used by this plugin's build, only by the FE2 template ci/control
# shares with the native (non-cross) jobs, which resolve it natively fine.
mkdir -p /tmp/build-dep/debian
grep -v '^ python3-pip,\?$' ./ci/control > /tmp/build-dep/debian/control
sudo apt-get build-dep -y -a armhf /tmp/build-dep

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
