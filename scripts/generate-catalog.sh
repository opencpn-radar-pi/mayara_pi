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
# A target's upload is pushed with --no-wait-for-sync, so a 404 right after
# all CI jobs finish can just mean Cloudsmith's CDN hasn't caught up yet, not
# that the target failed to build -- so a 404 is retried for up to
# $sync_timeout before that target is finally skipped rather than published
# with a broken tarball-url.
#
# Usage: generate-catalog.sh <channel> <cloudsmith-repo> <cloudsmith-version> <package-version> <output-file> [darwin-version]
#   channel:            alpha | beta | prod
#   cloudsmith-repo:     e.g. opencpn-radar-pi/mayara-beta
#   cloudsmith-version:  the Cloudsmith package version segment, e.g. v0.1.4
#                        (tagged builds) or 0.1.4.0+195.859b757 (alpha)
#   package-version:     the 4-part PACKAGE_VERSION baked into filenames,
#                        e.g. 0.1.4.0
#   output-file:         where to write the resulting catalog XML
#   darwin-version:      the macOS runner's actual "sw_vers -productVersion"
#                        (from the macos-universal job's own output -- see
#                        PluginSetup.cmake's PKG_TARGET_VERSION, which bakes
#                        the same value into the package name). GitHub patches
#                        that runner's OS over time independently of this
#                        script, so a hardcoded value would silently fall out
#                        of sync and make every darwin target 404 forever
#                        (see below). Optional: omitted or empty skips the
#                        darwin target instead of guessing a version.
set -euo pipefail

channel="$1"
repo="$2"
version="$3"
pkg_version="$4"
out="$5"
darwin_version="${6:-}"

case "$channel" in
  alpha) meta_branch=Alpha ;;
  beta)  meta_branch=Beta ;;
  prod)  meta_branch=master ;;
  *) echo "generate-catalog.sh: unknown channel '$channel' (want alpha/beta/prod)" >&2; exit 1 ;;
esac

# Every target mayara_pi currently ships to the OpenCPN catalog, other than
# darwin (below) and fedora: fedora is built and published like the others,
# but excluded here too, since its metadata fails ocpn-plugin.xsd validation
# upstream (no "fedora" target enum) -- the same reason the hand-submitted
# catalog PRs have always left it out.
targets=(
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

# darwin's version segment tracks the macos-universal job's actual runner OS
# (see the darwin-version usage comment above) rather than being one more
# fixed entry above, since GitHub -- not this script or that workflow --
# controls when it changes.
darwin_skip_reason=""
if [ -n "$darwin_version" ]; then
  targets+=("darwin-wx32-arm64-x86_64-${darwin_version}-macos")
else
  darwin_skip_reason="macos-universal did not report a target version"
fi

# How long to keep retrying a 404 (Cloudsmith CDN sync lag) before giving up
# on a target, and how long to sleep between attempts. The catalog job has
# nothing else to do meanwhile, so this errs long.
sync_timeout=600
retry_interval=10

{
  echo '<?xml version="1.0" encoding="UTF-8"?>'
  echo '<plugins>'
  echo '  <version>1</version>'
  echo "  <date>$(date -u +%Y-%m-%d)</date>"
  echo '  <plugin version="1">'
  echo "    <meta-url>https://raw.githubusercontent.com/OpenCPN/plugins/${meta_branch}/ocpn-plugins.xml</meta-url>"
  echo '  </plugin>'

  if [ -n "$darwin_skip_reason" ]; then
    echo "  <!-- darwin: $darwin_skip_reason, skipped -->"
  fi

  for t in "${targets[@]}"; do
    name="mayara_pi-${pkg_version}-${t}-metadata"
    url="https://dl.cloudsmith.io/public/${repo}/raw/names/${name}/versions/${version}/mayara_pi-${pkg_version}-${t}.xml"
    deadline=$(( $(date +%s) + sync_timeout ))
    while :; do
      tmp="$(mktemp)"
      # -f alone can't tell a real 404 (target didn't build this run, or
      # hasn't synced to the CDN yet) apart from a 5xx/DNS/timeout -- read
      # the status code instead, let curl retry transient failures itself,
      # and only fall through to the 404 case on a confirmed one.
      http_code=$(curl -sS -o "$tmp" -w '%{http_code}' \
        --retry 5 --retry-all-errors --retry-delay 3 \
        --connect-timeout 10 --max-time 60 \
        "$url" || echo 000)
      if [ "$http_code" = 200 ]; then
        sed '/<?xml/d' "$tmp"
        rm -f "$tmp"
        break
      fi
      rm -f "$tmp"
      if [ "$http_code" != 404 ]; then
        echo "generate-catalog.sh: $t: HTTP $http_code fetching $url" >&2
        exit 1
      fi
      if [ "$(date +%s)" -ge "$deadline" ]; then
        # $t is one of our own fixed target names above, safe to embed as-is.
        # $version is not: it can come straight from a git tag, which (unlike
        # an XML comment) is allowed to contain "--".
        echo "  <!-- $t: not published, skipped -->"
        break
      fi
      wait_for="$retry_interval"
      remaining=$(( deadline - $(date +%s) ))
      if [ "$remaining" -lt "$wait_for" ]; then wait_for="$remaining"; fi
      echo "generate-catalog.sh: $t: not synced yet, retrying in ${wait_for}s..." >&2
      sleep "$wait_for"
    done
  done

  echo '</plugins>'
} > "$out"

echo "Wrote $out (channel: $channel, version: $version)" >&2
