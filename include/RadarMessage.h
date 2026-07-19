/******************************************************************************
 * mayara_pi - minimal proto3 decoder for the mayara spoke stream.
 *
 * Schema (web/api/RadarMessage.proto):
 *   message RadarMessage { repeated Spoke spokes = 2; }
 *   message Spoke {
 *     uint32 angle = 1; optional uint32 bearing = 2; uint32 range = 3;
 *     optional uint64 time = 4; bytes data = 5;
 *     optional double lat = 6; optional double lon = 7;
 *   }
 *
 * Hand-rolled (no libprotobuf/nanopb): the schema is tiny and stable within a
 * major version. `data` points into the caller's buffer (zero-copy); it is only
 * valid for the lifetime of that buffer.
 *****************************************************************************/
#ifndef MAYARA_RADAR_MESSAGE_H_
#define MAYARA_RADAR_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

struct MayaraSpoke {
  uint32_t angle = 0;
  uint32_t bearing = 0;
  bool has_bearing = false;
  uint32_t range = 0;  // meters of the last pixel in data
  uint64_t time = 0;
  bool has_time = false;
  double lat = 0.0;
  double lon = 0.0;
  bool has_lat = false;
  bool has_lon = false;
  const uint8_t* data = nullptr;  // points into the decoded buffer
  size_t data_len = 0;
};

// Decode a RadarMessage. Appends spokes to `out`. Returns false on malformed
// input (out may contain partially decoded spokes).
bool DecodeRadarMessage(const uint8_t* buf, size_t len,
                        std::vector<MayaraSpoke>& out);

#endif  // MAYARA_RADAR_MESSAGE_H_
