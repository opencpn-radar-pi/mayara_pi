/******************************************************************************
 * mayara_pi tests - the polar raster and CPU renderer (src/RadarState.cpp).
 *
 * This is where a silent regression hurts most: the geometry is only ever
 * checked by eye, on a boat, against a coastline the skipper already knows.
 * The tests below pin the two things the picture depends on -- that spoke 0
 * points at the bow and angles run clockwise, and that an echo's distance from
 * the centre follows the cell count of the spoke it arrived in -- plus the
 * legend, threshold and palette mapping on top of them.
 *****************************************************************************/
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "RadarPalette.h"
#include "RadarState.h"
#include "Rgba.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

Rgba C(int r, int g, int b, int a = 255) {
  Rgba c;
  c.r = static_cast<uint8_t>(r);
  c.g = static_cast<uint8_t>(g);
  c.b = static_cast<uint8_t>(b);
  c.a = static_cast<uint8_t>(a);
  return c;
}

bool Same(const Rgba& a, const Rgba& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A legend whose entry i is (10*i, 0, 0): the index an echo carried can be
// read straight back off a rendered pixel.
std::vector<Rgba> CountingLegend(int n) {
  std::vector<Rgba> legend;
  legend.push_back(C(0, 0, 0, 0));  // index 0 is "no echo"
  for (int i = 1; i < n; ++i) legend.push_back(C(10 * i, 0, 0, 255));
  return legend;
}

// data[i] = i, so the cell an echo landed in is readable from its colour.
std::vector<uint8_t> CountingSpoke(int cells) {
  std::vector<uint8_t> d(static_cast<size_t>(cells));
  for (int i = 0; i < cells; ++i) d[i] = static_cast<uint8_t>(i);
  return d;
}

// The disc is square, bow-up and centred; bearing is degrees clockwise from
// the bow, distance is in disc pixels from the centre.
Rgba DiscAt(const std::vector<uint8_t>& disc, int size, double bearing_deg,
            double dist_px) {
  const double b = bearing_deg * kPi / 180.0;
  const int x = static_cast<int>(std::lround(size / 2.0 + dist_px * std::sin(b)));
  const int y = static_cast<int>(std::lround(size / 2.0 - dist_px * std::cos(b)));
  REQUIRE(x >= 0);
  REQUIRE(y >= 0);
  REQUIRE(x < size);
  REQUIRE(y < size);
  const uint8_t* p = &disc[(static_cast<size_t>(y) * size + x) * 4];
  return C(p[0], p[1], p[2], p[3]);
}

// Mid-cell distance for cell `cell` of `cells`, in disc pixels. The disc's
// usable radius is (size/2 - 1); sampling the middle of a cell keeps the
// assertion clear of the rounding at its edges.
double CellDist(int size, int cell, int cells) {
  const double radius = size / 2.0 - 1.0;
  return (cell + 0.5) * radius / cells;
}

// Configure + one spoke, the setup nearly every case here needs.
// Four spokes per revolution means one spoke covers a whole quadrant, which
// makes an orientation mistake obvious rather than subtle.
struct Fixture {
  RadarState state;
  std::vector<uint8_t> disc;
  int size = 0;

  void Configure(int spokes, int cells, std::vector<Rgba> legend) {
    state.Configure(spokes, cells, std::move(legend));
  }

  void Refresh() {
    size = state.CopyDisc(disc);
  }

  Rgba At(double bearing_deg, double dist_px) {
    return DiscAt(disc, size, bearing_deg, dist_px);
  }
};

}  // namespace

