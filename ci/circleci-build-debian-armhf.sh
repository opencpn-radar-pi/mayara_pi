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
# GitHub-hosted runners ship a lot of pre-installed amd64 packages. A
# Multi-Arch: foreign package (e.g. libglib2.0-dev-bin) that a fresh
# armhf :dev package depends on at an exact version won't be pulled in as
# a foreign-arch match if the already-installed host copy is older and
# build-dep doesn't upgrade it on its own -- so bring the host up to date
# first, before it can conflict with an exact-version armhf dependency.
#
# The upgrade touches every pre-installed package, including runner
# bloatware this build never needs (browsers, mainly) whose maintainer
# scripts make their own network calls and have hit transient failures
# there (e.g. a snapcraft.io assertion fetch timing out). Hold the ones
# that don't matter here, and retry the upgrade itself in case something
# else hits a similar transient blip.
sudo apt-mark hold firefox google-chrome-stable microsoft-edge-stable 2>/dev/null || true
for attempt in 1 2 3; do
    if sudo apt-get -y upgrade; then
        break
    fi
    echo "apt-get upgrade failed (attempt $attempt/3), retrying..." >&2
    [ "$attempt" -eq 3 ] && exit 1
    sudo dpkg --configure -a || true
    sleep 10
done
sudo apt-get install -y --no-install-recommends \
    devscripts equivs crossbuild-essential-armhf python3-pip
# On the bare noble runner (not the jammy container), something about the
# upgrade + the armhf package transaction above has been leaving
# `python3 -m venv` broken ("ensurepip is not available") for the later
# Cloudsmith publish step. Not reproducible from a clean image, so whatever
# it is, just make sure venv support is actually there afterward.
sudo apt-get install -y --reinstall python3-venv

# jammy's own cmake (3.22) has a FindwxWidgets bug: it comes back with
# wxWidgets_LIBRARIES empty even though `wx-config --libs` (which it does
# call, correctly) prints the right flags -- verified by running the exact
# same configure with a newer cmake, which resolves wxWidgets fine. Pull a
# current cmake from PyPI rather than chase the cmake-side bug.
# --break-system-packages (needed on noble's PEP 668-enforcing pip) doesn't
# exist on jammy's older pip, which errors on an unrecognized option.
if pip3 install --help 2>&1 | grep -q -- --break-system-packages; then
    pip3 install --quiet --break-system-packages "cmake>=3.28"
else
    pip3 install --quiet "cmake>=3.28"
fi
export PATH="$HOME/.local/bin:$PATH"
cmake --version

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
