/******************************************************************************
 * mayara_pi - shared radar image state (LUT-cached CPU renderer).
 *****************************************************************************/
#include "RadarState.h"

#include <chrono>

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
  legend_src_ = std::move(legend);
  legend_ = legend_src_;
  RebuildLegend();
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

  // On a range change the old echoes are at the wrong distance until re-swept,
  // so drop them and let the new sweep repopulate.
  if (has_data_ && range_meters != range_) {
    std::fill(spoke_len_.begin(), spoke_len_.end(), 0);
  }

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
  // Display threshold: hide normal returns below the chosen band. The floor
  // never exceeds the strong-return index, so Doppler/history colours (higher
  // indices) always pass through.
  const int floor_idx = threshold_level_ == 1   ? band_medium_
                        : threshold_level_ == 2 ? band_strong_
                                                : 0;
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
    if (floor_idx > 0 && idx < floor_idx) {  // below the display threshold
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

bool RadarState::RenderPPI(uint8_t* rgb, int w, int h, double zoom,
                          double rot_deg, double off_x, double off_y) {
  std::lock_guard<std::mutex> lock(m_);
  std::memset(rgb, 0, static_cast<size_t>(w) * h * 3);
  if (!has_data_ || disc_size_ <= 0) return false;
  EnsureDisc();

  // Nearest-neighbour sample the cached (bow-up) disc over the WHOLE w*h
  // rectangle (not clamped to a centred square), so the picture fills the
  // window and the overzoom shows in the corners. The scale keeps the reported
  // range at min(w,h)/2, so the range rings stay circular. `zoom` magnifies
  // about the sweep origin; `rot_deg` rotates the picture clockwise; the origin
  // itself sits at the window centre plus (off_x, off_y).
  const int refside = std::min(w, h);
  if (refside <= 0) return true;
  if (zoom <= 0.0) zoom = 1.0;
  const double dcenter = disc_size_ / 2.0;
  const double step = (static_cast<double>(disc_size_) / refside) / zoom;
  const double th = rot_deg * 3.14159265358979323846 / 180.0;
  const double cs = std::cos(th), sn = std::sin(th);
  const double ocx = w / 2.0 + off_x, ocy = h / 2.0 + off_y;
  for (int y = 0; y < h; ++y) {
    const double fy = (y - ocy) * step;
    uint8_t* orow = rgb + static_cast<size_t>(y) * w * 3;
    for (int x = 0; x < w; ++x) {
      const double fx = (x - ocx) * step;
      const int sx = static_cast<int>(dcenter + fx * cs + fy * sn);
      const int sy = static_cast<int>(dcenter - fx * sn + fy * cs);
      if (sx < 0 || sx >= disc_size_ || sy < 0 || sy >= disc_size_) continue;
      const uint8_t* s =
          &disc_[(static_cast<size_t>(sy) * disc_size_ + sx) * 4];
      const uint8_t a = s[3];
      if (a) {
        const float f = a * intensity_ / 255.0f;
        orow[x * 3 + 0] = static_cast<uint8_t>(s[0] * f);
        orow[x * 3 + 1] = static_cast<uint8_t>(s[1] * f);
        orow[x * 3 + 2] = static_cast<uint8_t>(s[2] * f);
      }
    }
  }
  return true;
}

void RadarState::SetIntensity(float f) {
  std::lock_guard<std::mutex> lock(m_);
  intensity_ = f;
}

void RadarState::SetLegendLayout(const LegendLayout& layout) {
  std::lock_guard<std::mutex> lock(m_);
  layout_ = layout;
  RebuildLegend();
  ++generation_;
}

void RadarState::SetPalette(const RadarPalette& palette) {
  std::lock_guard<std::mutex> lock(m_);
  palette_ = palette;
  has_palette_ = true;
  RebuildLegend();
  ++generation_;
}

namespace {

Rgba Mix(const Rgba& a, const Rgba& b, double t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  Rgba c;
  c.r = static_cast<uint8_t>(a.r + (b.r - a.r) * t + 0.5);
  c.g = static_cast<uint8_t>(a.g + (b.g - a.g) * t + 0.5);
  c.b = static_cast<uint8_t>(a.b + (b.b - a.b) * t + 0.5);
  c.a = static_cast<uint8_t>(a.a + (b.a - a.a) * t + 0.5);
  return c;
}

}  // namespace

// Re-colour the server's legend in place, entry by entry, keeping its length
// and its meaning. Anything the layout does not account for is left as the
// server sent it: a legend that grows a new kind of entry then shows through
// unaltered rather than coming out wrong.
void RadarState::RebuildLegend() {
  legend_ = legend_src_;
  if (!has_palette_ || palette_.from_server || legend_src_.empty()) return;

  const int n = static_cast<int>(legend_src_.size());
  const int ramp_end = layout_.pixel_colors > 1 ? layout_.pixel_colors : 0;
  for (int i = 1; i < ramp_end && i < n; ++i) {
    // Index 0 is "no echo" and stays transparent whatever the palette says.
    const double t = ramp_end > 2 ? static_cast<double>(i - 1) / (ramp_end - 2)
                                  : 1.0;
    legend_[i] = t < 0.5 ? Mix(palette_.weak, palette_.medium, t * 2.0)
                         : Mix(palette_.medium, palette_.strong, t * 2.0 - 1.0);
  }
  auto at = [&](int idx, const Rgba& c) {
    if (idx >= 0 && idx < n) legend_[idx] = c;
  };
  at(layout_.static_background, palette_.background);
  at(layout_.doppler_approaching, palette_.doppler_approaching);
  at(layout_.doppler_receding, palette_.doppler_receding);

  if (layout_.history_start > 0 && layout_.history_start < n) {
    const int last = n - 1;
    const int span = last - layout_.history_start;
    for (int i = layout_.history_start; i <= last; ++i)
      legend_[i] = Mix(palette_.trail_start, palette_.trail_end,
                       span > 0 ? static_cast<double>(i - layout_.history_start) / span
                                : 0.0);
  }
}

void RadarState::SetLegendBands(int low, int medium, int strong) {
  std::lock_guard<std::mutex> lock(m_);
  (void)low;
  band_medium_ = medium;
  band_strong_ = strong;
  disc_gen_ = ~0ull;  // re-map colours next EnsureDisc
}

void RadarState::SetThreshold(int level) {
  std::lock_guard<std::mutex> lock(m_);
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  if (level == threshold_level_) return;
  threshold_level_ = level;
  disc_gen_ = ~0ull;  // force the disc to re-map with the new floor
}

int RadarState::Threshold() const {
  std::lock_guard<std::mutex> lock(m_);
  return threshold_level_;
}

void RadarState::Clear() {
  std::lock_guard<std::mutex> lock(m_);
  std::fill(spoke_len_.begin(), spoke_len_.end(), 0);
  has_data_ = false;
  ++generation_;  // forces disc/PPI to rebuild empty
}

void RadarState::SetPosition(double lat, double lon) {
  std::lock_guard<std::mutex> lock(m_);
  radar_lat_ = lat;
  radar_lon_ = lon;
  has_pos_ = true;
}

bool RadarState::Position(double& lat, double& lon) const {
  std::lock_guard<std::mutex> lock(m_);
  if (!has_pos_) return false;
  lat = radar_lat_;
  lon = radar_lon_;
  return true;
}

void RadarState::SetHeadingFromBearing(uint32_t angle, uint32_t bearing) {
  std::lock_guard<std::mutex> lock(m_);
  if (spokes_ <= 0) return;
  int diff = static_cast<int>(bearing) - static_cast<int>(angle);
  diff %= spokes_;
  if (diff < 0) diff += spokes_;
  heading_deg_ = diff * 360.0 / spokes_;
  has_heading_ = true;
  heading_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
}

// How long ago that heading arrived. A radar that has stopped transmitting
// keeps its last heading for ever otherwise, and a stale heading pointing the
// picture the wrong way is worse than no heading at all.
int64_t RadarState::HeadingAgeMs() const {
  std::lock_guard<std::mutex> lock(m_);
  if (!has_heading_) return -1;
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  return now - heading_ms_;
}

bool RadarState::Heading(double& degrees) const {
  std::lock_guard<std::mutex> lock(m_);
  if (!has_heading_) return false;
  degrees = heading_deg_;
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
