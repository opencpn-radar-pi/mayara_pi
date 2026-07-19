/******************************************************************************
 * mayara_pi - PPI (Plan Position Indicator) window.
 *
 * Phase 0: a placeholder floating window. Phase 1 replaces the paint handler
 * with the OpenGL spoke renderer ported/simplified from radar_pi.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <wx/wx.h>

class MayaraPpiWindow : public wxDialog {
 public:
  explicit MayaraPpiWindow(wxWindow* parent);

 private:
  void OnPaint(wxPaintEvent& event);
  void OnClose(wxCloseEvent& event);

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
