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

class MayaraClient;

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
  void ApplyTheme(const MayaraTheme& theme);

 private:
  void OnPaint(wxPaintEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnLeftDown(wxMouseEvent& event);
  void DrawLozenges(wxDC& dc, const wxSize& sz);
  void TogglePower();
  void StepRange(int direction);  // -1 down, +1 up

  MayaraClient* m_client;  // not owned
  int m_index = 0;         // which radar this panel shows
  wxTimer m_timer;
  std::function<void()> m_on_menu;
  std::function<void()> m_on_focus;
  MayaraTheme m_theme;

  // Clickable overlay regions, updated each paint.
  wxRect m_menu_rect;
  wxRect m_power_rect;
  wxRect m_range_minus_rect;
  wxRect m_range_plus_rect;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_RADAR_DISPLAY_PANEL_H_
