/******************************************************************************
 * mayara_pi tests - the control schema and value store (src/RadarControls.cpp,
 * include/RadarControls.h).
 *
 * The generation counters are the contract with the UI: the controls panel
 * rebuilds itself whenever the schema generation moves, and mayara-server
 * repeats a control's metadata with every value update. If an unchanged
 * repeat bumped the counter the panel would rebuild several times a second,
 * so "an identical UpdateDef changes nothing" is a real behavioural
 * requirement, not an implementation detail.
 *****************************************************************************/
#include <doctest/doctest.h>

#include <clocale>
#include <string>
#include <vector>

#include "RadarControls.h"

namespace {

ControlDef GainDef() {
  ControlDef d;
  d.id = "gain";
  d.numeric_id = 3;
  d.name = "Gain";
  d.dataType = "number";
  d.category = "picture";
  d.hasAuto = true;
  d.has_min = true;
  d.has_max = true;
  d.minValue = 0;
  d.maxValue = 100;
  return d;
}

// The first of these that the host has; an empty result means none of them
// are installed and the locale half of the test cannot run.
std::string SetCommaLocale() {
  for (const char* name : {"de_DE.UTF-8", "nl_NL.UTF-8", "fr_FR.UTF-8",
                           "de_DE.utf8", "nl_NL.utf8", "fr_FR.utf8",
                           "de_DE", "nl_NL"}) {
    if (std::setlocale(LC_NUMERIC, name)) return name;
  }
  return std::string();
}

}  // namespace

