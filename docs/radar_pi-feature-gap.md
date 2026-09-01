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

**Guard-zone bogey alarm.** The plugin subscribes to
`notifications.radar.<key>.guardZone.<n>` and raises an OpenCPN notification
for each new one, which is where the operator already looks for alerts. The
server fires a fresh delta per target, not just on the zone's first bogey, so
a second vessel entering an already-alarming zone is announced too — the
plugin used to dedupe purely on the zone's inactive → active edge and
swallow every target after the first; it now tracks the message last shown
per (radar, zone) and notifies again whenever that changes.

**Audible alarm.** OpenCPN's own notification icon proved too easy to miss in
practice — it carries no sound of its own, and nothing else in a
chartplotter's usual layout draws the eye to it, exactly the case this
document flagged as worth an audible alarm "if its behaviour proves too
quiet". `data/alarm.wav` is radar_pi's own guard-zone alarm sound, carried
over as-is (people already know that voice), played once per newly-raised
alarm alongside the OpenCPN notification. A bell lozenge under
Power/Orientation on every picture — same idea and the same colours as the
web GUI's speaker lozenge, hand-drawn as a bell rather than a speaker — mutes
it with one click; a "Play a sound" checkbox in Display settings does the
same. Both toggle one plugin-wide setting, not per radar, matching the web
GUI (which only ever shows one). An alarm already standing when OpenCPN
starts, or when the client reconnects, is seeded silently rather than
announced as freshly raised.

Still missing next to radar_pi: a blob-count threshold and a re-warn timeout.
The threshold turned out to already live server-side — a target only alarms
once "promoted" (confirmed across several sweeps), not on first acquisition —
and the re-warn timeout stopped looking necessary once per-target
notification was fixed, so it stays unbuilt until proven otherwise.

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
- **Range Auto** — done, per radar rather than per canvas or a single global
  toggle: a plain "Auto" button next to Range in the controls, shown only
  while that radar is actually on some canvas's overlay, drives it to the
  shortest range that still covers the canvas. This is what radar_pi's own
  range control gets for free from its `RCS_AUTO_1` auto state (auto-range
  applies for any radar on the highest canvas where it is the only overlaid
  one) — mayara-server's `range` control has no such state to key off, so
  the "per radar, not global" property is reintroduced directly in the
  plugin instead, tracking each canvas's own last-asked-for value so two
  canvases sharing a radar settle rather than fight. Off by default, like
  anything that writes to the hardware unasked. Picking a range manually
  turns a radar's Auto back off, so it does not silently undo the choice on
  the next zoom.
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

- **Radar heading → NMEA HDT** — done, and seen arriving in OpenCPN as `$RAHDT`.
  Settings → Display → Feed OpenCPN. The radar's own heading, not OpenCPN's fix
  fed back to it, at 1 Hz. Off by
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

Five profiles ship, in Settings → Colours:

1. **Standard Mayara** — the ramp mayara-server computes in `default_legend()`
   (blue at a third, green at two thirds, red at the top), untouched.
2. **Red**, 3. **Yellow**, 4. **Green**, 5. **Blue** — single-hue ramps, dark at
   the weakest return through full colour to a pale tint of the same hue at the
   strongest, so echo strength reads as brightness alone.

Emulating maker palettes was tried and dropped. Their manuals document what
their colours *mean* (Furuno: "red, yellow or green, corresponding to strong,
medium and weak echoes"; Navico: diverging blue on every image palette; Garmin:
green away, red toward) but none publish RGB values, so a profile called
"Furuno" could only ever be an impression wearing a maker's name. A hue is
honest about what it is.

Doppler is mayara's own magenta/green on every profile, because a Doppler mark
has to be the one thing that cannot be mistaken for an echo. The green ramp is
the exception, where green would be exactly that; there receding is cyan.
Trails stay neutral white-to-grey, which contrasts with every hue.

None of the built-ins can be edited in place: changing a colour copies the
profile to one of your own first. Profiles of your own can be renamed and
deleted. They are stored in the OpenCPN config; the built-ins are rebuilt from
code each start, so improving one reaches everybody.

Not covered: ARPA edge and AIS text (drawn from the UI theme, not the legend)
and the PPI background (black, and the picture is designed against it).

## Diagnostics and testing

Done, as a Diagnostics page in Settings. It opens with a live readout of the
heading and position the drawing code would use *this instant*, and where each
came from — because "why is the picture in the wrong place" is nearly always
one of those two, and until now nothing said which.

- **Heading source** — Automatic (OpenCPN, else the radar) / OpenCPN only /
  Radar only / Fixed. Covers radar_pi's *ignore-radar-heading* and
  *fixed heading* in one control.
- **COG as heading** — off by default. COG is not heading; it differs by leeway
  and set, so it is a last resort and the readout says when it is being used.
- **Heading timeout** — a heading older than this is not used (0 = never
  expires). Radar headings are now timestamped for this. It matters more than
  it looks: a radar that stops transmitting keeps its last heading for ever,
  and a stale heading points the picture the wrong way with no sign that
  anything is wrong.
