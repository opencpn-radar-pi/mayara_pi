# Contributing

## Commit messages

Commit subjects (PR titles, since squash-merge makes the PR title the commit
message) follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<optional scope>): <subject>
```

`type` is one of:

| type       | shows up in the changelog as |
|------------|-------------------------------|
| `feat`     | Added                         |
| `fix`      | Fixed                         |
| `refactor` | Changed                       |
| `perf`     | Changed                       |
| `docs`     | Changed                       |
| `style`    | (skipped)                     |
| `test`     | (skipped)                     |
| `chore`    | (skipped)                     |
| `ci`       | (skipped)                     |

Lowercase, no capital letter after the colon. `cliff.toml` matches these
exactly (case-insensitively) to build `CHANGELOG.md` from commit history on
every merge to `main`; anything that doesn't match one of these types is
dropped rather than showing up as a stray entry. See `cliff.toml` for the
full parsing rules.

Examples:

```
fix(overlay): restore the caller's GL matrix mode after the overlay pass
feat(diagnostics): show which heading source is currently active
ci: install redhat-lsb-core so the fedora tarball name gets a version
```

## Tests

The wx-free core has unit tests under `test/`; see `test/README.md`.

```
make test       # build + run them
make test-asan  # the same, under AddressSanitizer + UBSan
make coverage   # line coverage for the tested sources
```

They need a C++17 compiler, CMake 3.15 and Make — no wxWidgets and no OpenCPN.
`make coverage` also wants `llvm-cov`/`llvm-profdata` (Clang) or `gcovr` (GCC).

They also run in CI on every pull request, as the `unit-tests` job, before
any of the packaging builds. A change to `src/RadarMessage.cpp`,
`src/RadarState.cpp`, `src/RadarPalette.cpp`, `src/RadarControls.cpp` or
`src/MayaraProtocol.cpp` should come with a test.

## Translations

Our own strings are marked with `_()` and collected by the `mayara-pot-update`
target into `po/mayara_pi.pot`.

The control panel also shows text the server sends: control names, their
descriptions (as tooltips) and enum labels ("Gain", "Harbor", "Off"). These are English literals in mayara-server,
which publishes them as `docs/ui-strings.json`. `tools/gen-server-strings.py`
turns that file into `po/server_strings.h` (never compiled, read only by
xgettext), so these strings land in the same `.pot`. Enum labels carry the
control id as msgctxt, so a language can word "Auto" differently per control;
`include/ServerText.h` looks the text up at run time and shows the English when
there is no translation.

The server version is pinned in `po/server-strings.ref`. To pick up new or
changed server text, bump that ref, then:

```
tools/gen-server-strings.py
cmake --build build --target mayara-pot-update
```

CI fails the `unit-tests` job when `po/server_strings.h` does not match the
pinned ref.

## Releasing

`./release.sh` manages the plugin version (`VERSION_MAJOR`/`VERSION_MINOR`/
`VERSION_PATCH` in `CMakeLists.txt`) and tagging; run it with no arguments for
usage. Pushing a `v*` tag triggers both `release.yml` (GitHub Release +
changelog) and `build.yml`'s Cloudsmith upload.
