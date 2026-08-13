/******************************************************************************
 * mayara_pi - the radar-image panel (CPU-rendered PPI) inside the radar window.
 *
 * Draws the radar image, the power/range lozenges (like the web GUI), and hosts
 * the hamburger button that toggles the control panel.
 *****************************************************************************/
#ifndef MAYARA_RADAR_DISPLAY_PANEL_H_
#define MAYARA_RADAR_DISPLAY_PANEL_H_

#include <functional>
#include <string>

#include <wx/wx.h>
#include <wx/timer.h>

#include "MayaraTheme.h"
#include "NavState.h"

class MayaraClient;

enum PpiOrientation { kHeadUp = 0, kNorthUp = 1, kCourseUp = 2 };

// Which extra layers to paint over the radar picture.
struct PpiLayers {
  bool range_rings = true;
  bool compass = true;  // degree ring with bearing ticks/labels
  bool heading_line = true;
  bool cog_line = true;
  bool north_marker = true;
  bool ais = true;
  bool arpa = true;         // server-tracked radar targets
  bool guard_zones = true;  // server guard zones (guardZone1/2)
  bool extreme_range = true;  // ring at the outer edge of the spoke data
};

// A guard zone being edited on the picture. The controls panel and the picture
// share one of these, so dragging a handle and typing in a field are the same
// edit seen from two places. Values are the zone's own: bow-relative radians
// and metres. Nothing is sent to the radar until the edit is committed.
struct ZoneEdit {
  bool active = false;
  int radar_index = -1;
  std::string id;  // "guardZone1" / "guardZone2"
  double start_rad = 0, end_rad = 0;
  double start_m = 0, end_m = 0;
};

// One VRM/EBL marker: a bearing line and a range ring that cross at the point
// being measured. Bow-relative radians and metres, like a guard zone -- but
// these live only in the plugin; the radar knows nothing about them.
struct VrmEbl {
  bool enabled = false;
  double bearing_rad = 0;
  double distance_m = 0;
};
const int kVrmEblCount = 2;

// Display preferences that are the operator's, not the radar's, so they are
// held by the plugin and pushed down rather than read from the control schema.
struct PpiPrefs {
  int refresh_hz = 5;       // PPI repaint rate, 1..15
  bool reverse_zoom = false;  // invert the wheel's zoom direction
  int menu_autohide = 0;    // 0 = never, 1 = 10 s, 2 = 30 s
  int overlay_alpha = 100;  // chart-overlay opacity, 25/50/75/100 %
  bool overlay_zones = true;  // draw guard zones on the chart too
  bool auto_range = false;    // nest the short radar at a quarter of the long
};

// Everything the picture and the layers over it are placed by. Computed once
// per use so the paint path, the hit tests and the layer drawing cannot
// disagree about where the sweep origin is -- which they would, now that the
// origin can be dragged away from the window centre.
struct PpiGeometry {
  wxPoint center;         // sweep origin on screen, pan offset included
  wxPoint offset;         // pan away from the window centre (committed + drag)
  double radius = 0;      // pixels the reported range maps to, at 1x zoom
  double report_m = 0;    // reported (range control) range, metres
  double spoke_m = 0;     // range of the last spoke pixel, metres; >= report_m
  bool metric = false;    // reported range unit
  double zoom = 1.0;      // free display zoom
  double up_bearing = 0;  // true bearing shown at screen-up
  double raster_rot = 0;  // clockwise rotation applied to the picture
  double heading = 0;
  bool has_heading = false;
  bool valid = false;  // usable for placing anything geographic
};

class RadarDisplayPanel : public wxPanel {
 public:
  RadarDisplayPanel(wxWindow* parent, MayaraClient* client, int radar_index = 0);

