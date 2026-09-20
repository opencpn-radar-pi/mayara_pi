/******************************************************************************
 * mayara_pi tests - minimal proto3 wire-format *encoder*.
 *
 * The plugin only ever decodes (src/RadarMessage.cpp), so the tests need
 * something to decode. This is the mirror image of that decoder, written
 * independently from the wire-format spec rather than by reusing its helpers:
 * a bug shared by both sides would otherwise cancel itself out and the tests
 * would pass on a stream no real server sends.
 *****************************************************************************/
#ifndef MAYARA_TEST_PROTO_BUILDER_H_
#define MAYARA_TEST_PROTO_BUILDER_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace test_proto {

// Wire types, per the proto3 encoding spec.
constexpr uint32_t kVarint = 0;
constexpr uint32_t kI64 = 1;
constexpr uint32_t kLen = 2;
constexpr uint32_t kI32 = 5;

class Builder {
 public:
  const std::vector<uint8_t>& bytes() const { return buf_; }
  size_t size() const { return buf_.size(); }
  const uint8_t* data() const { return buf_.data(); }

  Builder& Varint(uint64_t v) {
    do {
      uint8_t b = static_cast<uint8_t>(v & 0x7f);
      v >>= 7;
      if (v) b |= 0x80;
      buf_.push_back(b);
    } while (v);
    return *this;
  }

  Builder& Tag(uint32_t field, uint32_t wire) {
    return Varint((static_cast<uint64_t>(field) << 3) | wire);
  }

  Builder& VarintField(uint32_t field, uint64_t v) {
    return Tag(field, kVarint).Varint(v);
  }

  Builder& DoubleField(uint32_t field, double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    Tag(field, kI64);
    for (int i = 0; i < 8; ++i)  // little-endian, as proto3 fixed64 is
      buf_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xff));
    return *this;
  }

  Builder& Fixed32Field(uint32_t field, uint32_t v) {
    Tag(field, kI32);
    for (int i = 0; i < 4; ++i)
      buf_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
    return *this;
  }

  Builder& BytesField(uint32_t field, const std::vector<uint8_t>& data) {
    Tag(field, kLen).Varint(data.size());
    buf_.insert(buf_.end(), data.begin(), data.end());
    return *this;
  }

  Builder& BytesField(uint32_t field, const std::string& s) {
    return BytesField(field,
                      std::vector<uint8_t>(s.begin(), s.end()));
  }

  // A length-delimited field holding another message.
  Builder& MessageField(uint32_t field, const Builder& sub) {
    return BytesField(field, sub.bytes());
  }

  // Raw append, for deliberately malformed input.
  Builder& Raw(std::initializer_list<uint8_t> raw) {
    buf_.insert(buf_.end(), raw.begin(), raw.end());
    return *this;
  }

 private:
  std::vector<uint8_t> buf_;
};

// A RadarMessage carrying the given Spoke submessages (field 2, repeated).
inline Builder RadarMessageOf(const std::vector<Builder>& spokes) {
  Builder m;
  for (const Builder& s : spokes) m.MessageField(2, s);
  return m;
}

// A Spoke with the two required-in-practice fields plus data.
// Optional fields (bearing, time, lat, lon) are added by the caller.
inline Builder SpokeOf(uint32_t angle, uint32_t range,
                       const std::vector<uint8_t>& data) {
  Builder s;
  s.VarintField(1, angle);
  s.VarintField(3, range);
  s.BytesField(5, data);
  return s;
}

}  // namespace test_proto

#endif  // MAYARA_TEST_PROTO_BUILDER_H_