TEST_SUITE("RadarState") {

TEST_CASE("an unconfigured state renders nothing and says so") {
  RadarState s;
  CHECK_FALSE(s.Configured());
  CHECK(s.DiscSize() == 0);
  CHECK(s.RangeMeters() == 0);

  std::vector<uint8_t> disc;
  CHECK(s.CopyDisc(disc) == 0);

  std::vector<uint8_t> rgb(16 * 16 * 3, 0xAA);
  CHECK_FALSE(s.RenderPPI(rgb.data(), 16, 16));
  // Even with no data the caller's buffer must come back cleared, not left
  // showing whatever was in it before.
  for (uint8_t v : rgb) CHECK(v == 0);
}

TEST_CASE("Configure sets up the geometry and bumps the generation") {
  RadarState s;
  const uint64_t before = s.Generation();
  s.Configure(2048, 512, CountingLegend(8));

  CHECK(s.Configured());
  CHECK(s.DiscSize() == 1024);
  CHECK(s.Generation() > before);

  std::vector<uint8_t> disc;
  CHECK(s.CopyDisc(disc) == 0);  // configured, but no spokes yet
}

TEST_CASE("a radar that reports no spokes is refused, not divided by") {
  RadarState s;
  s.Configure(0, 512, CountingLegend(8));
  CHECK_FALSE(s.Configured());
  CHECK(s.DiscSize() == 0);

  const std::vector<uint8_t> echo{1, 1};
  s.WriteSpoke(0, echo.data(), echo.size(), 1852);  // nowhere to put it
  std::vector<uint8_t> disc;
  CHECK(s.CopyDisc(disc) == 0);
}

TEST_CASE("a zero-sized PPI is a no-op rather than a divide by zero") {
  Fixture f;
  f.Configure(4, 4, CountingLegend(5));
  const std::vector<uint8_t> echo{1, 1, 1, 1};
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);

  uint8_t unused = 0xAA;
  CHECK(f.state.RenderPPI(&unused, 0, 0));
  CHECK(unused == 0xAA);
}

TEST_CASE("spoke 0 points at the bow and angles run clockwise") {
  Fixture f;
  f.Configure(4, 4, CountingLegend(8));
  const std::vector<uint8_t> echo{1, 1, 1, 1};

  // Only the first quadrant is swept.
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);
  f.Refresh();
  REQUIRE(f.size == 1024);

  const double d = CellDist(f.size, 1, 4);
  CHECK(f.At(45, d).a != 0);    // between bow and starboard beam: swept
  CHECK(f.At(135, d).a == 0);
  CHECK(f.At(225, d).a == 0);
  CHECK(f.At(315, d).a == 0);

  // Now the third quadrant, i.e. astern to port. Clockwise means spoke 2.
  f.state.WriteSpoke(2, echo.data(), echo.size(), 1852);
  f.Refresh();
  CHECK(f.At(45, d).a != 0);
  CHECK(f.At(135, d).a == 0);
  CHECK(f.At(225, d).a != 0);
  CHECK(f.At(315, d).a == 0);
}

TEST_CASE("an echo's distance follows the cell it arrived in") {
  const int kCells = 12;
  Fixture f;
  f.Configure(4, kCells, CountingLegend(kCells + 1));
  const std::vector<uint8_t> data = CountingSpoke(kCells);
  f.state.WriteSpoke(0, data.data(), data.size(), 1852);
  f.Refresh();
  REQUIRE(f.size == 1024);

  // Cell i carries legend index i, which CountingLegend paints (10*i, 0, 0).
  // Cell 0 carries index 0, i.e. no echo, so start at 1.
  for (int cell = 1; cell < kCells; ++cell) {
    CAPTURE(cell);
    const Rgba got = f.At(45, CellDist(f.size, cell, kCells));
    CHECK(got.r == 10 * cell);
    CHECK(got.a == 255);
  }

  // Beyond the last cell, and beyond the disc, there is nothing.
  CHECK(f.At(45, f.size / 2.0 + 4).a == 0);
}

TEST_CASE("a shorter spoke does not stretch to the edge") {
  const int kCells = 12;
  Fixture f;
  f.Configure(4, kCells, CountingLegend(kCells + 1));

  // Half-length spoke: the radar is on a longer range than it has cells for.
  const std::vector<uint8_t> half{1, 1, 1, 1, 1, 1};
  f.state.WriteSpoke(0, half.data(), half.size(), 1852);
  f.Refresh();

  // The six cells spread over the whole radius -- the picture always fills the
  // disc, because the disc is the spoke's own range, not a fixed distance.
  for (int cell = 0; cell < 6; ++cell) {
    CAPTURE(cell);
    CHECK(f.At(45, CellDist(f.size, cell, 6)).a == 255);
  }
}

