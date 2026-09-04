# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **server:** let the plugin answer mayara-server's telemetry consent (#44) ([PR #44](https://github.com/opencpn-radar-pi/mayara_pi/pull/44))
- **server:** point at mayara-server's own GUI when no radar is found (#57) ([PR #57](https://github.com/opencpn-radar-pi/mayara_pi/pull/57))
- **changelog:** add an in-app changelog viewer (#88) ([PR #88](https://github.com/opencpn-radar-pi/mayara_pi/pull/88))

### Changed

- **changelog:** backfill 0.1.4-to-present, lost to the commit filter (#91) ([PR #91](https://github.com/opencpn-radar-pi/mayara_pi/pull/91))

### Fixed

- detect GIT_REPOSITORY from origin, not tracking-branch status (#42) ([PR #42](https://github.com/opencpn-radar-pi/mayara_pi/pull/42))
- **controls:** three bugs in the control panel (#49) ([PR #49](https://github.com/opencpn-radar-pi/mayara_pi/pull/49))
- **windows:** stop the radar and chart canvases erasing the control panel (#50) ([PR #50](https://github.com/opencpn-radar-pi/mayara_pi/pull/50))
- **i18n:** build the sources as UTF-8 and keep non-ASCII out of msgids (#55) ([PR #55](https://github.com/opencpn-radar-pi/mayara_pi/pull/55))
- **windows:** antialias and DPI-scale the hand-drawn chrome (#56) ([PR #56](https://github.com/opencpn-radar-pi/mayara_pi/pull/56))
- **windows:** survive the VS2017-era C++ runtime OpenCPN ships (#60) ([PR #60](https://github.com/opencpn-radar-pi/mayara_pi/pull/60))
- **overlay:** PPI show/hide, range-auto oscillation, per-radar Range Auto (#62) ([PR #62](https://github.com/opencpn-radar-pi/mayara_pi/pull/62))
- **server:** only link to a mayara GUI that is there, and keep its log (#64) ([PR #64](https://github.com/opencpn-radar-pi/mayara_pi/pull/64))
- **ui:** scale for DPI, repaint relabelled buttons, unify bearing readouts (#80) ([PR #80](https://github.com/opencpn-radar-pi/mayara_pi/pull/80))
- GTK Controls-panel corruption, pinned title row, close-button race (#93) ([PR #93](https://github.com/opencpn-radar-pi/mayara_pi/pull/93))

## [0.1.1] - 2026-08-29

### Fixed

- **release:** make --release's commit non-empty (#36) ([PR #36](https://github.com/opencpn-radar-pi/mayara_pi/pull/36))

[Unreleased]: https://github.com/opencpn-radar-pi/mayara_pi/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/opencpn-radar-pi/mayara_pi/releases/tag/v0.1.1


# Changelog history (manual)

Entries the automated pipeline (git-cliff, `cliff.toml`) cannot produce:
either pre-automation history, or commits its `conventional_commits` +
`filter_unconventional` settings silently dropped -- no warning, they just
never appeared in `CHANGELOG.md` -- before a CI check
([PR #89](https://github.com/opencpn-radar-pi/mayara_pi/pull/89)) started
rejecting non-conforming PR titles at merge time instead. This file is
appended to the bottom of the generated `CHANGELOG.md`.

## Since 0.1.4 (recovered from the conventional-commits filter)

Everything below merged between the 0.1.4 tag and PR #89 landing, and would
otherwise still be invisible. A CI-only fix ([PR #83](https://github.com/opencpn-radar-pi/mayara_pi/pull/83),
retrying a flaky catalog-metadata fetch) is left out: it changed no plugin
behaviour, so it has nothing to say here.

### Added

- Report the built commit id via `GetPlugInVersionBuild()`, so an alpha/beta
  build can be told apart from another build sharing the same version number
  ([PR #69](https://github.com/opencpn-radar-pi/mayara_pi/pull/69))
- Play a sound on a new guard-zone alarm
  ([PR #70](https://github.com/opencpn-radar-pi/mayara_pi/pull/70)), with
  per-target notification and a bell lozenge to mute it
  ([PR #72](https://github.com/opencpn-radar-pi/mayara_pi/pull/72))
- Local-server log verbosity (-v/-vv), and show the plugin/server versions in
  Settings ([PR #75](https://github.com/opencpn-radar-pi/mayara_pi/pull/75))
- Make the chart-canvas radar controls menu draggable and resizable
  ([PR #78](https://github.com/opencpn-radar-pi/mayara_pi/pull/78)), and
  persist its position and size per canvas across restarts
  ([PR #87](https://github.com/opencpn-radar-pi/mayara_pi/pull/87))
- Optional "On top" for floating PPI windows, default per platform
  ([PR #77](https://github.com/opencpn-radar-pi/mayara_pi/pull/77))
- Persist each PPI window's own shown/hidden state, independent of the
  master show/hide toggle
  ([PR #86](https://github.com/opencpn-radar-pi/mayara_pi/pull/86))
- Override the radar controls' font size in Preferences
  ([PR #81](https://github.com/opencpn-radar-pi/mayara_pi/pull/81))
- **server:** an "Extra arguments" field for the local mayara-server's launch
  command line ([PR #85](https://github.com/opencpn-radar-pi/mayara_pi/pull/85))

### Changed

- Show transmit/operating/warmup time in human units, not raw seconds
  ([PR #76](https://github.com/opencpn-radar-pi/mayara_pi/pull/76))
- Diagnostics page: group Fixed heading with Fixed position
  ([PR #74](https://github.com/opencpn-radar-pi/mayara_pi/pull/74))

### Fixed

- A stale local-server reconnect address, and a use-after-free crash
  ([PR #73](https://github.com/opencpn-radar-pi/mayara_pi/pull/73))
- **ui:** redraw the themed dropdown list instead of a native popup menu,
  which looked wrong and could crash on Windows
  ([PR #79](https://github.com/opencpn-radar-pi/mayara_pi/pull/79))
- Stuck mouse capture when dragging the Controls panel title bar
  ([PR #84](https://github.com/opencpn-radar-pi/mayara_pi/pull/84))

### Removed

- The "Fixed position and heading" diagnostics setting -- it never actually
  reached mayara-server or NMEA output, so it did not do what it looked like
  it did ([PR #85](https://github.com/opencpn-radar-pi/mayara_pi/pull/85))

## Pre-0.1.0

Everything below predates the plugin's first tagged release, 0.1.0. Entries
are grouped by the pull request that landed them, because a squash merge made
the PR the unit of change here.

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
