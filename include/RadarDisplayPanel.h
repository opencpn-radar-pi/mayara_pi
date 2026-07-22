/******************************************************************************
 * mayara_pi - the radar-image panel (CPU-rendered PPI) inside the radar window.
 *
 * Draws the radar image, the power/range lozenges (like the web GUI), and hosts
 * the hamburger button that toggles the control panel.
 *****************************************************************************/
#ifndef MAYARA_RADAR_DISPLAY_PANEL_H_
#define MAYARA_RADAR_DISPLAY_PANEL_H_

#include <functional>
#include <string>

#include <wx/wx.h>
#include <wx/timer.h>

#include "MayaraTheme.h"
#include "NavState.h"

class MayaraClient;

enum PpiOrientation { kHeadUp = 0, kNorthUp = 1, kCourseUp = 2 };

// Which extra layers to paint over the radar picture.
struct PpiLayers {
  bool range_rings = true;
  bool compass = true;  // degree ring with bearing ticks/labels
  bool heading_line = true;
  bool cog_line = true;
  bool north_marker = true;
  bool ais = true;
  bool arpa = true;  // server-tracked radar targets
};

class RadarDisplayPanel : public wxPanel {
 public:
  RadarDisplayPanel(wxWindow* parent, MayaraClient* client, int radar_index = 0);

  void SetMenuCallback(std::function<void()> cb) { m_on_menu = std::move(cb); }
  void SetViewCallback(std::function<void()> cb) { m_on_view = std::move(cb); }
  // Open a single control (gauge icons): the callback gets the control id.
  void SetControlCallback(std::function<void(const std::string&)> cb) {
    m_on_control = std::move(cb);
  }
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
  void SetOrientation(int o) { m_orientation = o; Refresh(false); }
  int Orientation() const { return m_orientation; }
  // Pixels obscured on the right by the open menu, so the picture re-centres in
  // the remaining space.
  void SetObscuredRight(int px) {
    if (px != m_obscured_right) {
      m_obscured_right = px;
      Refresh(false);
    }
  }
  void ApplyTheme(const MayaraTheme& theme);

 private:
  void OnPaint(wxPaintEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnLeftDown(wxMouseEvent& event);
  void DrawLozenges(wxDC& dc, const wxSize& sz);
  void DrawIconBar(wxDC& dc, const wxSize& sz);  // vertical hand-drawn toolbar
  // Paint the extra layers over the picture. `center` is the sweep origin,
  // `radius` the pixel radius of the reported range `report_m`.
  void DrawLayers(wxDC& dc, wxPoint center, double radius, double report_m,
                  bool metric);
  // Reported range (range control value, metres) + whether the range unit is
  // metric. Leaves the passed default report_m/metric if unavailable.
  void EffectiveRange(double& report_m, bool& metric) const;
  // Resolve the current orientation into: the true bearing shown at screen-up
  // (`up_bearing`), the clockwise raster rotation, and own-ship heading.
  void ResolveOrientation(double& up_bearing, double& raster_rot,
                          double& heading, bool& has_heading) const;
  void TogglePower();
  void StepRange(int direction);  // -1 down, +1 up

  MayaraClient* m_client;  // not owned
  int m_index = 0;         // which radar this panel shows
  wxTimer m_timer;
  std::function<void()> m_on_menu;
  std::function<void()> m_on_view;
  std::function<void(const std::string&)> m_on_control;
  std::function<void()> m_on_focus;
  std::function<NavState()> m_nav;  // own-ship nav provider (may be null)
  PpiLayers m_layers;
  int m_orientation = kHeadUp;
  int m_obscured_right = 0;  // px covered on the right by the open menu
  bool m_ebl_on = false;  // EBL/VRM toggle (placeholder until implemented)
  MayaraTheme m_theme;

  // Clickable overlay regions, updated each paint.
  wxRect m_menu_rect;    // icon-bar: Menu (hamburger)
  wxRect m_icon_ais;     // icon-bar: AIS on/off
  wxRect m_icon_gain;    // icon-bar: Gain gauge
  wxRect m_icon_sea;     // icon-bar: Sea gauge
  wxRect m_icon_rain;    // icon-bar: Rain gauge
  wxRect m_icon_ebl;     // icon-bar: EBL/VRM
  wxRect m_icon_view;    // icon-bar: View menu
  wxRect m_power_rect;
  wxRect m_range_minus_rect;
  wxRect m_range_plus_rect;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_RADAR_DISPLAY_PANEL_H_
