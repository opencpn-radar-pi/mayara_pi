/******************************************************************************
 * mayara_pi - radar window content (a grid of radar pictures + a shared
 * control panel).
 *
 * MayaraPpiWindow is a wxPanel so it can be hosted two ways: inside a floating
 * wxFrame (the default, free-floating window), or as a docked wxAuiManager
 * pane inside the OpenCPN main frame. The plugin picks the host; the content
 * and behaviour are identical either way.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <functional>
#include <string>
#include <vector>

#include <wx/wx.h>
#include <wx/aui/framemanager.h>

#include "MayaraTheme.h"
#include "NavState.h"
#include "RadarDisplayPanel.h"  // PpiPrefs

class MayaraClient;
class RadarDisplayPanel;
class ControlsPanel;

class MayaraPpiWindow : public wxPanel {
 public:
  MayaraPpiWindow(wxWindow* parent, MayaraClient* client,
                  std::vector<int> radar_indices);

  const wxString& Title() const { return m_title; }

  // Host wiring: exactly one of these is called right after construction.
  void SetFloatingHost(wxFrame* frame) { m_frame = frame; m_docked = false; }
  void SetDockedHost(wxAuiManager* aui) { m_aui = aui; m_docked = true; }
  bool IsDocked() const { return m_docked; }
  wxFrame* HostFrame() const { return m_frame; }  // null when docked

  // Host operations (dispatch to the floating frame or the AUI pane).
  void ShowWindow(bool show);
  bool IsWindowShown();
  wxRect WindowRect();
  void SetWindowRect(const wxRect& r);  // floating only
  // Borderless full-screen for the floating frame. solo=true fills the display
  // it is on (ShowFullScreen); otherwise it fills the given `target` rect
  // borderless (for two windows sharing a display).
  void EnterFullScreen(const wxRect& target, bool solo);
  void LeaveFullScreen();
  bool IsFullScreen() const { return m_fs; }

  void ApplyTheme(const MayaraTheme& theme);

  // Wire the plugin's overlay on/off state into the View section.
  void SetOverlayControl(std::function<bool()> get,
                         std::function<void(bool)> set);

  // Wire the settings (gear) button to the plugin's preferences dialog.
  void SetSettingsControl(std::function<void()> open);

  // Wire the View section's "Auto layout" button.
  void SetAutoLayoutControl(std::function<void()> cb);

  // Wire the View section's "Dock in OpenCPN" toggle.
  void SetDockControl(std::function<bool()> get, std::function<void(bool)> set);

  // Wire the View section's "On top" toggle -- floating only, see
  // mayara_pi.cpp for why this needs wxFRAME_FLOAT_ON_PARENT specifically.
  void SetStayOnTopControl(std::function<bool()> get,
                           std::function<void(bool)> set);

  // Where the rendering time goes, for every picture in this window. Wired
  // only while verbose logging is on.
  void SetPerfLog(std::function<void(const wxString&)> cb);

  // The plugin's heading resolver, injected rather than reimplemented here, so
  // the Diagnostics settings -- heading source, fixed heading, staleness
  // timeout -- govern the picture exactly as they govern the chart overlay.
  // Without it each picture kept its own OpenCPN-then-radar order and quietly
  // ignored all three.
  void SetHeadingProvider(std::function<bool(int, double&)> cb);

  // Provide own-ship nav state to the radar pictures (COG/heading/AIS layers).
  void SetNavProvider(std::function<NavState()> provider);

  // The guard-zone alarm sound lozenge on every picture: a plugin-wide
  // setting, so one getter/toggle pair is wired to every radar in the window.
  void SetAlarmSoundControl(std::function<bool()> get,
                            std::function<void()> toggle);

  // Per-radar orientation: `get_mode(radarId)` / `set_mode(radarId, mode)`
  // initialise each picture and wire the View toggle to the focused radar.
  void SetOrientationHandlers(
      std::function<int(const std::string&)> get_mode,
      std::function<void(const std::string&, int)> set_mode);

  // Per-radar display echo threshold, wired like the orientation handlers.
  void SetThresholdHandlers(
      std::function<int(const std::string&)> get_level,
      std::function<void(const std::string&, int)> set_level);

  // Range Auto: `get_relevant(index)` says whether that radar is on any
  // canvas's overlay right now; `get`/`set(radarId, on)` are the persisted
  // per-radar setting. Wired to the focused radar's Auto button, next to
  // Range in the controls.
  void SetRangeAutoHandlers(std::function<bool(int)> get_relevant,
                            std::function<bool(const std::string&)> get,
                            std::function<void(const std::string&, bool)> set);

  // Global display preferences (refresh rate, wheel direction, menu
  // auto-hide). Pushed to every picture and to the auto-hide timer.
  void SetPrefsHandlers(std::function<PpiPrefs()> get,
                        std::function<void(const PpiPrefs&)> set);
  // The guard-zone edit shared by the controls and every picture in this
  // window. Owned here because both sides need the same one.
  void WireZoneEditing();

  // Re-read the prefs and push them to the pictures + auto-hide timer. Public
  // because they are global: a change made in one window applies to them all.
  void ApplyPrefs();

 private:
  void OnSize(wxSizeEvent& event);
  void OnIdleTimer(wxTimerEvent& event);  // menu auto-hide
  // Widen the (floating) window by `extra` px to fit the controls, shifting it
  // left/up if that would push it off the display. No-op when docked.
  void GrowForControls(int extra);
  RadarDisplayPanel* FocusedPanel();  // panel driving the shared controls
  // Width the controls need, including the native vertical scrollbar.
  int ControlsWidth() const;
  // Float the controls immediately to the right of `focused`, growing the
  // window only if needed (and allowed) to make room.
  void PositionControls(RadarDisplayPanel* focused, bool allow_grow = true);
  void PositionControlsTopRight();     // small single-control popup
  void HideControls();                 // hide the panel + clear obscured/solo
  int DesiredCols() const;             // columns from the window's aspect
  void BuildGrid();                    // show all pictures in a grid
  void SoloPicture(RadarDisplayPanel* only);  // show just one picture
  // Show or hide the radar pictures for the View section PPI toggle. They are
  // hidden one by one rather than by hiding m_grid: m_controls is a child of
  // m_grid, so hiding the container would take the panel with it and leave no
  // way to switch the pictures back on.
  void ShowPictures(bool show);
  wxWindow* HostWindow();  // the floating frame, or this when docked

  MayaraClient* m_client = nullptr;  // not owned
  wxString m_title;
  wxFrame* m_frame = nullptr;    // floating host (null when docked)
  wxAuiManager* m_aui = nullptr;  // docked host (null when floating)
  bool m_docked = false;
  wxWindow* m_grid = nullptr;        // container of the radar pictures
  std::vector<RadarDisplayPanel*> m_radars;
  ControlsPanel* m_controls = nullptr;
  std::function<int(const std::string&)> m_orient_get;
  std::function<void(const std::string&, int)> m_orient_set;
  std::function<int(const std::string&)> m_thresh_get;
  std::function<void(const std::string&, int)> m_thresh_set;
  std::function<bool(int)> m_range_auto_relevant;
  std::function<bool(const std::string&)> m_range_auto_get;
  std::function<void(const std::string&, bool)> m_range_auto_set;
  std::function<PpiPrefs()> m_prefs_get;
  std::function<void(const PpiPrefs&)> m_prefs_set;
  ZoneEdit m_zone_edit;      // live, uncommitted guard-zone edit
  wxTimer m_idle_timer;      // 1 Hz, only while auto-hide is on
  int m_idle_secs = 0;       // seconds the pointer has been off the controls
  int m_grid_cols = 0;      // current grid column count (-1 while soloed)
  bool m_solo = false;      // a single picture is shown for its open menu
  RadarDisplayPanel* m_solo_panel = nullptr;  // which one, while m_solo
  bool m_pictures_shown = true;  // View section PPI toggle
  bool m_grew = false;      // the menu widened the window
  wxRect m_pre_grow;        // window geometry before it was widened
  bool m_fs = false;        // frame is in radar full-screen
  bool m_fs_solo = false;   // used ShowFullScreen (vs borderless tile)
  wxRect m_fs_saved;        // frame geometry before full-screen
  long m_fs_style = 0;      // frame style before borderless

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
