#!/usr/bin/env bash
#
# Put the build's metadata XML into its tarball as metadata.xml, so the
# tarball a CI run uploads as an artifact can be installed with OpenCPN's
# Options -> Plugins -> Import plugin... -- which refuses a tarball without
# one ("missing metadata.xml").
#
# ci/cloudsmith-upload.sh does exactly this, in place, before it publishes,
# so the artifacts of runs that publish are importable already. It does not
# run on pull requests (no Cloudsmith key reaches them), and nothing else
# added the file, so a pull request's artifacts never imported. The XML's
# <tarball-url> keeps its --placeholders-- here: that URL is only for a
# catalog to download from, and an import reads the file it is given.
#
# A tarball that already has metadata.xml is left alone. Anything unexpected
# (no tarball, or more than one) is a warning, not a failure: this only makes
# an artifact more convenient, and should never fail a build.
#
# Usage: add-artifact-metadata.sh [build-dir]   (default: build)

set -euo pipefail

cd "${1:-build}"

warn() {
  echo "::warning::add-artifact-metadata: $*"
  exit 0
}

shopt -s nullglob
tarballs=(*.tar.gz)
xmls=(*.xml)
[ ${#tarballs[@]} -eq 1 ] || warn "expected one .tar.gz in $PWD, found ${#tarballs[@]}"
[ ${#xmls[@]} -eq 1 ] || warn "expected one .xml in $PWD, found ${#xmls[@]}"
tarball=${tarballs[0]}
xml=${xmls[0]}

if tar -tzf "$tarball" | grep -qx 'metadata.xml'; then
  echo "$tarball already has metadata.xml"
  exit 0
fi

# Some builds (armhf, flatpak) leave build/ owned by root.
sudo=""
if [ ! -w . ] || [ ! -w "$tarball" ]; then
  if [ "$(id -u)" != 0 ] && command -v sudo > /dev/null; then sudo=sudo; fi
fi

$sudo cp -f "$xml" metadata.xml
$sudo gunzip -f "$tarball"
$sudo tar -rf "${tarball%.gz}" metadata.xml
$sudo gzip -f "${tarball%.gz}"
$sudo rm -f metadata.xml

tar -tzf "$tarball" | grep -qx 'metadata.xml' ||
  warn "metadata.xml still missing from $tarball"
echo "Added $xml to $tarball as metadata.xml"
