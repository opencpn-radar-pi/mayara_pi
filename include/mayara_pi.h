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
  void ShowSettings(wxWindow* parent);
  void LoadConfig();
  void SaveConfig();

  wxWindow* m_parent_window = nullptr;
  wxBitmap m_panel_bitmap;   // shown in the plugin manager
  wxBitmap m_tool_bitmap;    // toolbar icon (must outlive InsertPlugInTool)
  int m_tool_id = -1;
  MayaraPpiWindow* m_ppi_window = nullptr;
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
};

#endif  // MAYARA_PI_H_
