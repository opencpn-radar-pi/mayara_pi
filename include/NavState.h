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

#endif  // MAYARA_NAV_STATE_H_