TEST_SUITE("RadarControls") {

TEST_CASE("JsonNum writes a full stop whatever the process locale says") {
  CHECK(JsonNum(1.5) == "1.5");
  CHECK(JsonNum(0.0) == "0");
  CHECK(JsonNum(-3.25) == "-3.25");
  CHECK(JsonNum(1919.0) == "1919");

  // The bug this guards: %g follows LC_NUMERIC, and a comma decimal separator
  // turns {"value":1,5} into malformed JSON -- losing the whole body, not just
  // the field. OpenCPN adopts the user's locale, so this is the common case in
  // half of Europe, not an exotic one.
  const std::string locale = SetCommaLocale();
  if (locale.empty()) {
    MESSAGE("no comma-decimal locale installed; skipping the locale check");
  } else {
    CAPTURE(locale);
    CHECK(JsonNum(1.5) == "1.5");
    CHECK(JsonNum(-0.125) == "-0.125");
    CHECK(JsonNum(1919.5) == "1919.5");
    std::setlocale(LC_NUMERIC, "C");
  }
}

TEST_CASE("JsonNum never emits a comma, whatever the magnitude") {
  const double values[] = {0.0,     1.0,      -1.0,     0.1,   1e-7,
                           1234567.0, 1e20,   -1e-20,   3.14159265358979};
  for (double v : values) {
    CAPTURE(v);
    CHECK(JsonNum(v).find(',') == std::string::npos);
  }
}

TEST_CASE("a fresh store has no schema and no values") {
  RadarControls c;
  CHECK_FALSE(c.HasSchema());
  CHECK(c.Schema().empty());
  CHECK(c.SupportedRanges().empty());
  CHECK(c.Generation() == 0);
  CHECK(c.SchemaGeneration() == 0);

  const ControlValue v = c.Value("gain");
  CHECK_FALSE(v.has_value);
  CHECK(v.error.empty());
  CHECK(v.allowed);
}

TEST_CASE("SetSchema stores the definitions in the order they arrived") {
  RadarControls c;
  ControlDef range;
  range.id = "range";
  ControlDef gain = GainDef();
  ControlDef sea;
  sea.id = "sea";
  sea.hasAutoAdjustable = true;  // HALO Sea: auto with an offset

  c.SetSchema({range, gain, sea}, {50, 75, 100, 250, 500});

  CHECK(c.HasSchema());
  CHECK(c.Generation() > 0);
  CHECK(c.SchemaGeneration() > 0);

  const std::vector<ControlDef> got = c.Schema();
  REQUIRE(got.size() == 3);
  CHECK(got[0].id == "range");
  CHECK(got[1].id == "gain");
  CHECK(got[2].id == "sea");
  CHECK(got[2].hasAutoAdjustable);

  CHECK(c.SupportedRanges() == std::vector<int>{50, 75, 100, 250, 500});
}

TEST_CASE("re-sending an identical definition does not disturb the panel") {
  RadarControls c;
  c.SetSchema({GainDef()}, {});
  const uint64_t schema_gen = c.SchemaGeneration();

  c.UpdateDef(GainDef());
  CHECK(c.SchemaGeneration() == schema_gen);

  // ...even repeatedly, which is what the control stream actually does.
  for (int i = 0; i < 10; ++i) c.UpdateDef(GainDef());
  CHECK(c.SchemaGeneration() == schema_gen);
  CHECK(c.Schema().size() == 1);
}

TEST_CASE("a definition that really changed bumps the schema generation") {
  RadarControls c;
  c.SetSchema({GainDef()}, {});

  auto changes = [&](const ControlDef& def) {
    const uint64_t before = c.SchemaGeneration();
    c.UpdateDef(def);
    return c.SchemaGeneration() > before;
  };

  SUBCASE("a new range") {
    ControlDef d = GainDef();
    d.maxValue = 255;
    CHECK(changes(d));
  }
  SUBCASE("a new label") {
    ControlDef d = GainDef();
    d.name = "Gain (dB)";
    CHECK(changes(d));
  }
  SUBCASE("auto gained an adjustable offset") {
    ControlDef d = GainDef();
    d.hasAutoAdjustable = true;
    d.autoAdjustMin = -50;
    d.autoAdjustMax = 50;
    CHECK(changes(d));
  }
  SUBCASE("the enum labels changed") {
    ControlDef d = GainDef();
    d.descriptions[0] = "Off";
    d.descriptions[1] = "On";
    CHECK(changes(d));
  }
  SUBCASE("the settable values changed") {
    ControlDef d = GainDef();
    d.validValues = {0, 1, 2};
    CHECK(changes(d));
  }
  SUBCASE("it became read-only") {
    ControlDef d = GainDef();
    d.isReadOnly = true;
    CHECK(changes(d));
  }

  CHECK(c.Schema().size() == 1);  // it replaced the definition, not added one
}

TEST_CASE("a definition for an unknown control is appended") {
  RadarControls c;
  c.SetSchema({GainDef()}, {});
  const uint64_t before = c.SchemaGeneration();

  ControlDef doppler;
  doppler.id = "doppler";
  doppler.dataType = "enum";
  doppler.descriptions = {{0, "Off"}, {1, "Normal"}, {2, "Approaching only"}};
  doppler.validValues = {0, 1, 2};
  c.UpdateDef(doppler);

  CHECK(c.SchemaGeneration() > before);
  const std::vector<ControlDef> got = c.Schema();
  REQUIRE(got.size() == 2);
  CHECK(got[1].id == "doppler");
  CHECK(got[1].descriptions.at(2) == "Approaching only");
}

TEST_CASE("values are stored per control and bump only the value generation") {
  RadarControls c;
  c.SetSchema({GainDef()}, {});
  const uint64_t schema_gen = c.SchemaGeneration();
  const uint64_t gen = c.Generation();

  ControlValue v;
  v.has_value = true;
  v.value = 42;
  v.has_auto = true;
  v.auto_ = true;
  v.autoValue = 7;
  c.SetValue("gain", v);

  CHECK(c.Generation() > gen);
  CHECK(c.SchemaGeneration() == schema_gen);  // the panel need not rebuild

  const ControlValue got = c.Value("gain");
  CHECK(got.has_value);
  CHECK(got.value == doctest::Approx(42));
  CHECK(got.has_auto);
  CHECK(got.auto_);
  CHECK(got.autoValue == doctest::Approx(7));

  // An unrelated control is unaffected.
  CHECK_FALSE(c.Value("sea").has_value);
}

TEST_CASE("a later value for the same control replaces the earlier one") {
  RadarControls c;
  ControlValue first;
  first.has_value = true;
  first.value = 10;
  c.SetValue("gain", first);

  ControlValue second;
  second.has_value = true;
  second.value = 20;
  second.error = "not while transmitting";
  second.allowed = false;
  c.SetValue("gain", second);

  const ControlValue got = c.Value("gain");
  CHECK(got.value == doctest::Approx(20));
  CHECK(got.error == "not while transmitting");
  CHECK_FALSE(got.allowed);
}

TEST_CASE("a second SetSchema replaces the first one wholesale") {
  RadarControls c;
  c.SetSchema({GainDef()}, {50, 100});

  ControlDef sea;
  sea.id = "sea";
  c.SetSchema({sea}, {250, 500});

  const std::vector<ControlDef> got = c.Schema();
  REQUIRE(got.size() == 1);
  CHECK(got[0].id == "sea");
  CHECK(c.SupportedRanges() == std::vector<int>{250, 500});
}

}  // TEST_SUITE("RadarControls")
