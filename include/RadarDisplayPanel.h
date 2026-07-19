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

class MayaraClient;

class RadarDisplayPanel : public wxPanel {
 public:
  RadarDisplayPanel(wxWindow* parent, MayaraClient* client);

  void SetMenuCallback(std::function<void()> cb) { m_on_menu = std::move(cb); }

 private:
  void OnPaint(wxPaintEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnSize(wxSizeEvent& event);
  void DrawLozenges(wxDC& dc, const wxSize& sz);

  MayaraClient* m_client;  // not owned
  wxTimer m_timer;
  wxButton* m_menu_btn = nullptr;
  std::function<void()> m_on_menu;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_RADAR_DISPLAY_PANEL_H_
