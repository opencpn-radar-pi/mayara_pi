/******************************************************************************
 * mayara_pi tests - the built-in palettes and their config round trip
 * (src/RadarPalette.cpp).
 *
 * PaletteFromString parses a line out of the OpenCPN config file, which means
 * it parses whatever a hand-edited or half-written config happens to contain.
 * Everything it must reject is as much of a test as the round trip itself.
 *****************************************************************************/
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "RadarPalette.h"
#include "Rgba.h"

namespace {

bool Same(const Rgba& a, const Rgba& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The eight colours a palette carries, in the order the config line uses.
std::vector<Rgba> Colours(const RadarPalette& p) {
  return {p.weak,        p.medium,      p.strong,
          p.doppler_approaching,        p.doppler_receding,
          p.trail_start, p.trail_end,   p.background};
}

}  // namespace

TEST_SUITE("RadarPalette") {

TEST_CASE("the built-ins are all there, with the server's own first") {
  const std::vector<RadarPalette> all = BuiltinPalettes();
  REQUIRE(all.size() == 5);

  CHECK(all[0].name == "Standard Mayara");
  CHECK(all[0].from_server);
  CHECK(all[1].name == "Red");
  CHECK(all[2].name == "Yellow");
  CHECK(all[3].name == "Green");
  CHECK(all[4].name == "Blue");

  for (const RadarPalette& p : all) {
    CAPTURE(p.name);
    CHECK(p.builtin);
    CHECK_FALSE(p.name.empty());
    // Every colour is opaque; a transparent palette entry would make an echo
    // vanish rather than change colour.
    for (const Rgba& c : Colours(p)) CHECK(c.a == 255);
  }

  // Only "Standard Mayara" leaves the server's legend alone.
  for (size_t i = 1; i < all.size(); ++i) CHECK_FALSE(all[i].from_server);
}

TEST_CASE("the echo ramp of every hue palette runs dark to light") {
  const std::vector<RadarPalette> all = BuiltinPalettes();
  for (size_t i = 1; i < all.size(); ++i) {
    const RadarPalette& p = all[i];
    CAPTURE(p.name);
    const int weak = p.weak.r + p.weak.g + p.weak.b;
    const int medium = p.medium.r + p.medium.g + p.medium.b;
    const int strong = p.strong.r + p.strong.g + p.strong.b;
    CHECK(weak < medium);
    CHECK(medium < strong);
  }
}

TEST_CASE("Doppler stays distinguishable from the echoes around it") {
  const std::vector<RadarPalette> all = BuiltinPalettes();
  for (size_t i = 1; i < all.size(); ++i) {
    const RadarPalette& p = all[i];
    CAPTURE(p.name);
    CHECK_FALSE(Same(p.doppler_approaching, p.doppler_receding));
    CHECK_FALSE(Same(p.doppler_approaching, p.medium));
    CHECK_FALSE(Same(p.doppler_receding, p.medium));
  }
}

TEST_CASE("every built-in survives the config round trip") {
  for (const RadarPalette& original : BuiltinPalettes()) {
    CAPTURE(original.name);
    RadarPalette parsed;
    REQUIRE(PaletteFromString(PaletteToString(original), &parsed));

    CHECK(parsed.name == original.name);
    const std::vector<Rgba> want = Colours(original);
    const std::vector<Rgba> got = Colours(parsed);
    REQUIRE(got.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) {
      CAPTURE(i);
      CHECK(Same(got[i], want[i]));
    }

    // The line carries colours and a name, nothing else: a parsed palette is
    // always a user palette, even when it was written from a built-in.
    CHECK_FALSE(parsed.builtin);
    CHECK_FALSE(parsed.from_server);
  }
}

TEST_CASE("a name with spaces comes back intact") {
  RadarPalette p;
  p.name = "My Night Palette";
  RadarPalette parsed;
  REQUIRE(PaletteFromString(PaletteToString(p), &parsed));
  CHECK(parsed.name == "My Night Palette");
}

TEST_CASE("colours may be written with or without alpha, in either case") {
  RadarPalette p;
  const std::string six =
      "Six|ff0000|00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050";
  REQUIRE(PaletteFromString(six, &p));
  CHECK(Same(p.weak, Rgba{255, 0, 0, 255}));  // alpha defaults to opaque
  CHECK(p.background.a == 255);

  const std::string eight =
      "Eight|ff000080|00ff00ff|0000ffff|ff00ffff|00ffffff|ffffffff|454545ff|"
      "505050ff";
  REQUIRE(PaletteFromString(eight, &p));
  CHECK(p.weak.a == 0x80);

  const std::string upper =
      "Upper|FF0000|00FF00|0000FF|FF00FF|00FFFF|FFFFFF|454545|505050";
  REQUIRE(PaletteFromString(upper, &p));
  CHECK(Same(p.weak, Rgba{255, 0, 0, 255}));
}

TEST_CASE("a line that is not a palette is rejected") {
  RadarPalette p;
  const RadarPalette untouched;

  SUBCASE("empty") { CHECK_FALSE(PaletteFromString("", &p)); }
  SUBCASE("no colours") { CHECK_FALSE(PaletteFromString("Name", &p)); }
  SUBCASE("too few colours") {
    CHECK_FALSE(PaletteFromString("Name|ff0000|00ff00|0000ff", &p));
  }
  SUBCASE("missing the last colour") {
    CHECK_FALSE(PaletteFromString(
        "Name|ff0000|00ff00|0000ff|ff00ff|00ffff|ffffff|454545", &p));
  }
  SUBCASE("no name") {
    CHECK_FALSE(PaletteFromString(
        "|ff0000|00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050", &p));
  }
  SUBCASE("a colour that is not hex") {
    CHECK_FALSE(PaletteFromString(
        "Name|gg0000|00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050", &p));
  }
  SUBCASE("a colour too short to be one") {
    CHECK_FALSE(PaletteFromString(
        "Name|ff00|00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050", &p));
  }
  SUBCASE("an empty colour field") {
    CHECK_FALSE(PaletteFromString(
        "Name||00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050", &p));
  }
  SUBCASE("a bad alpha nibble") {
    CHECK_FALSE(PaletteFromString(
        "Name|ff0000zz|00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050", &p));
  }

  // A rejected line must leave the caller's palette as it found it, so a
  // corrupt config entry cannot half-overwrite a good palette.
  CHECK(p.name == untouched.name);
  CHECK(Same(p.weak, untouched.weak));
}

TEST_CASE("trailing fields are ignored rather than failing the parse") {
  // Room to add a field to the line later without older builds refusing it.
  RadarPalette p;
  REQUIRE(PaletteFromString(
      "Name|ff0000|00ff00|0000ff|ff00ff|00ffff|ffffff|454545|505050|extra|more",
      &p));
  CHECK(p.name == "Name");
  CHECK(Same(p.background, Rgba{0x50, 0x50, 0x50, 255}));
}

}  // TEST_SUITE("RadarPalette")