TEST_CASE("legend index 0 and out-of-range indices draw nothing") {
  Fixture f;
  f.Configure(4, 4, CountingLegend(5));  // valid indices are 1..4

  const std::vector<uint8_t> blank{0, 0, 0, 0};
  f.state.WriteSpoke(0, blank.data(), blank.size(), 1852);
  f.Refresh();
  for (int cell = 0; cell < 4; ++cell)
    CHECK(f.At(45, CellDist(f.size, cell, 4)).a == 0);

  const std::vector<uint8_t> beyond{9, 9, 9, 9};
  f.state.WriteSpoke(0, beyond.data(), beyond.size(), 1852);
  f.Refresh();
  for (int cell = 0; cell < 4; ++cell)
    CHECK(f.At(45, CellDist(f.size, cell, 4)).a == 0);
}

TEST_CASE("a spoke for an angle the radar does not have is dropped") {
  RadarState s;
  s.Configure(4, 4, CountingLegend(5));
  const uint64_t before = s.Generation();

  const std::vector<uint8_t> echo{1, 1, 1, 1};
  s.WriteSpoke(4, echo.data(), echo.size(), 1852);  // valid angles are 0..3

  CHECK(s.Generation() == before);
  CHECK(s.RangeMeters() == 0);
  std::vector<uint8_t> disc;
  CHECK(s.CopyDisc(disc) == 0);
}

TEST_CASE("a spoke longer than the configured maximum is clipped, not spilled") {
  Fixture f;
  f.Configure(4, 4, CountingLegend(5));

  const std::vector<uint8_t> too_long(64, 1);  // maxlen is 4
  f.state.WriteSpoke(0, too_long.data(), too_long.size(), 1852);
  f.Refresh();

  REQUIRE(f.size == 1024);
  CHECK(f.state.RangeMeters() == 1852);
  // The first four cells were taken; the rest of the spoke was dropped rather
  // than written past the end of the raster row.
  for (int cell = 0; cell < 4; ++cell) {
    CAPTURE(cell);
    CHECK(f.At(45, CellDist(f.size, cell, 4)).a == 255);
  }
}

TEST_CASE("writes are erased when the range changes under them") {
  Fixture f;
  f.Configure(4, 4, CountingLegend(5));
  const std::vector<uint8_t> echo{1, 1, 1, 1};

  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);
  f.Refresh();
  const double d = CellDist(f.size, 1, 4);
  REQUIRE(f.At(45, d).a == 255);

  // A new range makes every older echo wrong: it is at the wrong distance
  // until that bearing is swept again.
  f.state.WriteSpoke(2, echo.data(), echo.size(), 3704);
  f.Refresh();
  CHECK(f.At(45, d).a == 0);
  CHECK(f.At(225, d).a == 255);
  CHECK(f.state.RangeMeters() == 3704);
}

TEST_CASE("Clear erases the picture until the next sweep") {
  Fixture f;
  f.Configure(4, 4, CountingLegend(5));
  const std::vector<uint8_t> echo{1, 1, 1, 1};
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);
  f.Refresh();
  REQUIRE(f.size == 1024);

  const uint64_t before = f.state.Generation();
  f.state.Clear();
  CHECK(f.state.Generation() > before);

  std::vector<uint8_t> disc;
  CHECK(f.state.CopyDisc(disc) == 0);
  std::vector<uint8_t> rgb(32 * 32 * 3, 0xAA);
  CHECK_FALSE(f.state.RenderPPI(rgb.data(), 32, 32));

  // ...and a new spoke brings it back.
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);
  f.Refresh();
  CHECK(f.size == 1024);
  CHECK(f.At(45, CellDist(f.size, 1, 4)).a == 255);
}

