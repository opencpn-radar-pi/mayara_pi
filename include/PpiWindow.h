/******************************************************************************
 * mayara_pi - the radar window: radar image + control panel side by side.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <functional>
#include <string>
#include <vector>

#include <wx/wx.h>

#include "MayaraTheme.h"
#include "NavState.h"

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

  // Provide own-ship nav state to the radar pictures (COG/heading/AIS layers).
  void SetNavProvider(std::function<NavState()> provider);

  // Per-radar orientation: `get_mode(radarId)` / `set_mode(radarId, mode)`
  // initialise each picture and wire the View toggle to the focused radar.
  void SetOrientationHandlers(
      std::function<int(const std::string&)> get_mode,
      std::function<void(const std::string&, int)> set_mode);

 private:
  void OnClose(wxCloseEvent& event);
  void OnSize(wxSizeEvent& event);
  // Widen the window by `extra` px to fit the controls, shifting it left/up if
  // that would push it off the current display.
  void GrowForControls(int extra);
  RadarDisplayPanel* FocusedPanel();  // panel driving the shared controls
  // Float the controls immediately to the right of `focused`, growing the
  // window only if needed (and allowed) to make room.
  void PositionControls(RadarDisplayPanel* focused, bool allow_grow = true);
  void PositionControlsTopRight();     // small single-control popup
  int DesiredCols() const;             // columns from the window's aspect
  void BuildGrid();                    // show all pictures in a grid
  void SoloPicture(RadarDisplayPanel* only);  // show just one picture

  MayaraClient* m_client = nullptr;  // not owned
  wxWindow* m_grid = nullptr;        // container of the radar pictures
  std::vector<RadarDisplayPanel*> m_radars;
  int m_grid_cols = 0;      // current grid column count (-1 while soloed)
  bool m_solo = false;      // a single picture is shown for its open menu
  ControlsPanel* m_controls = nullptr;
  std::function<int(const std::string&)> m_orient_get;
  std::function<void(const std::string&, int)> m_orient_set;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
