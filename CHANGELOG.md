# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **server:** let the plugin answer mayara-server's telemetry consent (#44) ([PR #44](https://github.com/opencpn-radar-pi/mayara_pi/pull/44))

### Fixed

- detect GIT_REPOSITORY from origin, not tracking-branch status (#42) ([PR #42](https://github.com/opencpn-radar-pi/mayara_pi/pull/42))
- **controls:** three bugs in the control panel (#49) ([PR #49](https://github.com/opencpn-radar-pi/mayara_pi/pull/49))
- **windows:** stop the radar and chart canvases erasing the control panel (#50) ([PR #50](https://github.com/opencpn-radar-pi/mayara_pi/pull/50))

## [0.1.1] - 2026-08-29

### Fixed

- **release:** make --release's commit non-empty (#36) ([PR #36](https://github.com/opencpn-radar-pi/mayara_pi/pull/36))

[Unreleased]: https://github.com/opencpn-radar-pi/mayara_pi/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/opencpn-radar-pi/mayara_pi/releases/tag/v0.1.1


# Changelog history (pre-automation)

Written by hand, before git-cliff started generating `CHANGELOG.md` from
commit messages. This file is appended to the bottom of the generated
`CHANGELOG.md` as history and is no longer updated.

Everything below predates the plugin's first tagged release, 0.1.0. Entries
are grouped by the pull request that landed them, because a squash merge made
the PR the unit of change here.

## Pre-0.1.0

### Added

- **Diagnostics page** (#21) — live readout of the heading and position being
  used and where each came from; heading source (automatic / OpenCPN only /
  radar only / fixed), COG-as-heading, staleness timeout, fixed position, and
  logging into OpenCPN's own log.
- **Chart overlay without OpenGL** (#21) — `RenderOverlayMultiCanvas(wxDC&, …)`
  over the same cached picture, so the overlay survives OpenCPN's hardware
  acceleration being switched off.
- **Orientation lozenge** on each picture (#21), per radar, click to cycle,
  naming the missing input when north-up or course-up cannot be honoured.
- **Echo colour profiles** (#18) — the server's own legend plus Red, Yellow,
  Green and Blue single-hue ramps, all copyable, renameable and editable. A
  palette re-colours the legend by role, so it fits any radar.
- **NMEA out to OpenCPN** (#18) — radar heading as HDT and radar targets as TTM,
  both off by default.
- **Per-canvas overlay assignment** (#17) — each chart canvas draws None, a
  named radar, or all of them; two radars on one canvas nest as annuli. Overlay
  opacity, guard zones on the chart, chart-scale-driven ranging and
  nest-at-quarter, the last two off by default.
- **The control panel over the chart** (#17), without a PPI window.
- **VRM/EBL markers** and the **guard-zone alarm** as OpenCPN notifications
  (#14).
- **Guard zones** drawn as the mayara GUI draws them, editable by dragging
  handles or typing numbers (#12, #15).
- **PPI improvements** (#9) — EBL/VRM, off-centre view, cursor readout, extreme
  range ring, refresh rate, reverse zoom, menu auto-hide.
- **Local mayara-server** (#7, #10) — download, run and stop one when there is
  no server on the network, tied to OpenCPN's lifetime; server settings page and
  mDNS discovery that works on macOS.
- **ARPA targets** rendered from the server, with click to acquire and cancel
  (#1, #2).
- **Display echo threshold** (#4) and **free display zoom** (#3).
- **arm64 flatpak packages** (#22) alongside x86_64.
- **CodeRabbit review configuration** (#20).

### Fixed

- **The flatpak package would not install** (#23). `WX_VER` put `-32` in the
  ABI string, so the metadata said `flatpak-32-aarch64` where OpenCPN expects
  `flatpak-aarch64`, and importing failed with "incompatible plugin detected".
- **Two capability flags were never declared** (#21). Without
  `WANTS_NMEA_EVENTS`, OpenCPN never sent the plugin a position fix at all — no
  position, COG, SOG or heading. Without `INSTALLS_CONTEXTMENU_ITEMS`,
  `PrepareContextMenu` was never called, so canvas menu labels never refreshed.
- **The non-GL overlay landed in the corner on HiDPI screens** (#21). The
  viewport is in physical pixels and a `wxDC` draws in logical points.
- **A heading of exactly 0° counted as "no heading"** (#21), and a missing
  heading drew the overlay at north instead of not at all.
- **Linux and flatpak builds broke** on `uint64_t` without `<cstdint>` (#21) —
  it arrived transitively on macOS and not on libstdc++.
- **Cancelling the settings dialog could crash OpenCPN** (#21): a heap timer
  owned by a stack dialog kept ticking into a destroyed handler.
- **Targets never appeared when connected through a Signal K server** (#18).
  The bridge republishes controls but not targets; the client now falls back to
  the REST target list when no delta arrives.
- **Guard zones saved as zeroes** (#15). `%g` follows `LC_NUMERIC`, which writes
  a decimal comma in half of Europe and produced invalid JSON.
- **The chart overlay was drawn `cos(lat)` too small** (#8) — 40% short at 53°N.
  `view_scale_ppm` is pixels per *Mercator* metre, not per ground metre.
- **mDNS discovery found nothing on macOS** (#10), where mDNSResponder owns port
  5353 and a plugin's own socket never sees the answers.
