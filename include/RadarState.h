/******************************************************************************
 * mayara_pi - shared radar image state.
 *
 * Holds the polar spoke raster and the legend colour map, written by the
 * network thread and read by the UI thread. Deliberately free of wx and JSON so
 * it can be unit-tested and reused by the (later) GL overlay.
 *****************************************************************************/
#ifndef MAYARA_RADAR_STATE_H_
#define MAYARA_RADAR_STATE_H_

#include <cstdint>
#include <mutex>
#include <vector>

struct Rgba {
  uint8_t r = 0, g = 0, b = 0, a = 0;
};

class RadarState {
 public:
  // Set geometry + legend (byte value -> colour). Clears the raster.
  void Configure(int spokes_per_rev, int max_spoke_len, std::vector<Rgba> legend);

  // Write one spoke (called from the network thread). `angle` in
  // [0, spokes_per_rev); `data` is one byte (legend index) per range cell.
  void WriteSpoke(uint32_t angle, const uint8_t* data, size_t len,
                  uint32_t range_meters);

  // Render a head-up PPI (bow up, clockwise) into an RGB buffer of w*h*3 bytes,
  // pre-blended over black. Returns true if any radar data has been received.
  bool RenderPPI(uint8_t* rgb, int w, int h) const;

  uint32_t RangeMeters() const;
  uint64_t Generation() const;   // bumps on every spoke; UI can poll for change
  bool Configured() const;

 private:
  mutable std::mutex m_;
  int spokes_ = 0;
  int maxlen_ = 0;
  std::vector<uint8_t> raster_;      // spokes_ * maxlen_ legend indices
  std::vector<uint16_t> spoke_len_;  // valid cells per spoke
  std::vector<Rgba> legend_;
  uint32_t range_ = 0;
  uint64_t generation_ = 0;
  bool has_data_ = false;
};

#endif  // MAYARA_RADAR_STATE_H_
