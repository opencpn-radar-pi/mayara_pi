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

  // The legend exactly as mayara-server computes it: a blue-green-red ramp it
  // builds itself in default_legend(). The reference the others depart from.
  RadarPalette mayara;
  mayara.name = "Standard Mayara";
  mayara.builtin = true;
  mayara.from_server = true;
  out.push_back(mayara);

  // Four single-hue ramps: dark at the weakest return, full colour in the
  // middle, a pale tint of the same hue at the strongest. One hue means the
  // strength of an echo reads as brightness alone, which is what a monochrome
  // radar display has always done.
  //
  // Doppler stays on mayara's own two colours across all of them -- magenta
  // approaching, green receding -- because a Doppler mark has to be the one
  // thing on the screen that cannot be mistaken for an echo. The green ramp is
  // the exception, where green would be exactly that; there receding is cyan.
  // Trails stay neutral white-to-grey, which contrasts with every hue.
  struct Hue {
    const char* name;
    Rgba weak, medium, strong, receding;
  };
  const Hue hues[] = {
      {"Red", C(64, 0, 0), C(220, 0, 0), C(255, 170, 150), C(0, 255, 0)},
      {"Yellow", C(64, 56, 0), C(220, 200, 0), C(255, 250, 180), C(0, 255, 0)},
      {"Green", C(0, 64, 0), C(0, 210, 0), C(180, 255, 180), C(0, 255, 255)},
      {"Blue", C(0, 0, 80), C(40, 90, 255), C(180, 215, 255), C(0, 255, 0)},
  };
  for (const Hue& h : hues) {
    RadarPalette p;
    p.name = h.name;
    p.builtin = true;
    p.weak = h.weak;
    p.medium = h.medium;
    p.strong = h.strong;
    p.doppler_approaching = C(255, 0, 255);
    p.doppler_receding = h.receding;
    p.trail_start = C(255, 255, 255);
    p.trail_end = C(69, 69, 69);
    p.background = C(80, 80, 80);
    out.push_back(p);
  }

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
