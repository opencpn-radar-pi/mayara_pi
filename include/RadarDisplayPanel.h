/******************************************************************************
 * mayara_pi - the radar-image panel (CPU-rendered PPI) inside the radar window.
 *
 * Draws the radar image, the power/range lozenges (like the web GUI), and hosts
 * the hamburger button that toggles the control panel.
 *****************************************************************************/
#ifndef MAYARA_RADAR_DISPLAY_PANEL_H_
#define MAYARA_RADAR_DISPLAY_PANEL_H_

#include <functional>

#include <wx/wx.h>
#include <wx/timer.h>

#include "MayaraTheme.h"
#include "NavState.h"

class MayaraClient;

// Which extra layers to paint over the radar picture.
struct PpiLayers {
  bool range_rings = true;
  bool heading_line = true;
  bool cog_line = true;
  bool north_marker = true;
  bool ais = true;
};

class RadarDisplayPanel : public wxPanel {
 public:
  RadarDisplayPanel(wxWindow* parent, MayaraClient* client, int radar_index = 0);

  void SetMenuCallback(std::function<void()> cb) { m_on_menu = std::move(cb); }
  // Fired when the picture (not a lozenge) is clicked; the window uses it to
  // focus this radar's controls.
  void SetFocusCallback(std::function<void()> cb) {
    m_on_focus = std::move(cb);
  }
  void SetRadarIndex(int index) { m_index = index; }
  int RadarIndex() const { return m_index; }
  void SetNavProvider(std::function<NavState()> p) { m_nav = std::move(p); }
  void SetLayers(const PpiLayers& l) { m_layers = l; Refresh(false); }
  const PpiLayers& Layers() const { return m_layers; }
  void ApplyTheme(const MayaraTheme& theme);

 private:
  void OnPaint(wxPaintEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnLeftDown(wxMouseEvent& event);
  void DrawLozenges(wxDC& dc, const wxSize& sz);
  // Paint the extra layers over the picture. `center` is the sweep origin,
  // `radius` the pixel radius of `range_m`.
  void DrawLayers(wxDC& dc, wxPoint center, double radius, uint32_t range_m);
  void TogglePower();
  void StepRange(int direction);  // -1 down, +1 up

  MayaraClient* m_client;  // not owned
  int m_index = 0;         // which radar this panel shows
  wxTimer m_timer;
  std::function<void()> m_on_menu;
  std::function<void()> m_on_focus;
  std::function<NavState()> m_nav;  // own-ship nav provider (may be null)
  PpiLayers m_layers;
  MayaraTheme m_theme;

  // Clickable overlay regions, updated each paint.
  wxRect m_menu_rect;
  wxRect m_power_rect;
  wxRect m_range_minus_rect;
  wxRect m_range_plus_rect;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_RADAR_DISPLAY_PANEL_H_
