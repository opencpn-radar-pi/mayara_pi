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
#include <memory>
#include <vector>

#include <wx/wx.h>

#include "ocpn_plugin.h"

#include "NavState.h"

// Forward declarations keep implementation types out of this header.
class MayaraPpiWindow;
class MayaraClient;

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

  // --- Own-ship state ------------------------------------------------------
  void SetPositionFixEx(PlugIn_Position_Fix_Ex& pfix) override;
  void SetColorScheme(PI_ColorScheme cs) override;

  // --- Preferences ---------------------------------------------------------
  void ShowPreferencesDialog(wxWindow* parent) override;

 private:
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
  void ShowSettings(wxWindow* parent);
  void LoadConfig();
  void SaveConfig();

  wxWindow* m_parent_window = nullptr;
  wxBitmap m_panel_bitmap;   // shown in the plugin manager
  wxBitmap m_tool_bitmap;    // toolbar icon (must outlive InsertPlugInTool)
  int m_tool_id = -1;
  int m_mi_overlay = -1;  // canvas context-menu item ids
  int m_mi_ppi = -1;
  wxMenuItem* m_mi_overlay_item = nullptr;  // owned by OpenCPN after adding
  wxMenuItem* m_mi_ppi_item = nullptr;
  std::vector<MayaraPpiWindow*> m_windows;
  bool m_windows_visible = false;  // user's show/hide intent for the windows
  int m_windows_radar_count = -1;  // radar count the windows were built for
  std::unique_ptr<MayaraClient> m_client;
  PI_ColorScheme m_color_scheme = PI_GLOBAL_COLOR_SCHEME_DAY;
  float m_radar_intensity = 1.0f;
  bool m_overlay_enabled = true;

  // Presentation: how many PPI windows to spread the discovered radars across.
  // 8 radars with m_windows_count = 2 => 4 radars per window. Persisted.
  int m_windows_count = 1;

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

  // Latest own-ship fix, for the overlay/PPI to place the radar.
  double m_ownship_lat = 0.0;
  double m_ownship_lon = 0.0;
  double m_ownship_cog = 0.0;
  double m_heading_true = 0.0;
  NavState m_nav;  // shared with the PPI overlays
};

#endif  // MAYARA_PI_H_