- **Fixed position** — run on a bench with no GPS.
- **Log level** — Off / Problems / Verbose, into OpenCPN's own log. Worker
  threads never call `wxLog` (its deferred cross-thread flush can dereference
  an unloaded plugin dylib), so the client queues its lines and the UI thread
  drains them each heartbeat. Turning logging up writes the whole configuration
  in one line, so a log sent by a user explains itself.

Turning it on immediately earned its keep: it showed OpenCPN was handing the
plugin no position fix at all. `SendPositionFixToAllPlugIns()` gates both
`SetPositionFix` and `SetPositionFixEx` on `WANTS_NMEA_EVENTS`, which this
plugin never declared — so COG, SOG, heading and position from OpenCPN had
never arrived, and everything had been running on what the radar stamps into
its own spokes. `INSTALLS_CONTEXTMENU_ITEMS` was missing too, which is what
`PrepareAllPluginContextMenus()` checks before calling `PrepareContextMenu`;
the menu items appear without it, so the only symptom was labels that never
updated.

Two behaviour fixes came out of it. Heading was previously taken as "0 means
missing", which is a lie on a boat heading due north; and a missing heading now
means the chart overlay is not drawn at all rather than drawn at north.

The PPI reaches the same resolver through a provider, so the heading source,
the fixed heading and the timeout mean the same thing on the picture as on the
chart. It does not share the second fix: a picture with no heading is still
drawn, because a PPI is bow-relative to begin with and head-up needs no heading
at all. Its orientation lozenge says which input is missing when north-up or
course-up cannot be honoured.

**Skew-factor correction** is not a gap: it corrects a brand's wire protocol,
which is mayara-server's side of the line. **Radar description text** is already
there — name, model, firmware and serial are in the Info section, with the
server URL.

## PPI orientation

Already per radar before this work (`m_orient`, keyed by radar id): Head up,
North up, Course up, in the View section. What was missing is that the picture
never said which one it was in, and in a two-radar window the controls follow
whichever picture has focus, so the setting looked global.

Each picture now carries an orientation lozenge under the power one, clicking it
cycles that radar's own orientation, and the View rows name the radar they act
on ("Orientation (Halo A)"). The lozenge dims and says "no heading" when the
picture is head-up because nothing reports a heading — course-up also needs a
course, and claiming "CU" without one would be a lie.

## Rendering: measured, not assumed

Whether the PPI should be duplicated in OpenGL was asked and answered with
numbers rather than opinion. The picture is now cached and only rasterised when
something about it changes (spoke generation, size, zoom, rotation, off-centre,
intensity, threshold), and with verbose logging on each picture reports what it
costs:

```
PPI radar 0: 420x503, rasterise 1.6 ms + convert 0.2 ms, 288 rasterised / 12 blitted
```

Two things that says. The cost is small — under 2 ms per picture per frame at
this size on an M-series Mac, so ~3% of one core for two radars at 10 Hz. And
the cache almost never hits while a radar is transmitting: the generation bumps
with every spoke batch, so the picture really is new nearly every frame. The
"nineteen frames in twenty" that motivated the cache was wrong; it pays only in
standby, when paused, or between batches.

The number to watch is not this Mac's. A full-screen 1080p PPI samples ten
times as many pixels, and a Raspberry Pi is a good deal slower per pixel, so
that is where the measurement should be repeated before anyone writes a second
renderer. If it does turn out to be too slow there, the cheaper fix comes first:
`EnsureDisc()` rebuilds the whole cached disc (1024²) on every generation change
even when the window sampling it is 420×503, which is most of the 1.6 ms.

## Not gaps, despite appearances

- **Brand receivers and hardware controls** — mayara-server's job. The
  schema-driven `ControlsPanel` covers them generically, and the server exposes
  several radar_pi lacks (BirdMode, ScanAverage, PulseWidth, STC curves).
- **ARPA tracking, trails, guard-zone detection** — server-side by design.
  Trails are baked into the spoke legend as history colours, so they already
  render.
- **Timed idle/run, antenna offsets, no-transmit zones** — present as server
  controls (`TimedIdle`, `AntennaForward`, …), surfaced automatically.
- **wxDC (non-GL) overlay** — was listed here as "not a gap" because radar_pi's
  own `RenderOverlay` is a no-op that only flips a GL flag. That was wrong for
  us: with OpenCPN's hardware acceleration off, our overlay simply vanished
  while the PPI carried on (it never used GL — it is a `wxPanel` blitting a
  CPU-rendered bitmap). `RenderOverlayMultiCanvas(wxDC&, …)` is now
  implemented: same geometry, same cached disc, drawn through
  `wxGraphicsContext` because a plain `wxDC` has no alpha. The disc is
  rasterised at up to 1536 px and scaled up by the blit beyond that, and cached
  against picture generation, size, rotation and the occluded middle — there is
  no GPU on this path to waste work on.
