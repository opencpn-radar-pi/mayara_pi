/******************************************************************************
 * mayara_pi - control panel beside the radar image.
 *
 * Schema-driven like the mayara web GUI: widgets are generated from the radar's
 * capability schema (one widget per dataType, grouped by category). Power /
 * Range / RangeUnits get prominent fixed placement at the top.
 *****************************************************************************/
#ifndef MAYARA_CONTROLS_PANEL_H_
#define MAYARA_CONTROLS_PANEL_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include "MayaraTheme.h"
#include "RadarControls.h"

class MayaraClient;

class ControlsPanel : public wxScrolledWindow {
 public:
  ControlsPanel(wxWindow* parent, MayaraClient* client, int radar_index = 0);

  // Bind these controls to a specific radar, and (when a window hosts more than
  // one) the set of radars its selector may switch between.
  void SetRadarIndex(int index);
  int RadarIndex() const { return m_index; }
  void SetRadarList(std::vector<int> indices) {
    m_radar_list = std::move(indices);
    if (m_built) Rebuild();
  }

  void SetCloseCallback(std::function<void()> cb) { m_on_close = std::move(cb); }
  void SetSettingsCallback(std::function<void()> cb) {
    m_on_settings = std::move(cb);
  }
  void ApplyTheme(const MayaraTheme& theme);

  // View section wiring (overlay + PPI visibility live in the plugin/window).
  void SetViewControls(std::function<bool()> get_overlay,
                       std::function<void(bool)> set_overlay,
                       std::function<bool()> get_ppi,
                       std::function<void(bool)> set_ppi);

 private:
  wxSizer* MakeCloseRow();  // a "Controls  ×" header row
  void ThemeChildren();
  void AddCollapsibleSection(wxSizer* root, const wxString& title,
                             const std::string& key,
                             std::function<void(wxSizer*)> fill);
  void AddControl(wxSizer* content, const ControlDef& def);
  void FillViewSection(wxSizer* content);
  void OnTimer(wxTimerEvent& event);
  void Rebuild();      // (re)build widgets from the schema
  void ApplyValues();  // push current model values into the widgets

  // Widget builders. Each adds a row to `sizer` and registers a value updater.
  void AddNumber(wxSizer* sizer, const ControlDef& def);
  void AddEnum(wxSizer* sizer, const ControlDef& def, bool as_buttons);
  void AddButton(wxSizer* sizer, const ControlDef& def);
  void AddReadonly(wxSizer* sizer, const ControlDef& def);
  void AddRange(wxSizer* sizer, const ControlDef& def,
                const std::vector<int>& ranges);
  void AddPlaceholder(wxSizer* sizer, const ControlDef& def);

  void Set(const std::string& id, const std::string& json_body);
  RadarControls* controls();  // the bound radar's controls, or null

  MayaraClient* m_client;  // not owned
  int m_index = 0;         // which radar these controls drive
  std::vector<int> m_radar_list;  // radars this window hosts (for the selector)
  wxTimer m_timer;
  uint64_t m_last_gen = ~0ull;
  uint64_t m_schema_gen = ~0ull;
  bool m_built = false;

  // Value updaters: read the model and refresh the corresponding widgets.
  std::vector<std::function<void()>> m_updaters;
  std::function<void()> m_on_close;
  std::function<void()> m_on_settings;
  MayaraTheme m_theme;

  std::map<std::string, bool> m_collapsed;  // per-section collapse state

  std::function<bool()> m_get_overlay;
  std::function<void(bool)> m_set_overlay;
  std::function<bool()> m_get_ppi;
  std::function<void(bool)> m_set_ppi;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_CONTROLS_PANEL_H_
