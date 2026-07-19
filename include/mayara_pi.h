/******************************************************************************
 * mayara_pi - OpenCPN plugin for mayara-server.
 *
 * The plugin shell: an opencpn_plugin_118 subclass. Phase 0 only proves the
 * load/toolbar/window/overlay seams; the radar networking and rendering land
 * in later phases.
 *****************************************************************************/
#ifndef MAYARA_PI_H_
#define MAYARA_PI_H_

#include <memory>

#include <wx/wx.h>

#include "ocpn_plugin.h"

// Forward declarations keep implementation types out of this header.
class MayaraPpiWindow;
class MayaraClient;

class mayara_pi : public opencpn_plugin_118 {
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

 private:
  void TogglePpiWindow();

  wxWindow* m_parent_window = nullptr;
  wxBitmap m_panel_bitmap;   // shown in the plugin manager
  wxBitmap m_tool_bitmap;    // toolbar icon (must outlive InsertPlugInTool)
  int m_tool_id = -1;
  MayaraPpiWindow* m_ppi_window = nullptr;
  std::unique_ptr<MayaraClient> m_client;

  // Latest own-ship fix, for the overlay/PPI to place the radar.
  double m_ownship_lat = 0.0;
  double m_ownship_lon = 0.0;
  double m_ownship_cog = 0.0;
  double m_heading_true = 0.0;
};

#endif  // MAYARA_PI_H_
