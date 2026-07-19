#!/usr/bin/env bash
#
# Assemble an OpenCPN-importable plugin tarball on macOS:
#   1. cpack TGZ  (payload: OpenCPN.app/Contents/PlugIns/libmayara_pi.dylib)
#   2. fill the metadata.xml placeholders
#   3. inject metadata.xml at the tarball root (as the catalog format requires)
#
# Usage: tools/mac-package.sh [build-dir]
set -euo pipefail

BUILD_DIR="${1:-build}"
cd "$BUILD_DIR"

# CPack produces <PACKAGING_NAME_XML>.tar.gz and the matching .xml.
make package >/dev/null
BASE="$(ls -t *.tar.gz | grep -v -- '-tarball' | head -1)"
BASE="${BASE%.tar.gz}"
[ -n "$BASE" ] || { echo "no CPack tarball found" >&2; exit 1; }

# Fill placeholders. tarball-url is irrelevant for a local import but keep the
# xml well-formed.
sed -e 's#--pkg_repo--#opencpn-radar-pi/mayara-alpha#' \
    -e "s#--name--#${BASE}-tarball#" \
    -e 's#--version--#0.1.0.0#' \
    -e "s#--filename--#${BASE}.tar.gz#" \
    "${BASE}.xml" > metadata.xml

# Inject metadata.xml at the tar root.
cp -f "${BASE}.tar.gz" _work.tar.gz
gunzip -f _work.tar.gz
tar -rf _work.tar metadata.xml
gzip -f _work.tar
mv -f _work.tar.gz "${BASE}.tar.gz"

echo "Importable tarball:"
echo "  $(pwd)/${BASE}.tar.gz"
echo "Load via OpenCPN → Options → Plugins → Import plugin…"
