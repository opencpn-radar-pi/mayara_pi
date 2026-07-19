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
#include <vector>

#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include "RadarControls.h"

class MayaraClient;

class ControlsPanel : public wxScrolledWindow {
 public:
  ControlsPanel(wxWindow* parent, MayaraClient* client);

 private:
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

  MayaraClient* m_client;  // not owned
  wxTimer m_timer;
  uint64_t m_last_gen = ~0ull;
  bool m_built = false;

  // Value updaters: read the model and refresh the corresponding widgets.
  std::vector<std::function<void()>> m_updaters;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_CONTROLS_PANEL_H_
