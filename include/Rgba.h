/******************************************************************************
 * mayara_pi - one colour, as the legend and the palettes both use it.
 *
 * Its own header so RadarState and RadarPalette can each have it without
 * including one another: the state owns a palette, and a palette is made of
 * these.
 *****************************************************************************/
#ifndef MAYARA_RGBA_H_
#define MAYARA_RGBA_H_

#include <cstdint>

struct Rgba {
  uint8_t r = 0, g = 0, b = 0, a = 0;
};

#endif  // MAYARA_RGBA_H_
