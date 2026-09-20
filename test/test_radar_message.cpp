/******************************************************************************
 * mayara_pi tests - the proto3 spoke decoder (src/RadarMessage.cpp).
 *
 * This decoder reads straight off the wire from mayara-server, so the tests
 * that matter most are the ones about malformed input: every reader in it
 * bounds-checks and drops `ok` on truncation, and these pin that down.
 *****************************************************************************/
#include <doctest/doctest.h>

#include <vector>

#include "RadarMessage.h"
#include "proto_builder.h"

using test_proto::Builder;
using test_proto::RadarMessageOf;
using test_proto::SpokeOf;

TEST_SUITE("RadarMessage") {

TEST_CASE("an empty message decodes to no spokes") {
  std::vector<MayaraSpoke> spokes;
  CHECK(DecodeRadarMessage(nullptr, 0, spokes));
  CHECK(spokes.empty());
}

TEST_CASE("a spoke's mandatory fields survive the round trip") {
  const std::vector<uint8_t> data{0, 1, 2, 3, 250, 255};
  const Builder msg = RadarMessageOf({SpokeOf(1023, 1852, data)});

  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);

  const MayaraSpoke& s = spokes[0];
  CHECK(s.angle == 1023);
  CHECK(s.range == 1852);
  REQUIRE(s.data_len == data.size());
  CHECK(std::vector<uint8_t>(s.data, s.data + s.data_len) == data);

  // `data` is documented as zero-copy: it must point into the caller's buffer,
  // not at a copy that dies with the decoder.
  CHECK(s.data >= msg.data());
  CHECK(s.data + s.data_len <= msg.data() + msg.size());
}

TEST_CASE("absent optional fields are reported as absent") {
  const Builder msg = RadarMessageOf({SpokeOf(0, 100, {1})});
  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);

  CHECK_FALSE(spokes[0].has_bearing);
  CHECK_FALSE(spokes[0].has_time);
  CHECK_FALSE(spokes[0].has_lat);
  CHECK_FALSE(spokes[0].has_lon);
}

TEST_CASE("bearing, time and position decode when present") {
  Builder spoke = SpokeOf(2047, 5556, {7});
  spoke.VarintField(2, 512);                 // bearing
  spoke.VarintField(4, 1726840000000ull);    // time, ms since epoch
  spoke.DoubleField(6, 52.0907374);          // lat
  spoke.DoubleField(7, 5.1214201);           // lon
  const Builder msg = RadarMessageOf({spoke});

  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);
  const MayaraSpoke& s = spokes[0];

  CHECK(s.has_bearing);
  CHECK(s.bearing == 512);
  CHECK(s.has_time);
  CHECK(s.time == 1726840000000ull);
  CHECK(s.has_lat);
  CHECK(s.lat == 52.0907374);   // bit-for-bit: fixed64 is not lossy
  CHECK(s.has_lon);
  CHECK(s.lon == 5.1214201);
}

TEST_CASE("negative latitudes and longitudes survive the fixed64 round trip") {
  Builder spoke = SpokeOf(0, 100, {1});
  spoke.DoubleField(6, -33.8688197);
  spoke.DoubleField(7, -151.2092955);
  const Builder msg = RadarMessageOf({spoke});

  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);
  CHECK(spokes[0].lat == -33.8688197);
  CHECK(spokes[0].lon == -151.2092955);
}

TEST_CASE("several spokes come out in wire order and append to the vector") {
  const Builder msg = RadarMessageOf({
      SpokeOf(0, 1852, {1}),
      SpokeOf(1, 1852, {2, 2}),
      SpokeOf(2, 1852, {3, 3, 3}),
  });

  std::vector<MayaraSpoke> spokes;
  spokes.push_back(MayaraSpoke{});  // a caller reusing its buffer
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 4);
  CHECK(spokes[1].angle == 0);
  CHECK(spokes[1].data_len == 1);
  CHECK(spokes[2].angle == 1);
  CHECK(spokes[2].data_len == 2);
  CHECK(spokes[3].angle == 2);
  CHECK(spokes[3].data_len == 3);
}

TEST_CASE("an empty data field is legal and yields a zero-length spoke") {
  const Builder msg = RadarMessageOf({SpokeOf(5, 1852, {})});
  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);
  CHECK(spokes[0].data_len == 0);
}