  void SetMenuCallback(std::function<void()> cb) { m_on_menu = std::move(cb); }
  // Open a single control (gauge icons): the callback gets the control id.
  void SetControlCallback(std::function<void(const std::string&)> cb) {
    m_on_control = std::move(cb);
  }
  // Fired when the picture (not a lozenge) is clicked; the window uses it to
  // focus this radar's controls.
  void SetFocusCallback(std::function<void()> cb) {
    m_on_focus = std::move(cb);
  }
  void SetRadarIndex(int index) { m_index = index; }
  int RadarIndex() const { return m_index; }
  void SetNavProvider(std::function<NavState()> p) { m_nav = std::move(p); }
  void SetLayers(const PpiLayers& l) { m_layers = l; Refresh(false); }
  const PpiLayers& Layers() const { return m_layers; }
  void SetOrientation(int o) { m_orientation = o; Refresh(false); }
  int Orientation() const { return m_orientation; }
  // Display echo threshold (0 all, 1 hide weak, 2 only strong). Applied to the
  // shared radar state so the disc re-maps.
  void SetThreshold(int level);
  int Threshold() const { return m_threshold; }
  // Repaint rate and wheel direction; the rest of PpiPrefs is the window's.
  void SetPrefs(const PpiPrefs& p);
  // The two VRM/EBL markers, for the controls panel to show and switch off.
  const VrmEbl& Marker(int i) const { return m_vrmebl[i]; }
  void SetMarker(int i, const VrmEbl& m) {
    m_vrmebl[i] = m;
    NotifyMarkers();
  }
  // Called whenever a marker changes, so the panel showing them can re-read.
  void SetMarkerChangedCallback(std::function<void()> cb) {
    m_on_markers = std::move(cb);
  }

  // Share the live guard-zone edit. `set` is called with commit=false while a
  // handle is being dragged (preview only) and commit=true on release.
  void SetZoneEditHandlers(
      std::function<ZoneEdit()> get,
      std::function<void(const ZoneEdit&, bool commit)> set) {
    m_zone_get = std::move(get);
    m_zone_set = std::move(set);
  }
  // Pixels obscured on the right by the open menu, so the picture re-centres in
  // the remaining space.
  void SetObscuredRight(int px) {
    if (px != m_obscured_right) {
      m_obscured_right = px;
      Refresh(false);
    }
  }
  void ApplyTheme(const MayaraTheme& theme);

  // Recentre the picture and reset the free zoom ("look around" -> centred).
  void CenterView();
  bool IsOffCenter() const;

