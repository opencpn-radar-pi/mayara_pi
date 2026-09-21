/******************************************************************************
 * mayara_pi tests - the mayara-server wire protocol (src/MayaraProtocol.cpp).
 *
 * The JSON here is shaped after mayara-server's own serialisation, not
 * invented: ControlDefinition and AutomaticValue in its
 * src/lib/radar/settings.rs, and ArpaTargetApi in src/lib/radar/target/mod.rs.
 * Two details from there that a test is the only way to keep hold of:
 *
 *  - AutomaticValue is `#[serde(flatten)]`ed into the control object, so
 *    hasAuto/hasAutoAdjustable/autoAdjust*Value arrive as siblings of name and
 *    dataType rather than under an "automatic" key.
 *  - nearly every field is `skip_serializing_if`, so "absent" is the normal
 *    way to say false or none. A parser that expected nulls would see nothing.
 *****************************************************************************/
#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include <nlohmann/json.hpp>

#include "MayaraProtocol.h"

using nlohmann::json;
using namespace MayaraProtocol;

TEST_SUITE("MayaraProtocol") {

TEST_CASE("trailing slashes come off a base URL") {
  auto strip = [](std::string s) {
    StripTrailingSlash(s);
    return s;
  };
  CHECK(strip("http://10.56.0.1:6502/") == "http://10.56.0.1:6502");
  CHECK(strip("http://10.56.0.1:6502///") == "http://10.56.0.1:6502");
  CHECK(strip("http://10.56.0.1:6502") == "http://10.56.0.1:6502");
  CHECK(strip("") == "");
  CHECK(strip("///") == "");
}

TEST_CASE("the spoke stream URL is the HTTP base turned WebSocket") {
  CHECK(WsBase("http://10.56.0.1:6502") == "ws://10.56.0.1:6502");
  CHECK(WsBase("https://boat.local") == "wss://boat.local");
  CHECK(WsBase("ws://already") == "ws://already");   // left alone
  CHECK(WsBase("boat.local:6502") == "boat.local:6502");

  CHECK(WsUrl("http://10.56.0.1:6502", "nav1034A") ==
        "ws://10.56.0.1:6502/signalk/v2/api/vessels/self/radars/nav1034A/"
        "spokes");
  CHECK(WsUrl("https://boat.local", "garmin0") ==
        "wss://boat.local/signalk/v2/api/vessels/self/radars/garmin0/spokes");
}

TEST_CASE("the same host on mayara's own port") {
  // The radar API answers wherever it is mounted, but the web GUI only ever
  // lives on 6502, so the plugin has to be able to find its way there.
  CHECK(OnMayaraPort("http://10.56.0.1:3000") == "http://10.56.0.1:6502");
  CHECK(OnMayaraPort("http://boat.local") == "http://boat.local:6502");
  CHECK(OnMayaraPort("https://boat.local") == "https://boat.local:6502");

  SUBCASE("already there, so there is nowhere to redirect to") {
    CHECK(OnMayaraPort("http://10.56.0.1:6502").empty());
    CHECK(OnMayaraPort("https://boat.local:6502").empty());
  }

  SUBCASE("an IPv6 literal's colons are not a port") {
    CHECK(OnMayaraPort("http://[fe80::1]:3000") == "http://[fe80::1]:6502");
    CHECK(OnMayaraPort("http://[fe80::1]") == "http://[fe80::1]:6502");
    CHECK(OnMayaraPort("http://[fe80::1]:6502").empty());
  }

  SUBCASE("anything it cannot take apart") {
    CHECK(OnMayaraPort("10.56.0.1:6502").empty());  // no scheme
    CHECK(OnMayaraPort("").empty());
    CHECK(OnMayaraPort("http://").empty());         // no host
  }

  SUBCASE("a path is dropped: the GUI lives at the root of that port") {
    CHECK(OnMayaraPort("http://boat.local:3000/signalk/v2/api") ==
          "http://boat.local:6502");
  }
}

TEST_CASE("a Signal K server without mayara is not mistaken for a radar API") {
  // Reaching Signal K is no proof the radar plugin is there; settling on such
  // a server means never producing a radar.
  CHECK_FALSE(LooksLikeRadarApi(json::object()));
  CHECK_FALSE(LooksLikeRadarApi(json::parse(R"({"version":"3.4.0"})")));
  CHECK_FALSE(LooksLikeRadarApi(json("a string")));
  CHECK_FALSE(LooksLikeRadarApi(json(42)));
  CHECK_FALSE(LooksLikeRadarApi(json(nullptr)));
  // A field that is not an object tells us nothing either.
  CHECK_FALSE(LooksLikeRadarApi(json::parse(R"({"self":"vessels.urn:x"})")));

  SUBCASE("the shapes mayara really answers with") {
    CHECK(LooksLikeRadarApi(json::parse(R"({"radars":{}})")));
    CHECK(LooksLikeRadarApi(
        json::parse(R"({"version":"3.4.0","radars":{"nav1034A":{}}})")));
    // Older shape: radars keyed at the top level.
    CHECK(LooksLikeRadarApi(json::parse(R"({"nav1034A":{"name":"Halo"}})")));
    CHECK(LooksLikeRadarApi(json::array()));
  }
}

TEST_CASE("a control definition, as mayara-server serialises one") {
  // A HALO gain control: automatic is flattened in, so hasAuto and its
  // adjustable range are siblings of name and dataType.
  const json c = json::parse(R"({
    "id": 3,
    "name": "Gain",
    "description": "Receiver gain",
    "category": "picture",
    "dataType": "number",
    "units": "%",
    "hasAuto": true,
    "hasAutoAdjustable": true,
    "autoAdjustMinValue": -50,
    "autoAdjustMaxValue": 50,
    "minValue": 0,
    "maxValue": 100,
    "stepValue": 1
  })");

  const ControlDef d = ParseControlDef("gain", c);
  CHECK(d.id == "gain");
  CHECK(d.numeric_id == 3);
  CHECK(d.name == "Gain");
  CHECK(d.description == "Receiver gain");
  CHECK(d.category == "picture");
  CHECK(d.dataType == "number");
  CHECK(d.units == "%");
  CHECK(d.hasAuto);
  CHECK(d.hasAutoAdjustable);
  CHECK(d.autoAdjustMin == doctest::Approx(-50));
  CHECK(d.autoAdjustMax == doctest::Approx(50));
  CHECK(d.has_min);
  CHECK(d.minValue == doctest::Approx(0));
  CHECK(d.has_max);
  CHECK(d.maxValue == doctest::Approx(100));
  CHECK(d.has_step);
  CHECK(d.stepValue == doctest::Approx(1));
  CHECK_FALSE(d.isReadOnly);
}

