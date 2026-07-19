/******************************************************************************
 * mayara_pi - PPI window (Phase 1b: first-light radar image).
 *****************************************************************************/
#include "PpiWindow.h"

#include <algorithm>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/image.h>

#include "MayaraClient.h"
#include "RadarState.h"

enum { kTimerId = wxID_HIGHEST + 1 };

wxBEGIN_EVENT_TABLE(MayaraPpiWindow, wxDialog)
    EVT_PAINT(MayaraPpiWindow::OnPaint)
    EVT_CLOSE(MayaraPpiWindow::OnClose)
    EVT_TIMER(kTimerId, MayaraPpiWindow::OnTimer)
wxEND_EVENT_TABLE()

MayaraPpiWindow::MayaraPpiWindow(wxWindow* parent, MayaraClient* client)
    : wxDialog(parent, wxID_ANY, _("Mayara Radar"), wxDefaultPosition,
               wxSize(512, 540), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_client(client),
      m_timer(this, kTimerId) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(256, 288));
  m_timer.Start(200);  // ~5 Hz refresh
}

void MayaraPpiWindow::OnTimer(wxTimerEvent&) { Refresh(false); }

void MayaraPpiWindow::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(*wxBLACK_BRUSH);
  dc.Clear();

  RadarState* state = m_client ? m_client->State() : nullptr;
  const int side = std::max(16, std::min(sz.x, sz.y));

  bool painted = false;
  if (state) {
    std::vector<uint8_t> rgb(static_cast<size_t>(side) * side * 3);
    if (state->RenderPPI(rgb.data(), side, side)) {
      // static_data=true: wxImage references rgb; wxBitmap copies it out.
      wxImage img(side, side, rgb.data(), true);
      wxBitmap bmp(img);
      dc.DrawBitmap(bmp, (sz.x - side) / 2, (sz.y - side) / 2, false);
      painted = true;
    }
  }

  if (!painted) {
    // Placeholder scope while waiting for data.
    const int cx = sz.x / 2, cy = sz.y / 2;
    const int r = (std::min(sz.x, sz.y) / 2) - 8;
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(wxColour(0, 100, 0), 1));
    for (int i = 1; i <= 4 && r > 0; ++i) dc.DrawCircle(cx, cy, (r * i) / 4);
  }

  wxString status =
      m_client ? wxString::FromUTF8(m_client->StatusLine().c_str())
               : wxString("no client");
  if (state && state->RangeMeters() > 0)
    status += wxString::Format(wxT("  —  range %u m"),
                               state->RangeMeters());
  dc.SetTextForeground(wxColour(0, 230, 0));
  dc.DrawText(wxString(_("Mayara: ")) + status, 8, sz.y - 20);
}

void MayaraPpiWindow::OnClose(wxCloseEvent& event) {
  m_timer.Stop();
  Hide();
  event.Veto();
}
