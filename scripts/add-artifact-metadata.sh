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

# grep without -q, so it reads the whole listing: with -q it can exit at the
# first match while tar is still writing, and pipefail then turns tar's
# SIGPIPE into a "no match".
has_metadata() {
  tar -tzf "$1" | grep -x 'metadata.xml' > /dev/null
}

if has_metadata "$tarball"; then
  echo "$tarball already has metadata.xml"
  exit 0
fi

# Build the new tarball in a scratch directory and only then replace the
# original, so a failure part way leaves the build's tarball as it was.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$xml" "$work/metadata.xml"
gzip -dc "$tarball" > "$work/plugin.tar" ||
  warn "could not unpack $tarball"
(cd "$work" && tar -rf plugin.tar metadata.xml && gzip -n plugin.tar) ||
  warn "could not add metadata.xml to $tarball"
has_metadata "$work/plugin.tar.gz" ||
  warn "metadata.xml still missing from the rebuilt $tarball"

# Some builds (armhf, flatpak) leave build/ owned by root.
sudo=""
if [ ! -w "$tarball" ]; then
  if [ "$(id -u)" != 0 ] && command -v sudo > /dev/null; then sudo=sudo; fi
fi
$sudo cp -f "$work/plugin.tar.gz" "$tarball"
echo "Added $xml to $tarball as metadata.xml"
