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

  // The legend exactly as mayara-server computes it -- a blue-green-red ramp
  // it builds itself in default_legend(), not the maker's palette. It is the
  // reference the other profiles depart from.
  RadarPalette mayara;
  mayara.name = "Standard Mayara";
  mayara.builtin = true;
  mayara.from_server = true;
  out.push_back(mayara);

  // The three below follow what each maker's own documentation says, with the
  // sources named. Where a manual gives a colour by name ("red", "yellow") the
  // exact shade is still ours: no maker publishes RGB values.

  // Navico's "Black/Yellow" radar image palette. VelocityTrack colours are
  // documented per palette -- Lowrance HDS Live operator manual, "Radar view
  // options": "Diverging targets are blue colored on all radar image
  // palettes", and for the Black/Yellow palette approaching targets are red.
  RadarPalette navico;
  navico.name = "Navico yellow";
  navico.builtin = true;
  navico.weak = C(90, 70, 0);
  navico.medium = C(210, 170, 0);
  navico.strong = C(255, 245, 140);
  navico.doppler_approaching = C(255, 40, 40);
  navico.doppler_receding = C(60, 130, 255);
  navico.trail_start = C(255, 255, 255);
  navico.trail_end = C(70, 65, 45);
  navico.background = C(70, 65, 45);
  out.push_back(navico);

  // Garmin MotionScope, from the GPSMAP owner's manual: "On most color
  // schemes, green indicates the target is moving away from you and red
  // indicates the target is moving toward you." The strength ramp is not
  // documented -- Garmin's manuals only say "Frgd. Color - Sets the color
  // scheme for the radar returns" without naming the schemes -- so the greens
  // below remain our reading of how those displays look.
  RadarPalette garmin;
  garmin.name = "Garmin";
  garmin.builtin = true;
  garmin.weak = C(0, 70, 40);
  garmin.medium = C(0, 200, 90);
  garmin.strong = C(255, 255, 170);
  garmin.doppler_approaching = C(255, 40, 40);
  garmin.doppler_receding = C(0, 220, 80);
  garmin.trail_start = C(230, 255, 230);
  garmin.trail_end = C(50, 70, 55);
  garmin.background = C(50, 70, 55);
  out.push_back(garmin);

  // Furuno's multicolour, from the DRS4W operator's manual (OME-C8, "Echo
  // Color"): "Multicolor paints each radar echo in a color according to its
  // strength, in red, yellow or green, corresponding to strong, medium and
  // weak echoes." Doppler follows Target Analyzer on the DRS-NXT series:
  // "red echoes are hazardous targets that are moving towards your vessel",
  // "green echoes are targets that stay stationary, or are moving away".
  RadarPalette furuno;
  furuno.name = "Furuno";
  furuno.builtin = true;
  furuno.weak = C(0, 200, 0);
  furuno.medium = C(255, 235, 0);
  furuno.strong = C(255, 30, 30);
  furuno.doppler_approaching = C(255, 30, 30);
  furuno.doppler_receding = C(0, 200, 0);
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