 private:
  void OnPaint(wxPaintEvent& event);
  void OnTimer(wxTimerEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnLeftDown(wxMouseEvent& event);
  void OnLeftUp(wxMouseEvent& event);      // click actions land here, not on
                                           // down, so a drag is not a click
  void OnMotion(wxMouseEvent& event);      // drag to pan + cursor readout
  void OnLeave(wxMouseEvent& event);       // drop the cursor readout
  void OnLeftDClick(wxMouseEvent& event);  // acquire an ARPA target
  void OnMouseWheel(wxMouseEvent& event);  // free PPI display zoom
  // Act on a click that was not a drag, at `p`.
  void HandleClick(const wxPoint& p);
  // The current picture placement; see PpiGeometry.
  PpiGeometry Geometry() const;
  // Convert a click in the picture to a true bearing (deg) and distance (m)
  // from the radar. False if outside the picture or the range is unknown.
  bool PointToPolar(const wxPoint& p, double& bearing_deg,
                    double& distance_m) const;
  void DrawLozenges(wxDC& dc, const wxSize& sz);
  void DrawIconBar(wxDC& dc, const wxSize& sz);  // vertical hand-drawn toolbar
  // Paint the extra layers over the picture.
  void DrawLayers(wxDC& dc, const PpiGeometry& g);
  // Guard zones as the server reports them (bow-relative radians + metres).
  void DrawGuardZones(wxDC& dc, const PpiGeometry& g, double geo);
  // VRM/EBL markers, styled after the mayara GUI.
  void DrawVrmEbl(wxDC& dc, const PpiGeometry& g, double geo);
  // Cursor crosshair + a bearing/range readout for the pointer position.
  void DrawCursor(wxDC& dc, const PpiGeometry& g);
  // Drag handles for the zone being edited; mirrors the mayara GUI.
  void DrawZoneHandles(wxDC& dc, const PpiGeometry& g, double geo,
                       const ZoneEdit& z);
  // Screen position of each handle, in the order start/end/inner/outer.
  // Returns false when the zone cannot be placed.
  bool ZoneHandlePoints(const PpiGeometry& g, double geo, const ZoneEdit& z,
                        wxPoint out[4]) const;
  // Which handle (0..3) is under `p`, or -1.
  int ZoneHandleHit(const wxPoint& p) const;
  // Move the grabbed handle to `p`; commit=true also writes it to the radar.
  void DragZoneHandle(const wxPoint& p, bool commit);
  // Reported range (range control value, metres) + whether the range unit is
  // metric. Leaves the passed default report_m/metric if unavailable.
  void EffectiveRange(double& report_m, bool& metric) const;
  // Resolve the current orientation into: the true bearing shown at screen-up
  // (`up_bearing`), the clockwise raster rotation, and own-ship heading.
  void ResolveOrientation(double& up_bearing, double& raster_rot,
                          double& heading, bool& has_heading) const;
  void TogglePower();
  void StepRange(int direction);  // -1 down, +1 up

  MayaraClient* m_client;  // not owned
  int m_index = 0;         // which radar this panel shows
  wxTimer m_timer;
  std::function<void()> m_on_menu;
  std::function<void(const std::string&)> m_on_control;
  std::function<void()> m_on_focus;
  std::function<NavState()> m_nav;  // own-ship nav provider (may be null)
  PpiLayers m_layers;
  int m_orientation = kHeadUp;
  int m_threshold = 0;          // display echo threshold (0 all/1 weak/2 strong)
  double m_display_zoom = 1.0;  // free PPI magnification (0.5x - 5x), transient
  int m_obscured_right = 0;  // px covered on the right by the open menu

  // Off-centre view ("look around"): drag the picture away from the window
  // centre. m_drag is the in-flight delta, folded into m_off_center on release.
  wxPoint m_off_center = wxPoint(0, 0);
  wxPoint m_drag = wxPoint(0, 0);
  wxPoint m_mouse_down = wxPoint(0, 0);
  bool m_dragging = false;

  // VRM/EBL markers. m_ebl_arm is which one a click places: 0 none, 1 or 2 for
  // the marker of that number. A placed marker stays until switched off, so
  // arming the other one does not disturb it.
  VrmEbl m_vrmebl[kVrmEblCount];
  int m_ebl_arm = 0;
  std::function<void()> m_on_markers;
  // A marker is the plugin's own, so nothing on the wire changes when one
  // moves: the panel would otherwise never re-read it, because its updaters
  // only run when the radar sends a control update.
  void NotifyMarkers() {
    Refresh(false);
    if (m_on_markers) m_on_markers();
  }

  // Pointer position, for the cursor readout. Only while over the picture.
  wxPoint m_cursor = wxPoint(0, 0);
  bool m_cursor_in = false;

  bool m_reverse_zoom = false;  // from PpiPrefs

  // Live guard-zone edit, shared with the controls panel.
  std::function<ZoneEdit()> m_zone_get;
  std::function<void(const ZoneEdit&, bool)> m_zone_set;
  int m_zone_drag = -1;        // handle being dragged, or -1
  wxPoint m_zone_pts[4];       // last drawn handle positions, for hit testing
  bool m_zone_pts_valid = false;

  MayaraTheme m_theme;

  // Clickable overlay regions, updated each paint.
  wxRect m_menu_rect;    // icon-bar: Menu (hamburger)
  wxRect m_icon_ais;     // icon-bar: AIS on/off
  wxRect m_icon_gain;    // icon-bar: Gain gauge
  wxRect m_icon_sea;     // icon-bar: Sea gauge
  wxRect m_icon_rain;    // icon-bar: Rain gauge
  wxRect m_icon_ebl;     // icon-bar: EBL/VRM
  wxRect m_power_rect;
  wxRect m_range_minus_rect;
  wxRect m_range_plus_rect;
  wxRect m_recenter_rect;  // "centre" chip, only while off-centre or zoomed

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_RADAR_DISPLAY_PANEL_H_