// Forward compatibility: mayara-server may add fields within a major version,
// and an older plugin has to keep decoding the ones it knows.
TEST_CASE("unknown fields inside a spoke are skipped, whatever their type") {
  Builder spoke;
  spoke.VarintField(1, 42);                       // angle
  spoke.VarintField(11, 123456);                  // unknown varint
  spoke.VarintField(3, 1852);                     // range
  spoke.BytesField(12, std::string("a future string"));  // unknown bytes
  spoke.DoubleField(13, 3.5);                     // unknown fixed64
  spoke.Fixed32Field(14, 0xDEADBEEF);             // unknown fixed32
  spoke.BytesField(5, std::vector<uint8_t>{9, 9});  // data, after all of it
  const Builder msg = RadarMessageOf({spoke});

  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);
  CHECK(spokes[0].angle == 42);
  CHECK(spokes[0].range == 1852);
  CHECK(spokes[0].data_len == 2);
}

TEST_CASE("unknown top-level fields are skipped without losing the spokes") {
  Builder msg;
  msg.VarintField(1, 7);                          // unknown varint
  msg.MessageField(2, SpokeOf(3, 1852, {1, 2}));  // a real spoke
  msg.BytesField(9, std::string("trailer"));      // unknown bytes
  msg.Fixed32Field(10, 1);                        // unknown fixed32

  std::vector<MayaraSpoke> spokes;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
  REQUIRE(spokes.size() == 1);
  CHECK(spokes[0].angle == 3);
}

TEST_CASE("a declared length past the end of the buffer is rejected") {
  Builder msg;
  msg.Tag(2, test_proto::kLen).Varint(64);  // says 64 bytes of spoke...
  msg.Raw({0x08, 0x01});                    // ...and supplies 2

  std::vector<MayaraSpoke> spokes;
  CHECK_FALSE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
}

TEST_CASE("an overlong varint is rejected rather than read forever") {
  Builder msg;
  // Eleven continuation bytes: more than the 64 bits a varint can hold.
  msg.Raw({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F});

  std::vector<MayaraSpoke> spokes;
  CHECK_FALSE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
}

TEST_CASE("a truncated fixed64 is rejected") {
  Builder spoke = SpokeOf(0, 100, {1});
  spoke.Tag(6, test_proto::kI64).Raw({1, 2, 3});  // 3 bytes, not 8
  const Builder msg = RadarMessageOf({spoke});

  std::vector<MayaraSpoke> spokes;
  CHECK_FALSE(DecodeRadarMessage(msg.data(), msg.size(), spokes));
}

// The property that matters for a stream off a socket: no prefix of a valid
// message may be read out of bounds, and any prefix that does decode must
// decode to a prefix of the real spoke list -- never to something invented.
// Run under `make test-asan` this also proves the bounds checks themselves.
TEST_CASE("every truncation of a valid message is handled safely") {
  Builder spoke0 = SpokeOf(0, 1852, {1, 2, 3, 4});
  spoke0.VarintField(2, 16);
  spoke0.DoubleField(6, 52.09);
  Builder spoke1 = SpokeOf(1, 1852, {5, 6});
  spoke1.VarintField(4, 1726840000000ull);
  const Builder msg = RadarMessageOf({spoke0, spoke1});

  std::vector<MayaraSpoke> full;
  REQUIRE(DecodeRadarMessage(msg.data(), msg.size(), full));
  REQUIRE(full.size() == 2);

  for (size_t n = 0; n < msg.size(); ++n) {
    CAPTURE(n);
    std::vector<MayaraSpoke> got;
    const bool ok = DecodeRadarMessage(msg.data(), n, got);
    if (ok) {
      // A clean decode can only happen on a spoke boundary.
      REQUIRE(got.size() <= full.size());
      for (size_t i = 0; i < got.size(); ++i) {
        CHECK(got[i].angle == full[i].angle);
        CHECK(got[i].range == full[i].range);
        CHECK(got[i].data_len == full[i].data_len);
      }
    } else {
      // On failure the caller is told the buffer is junk; whatever landed in
      // `got` is documented as partial, but it must still be in bounds.
      for (const MayaraSpoke& s : got) {
        if (s.data == nullptr) continue;
        CHECK(s.data >= msg.data());
        CHECK(s.data + s.data_len <= msg.data() + n);
      }
    }
  }
}

TEST_CASE("garbage bytes are rejected rather than crashing") {
  // Not proto at all: every byte pattern the wire type switch can see,
  // including wire types 3, 4, 6 and 7, which have no valid meaning.
  for (int b = 0; b < 256; ++b) {
    CAPTURE(b);
    const uint8_t junk[] = {static_cast<uint8_t>(b), 0xFF, 0x00, 0x7F};
    std::vector<MayaraSpoke> spokes;
    DecodeRadarMessage(junk, sizeof(junk), spokes);  // must simply return
  }
}

}  // TEST_SUITE("RadarMessage")