TEST_CASE("an absent field means false or none, not a parse failure") {
  // Almost every field is skip_serializing_if on the server, so a plain enum
  // control arrives with most of them simply missing.
  const json c = json::parse(R"({"id": 7, "dataType": "enum"})");
  const ControlDef d = ParseControlDef("doppler", c);

  CHECK(d.id == "doppler");
  CHECK(d.name == "doppler");  // falls back to the key it arrived under
  CHECK(d.description.empty());
  CHECK(d.units.empty());
  CHECK_FALSE(d.hasAuto);
  CHECK_FALSE(d.hasAutoAdjustable);
  CHECK_FALSE(d.hasEnabled);
  CHECK_FALSE(d.isReadOnly);
  CHECK_FALSE(d.has_min);
  CHECK_FALSE(d.has_max);
  CHECK_FALSE(d.has_step);
  CHECK(d.descriptions.empty());
  CHECK(d.validValues.empty());
  CHECK(d.maxDistance == doctest::Approx(0));
}

TEST_CASE("enum labels and settable values") {
  const json c = json::parse(R"({
    "id": 9,
    "name": "Doppler",
    "dataType": "enum",
    "descriptions": {"0": "Off", "1": "Normal", "2": "Approaching only"},
    "validValues": [0, 1, 2],
    "isReadOnly": true
  })");

  const ControlDef d = ParseControlDef("doppler", c);
  REQUIRE(d.descriptions.size() == 3);
  CHECK(d.descriptions.at(0) == "Off");
  CHECK(d.descriptions.at(2) == "Approaching only");
  CHECK(d.validValues == std::vector<int>{0, 1, 2});
  CHECK(d.isReadOnly);
}

