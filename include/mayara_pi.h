/******************************************************************************
 * mayara_pi - OpenCPN plugin for mayara-server.
 *
 * The plugin shell: an opencpn_plugin_118 subclass. Phase 0 only proves the
 * load/toolbar/window/overlay seams; the radar networking and rendering land
 * in later phases.
 *****************************************************************************/
#ifndef MAYARA_PI_H_
#define MAYARA_PI_H_

#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include <wx/wx.h>

#include "ocpn_plugin.h"

#include "NavState.h"
#include "RadarPalette.h"
#include "RadarDisplayPanel.h"  // PpiPrefs, ZoneEdit

// Forward declarations keep implementation types out of this header.
class wxGraphicsContext;
class ControlsPanel;
class MayaraPpiWindow;
class MayaraClient;
class MayaraServer;
class wxAuiManager;

class mayara_pi : public opencpn_plugin_121 {
 public:
  explicit mayara_pi(void* ppimgr);
  ~mayara_pi() override;

  // --- Required plugin API -------------------------------------------------
  int Init() override;
  bool DeInit() override;

  int GetAPIVersionMajor() override;
  int GetAPIVersionMinor() override;
  int GetPlugInVersionMajor() override;
  int GetPlugInVersionMinor() override;
  int GetPlugInVersionPatch() override;
  const char* GetPlugInVersionBuild() override;
  wxBitmap* GetPlugInBitmap() override;
  wxString GetCommonName() override;
  wxString GetShortDescription() override;
  wxString GetLongDescription() override;

  // --- Toolbar -------------------------------------------------------------
  int GetToolbarToolCount() override;
  void OnToolbarToolCallback(int id) override;

  // --- Canvas context menu -------------------------------------------------
  void OnContextMenuItemCallback(int id) override;
  void PrepareContextMenu(int canvasIndex) override;