TEST_CASE("the display threshold hides weak returns but never Doppler") {
  const int kCells = 8;
  Fixture f;
  f.Configure(4, kCells, CountingLegend(kCells + 1));
  // Bands as the server reports them: weak returns start at 1, medium at 3,
  // strong at 5. Indices above `strong` are the Doppler and trail colours.
  f.state.SetLegendBands(1, 3, 5);

  const std::vector<uint8_t> data = CountingSpoke(kCells);  // cell i -> index i
  f.state.WriteSpoke(0, data.data(), data.size(), 1852);

  auto alpha_at_cell = [&](int cell) {
    f.Refresh();
    return f.At(45, CellDist(f.size, cell, kCells)).a;
  };

  CHECK(f.state.Threshold() == 0);
  CHECK(alpha_at_cell(2) == 255);
  CHECK(alpha_at_cell(4) == 255);
  CHECK(alpha_at_cell(6) == 255);

  f.state.SetThreshold(1);  // hide weak
  CHECK(f.state.Threshold() == 1);
  CHECK(alpha_at_cell(2) == 0);    // index 2, below medium
  CHECK(alpha_at_cell(4) == 255);  // index 4, medium
  CHECK(alpha_at_cell(6) == 255);  // index 6, above strong

  f.state.SetThreshold(2);  // only strong
  CHECK(alpha_at_cell(2) == 0);
  CHECK(alpha_at_cell(4) == 0);
  CHECK(alpha_at_cell(6) == 255);

  f.state.SetThreshold(0);
  CHECK(alpha_at_cell(2) == 255);
}

TEST_CASE("the threshold level is clamped to the three it has") {
  RadarState s;
  s.Configure(4, 4, CountingLegend(5));
  s.SetThreshold(-7);
  CHECK(s.Threshold() == 0);
  s.SetThreshold(99);
  CHECK(s.Threshold() == 2);
}

TEST_CASE("the server's legend is left alone by the 'from server' palette") {
  const int kCells = 8;
  Fixture f;
  std::vector<Rgba> legend = CountingLegend(kCells + 1);
  f.Configure(4, kCells, legend);

  LegendLayout layout;
  layout.pixel_colors = 6;
  f.state.SetLegendLayout(layout);

  RadarPalette from_server = BuiltinPalettes()[0];
  REQUIRE(from_server.from_server);
  f.state.SetPalette(from_server);

  const std::vector<uint8_t> data = CountingSpoke(kCells);
  f.state.WriteSpoke(0, data.data(), data.size(), 1852);
  f.Refresh();

  for (int cell = 1; cell < kCells; ++cell) {
    CAPTURE(cell);
    CHECK(Same(f.At(45, CellDist(f.size, cell, kCells)), legend[cell]));
  }
}

TEST_CASE("a palette re-colours the legend by what each index means") {
  // A legend the way the server describes one: an echo ramp, a static
  // background entry, two Doppler entries and a trail ramp on the end.
  const int kCells = 12;
  Fixture f;
  f.Configure(4, kCells, CountingLegend(kCells));

  LegendLayout layout;
  layout.pixel_colors = 6;        // echo ramp is indices 1..5
  layout.static_background = 6;
  layout.doppler_approaching = 7;
  layout.doppler_receding = 8;
  layout.history_start = 9;       // trail ramp is 9..11
  f.state.SetLegendLayout(layout);

  RadarPalette p;
  p.name = "test";
  p.weak = C(10, 20, 30);
  p.medium = C(100, 110, 120);
  p.strong = C(200, 210, 220);
  p.background = C(1, 2, 3);
  p.doppler_approaching = C(255, 0, 255);
  p.doppler_receding = C(0, 255, 255);
  p.trail_start = C(250, 250, 250);
  p.trail_end = C(60, 60, 60);
  f.state.SetPalette(p);

  const std::vector<uint8_t> data = CountingSpoke(kCells);
  f.state.WriteSpoke(0, data.data(), data.size(), 1852);
  f.Refresh();

  auto at_index = [&](int idx) {
    return f.At(45, CellDist(f.size, idx, kCells));
  };

  CHECK(at_index(0).a == 0);                     // no echo, whatever the palette
  CHECK(Same(at_index(1), p.weak));              // bottom of the ramp
  CHECK(Same(at_index(5), p.strong));            // top of the ramp
  CHECK(Same(at_index(6), p.background));
  CHECK(Same(at_index(7), p.doppler_approaching));
  CHECK(Same(at_index(8), p.doppler_receding));
  CHECK(Same(at_index(9), p.trail_start));       // freshest trail
  CHECK(Same(at_index(11), p.trail_end));        // oldest trail

  // The middle of the ramp lies between weak and strong on every channel.
  const Rgba mid = at_index(3);
  CHECK(mid.r > p.weak.r);
  CHECK(mid.r < p.strong.r);
}

