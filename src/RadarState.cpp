/******************************************************************************
 * mayara_pi - shared radar image state (LUT-cached CPU renderer).
 *****************************************************************************/
#include "RadarState.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr uint32_t kOutside = 0xFFFFFFFFu;
constexpr int kDiscSize = 1024;  // cartesian cache edge (radius 512)
}  // namespace

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

  disc_size_ = (spokes_ > 0) ? kDiscSize : 0;
  disc_.assign(static_cast<size_t>(disc_size_) * disc_size_ * 4, 0);
  disc_gen_ = ~0ull;
  BuildLut();
}

void RadarState::BuildLut() {
  if (disc_size_ <= 0 || spokes_ <= 0) {
    lut_.clear();
    return;
  }
  lut_.assign(static_cast<size_t>(disc_size_) * disc_size_, kOutside);
  const double c = disc_size_ / 2.0;
  const double radius = disc_size_ / 2.0 - 1.0;
  const double spokes_per_2pi = spokes_ / (2.0 * M_PI);
  for (int y = 0; y < disc_size_; ++y) {
    const double dy = y - c;
    for (int x = 0; x < disc_size_; ++x) {
      const double dx = x - c;
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (dist > radius) continue;
      double a = std::atan2(dx, -dy);  // bow up, clockwise
      if (a < 0) a += 2.0 * M_PI;
      int spoke = static_cast<int>(a * spokes_per_2pi);
      if (spoke >= spokes_) spoke -= spokes_;
      const uint32_t rnorm =
          static_cast<uint32_t>(dist / radius * 65535.0) & 0xFFFF;
      lut_[static_cast<size_t>(y) * disc_size_ + x] =
          (static_cast<uint32_t>(spoke) << 16) | rnorm;
    }
  }
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

// Rebuild the cartesian disc from the polar raster via the LUT. No trig; only
// runs when new spokes have arrived. Caller must hold m_.
void RadarState::EnsureDisc() {
  if (disc_gen_ == generation_ || disc_size_ <= 0) return;
  const int legend_n = static_cast<int>(legend_.size());
  for (size_t i = 0; i < lut_.size(); ++i) {
    uint8_t* d = &disc_[i * 4];
    const uint32_t e = lut_[i];
    if (e == kOutside) {
      d[3] = 0;
      continue;
    }
    const int spoke = static_cast<int>(e >> 16);
    const uint32_t rnorm = e & 0xFFFF;
    const int len = spoke_len_[spoke];
    if (len <= 0) {
      d[3] = 0;
      continue;
    }
    const int cell = static_cast<int>((static_cast<uint64_t>(rnorm) * len) >> 16);
    if (cell >= len) {
      d[3] = 0;
      continue;
    }
    const uint8_t idx = raster_[static_cast<size_t>(spoke) * maxlen_ + cell];
    if (idx == 0 || idx >= legend_n) {
      d[3] = 0;
      continue;
    }
    const Rgba& c = legend_[idx];
    d[0] = c.r;
    d[1] = c.g;
    d[2] = c.b;
    d[3] = c.a;
  }
  disc_gen_ = generation_;
}

int RadarState::CopyDisc(std::vector<uint8_t>& dst) {
  std::lock_guard<std::mutex> lock(m_);
  if (!has_data_ || disc_size_ <= 0) return 0;
  EnsureDisc();
  dst = disc_;
  return disc_size_;
}

bool RadarState::RenderPPI(uint8_t* rgb, int w, int h) {
  std::lock_guard<std::mutex> lock(m_);
  std::memset(rgb, 0, static_cast<size_t>(w) * h * 3);
  if (!has_data_ || disc_size_ <= 0) return false;
  EnsureDisc();

  // Nearest-neighbour scale the cached disc into a centred square. No trig.
  const int side = std::min(w, h);
  if (side <= 0) return true;
  const int ox = (w - side) / 2;
  const int oy = (h - side) / 2;
  for (int y = 0; y < side; ++y) {
    const int sy = y * disc_size_ / side;
    const uint8_t* srow = &disc_[static_cast<size_t>(sy) * disc_size_ * 4];
    uint8_t* orow = rgb + (static_cast<size_t>(oy + y) * w + ox) * 3;
    for (int x = 0; x < side; ++x) {
      const uint8_t* s = srow + static_cast<size_t>(x * disc_size_ / side) * 4;
      const uint8_t a = s[3];
      if (a) {
        orow[x * 3 + 0] = static_cast<uint8_t>(s[0] * a / 255);
        orow[x * 3 + 1] = static_cast<uint8_t>(s[1] * a / 255);
        orow[x * 3 + 2] = static_cast<uint8_t>(s[2] * a / 255);
      }
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