TEST_CASE("a label key that is not a number is dropped, not thrown") {
  // A server that grew a non-numeric key must not cost us the whole control.
  const json c = json::parse(R"({
    "name": "Doppler",
    "descriptions": {"0": "Off", "later": "Something New", "2": "Both"}
  })");

  ControlDef d;
  REQUIRE_NOTHROW(d = ParseControlDef("doppler", c));
  CHECK(d.name == "Doppler");
  CHECK(d.descriptions.count(0) == 1);
  CHECK(d.descriptions.count(2) == 1);
}

TEST_CASE("the older 'automatic' object still means the control has auto") {
  // Before AutomaticValue was flattened it arrived under its own key.
  const json c = json::parse(R"({"name":"Sea","automatic":{"hasAuto":true}})");
  CHECK(ParseControlDef("sea", c).hasAuto);
}

TEST_CASE("a control value, in each shape the server sends") {
  SUBCASE("a number, with auto engaged") {
    const json v = json::parse(R"({"value": 42, "auto": true,
                                   "autoValue": 7})");
    const ControlValue cv = ParseControlValue(v);
    CHECK(cv.has_value);
    CHECK(cv.value == doctest::Approx(42));
    CHECK(cv.has_auto);
    CHECK(cv.auto_);
    CHECK(cv.autoValue == doctest::Approx(7));
    CHECK(cv.allowed);  // defaults to true
    CHECK(cv.error.empty());
  }

  SUBCASE("a string value lands in str_value, not value") {
    const ControlValue cv =
        ParseControlValue(json::parse(R"({"value": "Halo24"})"));
    CHECK(cv.has_value);
    CHECK(cv.str_value == "Halo24");
    CHECK(cv.value == doctest::Approx(0));
  }

  SUBCASE("an explicit null is no value at all") {
    const ControlValue cv = ParseControlValue(json::parse(R"({"value": null})"));
    CHECK_FALSE(cv.has_value);
  }

  SUBCASE("no value key at all") {
    const ControlValue cv = ParseControlValue(json::parse(R"({"enabled": false})"));
    CHECK_FALSE(cv.has_value);
    CHECK(cv.has_enabled);
    CHECK_FALSE(cv.enabled);
    CHECK_FALSE(cv.has_auto);
  }

  SUBCASE("a refusal carries why, and that it was refused") {
    const json v = json::parse(R"({"value": 0, "allowed": false,
                                   "error": "not while in standby"})");
    const ControlValue cv = ParseControlValue(v);
    CHECK_FALSE(cv.allowed);
    CHECK(cv.error == "not while in standby");
  }

  SUBCASE("a zone's span") {
    const json v = json::parse(R"({"value": 10, "endValue": 45,
                                   "startDistance": 100,
                                   "endDistance": 900})");
    const ControlValue cv = ParseControlValue(v);
    CHECK(cv.endValue == doctest::Approx(45));
    CHECK(cv.startDistance == doctest::Approx(100));
    CHECK(cv.endDistance == doctest::Approx(900));
  }
}

TEST_CASE("an ARPA target arrives in SI units and comes out in ours") {
  // Shaped after ArpaTargetApi: bearing and course in radians, distance in
  // whole metres, speed in m/s.
  const json t = json::parse(R"({
    "id": 12,
    "status": "tracking",
    "acquisition": "manual",
    "position": {"bearing": 1.5707963267948966, "distance": 1852},
    "motion": {"course": 3.141592653589793, "speed": 5.144444},
    "danger": {"cpa": 400.0, "tcpa": 120.0, "is_dangerous": true},
    "firstSeen": "2026-09-20T07:54:00Z",
    "lastSeen": "2026-09-20T07:54:30Z"
  })");

  const RadarTarget got = ParseTarget(12, t);
  CHECK(got.id == 12);
  CHECK(got.status == RadarTarget::kTracking);
  CHECK(got.manual);
  CHECK(got.bearing_deg == doctest::Approx(90.0));
  CHECK(got.distance_m == doctest::Approx(1852));
  CHECK(got.has_motion);
  CHECK(got.course_deg == doctest::Approx(180.0));
  CHECK(got.speed_kn == doctest::Approx(10.0).epsilon(0.001));  // 5.144 m/s
  CHECK(got.has_danger);
  CHECK(got.cpa_m == doctest::Approx(400.0));
  CHECK(got.tcpa_s == doctest::Approx(120.0));
  CHECK(got.is_dangerous);
}

