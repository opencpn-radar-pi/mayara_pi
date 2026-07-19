/******************************************************************************
 * mayara_pi - the radar window: radar image + control panel side by side.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <functional>

#include <wx/wx.h>

#include "MayaraTheme.h"

class MayaraClient;
class RadarDisplayPanel;
class ControlsPanel;

class MayaraPpiWindow : public wxDialog {
 public:
  MayaraPpiWindow(wxWindow* parent, MayaraClient* client);

  void ApplyTheme(const MayaraTheme& theme);

  // Wire the plugin's overlay on/off state into the View section.
  void SetOverlayControl(std::function<bool()> get,
                         std::function<void(bool)> set);

 private:
  void OnClose(wxCloseEvent& event);

  RadarDisplayPanel* m_radar = nullptr;
  ControlsPanel* m_controls = nullptr;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
