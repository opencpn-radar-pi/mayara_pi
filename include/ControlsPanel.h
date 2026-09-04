/******************************************************************************
 * mayara_pi - control panel beside the radar image.
 *
 * Schema-driven like the mayara web GUI: widgets are generated from the radar's
 * capability schema (one widget per dataType, grouped by category). Power /
 * Range / RangeUnits get prominent fixed placement at the top.
 *****************************************************************************/
#ifndef MAYARA_CONTROLS_PANEL_H_
#define MAYARA_CONTROLS_PANEL_H_

#include <functional>
#include <string>
#include <vector>

#include <wx/wx.h>

#include "MayaraTheme.h"
#include "RadarControls.h"
#include "RadarDisplayPanel.h"  // PpiPrefs

class MayaraClient;
class ControlsBody;  // the scrollable content; defined in ControlsPanel.cpp

// A fixed "<title>  <gear>  X" header over a scrollable body of schema-driven
// radar controls (ControlsBody). The header is a plain, non-scrolling wxWindow
// so it never scrolls out of view with the body's content, and so the close
// button can never end up under the body's scrollbar -- which, unlike this
// shell, has to stay native and toolkit-managed on GTK; see ControlsBody's
// constructor for why.
class ControlsPanel : public wxWindow {
 public:
  ControlsPanel(wxWindow* parent, MayaraClient* client, int radar_index = 0,
                const wxString& title = wxEmptyString);

  // Bind these controls to a specific radar, and (when a window hosts more than
  // one) the set of radars its selector may switch between.
  void SetRadarIndex(int index);
  int RadarIndex() const;
  // Single-control mode shows just one control (opened by a gauge icon).
  void SetSingleControl(const std::string& id);
  const std::string& SingleControl() const;
  void SetRadarList(std::vector<int> indices);

  void SetCloseCallback(std::function<void()> cb) { m_on_close = std::move(cb); }
  void SetSettingsCallback(std::function<void()> cb) {
    m_on_settings = std::move(cb);
  }
  void SetAutoLayoutCallback(std::function<void()> cb);
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
                         std::function<void(int, const VrmEbl&)> set);
  // How to read a marker's bearing: the focused picture's own reference, so
  // that a row and the picture cannot print the same marker differently.
  void SetBearingRefProvider(std::function<BearingRef()> p);

  // Share the live guard-zone edit with the picture, so a dragged handle and a
  // typed number are one edit.
  // Re-read every widget from the model now, rather than at the next timer
  // tick. Used when a drag on the picture changes a value the panel shows.
  void SyncNow();

  void SetZoneEditHandlers(
      std::function<ZoneEdit()> get,
      std::function<void(const ZoneEdit&, bool commit)> set);

  // Operator display preferences (refresh rate, wheel direction, menu
  // auto-hide) for the View section. Global, not per radar.
  void SetPrefsControl(std::function<PpiPrefs()> get,
                       std::function<void(const PpiPrefs&)> set);

  // Let the host make this panel free-floating: draggable by its title bar,
  // resizable via a bottom-right corner grip. Both callbacks get the pointer
  // delta since the last event; the host owns clamping/persisting the actual
  // geometry (this panel doesn't know its parent's bounds). Used only for the
  // chart-canvas overlay -- the PPI window's docked/popup placements manage
  // their own geometry and never call this.
  void SetFreeFloatHandlers(std::function<void(int dx, int dy)> on_drag,
                            std::function<void(int dw, int dh)> on_resize);

 private:
  wxSizer* MakeCloseRow(const wxString& title);  // fixed "<title>  <gear>  X"
  wxRect GripRect() const;  // free-float resize grip, bottom-right corner
  void OnPaint(wxPaintEvent& event);
  void OnBarMouse(wxMouseEvent& event);    // free-float drag/resize only
  void OnTitleMouse(wxMouseEvent& event);  // free-float drag, on the title row
  void OnCaptureLost(wxMouseCaptureLostEvent& event);
  void ThemeChildren();  // this shell's own chrome; the body themes itself

  ControlsBody* m_body;
  MayaraTheme m_theme;
  std::function<void()> m_on_close;
  std::function<void()> m_on_settings;

  wxStaticText* m_title = nullptr;            // "<title>" header label
  std::function<void(int, int)> m_on_drag;    // free-float: title bar drag
  std::function<void(int, int)> m_on_resize;  // free-float: corner grip drag
  bool m_dragging_title = false;
  bool m_resizing = false;
  wxPoint m_drag_last;  // screen coords, valid while dragging or resizing
  // The reserved strip GripRect() draws into; zero height until
  // SetFreeFloatHandlers wires an on_resize, so instances that never do (the
  // PPI window's) don't lose body space for a grip they can never show.
  wxSizerItem* m_grip_spacer = nullptr;
};

#endif  // MAYARA_CONTROLS_PANEL_H_
