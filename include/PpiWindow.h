/******************************************************************************
 * mayara_pi - the radar window: radar image + control panel side by side.
 *****************************************************************************/
#ifndef MAYARA_PPI_WINDOW_H_
#define MAYARA_PPI_WINDOW_H_

#include <wx/wx.h>

class MayaraClient;

class MayaraPpiWindow : public wxDialog {
 public:
  MayaraPpiWindow(wxWindow* parent, MayaraClient* client);

 private:
  void OnClose(wxCloseEvent& event);

  wxDECLARE_EVENT_TABLE();
};

#endif  // MAYARA_PPI_WINDOW_H_