  // --- Chart overlay (filled in Phase 1) -----------------------------------
  bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                  int canvasIndex, int priority) override;
  // The same overlay without OpenGL, for when hardware acceleration is off.
  bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex,
                                int priority) override;

  // --- Own-ship state ------------------------------------------------------
  void SetPositionFixEx(PlugIn_Position_Fix_Ex& pfix) override;
  void SetColorScheme(PI_ColorScheme cs) override;

  // --- Preferences ---------------------------------------------------------
  void ShowPreferencesDialog(wxWindow* parent) override;

 private:
  // Overlay selection sentinels, and how many radars the context submenu has
  // room for. Declared first: members below use them.
  static const int kOverlayNone = -1;
  static const int kOverlayAll = -2;
  static const int kMaxMenuRadars = 4;

  void TogglePpiWindow();
  void RebuildWindows();            // (re)create windows from m_windows_count
  void DestroyWindows(bool sync);  // sync=true: delete now (teardown-safe)
  bool AnyWindowShown() const;
  // Distribute the discovered radars across m_windows_count windows.
  std::vector<std::vector<int>> RadarGroups() const;
  void ApplyThemeToWindows();
  // Tile the radar windows down the right edge of the OpenCPN display. When
  // reflow_ocpn is set, also resize the OpenCPN main window to fill the left,
  // so together they fill the screen.
  void AutoLayoutWindows(bool reflow_ocpn);
  // Follow OpenCPN in/out of full screen: floating radar windows on a secondary
  // display fill that display (splitting it if several share it).
  void SyncRadarFullScreen(bool on);
  void ShowSettings(wxWindow* parent);
  void ShowSearchDialog();  // "looking for a server" + manual entry
  // "found the server, it just isn't seeing a radar", with a link to
  // mayara-server's GUI when there is one to link to (help_url may be empty,
  // in which case the notice is only the bad news and a Dismiss button).
  void ShowNoRadarNotice(const std::string& server_url,
                         const std::string& help_url);
  // "the server won't let us control the radar" + the access-request flow.
  void ShowAccessDialog();
  void UpdateAccessDialog();  // live status while approval is pending
  // Persist a token or pending request the client has just obtained.
  void SyncAccessConfig();
  // Point the client at the mayara-server we run ourselves, or clear it.
  void SyncLocalServerUrl();
  // Clears the remembered fast-reconnect address when it is the local
  // server's and "Run it here" is now off. See mayara_pi.cpp for why.
  void ForgetLocalServerIfDisabled();
  // OpenCPN's own Signal K connection, as a discovery hint. Empty if none.
  std::string OpenCpnSignalKUrl() const;
  // Raise a guard-zone alarm with OpenCPN when the server reports a new one.
  void PollGuardAlarms();
  // One short chime for a newly-raised guard-zone alarm; see PollGuardAlarms.
  void PlayGuardAlarmSound();
  // Ask, once per release, whether to install a newer local mayara-server.
  void MaybeOfferServerUpdate();
  void LoadConfig();
  void SaveConfig();
  void SaveWindowState();          // visibility + geometry of the PPI windows
  bool RestoreWindowGeometry();    // apply saved geometry; false if none match
  void CaptureWindowState();       // snapshot geometry while windows are alive
  wxString SavedPaneInfo(int index) const;  // saved AUI pane layout, or empty
  int OrientationFor(const std::string& radar_id) const;   // per-radar mode
  void SetOrientationFor(const std::string& radar_id, int mode);
  int ThresholdFor(const std::string& radar_id) const;     // per-radar echo cut
  void SetThresholdFor(const std::string& radar_id, int level);
  // Range Auto: per-radar, so the detail radar overlaid on the chart can
  // follow its scale while another radar kept at a fixed long range (weather
  // watching, say) is left alone.
  bool RangeAutoFor(const std::string& radar_id) const;
  void SetRangeAutoFor(const std::string& radar_id, bool on);
  // Whether this radar is currently shown by any canvas's overlay -- Range
  // Auto only means something for a radar actually on the chart.
  bool RadarInOverlay(int radar_index) const;

  wxWindow* m_parent_window = nullptr;
  wxBitmap m_panel_bitmap;   // shown in the plugin manager
  wxBitmap m_tool_bitmap;    // toolbar icon (must outlive InsertPlugInTool)
  int m_tool_id = -1;
  int m_mi_overlay = -1;  // canvas context-menu item ids
  int m_mi_ppi = -1;
  int m_mi_menu = -1;          // "Radar menu" context item
  int m_mi_ov_none = -1;       // overlay submenu item ids
  int m_mi_ov_all = -1;
  int m_mi_ov_radar[kMaxMenuRadars] = {-1, -1, -1, -1};
  wxMenuItem* m_mi_menu_item = nullptr;
  wxMenuItem* m_mi_overlay_item = nullptr;  // owned by OpenCPN after adding
  wxMenuItem* m_mi_ppi_item = nullptr;
  std::vector<MayaraPpiWindow*> m_windows;
  std::unique_ptr<wxTimer> m_heartbeat;  // 1 Hz: restore + geometry snapshot
  bool m_windows_visible = false;  // user's show/hide intent for the windows
  int m_windows_radar_count = -1;  // radar count the windows were built for
  std::unique_ptr<MayaraClient> m_client;
  std::unique_ptr<MayaraServer> m_server;  // optional local mayara-server
  std::string m_update_declined;  // release tag the user said "later" to
  PI_ColorScheme m_color_scheme = PI_GLOBAL_COLOR_SCHEME_DAY;
  float m_radar_intensity = 1.0f;
  // What each canvas overlays: kOverlayNone, kOverlayAll, or a radar index.
  // With two radars and two canvases this is what lets A go left and B right.
  // Absent means kOverlayAll, so the default needs no entry. Persisted.
  std::map<int, int> m_overlay_sel;
  int m_menu_canvas = 0;  // canvas whose context menu is open
  static const int kChartMenuMargin = 10;
  ControlsPanel* m_chart_menu = nullptr;  // controls over the chart, not owned
  int m_chart_menu_canvas = -1;
  // Once the user drags or resizes the chart menu, FitChartMenu() stops
  // fighting them over that axis until it is next reopened.
  bool m_chart_menu_user_moved = false;
  bool m_chart_menu_user_sized = false;
  // Last position/size the user left the menu at, keyed by canvas. Persisted,
  // and applied the next time that canvas's menu opens -- in this session or
  // the next -- instead of the auto-fit corner/size.
  std::map<int, wxRect> m_chart_menu_rect;
  ZoneEdit m_chart_zone;  // the chart menu's own live guard-zone edit
  int OverlaySel(int canvas) const;
  bool OverlayOn(int canvas) const { return OverlaySel(canvas) != kOverlayNone; }
  bool OverlayOnAny() const;
  void SetOverlayAll(bool on);
  // Radars this canvas should draw, honouring its selection.
  std::vector<int> OverlayRadars(int canvas) const;
  // Keep the shorter-range radar at a quarter of the longer one's range.
  void SyncAutoRange();
  void SyncChartRange();

  // --- Diagnostics ---------------------------------------------------------
  // Everything here exists to answer "why is the picture in the wrong place",
  // which is nearly always heading or position, and to let a bench setup run
  // without a compass or a GPS.
  struct Diagnostics {
    enum HeadingSource { kAuto = 0, kOpenCpnOnly, kRadarOnly };
    int heading_source = kAuto;
    bool cog_as_heading = false;  // last resort when nothing reports heading
    int heading_timeout_s = 5;    // 0 = a heading never goes stale
    int log_level = 0;  // 0 off, 1 problems, 2 verbose
  };
  Diagnostics m_diag;
  int64_t m_hdt_ms = 0;  // when OpenCPN last gave us a usable heading

  // The heading to draw with, and where it came from. `radar` may be -1 for
  // "any radar". Returns false when nothing usable is available -- which is
  // not the same as 0 degrees, and the difference shows on the chart.
  bool ResolveHeading(int radar, double* deg, wxString* source) const;
  // Own-ship position to draw from.
  bool ResolvePosition(int radar, double* lat, double* lon,
                       wxString* source) const;
  void Log(int level, const wxString& msg) const;
  void LogSettings() const;  // the whole configuration, in one line
  wxString m_last_heading_source;
  int m_fix_flags = -1;  // position/COG/heading validity, to log transitions
  // Echo colour profiles: the four built-ins plus whatever the user has made
  // from them. The active one is applied to every radar.
  std::vector<RadarPalette> m_palettes;
  std::string m_palette_active;
  void LoadPalettes(wxFileConfig* cfg);
  void SavePalettes(wxFileConfig* cfg);
  const RadarPalette& ActivePalette() const;
  void ApplyPalette();
  // NMEA out: the radar's heading and its tracked targets, for OpenCPN itself.
  void FeedHeading();
  void FeedTargets();
  bool m_feed_heading = false;
  uint64_t m_feed_hdt_count = 0;
  uint64_t m_feed_ttm_ticks = 0;
  bool m_feed_hdt_silent = false;  // warned once that there is nothing to send
  bool m_feed_targets = false;
  std::map<std::string, int> m_ttm_number;  // target key -> TTM target number  // the chart's zoom drives the overlaid radar
  std::map<int, double> m_canvas_radius_m;  // per canvas, what the chart shows
  // Range Auto: per canvas, the range value it last asked for, so a canvas
  // only touches its radar's range again when its own desired value changes
  // -- not every heartbeat, and not fighting another canvas that shares the
  // same radar.
  std::map<int, int> m_chart_range_last_want;
  struct OverlayItem {
    int idx;
    uint32_t range;
  };
  std::vector<OverlayItem> OverlayItems(int canvasIndex);
  void RecordCanvasRadius(PlugIn_ViewPort* vp, int canvasIndex);
  bool DrawRadarOverlayDC(wxGraphicsContext* gc, int index, PlugIn_ViewPort* vp,
                          double inner_frac);
  void DrawZonesOverlayDC(wxGraphicsContext* gc, int index,
                          PlugIn_ViewPort* vp);
  // The rotated, scaled disc as a bitmap. Rebuilt only when the picture, the
  // size, the rotation or the occluded middle changes: this is the path with
  // no GPU to lean on.
  struct OverlayBmp {
    wxBitmap bmp;
    uint64_t gen = ~0ull;
    int size = 0;
    int rot10 = -1;
    double inner = -1.0;
  };
  std::vector<OverlayBmp> m_overlay_bmp;
  // Per canvas: with two of them, a single value alternates and logs on every
  // repaint, which is the opposite of what the throttle is for.
  std::map<int, wxString> m_last_dc_shape;
  uint64_t m_dc_frames = 0;
  // Open the controls with no picture, for someone who only wants the menu.
  // The controls drawn over the chart canvas, without a picture.
  void ShowRadarMenu(int canvas);
  void DestroyChartMenu();
  void FitChartMenu();  // size it to the canvas and to its own content
  // Which canvas the pointer is over, or -1. PrepareContextMenu is told this
  // by OpenCPN, but only on versions that dispatch it for our API level.
  int CanvasUnderMouse() const;
  // Push the current radar names and per-canvas selection into the context
  // menu items. OpenCPN copies their labels and checks when it pops the menu.
  void RefreshContextMenu(int canvas);
  wxString OverlayLabel(int canvas) const;
  void ShowOverlayMenu(int canvas);  // our own popup, not a host submenu
  void RaisePpiWindows();
  PpiPrefs m_prefs;  // global display prefs, shared by every radar window

  // Presentation: how many PPI windows to spread the discovered radars across.
  // 8 radars with m_windows_count = 2 => 4 radars per window. Persisted.
  int m_windows_count = 1;
  // Per-radar orientation (0 head-up, 1 north-up, 2 course-up), keyed by radar
  // id. Persisted.
  std::map<std::string, int> m_orient;
  // Per-radar display echo threshold (0 all, 1 hide weak, 2 only strong),
  // keyed by radar id. Persisted.
  std::map<std::string, int> m_threshold;
  // Range Auto, keyed by radar id. Persisted.
  std::map<std::string, bool> m_range_auto;
  std::vector<wxRect> m_geom_cache;  // last live snapshot of window geometry
  std::vector<wxString> m_persp_cache;  // last live snapshot of AUI pane info
  bool m_docked = false;             // radar windows docked into OpenCPN (AUI)
  // Floating windows only (meaningless docked): wxFRAME_FLOAT_ON_PARENT, not
  // wxSTAY_ON_TOP -- the latter is worse, and even this is technically a
  // global NSWindowLevel on macOS too, not truly parent-scoped, but live
  // testing found no trouble against a plain wxFrame. See LoadConfig() for
  // the real default; this initialiser only covers the sliver of time
  // before it runs.
  bool m_ppi_stay_on_top = true;
  int m_menu_font_pt = 0;  // control panel text size override; 0 = default
  wxAuiManager* m_aui = nullptr;     // OpenCPN main-frame AUI manager
  bool m_ocpn_fullscreen = false;    // last-seen OpenCPN full-screen state
  std::string m_saved_server_url;    // last-known-good server, persisted
  std::string m_explicit_server_url; // user-set server (Settings); wins, persisted
  wxDialog* m_search_dialog = nullptr;  // "looking for a server" dialog
  // Signal K device access, persisted so an approval survives a restart.
  std::string m_client_id;       // our device identity towards Signal K
  std::string m_sk_token;        // token issued after approval
  std::string m_sk_token_server;  // the server that issued it
  std::string m_sk_pending_href;   // request awaiting approval
  std::string m_sk_pending_server;
  wxDialog* m_access_dialog = nullptr;
  wxStaticText* m_access_status = nullptr;
  wxButton* m_access_button = nullptr;
  bool m_access_dismissed = false;
  wxString m_access_last_line;  // avoids re-laying out the dialog every tick
  // Guard-zone alarms already raised with OpenCPN, keyed "radarid/zone",
  // value the message last shown -- a standing alarm is reported once per
  // distinct message (the server sends a fresh one per target), not every
  // heartbeat.
  std::map<std::string, std::string> m_alarms_raised;
  // True once m_alarms_raised has been seeded from the first connected
  // snapshot, so an alarm already standing when OpenCPN starts (or
  // reconnects) is not announced as freshly raised.
  bool m_alarms_seeded = false;
  // Enabled by default so the audible chime complements RaiseNotification()
  // rather than replacing it -- OpenCPN's own notification icon is easy to
  // miss unattended. The operator can disable just the chime in Display
  // settings, or with the on-picture bell lozenge.
  bool m_guard_alarm_sound = true;
  int m_no_radar_ticks = 0;          // heartbeat ticks with no radar
  bool m_search_dismissed = false;   // user closed the search dialog
  bool m_no_radar_notice_open = false;       // the "no radar found" dialog is up
  bool m_no_radar_notice_dismissed = false;  // user closed it; don't re-nag

  // GL chart-overlay: each radar's cached disc uploaded as a texture and drawn
  // as a rotated/scaled quad, re-uploaded only when its disc changes. Radars
  // are drawn longest-range first so the shortest range composites on top.
  struct OverlayTex {
    unsigned int tex = 0;
    uint64_t gen = ~0ull;
  };
  std::vector<OverlayTex> m_overlay_tex;  // per radar index
  std::vector<uint8_t> m_overlay_disc;    // scratch reused per radar

  // Draw one radar's disc. inner_frac > 0 draws only the annulus from that
  // fraction of the radius outward, so a shorter-range radar occludes this one
  // within its radius.
  bool DrawRadarOverlay(int index, PlugIn_ViewPort* vp, double inner_frac);
  // Guard zones of one radar, drawn over the chart.
  void DrawZonesOverlay(int index, PlugIn_ViewPort* vp);

  // Latest own-ship fix, for the overlay/PPI to place the radar.
  double m_ownship_lat = 0.0;
  double m_ownship_lon = 0.0;
  double m_ownship_cog = 0.0;
  double m_heading_true = 0.0;
  NavState m_nav;  // shared with the PPI overlays
};

#endif  // MAYARA_PI_H_
