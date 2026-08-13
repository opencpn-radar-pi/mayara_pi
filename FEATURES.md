# Features

What mayara_pi does today. Everything here works against a live radar unless
marked otherwise; anything deliberately *not* built, and why, is in
[docs/radar_pi-feature-gap.md](docs/radar_pi-feature-gap.md).

The plugin renders and controls; [mayara-server](https://github.com/MarineYachtRadar/mayara-server)
owns radar I/O, target tracking, trails and guard-zone detection. That split
decides what belongs here.

## The picture

- **PPI windows**, one or several, each showing one or more radars. Floating,
  or docked into OpenCPN as an AUI pane.
- **Orientation per radar** — head-up, north-up, course-up — with a lozenge on
  the picture that says which is in force and cycles it on click. It dims and
  names the missing input when north-up or course-up cannot be honoured
  (north-up needs a heading; course-up needs a course as well).
- **Free display zoom** independent of the radar's own range, **off-centre
  ("look around")** by dragging, and a **cursor readout** of range and bearing.
- **Range rings**, an **extreme-range ring**, and a **centre marker**.
- **Refresh rate** (1/2/5/10 Hz) and **reverse zoom wheel**.
- **Echo threshold** — show all returns, hide weak, or only strong — per radar.
- **Day/dusk/night** follows OpenCPN's colour scheme, dimming the echoes with it.

## Colours

- Five **palette profiles**: the server's own legend, plus single-hue Red,
  Yellow, Green and Blue ramps where echo strength reads as brightness.
- Doppler stays magenta/green on every profile, because a Doppler mark must not
  be mistakable for an echo.
- Any built-in can be **copied, renamed and edited** into a profile of your own.
  A palette re-colours the server's legend by *role* (strength ramp, Doppler,
  trails, static background), so it fits any radar whatever legend it reports.

## Chart overlay

- **Per canvas, per radar**: the canvas context menu picks None, a named radar,
  or all of them. Two chart panes can carry one radar each, the same one, or
  both nested.
- Two radars on one canvas are drawn as **nested annuli** — the shorter-range
  radar owns the inner circle outright rather than blending with the longer one.
- **Opacity** 25–100%, and **guard zones drawn on the chart** in the PPI's
  colours.
- **Works with and without OpenGL.** With hardware acceleration off, the same
  cached picture is drawn through `wxGraphicsContext`. The PPI never needed GL
  in the first place — it blits a CPU-rendered bitmap — so the whole plugin runs
  without it.
- Optional, and off by default because they write to the hardware:
  **chart scale sets range** (the chart's zoom drives the radar) and
  **nest second radar at 1/4**.
- Choosing a radar to overlay takes it out of standby.

## Radar control

- The control panel is **built from the server's schema**, so every control a
  radar exposes appears without the plugin knowing its name — gain, sea, rain,
  Doppler, target boost/expansion/separation, scan speed, no-transmit sectors,
  antenna offsets, timed idle, and whatever a future radar adds.
- Numeric controls adapt: slider, −/+ stepper, or a typed field, by how many
  values they accept.
- **Auto modes** (none / simple / auto-adjustable, e.g. HALO Sea) come from the
  schema.
- The panel can be opened **over the chart** without a PPI window at all.

## Targets and zones

- **ARPA targets** tracked by the server, drawn with vector, CPA/TCPA and
  danger state. Click to acquire, click to cancel.
- **AIS** targets as a layer on the picture.
- **Guard zones**, drawn as the mayara GUI draws them, editable by dragging
  handles on the picture or by typing the numbers, and **alarms** raised as
  OpenCPN notifications when the server reports occupancy.
- **Two VRM/EBL markers**, local to the plugin, placed by clicking the picture.

## Talking to the server

- **Discovery** by mDNS (`_mayara-http._tcp` and `_signalk-http._tcp`), plus
  OpenCPN's own Signal K connection as a hint, plus a remembered address and a
  manual one.
- **Signal K access requests**: the plugin asks for permission to control the
  radar and explains when a write is refused.
- **Run mayara-server locally** if you have none: download, start, stop, with
  emulator and brand options. It is tied to OpenCPN's lifetime (`--parent`), so
  it does not outlive a crash.
- Targets arrive as deltas where the server streams them, and are **polled over
  REST where it does not** — a Signal K server in front of mayara republishes
  controls but not targets.

## Feeding OpenCPN

Both off by default, in Settings → Display:

- **Radar heading as NMEA HDT** — the radar's own heading, for boats where it is
  the best source. Whether OpenCPN then uses it is up to its own source
  priorities: another heading source may outrank it.
- **Radar targets as NMEA TTM** — so radar targets appear in OpenCPN's target
  list alongside AIS.

Both confirmed arriving in OpenCPN against live radar.

## Diagnostics

Settings → Diagnostics, for when the picture is not where it should be:

- A **live readout** of the heading and position being used, and where each came
  from, updated once a second.
- **Heading source** — automatic, OpenCPN only, radar only, or a fixed value —
  **COG as heading**, and a **staleness timeout** so a radar that stops
  transmitting cannot keep pointing the picture with a heading from minutes ago.
- **Fixed position**, for a bench with no GPS.
- **Logging** into OpenCPN's own log, including what each fix from OpenCPN
  actually contains and what the rendering costs per frame.
