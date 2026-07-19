/******************************************************************************
 * mayara_pi - PPI window (Phase 0 placeholder).
 *****************************************************************************/
#include "PpiWindow.h"

#include <algorithm>

#include <wx/dcbuffer.h>

wxBEGIN_EVENT_TABLE(MayaraPpiWindow, wxDialog)
    EVT_PAINT(MayaraPpiWindow::OnPaint)
    EVT_CLOSE(MayaraPpiWindow::OnClose)
wxEND_EVENT_TABLE()

MayaraPpiWindow::MayaraPpiWindow(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _("Mayara Radar"), wxDefaultPosition,
               wxSize(480, 480),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(240, 240));
}

void MayaraPpiWindow::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(*wxBLACK_BRUSH);
  dc.Clear();

  const int cx = sz.x / 2;
  const int cy = sz.y / 2;
  const int r = (std::min(sz.x, sz.y) / 2) - 8;

  // Range rings + heading marker, so the empty PPI still reads as a radar.
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.SetPen(wxPen(wxColour(0, 120, 0), 1));
  for (int i = 1; i <= 4 && r > 0; ++i) {
    dc.DrawCircle(cx, cy, (r * i) / 4);
  }
  dc.SetPen(wxPen(wxColour(0, 200, 0), 1));
  dc.DrawLine(cx, cy, cx, cy - r);  // bow / heading line

  dc.SetTextForeground(wxColour(0, 200, 0));
  dc.DrawText(_("Mayara PPI — no radar connected"), 10, 8);
}

void MayaraPpiWindow::OnClose(wxCloseEvent& event) {
  // The plugin owns this window's lifetime; hide instead of destroying.
  Hide();
  event.Veto();
}
