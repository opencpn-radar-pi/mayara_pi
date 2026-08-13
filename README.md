# mayara_pi

An [OpenCPN](https://opencpn.org) plugin that displays marine radar as a chart
overlay and in a PPI window. It is the spiritual successor to
[radar_pi](https://github.com/opencpn-radar-pi/radar_pi).


## Difference with radar_pi

Unlike radar_pi, this plugin does **not** talk to radar hardware directly.
Instead it consumes the [mayara-server](https://github.com/MarineYachtRadar/mayara-server)
REST + WebSocket API (the Signal K Radar API), which handles discovery and
communication with Navico, Garmin, Furuno and Raymarine radars. 

This has two advantages:
1. mayara-server supports many more radars than radar_pi.
2. mayara-server can run on a small router or computer with wired access to the radar, and you can now reliably run radar on OpenCPN on wirelessly connected computers.

If you do not have a Signal K installation, the plugin will download mayara for you, but then advantage 2 disappears.

## What it does

Radar as a chart overlay and in PPI windows: several radars, several windows,
per-canvas overlay assignment, guard zones with alarms, ARPA targets, VRM/EBL,
colour profiles, and every control the radar exposes. See
**[FEATURES.md](FEATURES.md)** for the full list, and
**[CHANGELOG.md](CHANGELOG.md)** for what has landed so far.

[docs/radar_pi-feature-gap.md](docs/radar_pi-feature-gap.md) tracks this against
radar_pi, including the things deliberately not built here and why.

## Installing

Packages for every platform are published to Cloudsmith and reach the OpenCPN
plugin catalog from there. To install one by hand — a CI build, say — use
**Options → Plugins → Import plugin…** and give it the `.tar.gz`. On flatpak the
file must live under your home directory, since the sandbox cannot see `/tmp`.

## Building

The build uses the OpenCPN **Frontend2 (FE2)** template. Everything under
`cmake/` and `ci/` is upstream FE2 machinery; the only plugin-specific build
file is `CMakeLists.txt`.

```sh
git clone --recurse-submodules https://github.com/opencpn-radar-pi/mayara_pi.git
cd mayara_pi
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requirements: CMake ≥ 3.15, a C++ compiler, wxWidgets (3.2 for distributable
builds; 3.3 works for local development), and gettext. macOS deployment target
matching OpenCPN.

Note that the effective language standard is **C++11**, not the C++17 that
`CMAKE_CXX_STANDARD` asks for: the FE2 template's `cmake/PluginSetup.cmake`
appends `-std=c++11` to `CMAKE_CXX_FLAGS` afterwards, and the flag wins. Code
that compiles locally with a newer standard will fail in CI.

## CI / distribution

CI runs on **GitHub Actions** (`.github/workflows/build.yml`) — the FE2
template's CircleCI/AppVeyor/Travis config is deliberately not used. Each job
runs the portable FE2 `ci/` build scripts on a GitHub-hosted runner, then
publishes the tarball + metadata to Cloudsmith for the OpenCPN plugin catalog.

## License

GPLv3+. See [LICENSE](LICENSE).
