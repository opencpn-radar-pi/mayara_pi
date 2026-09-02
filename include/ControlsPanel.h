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
#include "RadarDisplayPanel.h"  // PpiPrefs

class MayaraClient;

class ControlsPanel : public wxScrolledWindow {
 public:
  ControlsPanel(wxWindow* parent, MayaraClient* client, int radar_index = 0);

  // Bind these controls to a specific radar, and (when a window hosts more than
  // one) the set of radars its selector may switch between.
  void SetRadarIndex(int index);
  int RadarIndex() const { return m_index; }
  // Single-control mode shows just one control (opened by a gauge icon).
  void SetSingleControl(const std::string& id);
  const std::string& SingleControl() const { return m_single_id; }
  void SetRadarList(std::vector<int> indices) {
    m_radar_list = std::move(indices);
    if (m_built) Rebuild();
  }

  void SetCloseCallback(std::function<void()> cb) { m_on_close = std::move(cb); }
  void SetSettingsCallback(std::function<void()> cb) {
    m_on_settings = std::move(cb);
  }
  void SetAutoLayoutCallback(std::function<void()> cb) {
    m_on_autolayout = std::move(cb);
    if (m_built) Rebuild();  // add the button to the View section
  }
  void ApplyTheme(const MayaraTheme& theme);

  // View section wiring (overlay + PPI visibility live in the plugin/window).
  void SetViewControls(std::function<bool()> get_overlay,
                       std::function<void(bool)> set_overlay,
                       std::function<bool()> get_ppi,
                       std::function<void(bool)> set_ppi);

  // Orientation (0 head-up, 1 north-up, 2 course-up) for the View section.
  void SetOrientationControl(std::function<int()> get,
                             std::function<void(int)> set);

  // Display echo threshold (0 all, 1 hide weak, 2 only strong) for the View
  // section.
  void SetThresholdControl(std::function<int()> get,
                           std::function<void(int)> set);

  // "Dock in OpenCPN" toggle for the View section.
  void SetDockControl(std::function<bool()> get, std::function<void(bool)> set);

  // "Auto" button next to Range: whether the chart's zoom drives this
  // radar's range. get_relevant says whether this radar is on any canvas's
  // overlay right now -- the button is hidden entirely when it is not,
  // since toggling it would have nothing to act on.
  void SetRangeAutoControl(std::function<bool()> get_relevant,
                           std::function<bool()> get,
                           std::function<void(bool)> set);

  // The picture's two VRM/EBL markers. Local to the plugin, so they are read
  // and written straight on the focused picture rather than through a control.
  void SetVrmEblHandlers(std::function<VrmEbl(int)> get,
                         std::function<void(int, const VrmEbl&)> set) {
    m_vrm_get = std::move(get);
    m_vrm_set = std::move(set);
  }
  // How to read a marker's bearing: the focused picture's own reference, so
  // that a row and the picture cannot print the same marker differently.
  void SetBearingRefProvider(std::function<BearingRef()> p) {
    m_bearing_ref = std::move(p);
  }

  // Share the live guard-zone edit with the picture, so a dragged handle and a
  // typed number are one edit.
  // Re-read every widget from the model now, rather than at the next timer
  // tick. Used when a drag on the picture changes a value the panel shows.
  void SyncNow() { ApplyValues(); }

  void SetZoneEditHandlers(
      std::function<ZoneEdit()> get,
      std::function<void(const ZoneEdit&, bool commit)> set) {
    m_zone_get = std::move(get);
    m_zone_set = std::move(set);
  }

  // Operator display preferences (refresh rate, wheel direction, menu
  // auto-hide) for the View section. Global, not per radar.
  void SetPrefsControl(std::function<PpiPrefs()> get,
                       std::function<void(const PpiPrefs&)> set);

 private:
#ifdef __WXMSW__
  // Windows always shows a native scrollbar, so drawing our own would put a
  // second bar beside it. Only macOS, which hides the native one, needs it.
  static const int kScrollBarW = 0;
#else
  static const int kScrollBarW = 10;  // gutter for the scrollbar we draw
#endif
  wxSizer* WithScrollGutter(wxSizer* content);
  wxRect ThumbRect() const;
  void OnPaint(wxPaintEvent& event);
  void OnBarMouse(wxMouseEvent& event);
  wxSizer* MakeCloseRow();  // a "Controls  ×" header row
  void ThemeChildren();
  void ScrollSectionIntoView(wxWindow* header, wxSizer* content);
  void AddCollapsibleSection(wxSizer* root, const wxString& title,
                             const std::string& key,
                             std::function<void(wxSizer*)> fill);
  void AddControl(wxSizer* content, const ControlDef& def);
  void AddServerRow(wxSizer* content);  // active server URL, in Info
  void FillVrmEblSection(wxSizer* content);  // the two local markers
  void FillViewSection(wxSizer* content);
  // A labelled row of mutually exclusive buttons over an int, kept in sync by
  // an updater. The View section is made of these.
  void AddChoiceRow(wxSizer* content, const wxString& label,
                    const std::vector<wxString>& labels,
                    std::function<int()> get, std::function<void(int)> set);
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
  void AddSector(wxSizer* sizer, const ControlDef& def);  // no-transmit sector
  void AddZone(wxSizer* sizer, const ControlDef& def);    // guard zone
  void AddPlaceholder(wxSizer* sizer, const ControlDef& def);

  void Set(const std::string& id, const std::string& json_body);
  RadarControls* controls();  // the bound radar's controls, or null

  MayaraClient* m_client;  // not owned
  int m_index = 0;         // which radar these controls drive
  std::vector<int> m_radar_list;  // radars this window hosts (for the selector)
  std::string m_single_id;        // non-empty: show only this control
  wxTimer m_timer;
  uint64_t m_last_gen = ~0ull;
  uint64_t m_schema_gen = ~0ull;
  bool m_built = false;

  // Value updaters: read the model and refresh the corresponding widgets.
  std::vector<std::function<void()>> m_updaters;
  std::function<void()> m_on_close;
  std::function<void()> m_on_settings;
  std::function<void()> m_on_autolayout;
  MayaraTheme m_theme;

  std::map<std::string, bool> m_collapsed;  // per-section collapse state

  std::function<bool()> m_get_overlay;
  std::function<void(bool)> m_set_overlay;
  std::function<bool()> m_get_ppi;
  std::function<void(bool)> m_set_ppi;
  std::function<int()> m_get_orientation;
  std::function<void(int)> m_set_orientation;
  std::function<int()> m_get_threshold;
  std::function<void(int)> m_set_threshold;
  std::function<bool()> m_get_dock;
  std::function<void(bool)> m_set_dock;
  std::function<bool()> m_range_auto_relevant;
  std::function<bool()> m_get_range_auto;
  std::function<void(bool)> m_set_range_auto;
  bool m_rebuilding = false;
  bool m_dragging_bar = false;  // the drawn scrollbar's thumb  // true while widgets are being destroyed
  std::function<VrmEbl(int)> m_vrm_get;
  std::function<void(int, const VrmEbl&)> m_vrm_set;
  std::function<BearingRef()> m_bearing_ref;
  std::function<ZoneEdit()> m_zone_get;
  std::function<void(const ZoneEdit&, bool)> m_zone_set;
  std::function<PpiPrefs()> m_get_prefs;
  std::function<void(const PpiPrefs&)> m_set_prefs;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_CONTROLS_PANEL_H_
