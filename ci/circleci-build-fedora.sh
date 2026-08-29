#!/bin/sh  -xe

#
# Build the mingw artifacts inside the Fedora container
#
set -xe

su -c "dnf install -y sudo dnf-plugins-core"
# Fedora dropped LSB tooling years ago, but PluginSetup.cmake still shells
# out to `lsb_release -rs`/`-is` for PKG_TARGET_VERSION/PKG_TARGET (works on
# Debian/Ubuntu, which ship it by default). Without it, both come back
# empty and the packaged tarball's name is missing its version segment.
sudo dnf install -y redhat-lsb-core
sudo dnf builddep  -y ci/opencpn-fedora.spec
rm -rf build; mkdir build; cd build
cmake ..
make -j2
make package
