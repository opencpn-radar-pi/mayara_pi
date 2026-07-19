/******************************************************************************
 * mayara_pi - the radar-image panel (CPU-rendered PPI) inside the radar window.
 *****************************************************************************/
#ifndef MAYARA_RADAR_DISPLAY_PANEL_H_
#define MAYARA_RADAR_DISPLAY_PANEL_H_

#include <wx/wx.h>
#include <wx/timer.h>

class MayaraClient;

class RadarDisplayPanel : public wxPanel {
 public:
  RadarDisplayPanel(wxWindow* parent, MayaraClient* client);

 private:
  void OnPaint(wxPaintEvent& event);
  void OnTimer(wxTimerEvent& event);

  MayaraClient* m_client;  // not owned
  wxTimer m_timer;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_RADAR_DISPLAY_PANEL_H_