TEST_CASE("RenderPPI draws the disc and honours zoom, rotation and offset") {
  const int kCells = 8;
  const int kW = 256, kH = 256;
  Fixture f;
  f.Configure(4, kCells, CountingLegend(kCells + 1));
  const std::vector<uint8_t> echo(kCells, 4);
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);

  auto render = [&](std::vector<uint8_t>& out, double zoom, double rot,
                    double ox, double oy) {
    out.assign(static_cast<size_t>(kW) * kH * 3, 0xAA);
    return f.state.RenderPPI(out.data(), kW, kH, zoom, rot, ox, oy);
  };
  auto pix = [&](const std::vector<uint8_t>& buf, double bearing, double dist) {
    const double b = bearing * kPi / 180.0;
    const int x = static_cast<int>(std::lround(kW / 2.0 + dist * std::sin(b)));
    const int y = static_cast<int>(std::lround(kH / 2.0 - dist * std::cos(b)));
    const uint8_t* p = &buf[(static_cast<size_t>(y) * kW + x) * 3];
    return C(p[0], p[1], p[2]);
  };

  std::vector<uint8_t> plain;
  REQUIRE(render(plain, 1.0, 0.0, 0.0, 0.0));
  CHECK(pix(plain, 45, 64).r == 40);  // legend index 4 -> (40, 0, 0)
  CHECK(pix(plain, 225, 64).r == 0);  // unswept quadrant, blended over black

  SUBCASE("a non-positive zoom is treated as 1.0 rather than dividing by it") {
    std::vector<uint8_t> zero, negative;
    REQUIRE(render(zero, 0.0, 0.0, 0.0, 0.0));
    REQUIRE(render(negative, -3.0, 0.0, 0.0, 0.0));
    CHECK(zero == plain);
    CHECK(negative == plain);
  }

  SUBCASE("rotation turns the picture clockwise") {
    std::vector<uint8_t> turned;
    REQUIRE(render(turned, 1.0, 90.0, 0.0, 0.0));
    // The swept quadrant started between bow and starboard beam; a clockwise
    // quarter turn puts it between starboard beam and astern.
    CHECK(pix(turned, 135, 64).r == 40);
    CHECK(pix(turned, 45, 64).r == 0);
  }

  SUBCASE("zooming in magnifies about the sweep origin") {
    std::vector<uint8_t> zoomed;
    REQUIRE(render(zoomed, 2.0, 0.0, 0.0, 0.0));
    // At 2x, what was at 64 px from the centre is now at 128 px.
    CHECK(pix(zoomed, 45, 128).r == 40);
  }

  SUBCASE("an offset moves the origin, and nothing else") {
    std::vector<uint8_t> shifted;
    REQUIRE(render(shifted, 1.0, 0.0, 10.0, 0.0));
    // Pixel (x + 10) of the offset render sees exactly what pixel x of the
    // centred one did.
    for (int y = 0; y < kH; y += 16) {
      for (int x = 0; x < kW - 10; x += 16) {
        CAPTURE(x);
        CAPTURE(y);
        const size_t a = (static_cast<size_t>(y) * kW + x + 10) * 3;
        const size_t b = (static_cast<size_t>(y) * kW + x) * 3;
        CHECK(shifted[a] == plain[b]);
        CHECK(shifted[a + 1] == plain[b + 1]);
        CHECK(shifted[a + 2] == plain[b + 2]);
      }
    }
  }
}

TEST_CASE("intensity dims the echoes") {
  const int kW = 128, kH = 128;
  Fixture f;
  f.Configure(4, 4, {C(0, 0, 0, 0), C(200, 100, 50, 255)});
  const std::vector<uint8_t> echo{1, 1, 1, 1};
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);

  std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3, 0);
  auto sample = [&]() {
    REQUIRE(f.state.RenderPPI(rgb.data(), kW, kH));
    const int x = static_cast<int>(std::lround(kW / 2.0 + 20 * std::sin(kPi / 4)));
    const int y = static_cast<int>(std::lround(kH / 2.0 - 20 * std::cos(kPi / 4)));
    const uint8_t* p = &rgb[(static_cast<size_t>(y) * kW + x) * 3];
    return C(p[0], p[1], p[2]);
  };

  Rgba full = sample();
  CHECK(full.r == 200);
  CHECK(full.g == 100);
  CHECK(full.b == 50);

  f.state.SetIntensity(0.5f);
  Rgba dim = sample();
  CHECK(dim.r == 100);
  CHECK(dim.g == 50);
  CHECK(dim.b == 25);
}

