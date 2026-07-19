/******************************************************************************
 * mayara_pi - shared radar image state.
 *****************************************************************************/
#include "RadarState.h"

#include <cmath>
#include <cstring>

void RadarState::Configure(int spokes_per_rev, int max_spoke_len,
                           std::vector<Rgba> legend) {
  std::lock_guard<std::mutex> lock(m_);
  spokes_ = spokes_per_rev;
  maxlen_ = max_spoke_len;
  legend_ = std::move(legend);
  raster_.assign(static_cast<size_t>(spokes_) * maxlen_, 0);
  spoke_len_.assign(spokes_, 0);
  range_ = 0;
  has_data_ = false;
  ++generation_;
}

void RadarState::WriteSpoke(uint32_t angle, const uint8_t* data, size_t len,
                            uint32_t range_meters) {
  std::lock_guard<std::mutex> lock(m_);
  if (spokes_ <= 0 || maxlen_ <= 0) return;
  if (static_cast<int>(angle) >= spokes_) return;
  if (len > static_cast<size_t>(maxlen_)) len = maxlen_;

  uint8_t* row = &raster_[static_cast<size_t>(angle) * maxlen_];
  std::memcpy(row, data, len);
  if (len < static_cast<size_t>(maxlen_))
    std::memset(row + len, 0, maxlen_ - len);
  spoke_len_[angle] = static_cast<uint16_t>(len);
  range_ = range_meters;
  has_data_ = true;
  ++generation_;
}

bool RadarState::RenderPPI(uint8_t* rgb, int w, int h) const {
  std::lock_guard<std::mutex> lock(m_);
  std::memset(rgb, 0, static_cast<size_t>(w) * h * 3);
  if (!has_data_ || spokes_ <= 0 || maxlen_ <= 0) return false;

  const double cx = w / 2.0;
  const double cy = h / 2.0;
  const double radius = std::min(w, h) / 2.0 - 2.0;
  if (radius <= 0) return true;
  const double inv_radius = 1.0 / radius;
  const double spokes_per_2pi = spokes_ / (2.0 * M_PI);
  const int legend_n = static_cast<int>(legend_.size());

  for (int y = 0; y < h; ++y) {
    const double dy = y - cy;
    uint8_t* out = rgb + static_cast<size_t>(y) * w * 3;
    for (int x = 0; x < w; ++x, out += 3) {
      const double dx = x - cx;
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (dist > radius) continue;  // outside scope -> black

      // Bow up (screen -y), clockwise. atan2(dx, -dy): up=0, starboard=+pi/2.
      double a = std::atan2(dx, -dy);
      if (a < 0) a += 2.0 * M_PI;
      int spoke = static_cast<int>(a * spokes_per_2pi);
      if (spoke >= spokes_) spoke -= spokes_;

      const int len = spoke_len_[spoke];
      if (len <= 0) continue;
      const int cell = static_cast<int>(dist * inv_radius * len);
      if (cell >= len) continue;

      const uint8_t idx = raster_[static_cast<size_t>(spoke) * maxlen_ + cell];
      if (idx == 0 || idx >= legend_n) continue;  // 0 = no echo (transparent)

      const Rgba& c = legend_[idx];
      // Pre-blend over black using the legend alpha.
      out[0] = static_cast<uint8_t>(c.r * c.a / 255);
      out[1] = static_cast<uint8_t>(c.g * c.a / 255);
      out[2] = static_cast<uint8_t>(c.b * c.a / 255);
    }
  }
  return true;
}

uint32_t RadarState::RangeMeters() const {
  std::lock_guard<std::mutex> lock(m_);
  return range_;
}

uint64_t RadarState::Generation() const {
  std::lock_guard<std::mutex> lock(m_);
  return generation_;
}

bool RadarState::Configured() const {
  std::lock_guard<std::mutex> lock(m_);
  return spokes_ > 0 && maxlen_ > 0;
}
