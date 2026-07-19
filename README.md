# mayara_pi

An [OpenCPN](https://opencpn.org) plugin that displays marine radar as a chart
overlay and in a PPI window. It is the spiritual successor to
[radar_pi](https://github.com/opencpn-radar-pi/radar_pi).

Unlike radar_pi, this plugin does **not** talk to radar hardware directly.
Instead it consumes the [mayara-server](https://github.com/MarineYachtRadar/mayara-server)
REST + WebSocket API (the Signal K Radar API), which handles discovery and
communication with Navico, Garmin, Furuno and Raymarine radars. The plugin is a
thin, adaptive client:

- **REST** — discover radars, fetch each radar's `capabilities` (control schema
  + colour legend), read/write controls, list ARPA targets.
- **WebSocket (binary)** — per-radar protobuf spoke stream (the radar image).
- **WebSocket (JSON)** — Signal K delta stream for control values, targets, and
  own-ship navigation.

Because the control set is described by a self-describing schema, the plugin's
control UI is **generated from the capabilities** (one widget per `dataType`)
rather than hand-coded per radar brand.

## Status

Early scaffold. Phase 0 (a plugin that loads, shows a toolbar tool, and opens a
PPI window) builds and runs on macOS. Networking and rendering land next.

Roadmap:

| Phase | Content |
| ----- | ------- |
| 0 | Scaffold: FE2 build, GitHub Actions, loads + empty PPI window ✅ |
| 1 | `MayaraClient` REST discovery + spoke WebSocket + protobuf → radar image (PPI + overlay) |
| 2 | Signal K control stream + schema-driven control panel |
| 3 | ARPA targets, guard zones, trails, orientation modes |
| 4 | Full CI matrix (arm64/armhf/flatpak/android), catalog submission, config/persistence |

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

Requirements: CMake ≥ 3.15, a C++17 compiler, wxWidgets (3.2 for distributable
builds; 3.3 works for local development), and gettext. macOS deployment target
matching OpenCPN.

## CI / distribution

CI runs on **GitHub Actions** (`.github/workflows/build.yml`) — the FE2
template's CircleCI/AppVeyor/Travis config is deliberately not used. Each job
runs the portable FE2 `ci/` build scripts on a GitHub-hosted runner, then
publishes the tarball + metadata to Cloudsmith for the OpenCPN plugin catalog.

## License

GPLv3+. See [LICENSE](LICENSE).
