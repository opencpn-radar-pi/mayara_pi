/******************************************************************************
 * mayara_pi - echo colour palettes.
 *
 * The server sends a legend: one colour per spoke byte, split into an echo
 * strength ramp, a static-background entry, two Doppler entries and a trail
 * (history) ramp. A palette re-colours that legend in place, keeping its
 * structure -- so a profile is a handful of colours, not 51 of them, and it
 * works for any radar whatever legend length it reports.
 *
 * Four profiles ship with the plugin. The three named after makers are in the
 * spirit of their displays rather than measured from them; the point is a
 * familiar look to start from, and every one of them can be copied and edited.
 *
 * Deliberately free of wx and JSON, like RadarState, so the rendering side can
 * use it without dragging the UI in.
 *****************************************************************************/
#ifndef MAYARA_RADAR_PALETTE_H_
#define MAYARA_RADAR_PALETTE_H_

#include <string>
#include <vector>

#include "Rgba.h"

struct RadarPalette {
  RadarPalette();  // Furuno-ish neutral defaults, overwritten by the builtins

  std::string name;
  bool builtin = false;
  // "Standard Mayara": leave the server's own legend exactly as it came. The
  // colours below are still filled in, so copying it gives an editable start.
  bool from_server = false;

  Rgba weak, medium, strong;             // echo ramp: bottom, middle, top
  Rgba doppler_approaching, doppler_receding;
  Rgba trail_start;    // freshest trail
  Rgba trail_end;      // oldest trail before it fades out
  Rgba background;     // the legend's static-background entry
};

// The four that ship. Index 0 is "Standard Mayara".
std::vector<RadarPalette> BuiltinPalettes();

// Config-friendly round trip: "name|rrggbbaa|..." on one line. ParsePalette
// returns false if the line is not a palette (missing fields, bad colour).
std::string PaletteToString(const RadarPalette& p);
bool PaletteFromString(const std::string& s, RadarPalette* out);

#endif  // MAYARA_RADAR_PALETTE_H_
