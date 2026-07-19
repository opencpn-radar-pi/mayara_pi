/******************************************************************************
 * mayara_pi - PPI window (Phase 1a: status readout).
 *****************************************************************************/
#include "PpiWindow.h"

#include <algorithm>

#include <wx/dcbuffer.h>

#include "MayaraClient.h"

enum { kTimerId = wxID_HIGHEST + 1 };

wxBEGIN_EVENT_TABLE(MayaraPpiWindow, wxDialog)
    EVT_PAINT(MayaraPpiWindow::OnPaint)
    EVT_CLOSE(MayaraPpiWindow::OnClose)
    EVT_TIMER(kTimerId, MayaraPpiWindow::OnTimer)
wxEND_EVENT_TABLE()

MayaraPpiWindow::MayaraPpiWindow(wxWindow* parent, MayaraClient* client)
    : wxDialog(parent, wxID_ANY, _("Mayara Radar"), wxDefaultPosition,
               wxSize(480, 480), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_client(client),
      m_timer(this, kTimerId) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(240, 240));
  m_timer.Start(500);
}

void MayaraPpiWindow::OnTimer(wxTimerEvent&) { Refresh(false); }

void MayaraPpiWindow::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(*wxBLACK_BRUSH);
  dc.Clear();

  const int cx = sz.x / 2;
  const int cy = sz.y / 2;
  const int r = (std::min(sz.x, sz.y) / 2) - 8;

  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.SetPen(wxPen(wxColour(0, 120, 0), 1));
  for (int i = 1; i <= 4 && r > 0; ++i) dc.DrawCircle(cx, cy, (r * i) / 4);
  dc.SetPen(wxPen(wxColour(0, 200, 0), 1));
  dc.DrawLine(cx, cy, cx, cy - r);

  dc.SetTextForeground(wxColour(0, 220, 0));
  const wxString status =
      m_client ? wxString::FromUTF8(m_client->StatusLine().c_str())
               : wxString("no client");
  dc.DrawText(wxString(_("Mayara: ")) + status, 10, 8);
}

void MayaraPpiWindow::OnClose(wxCloseEvent& event) {
  // The plugin owns this window's lifetime; hide instead of destroying.
  m_timer.Stop();
  Hide();
  event.Veto();
}