TEST_CASE("RenderOverlayRGBA keeps the legend's alpha and cuts the inner hole") {
  const int kSize = 256;
  Fixture f;
  f.Configure(4, 8, {C(0, 0, 0, 0), C(40, 80, 120, 200)});
  const std::vector<uint8_t> echo(8, 1);
  f.state.WriteSpoke(0, echo.data(), echo.size(), 1852);

  std::vector<uint8_t> rgba(static_cast<size_t>(kSize) * kSize * 4, 0xAA);
  uint64_t generation = 0;
  REQUIRE(f.state.RenderOverlayRGBA(rgba.data(), kSize, 0.0, 0.0, &generation));
  CHECK(generation == f.state.Generation());

  auto pix = [&](double bearing, double dist) {
    const double b = bearing * kPi / 180.0;
    const int x = static_cast<int>(std::lround(kSize / 2.0 + dist * std::sin(b)));
    const int y = static_cast<int>(std::lround(kSize / 2.0 - dist * std::cos(b)));
    const uint8_t* p = &rgba[(static_cast<size_t>(y) * kSize + x) * 4];
    return C(p[0], p[1], p[2], p[3]);
  };

  // Straight RGBA: the legend's own alpha comes through, not blended to black.
  Rgba echo_px = pix(45, 60);
  CHECK(echo_px.a == 200);
  CHECK(echo_px.r == 40);

  CHECK(pix(225, 60).a == 0);  // unswept: transparent, not black
  CHECK(rgba[0] == 0);         // the corners fall outside the circle

  SUBCASE("an inner hole lets a shorter-range radar draw through") {
    REQUIRE(f.state.RenderOverlayRGBA(rgba.data(), kSize, 0.0, 0.5, nullptr));
    CHECK(pix(45, 20).a == 0);     // inside the hole
    CHECK(pix(45, 100).a == 200);  // outside it
  }

  SUBCASE("a non-positive size is refused before anything is written") {
    std::vector<uint8_t> canary(64, 0x5A);
    CHECK_FALSE(f.state.RenderOverlayRGBA(canary.data(), 0, 0.0, 0.0, nullptr));
    CHECK_FALSE(f.state.RenderOverlayRGBA(canary.data(), -8, 0.0, 0.0, nullptr));
    for (uint8_t v : canary) CHECK(v == 0x5A);
  }
}

TEST_CASE("heading is the difference between true bearing and bow-relative angle") {
  RadarState s;
  double heading = -1;
  CHECK_FALSE(s.Heading(heading));
  CHECK(s.HeadingAgeMs() == -1);

  // Before Configure there is no spoke count to scale by, so it is ignored.
  s.SetHeadingFromBearing(0, 512);
  CHECK_FALSE(s.Heading(heading));

  s.Configure(2048, 512, CountingLegend(4));
  s.SetHeadingFromBearing(0, 512);  // a quarter of 2048 spokes
  REQUIRE(s.Heading(heading));
  CHECK(heading == doctest::Approx(90.0));
  CHECK(s.HeadingAgeMs() >= 0);

  s.SetHeadingFromBearing(1024, 0);  // wraps negative
  REQUIRE(s.Heading(heading));
  CHECK(heading == doctest::Approx(180.0));

  s.SetHeadingFromBearing(1000, 1000);
  REQUIRE(s.Heading(heading));
  CHECK(heading == doctest::Approx(0.0));
}

TEST_CASE("position is remembered once the spokes carry one") {
  RadarState s;
  double lat = 0, lon = 0;
  CHECK_FALSE(s.Position(lat, lon));

  s.SetPosition(52.0907374, 5.1214201);
  REQUIRE(s.Position(lat, lon));
  CHECK(lat == doctest::Approx(52.0907374));
  CHECK(lon == doctest::Approx(5.1214201));
}

}  // TEST_SUITE("RadarState")
