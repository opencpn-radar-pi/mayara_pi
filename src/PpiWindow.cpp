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

  m_radar = new RadarDisplayPanel(this, client);
  m_controls = new ControlsPanel(this, client);
  m_controls->Hide();  // opened via the hamburger button

  auto* sizer = new wxBoxSizer(wxHORIZONTAL);
  sizer->Add(m_radar, 1, wxEXPAND);
  sizer->Add(m_controls, 0, wxEXPAND);
  SetSizer(sizer);

  RadarDisplayPanel* radar = m_radar;
  ControlsPanel* controls = m_controls;
  radar->SetMenuCallback([this, controls]() {
    controls->Show(!controls->IsShown());
    Layout();
  });
  controls->SetCloseCallback([this, controls]() {
    controls->Hide();
    Layout();
  });
}

void MayaraPpiWindow::ApplyTheme(const MayaraTheme& theme) {
  SetBackgroundColour(theme.panel_bg);
  if (m_radar) m_radar->ApplyTheme(theme);
  if (m_controls) m_controls->ApplyTheme(theme);
  Refresh();
}

void MayaraPpiWindow::OnClose(wxCloseEvent& event) {
  // The plugin owns this window's lifetime; hide instead of destroying.
  Hide();
  event.Veto();
}
