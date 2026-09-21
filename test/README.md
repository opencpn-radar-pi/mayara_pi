# Unit tests

Tests for the part of mayara_pi that has no wxWidgets and no OpenCPN plugin
API in it: the spoke decoder, the polar raster and CPU renderer, the palettes,
the control schema, and the wire protocol. That is deliberate — those are the
files where a mistake is *silent*. A broken widget is visible the moment you
open the panel; a spoke drawn at the wrong distance just looks like a radar
echo, and a misparsed control just looks like a radar that hasn't got one.

```
make test           # build + run (ctest)
make test-asan      # the same, under AddressSanitizer + UBSan
make coverage       # line coverage for the four sources
make coverage-html  # ...with an HTML report
make clean-test
```

A C++17 compiler, CMake 3.15 and Make are the whole list — no wxWidgets, no
OpenCPN, no submodules. (`make coverage` additionally wants `llvm-cov` and
`llvm-profdata` under Clang, or `gcovr` under GCC.) `test/` is its own CMake
project, not a subdirectory of the plugin's `CMakeLists.txt` (which is
OpenCPN's FE2 template and needs wx and the plugin API before it will even
configure), so:

```
cmake -B build-test -S test && cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

works on its own, without the Makefile. `ctest -R RadarState` runs one suite;
the test binary takes doctest's own flags too, e.g.
`./build-test/mayara_tests --test-case='*clockwise*' -s`.

## What is here

| File | Covers |
|---|---|
| `test_radar_message.cpp` | `src/RadarMessage.cpp` — the proto3 spoke decoder |
| `test_radar_state.cpp` | `src/RadarState.cpp` — raster, disc, PPI and overlay |
| `test_radar_palette.cpp` | `src/RadarPalette.cpp` — built-ins, config round trip |
| `test_radar_controls.cpp` | `src/RadarControls.cpp` + `JsonNum` |
| `test_mayara_protocol.cpp` | `src/MayaraProtocol.cpp` — server JSON in, plugin structs out |
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
running OpenCPN) and are not tested here.

Nor is the rest of `MayaraClient.cpp` — what remains of it after
`MayaraProtocol.cpp` was lifted out is sockets, threads and reconnection
logic, which needs a server to talk to rather than a table of inputs. That is
the next step: mayara-server can replay a recorded pcap
(`mayara-server --replay testdata/pcap/navico-halo24.pcap.gz`), which makes a
deterministic radar to run a headless client against, and it is the only way
to catch the two repositories drifting apart on the wire.
