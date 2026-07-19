/******************************************************************************
 * mayara_pi - the radar window: radar image + control panel side by side.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <functional>
#include <vector>

#include <wx/wx.h>

#include "MayaraTheme.h"

class MayaraClient;
class RadarDisplayPanel;
class ControlsPanel;

// A radar window shows one or more radars (a grid of PPI pictures) plus a
// shared, collapsible control panel bound to the focused radar.
class MayaraPpiWindow : public wxDialog {
 public:
  MayaraPpiWindow(wxWindow* parent, MayaraClient* client,
                  std::vector<int> radar_indices);

  void ApplyTheme(const MayaraTheme& theme);

  // Wire the plugin's overlay on/off state into the View section.
  void SetOverlayControl(std::function<bool()> get,
                         std::function<void(bool)> set);

  // Wire the settings (gear) button to the plugin's preferences dialog.
  void SetSettingsControl(std::function<void()> open);

  // Wire the View section's "Auto layout" button.
  void SetAutoLayoutControl(std::function<void()> cb);

 private:
  void OnClose(wxCloseEvent& event);

  wxWindow* m_grid = nullptr;  // container of the radar pictures
  std::vector<RadarDisplayPanel*> m_radars;
  ControlsPanel* m_controls = nullptr;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
