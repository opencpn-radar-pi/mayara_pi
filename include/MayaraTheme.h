/******************************************************************************
 * mayara_pi - UI colour theme for day/dusk/night.
 *
 * Plain struct (no OpenCPN dependency) so every panel can consume it. The
 * plugin computes it from OpenCPN's PI_ColorScheme and pushes it down.
 *****************************************************************************/
#ifndef MAYARA_THEME_H_
#define MAYARA_THEME_H_

#include <wx/colour.h>

struct MayaraTheme {
  wxColour panel_bg{32, 32, 36};
  wxColour text{230, 230, 235};
  wxColour dim_text{150, 150, 160};
  wxColour lozenge_bg{24, 24, 28};
  wxColour lozenge_border{90, 90, 96};
  wxColour accent{120, 255, 120};       // active / transmit
  wxColour accent_dim{235, 200, 110};   // standby / inactive
};

#endif  // MAYARA_THEME_H_
