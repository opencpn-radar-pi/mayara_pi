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

Every row above is addressed by the "PPI display improvements" change; the table
is left as written so it still reads as the record of what was missing. The
sections below are the open work.

## Alarms

**Guard-zone bogey alarm.** The plugin now subscribes to
`notifications.radar.<key>.guardZone.<n>` and raises an OpenCPN notification
for each new one, which is where the operator already looks for alerts.

Still missing next to radar_pi: a blob-count threshold, a re-warn timeout and a
configurable WAV. OpenCPN's own notification handles the presentation, so those
are only worth adding if its behaviour proves too quiet in practice.

Confirmed against a live HALO A: the server publishes the delta on its own
stream (`:6502`) and it also reaches the upstream Signal K server (`:3000`).
mayara-server did not relay these to its own stream clients at first; that was
fixed server-side once the two endpoints were compared.

## Chart overlay

- **Per-canvas overlay** (`CT_OVERLAY_CANVAS`) — done, and further than
  radar_pi takes it: the canvas context menu picks *which* radar that canvas
  overlays (None / each radar by name / All), so two charts can carry one radar
  each, the same one, or both nested. The View toggle still acts on every
  canvas, since a panel button has no canvas of its own.
- **Nest the second radar** — done, off by default. With a canvas set to
  "All", the second radar is held at the settable range nearest a quarter of
  the first's, so the inner picture stays worth looking at. Each radar is drawn
  as an annulus from the next-shorter radar's range out to its own, so the
  shorter radar owns the inner circle outright rather than blending with the
  longer one there.
- **Chart scale sets range** — done, off by default, and not something radar_pi
  offers per canvas: the chart's own zoom drives the overlaid radar to the
  shortest range that still covers the canvas.
- **Transmit on selection** — picking a radar to overlay takes it out of
  standby, since a standby radar draws nothing.
- **Overlay opacity** — done, 25/50/75/100% in the View section.
- **Guard zones on the overlay** — done, in the PPI's colours.
- **Trails on overlay** — not applicable. mayara-server bakes trails into the
  spoke legend as history colours, so they are part of the echo texture and
  cannot be drawn or suppressed separately.
- **Overlay on standby** — not applicable as radar_pi means it. Standby clears
  the picture (`RadarState::Clear`), so there is nothing to keep showing; the
  guard zones now drawn on the chart cover the case it existed for.

## Feeding OpenCPN

- **Radar heading → NMEA HDT** — done. Settings → Display → Feed OpenCPN. The
  radar's own heading, not OpenCPN's fix fed back to it, at 1 Hz. Off by
  default: on most boats another source already provides heading, and two
  disagreeing sources is worse than one.
- **ARPA targets → TTM** — done, same place. TTM's target number is two digits
  and target ids are not, so numbers are handed out and held for the life of a
  target; a target that goes lost is reported once as `L` and gives its number
  back.
- **ARPA targets → AIVDM** — not done. TTM is what OpenCPN's own target list
  wants; AIVDM would mean minting AIS identities for radar contacts, which
  makes them indistinguishable from real AIS traffic downstream.
- **Target-mixer address** — not done. Forwarding to another host is
  mayara-server's business: it already serves every client on the network,
  which is the same job done once instead of per plugin.

## Colours

Done, and answering radar_pi issue
[#294](https://github.com/opencpn-radar-pi/radar_pi/issues/294) ("Color palette
templates") rather than radar_pi's own eleven separate colour settings.

A palette re-colours the server's legend **by role** instead of replacing it:
the legend says which indices are the echo-strength ramp, which is the static
background, which two are Doppler and where the trail history starts, so eight
colours cover any radar whatever legend length it reports. Anything the layout
does not account for is left exactly as the server sent it.

Four profiles ship, in Settings → Colours:

1. **Standard Mayara** — the radar's own legend, untouched.
2. **Navico yellow**
3. **Garmin**
4. **Furuno**

"Standard Mayara" is the ramp mayara-server computes in `default_legend()` —
blue at a third, green at two thirds, red at the top — not a maker's palette.

The three named after makers follow what each maker's own documentation says
its colours *mean*. No maker publishes RGB values, so the exact shades are
ours; the mapping is not.

- **Furuno** — [DRS4W operator's manual](https://www.furuno.it/docs/OPERATOR_MANUAL/DRS4W_OME-C8.pdf),
  §1.15 Echo Color: "Multicolor paints each radar echo in a color according to
  its strength, in red, yellow or green, corresponding to strong, medium and
  weak echoes." Doppler follows Target Analyzer on the
  [DRS-NXT series](https://www.furuno.com/special/en/radar/drs4d-nxt/): "red
  echoes are hazardous targets that are moving towards your vessel", "green
  echoes are targets that stay stationary, or are moving away from you".
- **Navico** — the Black/Yellow radar image palette.
  [Lowrance HDS Live operator manual](https://reviewmarine.com/wp-content/uploads/2019/11/Lowrance-HDS-Live-Operators-Manual.pdf),
  "Radar view options": "Diverging targets are blue colored on all radar image
  palettes", and approaching targets are red on the Black/Green and
  Black/Yellow palettes (yellow on Black/Red and White/Red).
- **Garmin** — [GPSMAP owner's manual, MotionScope](https://www8.garmin.com/manuals/webhelp/gpsmap1002-1202/EN-US/GUID-745F4B45-874C-474D-84B6-EFFB987F3093.html):
  "On most color schemes, green indicates the target is moving away from you
  and red indicates the target is moving toward you." The strength ramp is
  **not** documented — Garmin's
  [radar appearance settings](https://www8.garmin.com/manuals/webhelp/gpsmap8400-8600/EN-US/GUID-E9EF00DE-8456-439A-BB46-AD72A410E62C.html)
  only say "Frgd. Color - Sets the color scheme for the radar returns" without
  naming the schemes — so the greens there remain our reading. None of the four can be edited in place: changing a colour copies the
profile to one of your own first, so "Navico yellow" means the same thing on
every boat. Profiles of your own can be renamed and deleted. They are stored in
the OpenCPN config; the built-ins are rebuilt from code each start, so improving
one reaches everybody.

Not covered: ARPA edge and AIS text (drawn from the UI theme, not the legend)
and the PPI background (black, and the picture is designed against it).

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
