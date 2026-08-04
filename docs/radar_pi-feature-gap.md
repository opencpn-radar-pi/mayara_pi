# Feature gap: radar_pi → mayara_pi

What [radar_pi](https://github.com/opencpn-radar-pi/radar_pi) does that mayara_pi
does not, as of 2026-08-04.

Compiled from radar_pi's `PersistentSettings` (`include/radar_pi.h`), its
`ControlType.inc`, and its canvas/overlay code, checked against mayara_pi's
source.

The two plugins split the work differently, so a feature missing here is not
automatically a feature to build here — mayara-server owns radar I/O, target
tracking, trails and guard-zone detection. Gaps below are the ones that really
do belong in the plugin; see [Not gaps](#not-gaps-despite-appearances) for the
ones that only look like gaps.

## PPI display and interaction

| Feature | radar_pi | mayara_pi |
|---|---|---|
| **EBL / VRM** | 2 bearing lines + 2 range rings per orientation, set by clicking the PPI, persisted | `m_ebl_on` was an unused placeholder with an icon |
| **Off-center / "Look Around"** | drag the PPI to pan; `CT_CENTER_VIEW` resets | wheel zoom only, always centred |
| **Cursor readout** | range/bearing under the pointer | declares `WANTS_CURSOR_LATLON` but never overrode `SetCursorLatLon` |
| **Guard-zone rendering** | drawn on PPI and optionally the overlay; shading / outline / both, transparency | zones could be *edited* (`ControlsPanel::AddZone`) but were never drawn |
| **Extreme-range ring** | red ring at max range + centre marker | — |
| **Refresh rate** | `CT_REFRESHRATE` control | hardcoded 200 ms timer |
| **Reverse zoom** | user option | — |
| **Menu auto-hide** | off / 10 s / 30 s | — |

The first four rows are addressed by the "PPI display improvements" change; this
table is left as written so the rest stays readable as a to-do list.

## Alarms

**Guard-zone bogey alarm.** radar_pi has a bogey dialog with a blob-count
threshold, a re-warn timeout and a configurable WAV. mayara-server *already
emits* `notifications.radar.<key>.guardZone.<n>` as Signal K alerts
(`radar/target/manager.rs`) — the plugin simply never subscribes. The cheapest
large win on this list: the detection half is done.

## Chart overlay

- **Per-canvas overlay** (`CT_OVERLAY_CANVAS`). `RenderGLOverlayMultiCanvas`
  takes `canvasIndex` and ignores it, so the overlay is all-canvases-or-none.
- **Overlay transparency** as a user control. mayara_pi derives intensity from
  the colour scheme theme only.
- **Toggles**: guard zones on overlay, trails on overlay, overlay-on-standby.

## Feeding OpenCPN

- **Radar heading → NMEA HDM/HDT** (`pass_heading_to_opencpn`), for boats where
  the radar is the best heading source.
- **ARPA targets → TTM / AIVDM** (`TTMtoO`, `AIVDMtoO`), so targets land in
  OpenCPN's own target list instead of only being drawn by us.
- **Target-mixer address** — forward targets to another host.

## Colours

radar_pi lets the user set trail start/end, Doppler approaching/receding,
strong/intermediate/weak, ARPA edge, AIS text and PPI background. mayara_pi
takes the legend wholesale from the server with no user override.

## Diagnostics and testing

Fixed heading, fixed position, ignore-radar-heading, COG-as-heading, heading
timeout, skew-factor correction, verbose log level, radar description text.

## Not gaps, despite appearances

- **Brand receivers and hardware controls** — mayara-server's job. The
  schema-driven `ControlsPanel` covers them generically, and the server exposes
  several radar_pi lacks (BirdMode, ScanAverage, PulseWidth, STC curves).
- **ARPA tracking, trails, guard-zone detection** — server-side by design.
  Trails are baked into the spoke legend as history colours, so they already
  render.
- **Timed idle/run, antenna offsets, no-transmit zones** — present as server
  controls (`TimedIdle`, `AntennaForward`, …), surfaced automatically.
- **wxDC (non-GL) overlay** — radar_pi declares `WANTS_OVERLAY_CALLBACK`, but
  its `RenderOverlay` is a no-op that only flips GL off. Both plugins are
  GL-only.
