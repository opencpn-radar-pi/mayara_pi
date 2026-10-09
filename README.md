# mayara_pi

An [OpenCPN](https://opencpn.org) plugin that displays marine radar as a chart
overlay and in a PPI window. It is the spiritual successor to
[radar_pi](https://github.com/opencpn-radar-pi/radar_pi).


## Difference with radar_pi

Unlike radar_pi, this plugin does **not** talk to radar hardware directly.
Instead it consumes the [mayara-server](https://github.com/MarineYachtRadar/mayara-server)
REST + WebSocket API (the Signal K Radar API), which handles discovery and
communication with Navico, Garmin, Furuno and Raymarine radars. 

This has three advantages:
1. mayara-server supports many more radars than radar_pi.
2. mayara-server can run on a small router or computer with wired access to the radar, and you can now reliably run radar on OpenCPN on wirelessly connected computers.
3. **No OpenGL required.** The PPI is a plain `wxPanel` blitting a bitmap that
   a CPU rasteriser produced, and the chart overlay is drawn either through
   OpenGL or through `wxGraphicsContext`, whichever OpenCPN is using. radar_pi
   needs OpenGL for both: its radar window is a `wxGLCanvas`, and its
   `RenderOverlay(wxDC&)` draws nothing at all — it only switches its own GL
   mode off. So on a machine where OpenCPN's hardware acceleration is off or
   unreliable, mayara_pi still shows radar.

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

mayara_pi needs **OpenCPN 5.14 or later** (it uses plugin API 1.21, which no
5.12.x release has).

> [!WARNING]
> **Windows with OpenCPN 5.14.0:** the OpenCPN installer puts an obsolete
> Microsoft Visual C++ runtime (`msvcp140.dll` version 14.12, from Visual
> Studio 2017) in the OpenCPN program folder, normally
> `C:\Program Files (x86)\OpenCPN`. Windows loads DLLs from a program's own
> folder before the system's, so OpenCPN and every plugin run against that old
> runtime instead of the current one, and plugins built with a current compiler
> can crash OpenCPN at startup. mayara_pi works around the crash we know of,
> but the reliable fix is to let OpenCPN use the system runtime:
>
> 1. Quit OpenCPN.
> 2. Install the current
>    [Microsoft Visual C++ Redistributable for x86](https://aka.ms/vs/17/release/vc_redist.x86.exe)
>    (OpenCPN is a 32-bit program, so the x86 version, even on 64-bit Windows).
> 3. In the OpenCPN program folder, delete — or rename to `.old` — the old
>    runtime DLLs: `msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll` and
>    `vccorlib140.dll`.
>
> To check whether your installation is affected, right-click `msvcp140.dll`
> in the OpenCPN folder, choose **Properties → Details**, and look at the file
> version: anything below 14.40 is the obsolete runtime. See the
> [forum thread](https://www.cruisersforum.com/forums/f134/new-radar-plugin-mayara_pi-in-beta-301792.html)
> for background.

mayara_pi is **not in OpenCPN's standard plugin catalogs yet** — neither the
default `master` catalog nor `Beta` lists it, so it will not appear under
**Options → Plugins** out of the box. There are two ways to install it.

### Option A: import a package by hand

Download the `.tar.gz` for your platform from Cloudsmith — the
[beta](https://cloudsmith.io/~opencpn-radar-pi/repos/mayara-beta/packages/) or
[alpha](https://cloudsmith.io/~opencpn-radar-pi/repos/mayara-alpha/packages/)
repository — or from a CI run's artifacts, and install it with
**Options → Plugins → Import plugin…**. You won't get update notifications
this way; Option B gives you those.

| Platform | Package name contains |
|---|---|
| Windows | `msvc-x86-wx32` |
| macOS (Intel and Apple Silicon) | `darwin-wx32-arm64-x86_64` |
| Debian 12 / 13 | `debian-<arch>-12-bookworm` / `debian-<arch>-13-trixie` |
| Ubuntu 22.04 / 24.04 | `debian-<arch>-22.04-jammy` / `debian-<arch>-24.04-noble` |
| Flatpak | `x86_64_flatpak` / `aarch64_flatpak` |

`<arch>` is `x86_64`, `arm64`, or `armhf` (Raspberry Pi OS 32-bit; Ubuntu
only). On flatpak the file must live under your home directory, since the
sandbox cannot see `/tmp`.

### Option B: point OpenCPN at the mayara catalog (recommended)

CI publishes its own catalog, so OpenCPN can install mayara_pi and offer
updates like any other plugin:

| Catalog | URL | Contents |
|---|---|---|
| beta | `https://raw.githubusercontent.com/opencpn-radar-pi/plugins/catalog/beta.xml` | tagged beta releases |
| alpha | `https://raw.githubusercontent.com/opencpn-radar-pi/plugins/catalog/alpha.xml` | every build of `main` — newest, least tested |

OpenCPN hides its catalog settings by default, so the first step enables
them:

1. **Quit OpenCPN** (it rewrites its config file on exit).
2. Open OpenCPN's config file and add this line under the `[PlugIns]` section
   (create the section if it is missing):

   ```ini
   [PlugIns]
   CatalogExpert=1
   ```

   | Platform | Config file |
   |---|---|
   | Windows | `C:\ProgramData\opencpn\opencpn.ini` |
   | macOS | `~/Library/Preferences/opencpn/opencpn.ini` |
   | Linux | `~/.opencpn/opencpn.conf` |
   | Linux (flatpak) | `~/.var/app/org.opencpn.OpenCPN/config/opencpn/opencpn.conf` |

3. Start OpenCPN and go to **Options → Plugins → Settings…**.
4. Click **Ultra advanced >>>**, paste the catalog URL into
   **Custom catalog URL**, click **Save**, then **Done**.
5. Click **Update Plugin Catalog**. **Mayara** now appears in the list; select
   it and click **Install**, then enable it.

The mayara catalog includes the matching official one — `beta.xml` lists
everything in OpenCPN's **Beta** catalog as well, `alpha.xml` everything in
**Alpha** — so your other plugins stay installable and updatable, but from that
channel rather than the default `master` one. To go back to `master`, open
**Settings… → Ultra advanced >>>**, click **Clear**, **Save**, **Done**, and
**Update Plugin Catalog** again.

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
