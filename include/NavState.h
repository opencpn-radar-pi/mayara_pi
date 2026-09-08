/******************************************************************************
 * mayara_pi - own-ship navigation state shared with the PPI overlays.
 *
 * Filled from OpenCPN's position fix and handed to each radar picture so it can
 * draw true-referenced layers (COG line, north marker, AIS) on the head-up PPI.
 *****************************************************************************/
#ifndef MAYARA_NAV_STATE_H_
#define MAYARA_NAV_STATE_H_

struct NavState {
  bool valid = false;   // a fix has been received
  double lat = 0.0;     // decimal degrees
  double lon = 0.0;
  double cog = 0.0;     // course over ground, degrees true
  double sog = 0.0;     // speed over ground, knots
  double hdt = 0.0;     // heading, degrees true
  bool has_hdt = false;  // hdt is valid (not just a COG fallback)
  bool has_cog = false;
};

// The chart's pointer, and the spot last clicked on the chart, as polar from
// one radar -- so a picture can place them without knowing where that radar
// is. Filled by the plugin from OpenCPN's cursor callback and mouse hook.
struct ChartCursor {
  bool live = false;      // the pointer has been over a chart
  double live_brg = 0.0;  // true bearing from the radar, degrees
  double live_m = 0.0;    // distance from the radar, metres
  bool mark = false;      // a left click on the chart dropped a marker
  double mark_brg = 0.0;
  double mark_m = 0.0;
};

#endif  // MAYARA_NAV_STATE_H_
