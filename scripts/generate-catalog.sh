#!/usr/bin/env bash
#
# Build a self-hosted OpenCPN plugin catalog for one channel (alpha/beta/
# prod), from the per-target metadata XML mayara_pi's own build already
# uploads to Cloudsmith as a "-metadata" raw package on every successful
# platform build -- nothing here regenerates or re-derives that XML, it is
# fetched verbatim.
#
# The result is a <plugins>-rooted catalog: a <meta-url> include pulling in
# the matching official OpenCPN/plugins channel (so pointing OpenCPN at ours
# does not hide every other plugin), followed by our own <plugin> entries.
# A target whose build failed this run (its -metadata package 404s) is
# skipped rather than published with a broken tarball-url.
#
# Usage: generate-catalog.sh <channel> <cloudsmith-repo> <cloudsmith-version> <package-version> <output-file>
#   channel:            alpha | beta | prod
#   cloudsmith-repo:     e.g. opencpn-radar-pi/mayara-beta
#   cloudsmith-version:  the Cloudsmith package version segment, e.g. v0.1.4
#                        (tagged builds) or 0.1.4.0+195.859b757 (alpha)
#   package-version:     the 4-part PACKAGE_VERSION baked into filenames,
#                        e.g. 0.1.4.0
#   output-file:         where to write the resulting catalog XML
set -euo pipefail

channel="$1"
repo="$2"
version="$3"
pkg_version="$4"
out="$5"

case "$channel" in
  alpha) meta_branch=Alpha ;;
  beta)  meta_branch=Beta ;;
  prod)  meta_branch=master ;;
  *) echo "generate-catalog.sh: unknown channel '$channel' (want alpha/beta/prod)" >&2; exit 1 ;;
esac

# Every target mayara_pi currently ships to the OpenCPN catalog. fedora is
# built and published like the others, but excluded here too: its metadata
# fails ocpn-plugin.xsd validation upstream (no "fedora" target enum), the
# same reason the hand-submitted catalog PRs have always left it out.
targets=(
  "darwin-wx32-arm64-x86_64-14.8.7-macos"
  "debian-arm64-12-bookworm"
  "debian-arm64-13-trixie"
  "debian-armhf-22.04-jammy"
  "debian-armhf-24.04-noble"
  "debian-x86_64-12-bookworm"
  "debian-x86_64-13-trixie"
  "debian-x86_64-22.04-jammy"
  "debian-x86_64-24.04-noble"
  "flatpak-aarch64-25.08-flatpak"
  "flatpak-x86_64-25.08-flatpak"
  "msvc-x86-wx32-10.0.20348-MSVC"
)

{
  echo '<?xml version="1.0" encoding="UTF-8"?>'
  echo '<plugins>'
  echo '  <version>1</version>'
  echo "  <date>$(date -u +%Y-%m-%d)</date>"
  echo '  <plugin version="1">'
  echo "    <meta-url>https://raw.githubusercontent.com/OpenCPN/plugins/${meta_branch}/ocpn-plugins.xml</meta-url>"
  echo '  </plugin>'

  for t in "${targets[@]}"; do
    name="mayara_pi-${pkg_version}-${t}-metadata"
    url="https://dl.cloudsmith.io/public/${repo}/raw/names/${name}/versions/${version}/mayara_pi-${pkg_version}-${t}.xml"
    tmp="$(mktemp)"
    # -f alone can't tell a real 404 (target didn't build this run) apart
    # from a 5xx/DNS/timeout/not-yet-synced-upload -- read the status code
    # instead, retry transient failures, and only skip on a confirmed 404.
    http_code=$(curl -sS -o "$tmp" -w '%{http_code}' \
      --retry 5 --retry-all-errors --retry-delay 3 \
      --connect-timeout 10 --max-time 60 \
      "$url" || echo 000)
    case "$http_code" in
      200) sed '/<?xml/d' "$tmp" ;;
      # $t is one of our own fixed target names above, safe to embed as-is.
      # $version is not: it can come straight from a git tag, which (unlike
      # an XML comment) is allowed to contain "--".
      404) echo "  <!-- $t: not published, skipped -->" ;;
      *)
        echo "generate-catalog.sh: $t: HTTP $http_code fetching $url" >&2
        rm -f "$tmp"
        exit 1
        ;;
    esac
    rm -f "$tmp"
  done

  echo '</plugins>'
} > "$out"

echo "Wrote $out (channel: $channel, version: $version)" >&2
