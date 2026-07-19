/******************************************************************************
 * mayara_pi - minimal proto3 wire-format decoder.
 *****************************************************************************/
#include "RadarMessage.h"

#include <cstring>

namespace {

// Protobuf wire types.
constexpr uint32_t kVarint = 0;
constexpr uint32_t kI64 = 1;
constexpr uint32_t kLen = 2;
constexpr uint32_t kI32 = 5;

// A cursor over a byte range with proto3 primitive readers. Every reader
// bounds-checks and sets `ok` false on truncation.
struct Reader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;

  Reader(const uint8_t* begin, const uint8_t* finish) : p(begin), end(finish) {}

  bool AtEnd() const { return p >= end; }

  uint64_t Varint() {
    uint64_t result = 0;
    int shift = 0;
    while (p < end && shift < 64) {
      uint8_t b = *p++;
      result |= static_cast<uint64_t>(b & 0x7f) << shift;
      if (!(b & 0x80)) return result;
      shift += 7;
    }
    ok = false;
    return 0;
  }

  uint64_t Fixed64() {
    if (end - p < 8) {
      ok = false;
      return 0;
    }
    uint64_t v;
    std::memcpy(&v, p, 8);  // wire order is little-endian, as is our target
    p += 8;
    return v;
  }

  void SkipFixed32() {
    if (end - p < 4)
      ok = false;
    else
      p += 4;
  }

  // Returns a length-delimited region and advances past it.
  const uint8_t* Bytes(size_t* out_len) {
    uint64_t n = Varint();
    if (!ok || static_cast<uint64_t>(end - p) < n) {
      ok = false;
      return nullptr;
    }
    const uint8_t* start = p;
    p += n;
    *out_len = static_cast<size_t>(n);
    return start;
  }

  // Skip a field of the given wire type whose tag was already consumed.
  void SkipField(uint32_t wire_type) {
    switch (wire_type) {
      case kVarint:
        Varint();
        break;
      case kI64:
        Fixed64();
        break;
      case kLen: {
        size_t n;
        Bytes(&n);
        break;
      }
      case kI32:
        SkipFixed32();
        break;
      default:
        ok = false;
    }
  }
};

double AsDouble(uint64_t bits) {
  double d;
  std::memcpy(&d, &bits, 8);
  return d;
}

bool DecodeSpoke(const uint8_t* buf, size_t len, MayaraSpoke* spoke) {
  Reader r{buf, buf + len};
  while (r.ok && !r.AtEnd()) {
    uint64_t tag = r.Varint();
    if (!r.ok) break;
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7);
    switch (field) {
      case 1:  // angle
        spoke->angle = static_cast<uint32_t>(r.Varint());
        break;
      case 2:  // bearing
        spoke->bearing = static_cast<uint32_t>(r.Varint());
        spoke->has_bearing = true;
        break;
      case 3:  // range
        spoke->range = static_cast<uint32_t>(r.Varint());
        break;
      case 4:  // time
        spoke->time = r.Varint();
        spoke->has_time = true;
        break;
      case 5:  // data
        spoke->data = r.Bytes(&spoke->data_len);
        break;
      case 6:  // lat
        spoke->lat = AsDouble(r.Fixed64());
        spoke->has_lat = true;
        break;
      case 7:  // lon
        spoke->lon = AsDouble(r.Fixed64());
        spoke->has_lon = true;
        break;
      default:
        r.SkipField(wire);
    }
  }
  return r.ok;
}

}  // namespace

bool DecodeRadarMessage(const uint8_t* buf, size_t len,
                        std::vector<MayaraSpoke>& out) {
  Reader r{buf, buf + len};
  while (r.ok && !r.AtEnd()) {
    uint64_t tag = r.Varint();
    if (!r.ok) break;
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7);
    if (field == 2 && wire == kLen) {  // repeated Spoke spokes = 2
      size_t sub_len = 0;
      const uint8_t* sub = r.Bytes(&sub_len);
      if (!r.ok) break;
      MayaraSpoke spoke;
      if (!DecodeSpoke(sub, sub_len, &spoke)) return false;
      out.push_back(spoke);
    } else {
      r.SkipField(wire);
    }
  }
  return r.ok;
}