TEST_CASE("both spellings of the danger flag are honoured") {
  // mayara-server's TargetDangerApi is the one struct in that file WITHOUT
  // #[serde(rename_all = "camelCase")], so it really does emit is_dangerous
  // while its siblings emit camelCase. If that is ever tidied up, the
  // camelCase branch below is what keeps the plugin working across the
  // change -- and this test is what says the snake_case one still matters.
  const char* tmpl = R"({"status":"tracking","acquisition":"auto",
                         "position":{"bearing":0,"distance":100},
                         "danger":{"cpa":10,"tcpa":20,"%s":true}})";
  char buf[512];

  std::snprintf(buf, sizeof(buf), tmpl, "is_dangerous");
  CHECK(ParseTarget(1, json::parse(buf)).is_dangerous);

  std::snprintf(buf, sizeof(buf), tmpl, "isDangerous");
  CHECK(ParseTarget(1, json::parse(buf)).is_dangerous);

  std::snprintf(buf, sizeof(buf), tmpl, "somethingElse");
  CHECK_FALSE(ParseTarget(1, json::parse(buf)).is_dangerous);
}

TEST_CASE("target status and acquisition map to what the plugin draws") {
  auto status_of = [](const char* s) {
    json t = json::parse(R"({"acquisition":"auto"})");
    t["status"] = s;
    return ParseTarget(1, t).status;
  };
  CHECK(status_of("tracking") == RadarTarget::kTracking);
  CHECK(status_of("lost") == RadarTarget::kLost);
  CHECK(status_of("acquiring") == RadarTarget::kAcquiring);
  // Anything unrecognised is treated as still acquiring, which draws but
  // claims nothing about the target.
  CHECK(status_of("something-new") == RadarTarget::kAcquiring);

  CHECK(ParseTarget(1, json::parse(R"({"acquisition":"manual"})")).manual);
  CHECK_FALSE(ParseTarget(1, json::parse(R"({"acquisition":"auto"})")).manual);
  CHECK_FALSE(ParseTarget(1, json::object()).manual);
}

TEST_CASE("motion and danger are omitted until the server knows them") {
  // Both are skip_serializing_if on the server: a target just acquired has
  // neither, and drawing a course for it would be inventing one.
  const json t = json::parse(R"({
    "id": 4, "status": "acquiring", "acquisition": "auto",
    "position": {"bearing": 0.5, "distance": 300}
  })");

  const RadarTarget got = ParseTarget(4, t);
  CHECK(got.status == RadarTarget::kAcquiring);
  CHECK_FALSE(got.has_motion);
  CHECK_FALSE(got.has_danger);
  CHECK(got.course_deg == doctest::Approx(0));
  CHECK(got.speed_kn == doctest::Approx(0));
  CHECK_FALSE(got.is_dangerous);
}

TEST_CASE("a negative TCPA is a CPA already passed, not a bad number") {
  const json t = json::parse(R"({
    "status": "tracking", "acquisition": "auto",
    "position": {"bearing": 0, "distance": 100},
    "danger": {"cpa": 50.0, "tcpa": -90.0, "is_dangerous": false}
  })");

  const RadarTarget got = ParseTarget(2, t);
  CHECK(got.has_danger);
  CHECK(got.tcpa_s == doctest::Approx(-90.0));
  CHECK_FALSE(got.is_dangerous);
}

TEST_CASE("bearings stay within a circle's worth of degrees") {
  // Signal K bearings are [0, 2pi); nothing should come out above 360.
  for (int i = 0; i < 36; ++i) {
    const double rad = i * 2.0 * M_PI / 36.0;
    json t = json::parse(R"({"status":"tracking","acquisition":"auto"})");
    t["position"] = {{"bearing", rad}, {"distance", 500}};
    const double deg = ParseTarget(1, t).bearing_deg;
    CAPTURE(i);
    CHECK(deg >= 0.0);
    CHECK(deg < 360.0);
    CHECK(deg == doctest::Approx(i * 10.0));
  }
}

}  // TEST_SUITE("MayaraProtocol")
