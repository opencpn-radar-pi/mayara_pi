/******************************************************************************
 * mayara_pi - PPI (Plan Position Indicator) window.
 *
 * Phase 1a: shows live client/connection status. Phase 1b replaces the paint
 * handler with the actual radar image.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <wx/wx.h>
#include <wx/timer.h>

class MayaraClient;

class MayaraPpiWindow : public wxDialog {
 public:
  MayaraPpiWindow(wxWindow* parent, MayaraClient* client);

 private:
  void OnPaint(wxPaintEvent& event);
  void OnClose(wxCloseEvent& event);
  void OnTimer(wxTimerEvent& event);

  MayaraClient* m_client;  // not owned
  wxTimer m_timer;

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
