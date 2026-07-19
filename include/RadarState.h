/******************************************************************************
 * mayara_pi - shared radar image state.
 *
 * Holds the polar spoke raster and the legend colour map, written by the
 * network thread and read by the UI thread. Deliberately free of wx and JSON so
 * it can be unit-tested and reused by the (later) GL overlay.
 *
 * Rendering is CPU-only but cheap: the polar->cartesian trig is precomputed
 * once into a lookup table, and the cartesian "disc" (bow-up, straight alpha)
 * is rebuilt only when new spokes arrive. Every display surface (PPI window, GL
 * overlay, wxDC overlay) blits/scales/rotates that one cached disc.
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
  // Set geometry + legend (byte value -> colour). Clears the raster and
  // rebuilds the polar->cartesian lookup table.
  void Configure(int spokes_per_rev, int max_spoke_len, std::vector<Rgba> legend);

  // Write one spoke (network thread). `angle` in [0, spokes_per_rev); `data` is
  // one legend-index byte per range cell.
  void WriteSpoke(uint32_t angle, const uint8_t* data, size_t len,
                  uint32_t range_meters);

  // Copy the current radar disc (fixed square, bow-up, straight RGBA) into dst
  // (sized DiscSize()^2 * 4). Rebuilds it only if spokes changed. Returns the
  // disc edge length, or 0 if no data yet.
  int CopyDisc(std::vector<uint8_t>& dst);
  int DiscSize() const { return disc_size_; }

  // Render a head-up PPI into an RGB buffer (w*h*3), pre-blended over black, by
  // scaling the cached disc. Returns true if radar data is present.
  bool RenderPPI(uint8_t* rgb, int w, int h);

  uint32_t RangeMeters() const;
  uint64_t Generation() const;
  bool Configured() const;

 private:
  void BuildLut();      // fills lut_ for disc_size_ (trig, once per Configure)
  void EnsureDisc();    // rebuilds disc_ if generation changed (LUT walk)

  mutable std::mutex m_;
  int spokes_ = 0;
  int maxlen_ = 0;
  std::vector<uint8_t> raster_;      // spokes_ * maxlen_ legend indices
  std::vector<uint16_t> spoke_len_;  // valid cells per spoke
  std::vector<Rgba> legend_;
  uint32_t range_ = 0;
  uint64_t generation_ = 0;
  bool has_data_ = false;

  // Cartesian render cache.
  int disc_size_ = 0;
  std::vector<uint32_t> lut_;   // disc_size_^2: (spoke<<16)|rnorm16, ~0u=outside
  std::vector<uint8_t> disc_;   // disc_size_^2 * 4 RGBA, bow-up, straight alpha
  uint64_t disc_gen_ = ~0ull;   // generation the disc was last built for
};

#endif  // MAYARA_RADAR_STATE_H_
