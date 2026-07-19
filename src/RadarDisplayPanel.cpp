/******************************************************************************
 * mayara_pi - radar-image panel.
 *****************************************************************************/
#include "RadarDisplayPanel.h"

#include <algorithm>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/image.h>

#include "MayaraClient.h"
#include "RadarControls.h"
#include "RadarState.h"

enum { kRadarTimerId = wxID_HIGHEST + 10 };

namespace {

wxString PowerLabel(MayaraClient* client, int value) {
  for (const auto& d : client->Controls()->Schema())
    if (d.id == "power") {
      auto it = d.descriptions.find(value);
      if (it != d.descriptions.end())
        return wxString::FromUTF8(it->second.c_str());
    }
  return wxString::Format("%d", value);
}

wxString RangeLabel(uint32_t m) {
  if (m == 0) return wxEmptyString;
  if (m >= 1852) return wxString::Format("%.2f NM", m / 1852.0);
  return wxString::Format("%u m", m);
}

// A rounded "lozenge" badge with dark background and light text.
void DrawLozenge(wxDC& dc, int cx_or_x, int y, const wxString& text,
                 const wxColour& fg, bool centered_at_x) {
  if (text.IsEmpty()) return;
  wxCoord tw, th;
  dc.GetTextExtent(text, &tw, &th);
  const int padx = 10, pady = 4;
  const int w = tw + 2 * padx, h = th + 2 * pady;
  const int x = centered_at_x ? (cx_or_x - w / 2) : cx_or_x;
  dc.SetBrush(wxBrush(wxColour(24, 24, 28)));
  dc.SetPen(wxPen(wxColour(90, 90, 96)));
  dc.DrawRoundedRectangle(x, y, w, h, h / 2);
  dc.SetTextForeground(fg);
  dc.DrawText(text, x + padx, y + pady);
}

}  // namespace

wxBEGIN_EVENT_TABLE(RadarDisplayPanel, wxPanel)
    EVT_PAINT(RadarDisplayPanel::OnPaint)
    EVT_TIMER(kRadarTimerId, RadarDisplayPanel::OnTimer)
    EVT_SIZE(RadarDisplayPanel::OnSize)
wxEND_EVENT_TABLE()

RadarDisplayPanel::RadarDisplayPanel(wxWindow* parent, MayaraClient* client)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxFULL_REPAINT_ON_RESIZE),
      m_client(client),
      m_timer(this, kRadarTimerId) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(256, 256));

  m_menu_btn = new wxButton(this, wxID_ANY, wxT("☰"), wxDefaultPosition,
                            wxSize(34, 28), wxBU_EXACTFIT);
  m_menu_btn->SetToolTip(_("Controls"));
  m_menu_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (m_on_menu) m_on_menu();
  });

  m_timer.Start(200);  // ~5 Hz
}

void RadarDisplayPanel::OnTimer(wxTimerEvent&) { Refresh(false); }

void RadarDisplayPanel::OnSize(wxSizeEvent& event) {
  if (m_menu_btn) {
    const wxSize sz = GetClientSize();
    const wxSize bs = m_menu_btn->GetSize();
    m_menu_btn->Move(sz.x - bs.x - 8, 8);
  }
  event.Skip();
}

void RadarDisplayPanel::OnPaint(wxPaintEvent&) {
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
      wxImage img(side, side, rgb.data(), true);
      wxBitmap bmp(img);
      dc.DrawBitmap(bmp, (sz.x - side) / 2, (sz.y - side) / 2, false);
      painted = true;
    }
  }

  if (!painted) {
    const int cx = sz.x / 2, cy = sz.y / 2;
    const int r = (std::min(sz.x, sz.y) / 2) - 8;
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(wxColour(0, 100, 0), 1));
    for (int i = 1; i <= 4 && r > 0; ++i) dc.DrawCircle(cx, cy, (r * i) / 4);
  }

  DrawLozenges(dc, sz);

  wxString status =
      m_client ? wxString::FromUTF8(m_client->StatusLine().c_str())
               : wxString("no client");
  dc.SetTextForeground(wxColour(0, 230, 0));
  dc.DrawText(status, 8, sz.y - 20);
}

void RadarDisplayPanel::DrawLozenges(wxDC& dc, const wxSize& sz) {
  if (!m_client) return;
  RadarControls* controls = m_client->Controls();

  // Power lozenge, top-left.
  ControlValue pw = controls->Value("power");
  if (pw.has_value) {
    const int p = static_cast<int>(pw.value);
    const bool tx = p >= 2;  // Transmit
    DrawLozenge(dc, 10, 10, PowerLabel(m_client, p),
                tx ? wxColour(120, 255, 120) : wxColour(230, 220, 120),
                /*centered=*/false);
  }

  // Range lozenge, top-centre.
  RadarState* state = m_client->State();
  wxString range = state ? RangeLabel(state->RangeMeters()) : wxString();
  if (!range.IsEmpty())
    DrawLozenge(dc, sz.x / 2, 10, range, wxColour(230, 230, 235),
                /*centered=*/true);
}
