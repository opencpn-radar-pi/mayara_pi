# Unit tests

Tests for the part of mayara_pi that has no wxWidgets and no OpenCPN plugin
API in it: the spoke decoder, the polar raster and CPU renderer, the palettes,
and the control schema. That is deliberate — those four files are the ones
where a mistake is *silent*. A broken widget is visible the moment you open
the panel; a spoke drawn at the wrong distance just looks like a radar echo.

```
make test           # build + run (ctest)
make test-asan      # the same, under AddressSanitizer + UBSan
make coverage       # line coverage for the four sources
make coverage-html  # ...with an HTML report
make clean-test
```

There is nothing to install first. `test/` is its own CMake project, not a
subdirectory of the plugin's `CMakeLists.txt` (which is OpenCPN's FE2 template
and needs wx and the plugin API before it will even configure), so:

```
cmake -B build-test -S test && cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

works with nothing but a C++17 compiler. `ctest -R RadarState` runs one suite;
the test binary takes doctest's own flags too, e.g.
`./build-test/mayara_tests --test-case='*clockwise*' -s`.

## What is here

| File | Covers |
|---|---|
| `test_radar_message.cpp` | `src/RadarMessage.cpp` — the proto3 spoke decoder |
| `test_radar_state.cpp` | `src/RadarState.cpp` — raster, disc, PPI and overlay |
| `test_radar_palette.cpp` | `src/RadarPalette.cpp` — built-ins, config round trip |
| `test_radar_controls.cpp` | `src/RadarControls.cpp` + `JsonNum` |
| `proto_builder.h` | A proto3 *encoder*, so the decoder has something to read |

`proto_builder.h` is written from the wire-format spec rather than by reusing
the decoder's own helpers, on purpose: sharing them would let a bug on both
sides cancel itself out and the tests would pass on a stream no real server
sends.

## Adding a source to the tests

Add it to `MAYARA_UNIT_SRCS` in `test/CMakeLists.txt` and to `SOURCES` in
`test/coverage.sh`. Keep that list free of wx and of `ocpn_plugin.h` — the
moment one creeps in, the tests stop being a plain compiler-only build and
the CI job has to install a GUI toolkit to run them.

## Writing a test that is worth having

Two things the existing tests lean on, both worth copying:

- **Assert the observable contract, not the implementation.** The geometry
  cases sample rendered pixels at a bearing and a distance rather than
  reaching for the lookup table, so the renderer can be rewritten without
  rewriting the tests.
- **Check the test can fail.** Break the line you think you are covering,
  confirm the test goes red, and put it back. A test that passes either way
  is worse than no test, because it reads like coverage.

## Where this does not reach

`mayara_pi.cpp`, the panels and the windows need wx (and, for most of it, a
running OpenCPN) and are not tested here. Neither is `MayaraClient.cpp`: its
JSON parsing is pure, but it lives in anonymous namespaces that nothing can
link against — lifting those into their own translation unit is the next step
that would pay for itself. Protocol agreement with mayara-server is better
checked end to end against `mayara-server --replay` over one of its recorded
pcaps than guessed at from this side.
