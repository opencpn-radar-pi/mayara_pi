/******************************************************************************
 * mayara_pi - radar window (composes the radar image + control panel).
 *****************************************************************************/
#include "PpiWindow.h"

#include "ControlsPanel.h"
#include "RadarDisplayPanel.h"

wxBEGIN_EVENT_TABLE(MayaraPpiWindow, wxDialog)
    EVT_CLOSE(MayaraPpiWindow::OnClose)
wxEND_EVENT_TABLE()

MayaraPpiWindow::MayaraPpiWindow(wxWindow* parent, MayaraClient* client)
    : wxDialog(parent, wxID_ANY, _("Mayara Radar"), wxDefaultPosition,
               wxSize(880, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  SetMinSize(wxSize(480, 320));

  auto* radar = new RadarDisplayPanel(this, client);
  auto* controls = new ControlsPanel(this, client);

  auto* sizer = new wxBoxSizer(wxHORIZONTAL);
  sizer->Add(radar, 1, wxEXPAND);
  sizer->Add(controls, 0, wxEXPAND);
  SetSizer(sizer);
}

void MayaraPpiWindow::OnClose(wxCloseEvent& event) {
  // The plugin owns this window's lifetime; hide instead of destroying.
  Hide();
  event.Veto();
}
