/******************************************************************************
 * mayara_pi - the four built-in palettes and their config round trip.
 *****************************************************************************/
#include "RadarPalette.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

Rgba C(int r, int g, int b) {
  Rgba c;
  c.r = static_cast<uint8_t>(r);
  c.g = static_cast<uint8_t>(g);
  c.b = static_cast<uint8_t>(b);
  c.a = 255;
  return c;
}

}  // namespace

RadarPalette::RadarPalette() {
  weak = C(0, 40, 170);
  medium = C(0, 200, 60);
  strong = C(255, 60, 40);
  doppler_approaching = C(255, 0, 255);
  doppler_receding = C(0, 255, 0);
  trail_start = C(255, 255, 255);
  trail_end = C(69, 69, 69);
  background = C(80, 80, 80);
}

std::vector<RadarPalette> BuiltinPalettes() {
  std::vector<RadarPalette> out;

  // What the radar itself says its colours are. Every server legend already
  // carries one, and for Navico it is the maker's own -- so this is not a
  // "plugin look", it is no opinion at all.
  RadarPalette mayara;
  mayara.name = "Standard Mayara";
  mayara.builtin = true;
  mayara.from_server = true;
  out.push_back(mayara);

  // Dark red through orange to a hot yellow-white: the look of a Navico set
  // with its yellow palette selected.
  RadarPalette navico;
  navico.name = "Navico yellow";
  navico.builtin = true;
  navico.weak = C(80, 0, 0);
  navico.medium = C(220, 60, 0);
  navico.strong = C(255, 240, 140);
  navico.doppler_approaching = C(255, 90, 90);
  navico.doppler_receding = C(90, 170, 255);
  navico.trail_start = C(255, 255, 255);
  navico.trail_end = C(70, 60, 60);
  navico.background = C(70, 60, 60);
  out.push_back(navico);

  // Garmin's radar sits in the greens, brightening to yellow for the hardest
  // returns.
  RadarPalette garmin;
  garmin.name = "Garmin";
  garmin.builtin = true;
  garmin.weak = C(0, 70, 40);
  garmin.medium = C(0, 200, 90);
  garmin.strong = C(255, 255, 170);
  garmin.doppler_approaching = C(255, 120, 0);
  garmin.doppler_receding = C(0, 190, 255);
  garmin.trail_start = C(230, 255, 230);
  garmin.trail_end = C(50, 70, 55);
  garmin.background = C(50, 70, 55);
  out.push_back(garmin);

  // Furuno's multicolour: blue for the faintest returns, green in the middle,
  // red at the top.
  RadarPalette furuno;
  furuno.name = "Furuno";
  furuno.builtin = true;
  furuno.weak = C(0, 40, 170);
  furuno.medium = C(0, 200, 60);
  furuno.strong = C(255, 60, 40);
  furuno.doppler_approaching = C(255, 0, 255);
  furuno.doppler_receding = C(0, 255, 255);
  furuno.trail_start = C(255, 255, 255);
  furuno.trail_end = C(60, 60, 70);
  furuno.background = C(60, 60, 70);
  out.push_back(furuno);

  return out;
}

namespace {

std::string Hex(const Rgba& c) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
  return buf;
}

bool UnHex(const std::string& s, Rgba* c) {
  if (s.size() < 6) return false;
  auto nib = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  auto byte = [&](size_t i, uint8_t* out) {
    const int hi = nib(s[i]), lo = nib(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    *out = static_cast<uint8_t>(hi * 16 + lo);
    return true;
  };
  if (!byte(0, &c->r) || !byte(2, &c->g) || !byte(4, &c->b)) return false;
  c->a = 255;
  if (s.size() >= 8 && !byte(6, &c->a)) return false;
  return true;
}

}  // namespace

std::string PaletteToString(const RadarPalette& p) {
  // The name is first and may not contain '|'; the caller strips it.
  std::string s = p.name;
  for (const Rgba* c : {&p.weak, &p.medium, &p.strong, &p.doppler_approaching,
                        &p.doppler_receding, &p.trail_start, &p.trail_end,
                        &p.background})
    s += "|" + Hex(*c);
  return s;
}

bool PaletteFromString(const std::string& s, RadarPalette* out) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    const size_t bar = s.find('|', start);
    parts.push_back(s.substr(start, bar == std::string::npos ? bar : bar - start));
    if (bar == std::string::npos) break;
    start = bar + 1;
  }
  if (parts.size() < 9 || parts[0].empty()) return false;

  RadarPalette p;
  p.name = parts[0];
  Rgba* fields[] = {&p.weak,        &p.medium,      &p.strong,
                    &p.doppler_approaching, &p.doppler_receding,
                    &p.trail_start, &p.trail_end,   &p.background};
  for (size_t i = 0; i < 8; ++i)
    if (!UnHex(parts[i + 1], fields[i])) return false;
  *out = p;
  return true;
}
