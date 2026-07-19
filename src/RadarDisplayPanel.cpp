/******************************************************************************
 * mayara_pi - radar-image panel.
 *****************************************************************************/
#include "RadarDisplayPanel.h"

#include <algorithm>
#include <cmath>
#include <string>
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

void LozengeBg(wxDC& dc, const wxRect& r, int radius) {
  dc.SetBrush(wxBrush(wxColour(24, 24, 28)));
  dc.SetPen(wxPen(wxColour(90, 90, 96)));
  dc.DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);
}

// Sorted settable ranges for the "range" control.
std::vector<int> RangeValues(MayaraClient* client) {
  std::vector<int> vals;
  for (const auto& d : client->Controls()->Schema())
    if (d.id == "range") {
      vals = d.validValues;
      break;
    }
  std::sort(vals.begin(), vals.end());
  return vals;
}

}  // namespace

wxBEGIN_EVENT_TABLE(RadarDisplayPanel, wxPanel)
    EVT_PAINT(RadarDisplayPanel::OnPaint)
    EVT_TIMER(kRadarTimerId, RadarDisplayPanel::OnTimer)
    EVT_SIZE(RadarDisplayPanel::OnSize)
    EVT_LEFT_DOWN(RadarDisplayPanel::OnLeftDown)
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
  m_power_rect = wxRect();
  m_range_minus_rect = wxRect();
  m_range_plus_rect = wxRect();
  if (!m_client) return;
  RadarControls* controls = m_client->Controls();

  // --- Power lozenge (top-left) with a clickable power icon ---
  ControlValue pw = controls->Value("power");
  {
    const int p = pw.has_value ? static_cast<int>(pw.value) : 0;
    const bool tx = p >= 2;  // Transmit
    const wxColour fg = tx ? wxColour(120, 255, 120) : wxColour(235, 200, 110);
    const wxString label =
        pw.has_value ? PowerLabel(m_client, p) : wxString(_("Power"));
    wxCoord tw, th;
    dc.GetTextExtent(label, &tw, &th);
    const int padx = 8, gap = 8, icon = 16;
    const int h = std::max<int>(th, icon) + 12;
    const int w = padx + icon + gap + tw + padx;
    const int x = 10, y = 10;
    LozengeBg(dc, wxRect(x, y, w, h), h / 2);

    // Power glyph: ring + top stem.
    const int ix = x + padx + icon / 2, iy = y + h / 2, r = icon / 2 - 1;
    dc.SetPen(wxPen(fg, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(ix, iy, r);
    dc.SetBrush(wxBrush(wxColour(24, 24, 28)));  // erase the top gap
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(ix - 2, iy - r - 2, 4, 5);
    dc.SetPen(wxPen(fg, 2));
    dc.DrawLine(ix, iy - r - 2, ix, iy);

    dc.SetTextForeground(fg);
    dc.DrawText(label, x + padx + icon + gap, y + (h - th) / 2);
    m_power_rect = wxRect(x, y, w, h);
  }

  // --- Range lozenge (left edge, vertically centred) with - / + ---
  RadarState* state = m_client->State();
  ControlValue rv = controls->Value("range");
  const double cur =
      rv.has_value ? rv.value : (state ? state->RangeMeters() : 0.0);
  const wxString rlabel = RangeLabel(static_cast<uint32_t>(cur));
  if (!rlabel.IsEmpty()) {
    wxCoord tw, th;
    dc.GetTextExtent(rlabel, &tw, &th);
    const int btn = 30, textw = std::max<int>(tw + 8, 56);
    const int h = th + 22, w = btn + textw + btn;
    const int x = 10, y = (sz.y - h) / 2;
    LozengeBg(dc, wxRect(x, y, w, h), 10);

    const wxColour fg(235, 235, 240);
    const int cy = y + h / 2;
    // minus
    dc.SetPen(wxPen(fg, 2));
    int mcx = x + btn / 2;
    dc.DrawLine(mcx - 7, cy, mcx + 7, cy);
    // plus
    int pcx = x + w - btn / 2;
    dc.DrawLine(pcx - 7, cy, pcx + 7, cy);
    dc.DrawLine(pcx, cy - 7, pcx, cy + 7);
    // value
    dc.SetTextForeground(fg);
    dc.DrawText(rlabel, x + btn + (textw - tw) / 2, cy - th / 2);

    m_range_minus_rect = wxRect(x, y, btn, h);
    m_range_plus_rect = wxRect(x + w - btn, y, btn, h);
  }
}

void RadarDisplayPanel::OnLeftDown(wxMouseEvent& event) {
  const wxPoint p = event.GetPosition();
  if (m_power_rect.Contains(p))
    TogglePower();
  else if (m_range_minus_rect.Contains(p))
    StepRange(-1);
  else if (m_range_plus_rect.Contains(p))
    StepRange(+1);
  else
    event.Skip();
}

void RadarDisplayPanel::TogglePower() {
  if (!m_client) return;
  ControlValue pw = m_client->Controls()->Value("power");
  const int target = (pw.has_value && static_cast<int>(pw.value) >= 2) ? 1 : 2;
  m_client->SetControl("power", "{\"value\":" + std::to_string(target) + "}");
}

void RadarDisplayPanel::StepRange(int direction) {
  if (!m_client) return;
  std::vector<int> vals = RangeValues(m_client);
  if (vals.empty()) return;
  const double cur = m_client->Controls()->Value("range").value;
  int idx = 0;
  double best = 1e18;
  for (int i = 0; i < static_cast<int>(vals.size()); ++i) {
    const double d = std::fabs(vals[i] - cur);
    if (d < best) {
      best = d;
      idx = i;
    }
  }
  idx = std::max(0, std::min<int>(idx + direction, vals.size() - 1));
  m_client->SetControl("range", "{\"value\":" + std::to_string(vals[idx]) + "}");
}
