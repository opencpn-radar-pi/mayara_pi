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
#include "ocpn_plugin.h"

enum { kRadarTimerId = wxID_HIGHEST + 10 };

namespace {

wxString PowerLabel(RadarControls* controls, int value) {
  if (controls)
    for (const auto& d : controls->Schema())
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

void LozengeBg(wxDC& dc, const wxRect& r, int radius, const MayaraTheme& t) {
  dc.SetBrush(wxBrush(t.lozenge_bg));
  dc.SetPen(wxPen(t.lozenge_border));
  dc.DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);
}

// Sorted settable ranges for the "range" control.
std::vector<int> RangeValues(RadarControls* c) {
  std::vector<int> vals;
  if (!c) return vals;
  for (const auto& d : c->Schema())
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

RadarDisplayPanel::RadarDisplayPanel(wxWindow* parent, MayaraClient* client,
                                     int radar_index)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxFULL_REPAINT_ON_RESIZE),
      m_client(client),
      m_index(radar_index),
      m_timer(this, kRadarTimerId) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(256, 256));
  m_timer.Start(200);  // ~5 Hz
}

void RadarDisplayPanel::OnTimer(wxTimerEvent&) { Refresh(false); }

void RadarDisplayPanel::ApplyTheme(const MayaraTheme& theme) {
  m_theme = theme;
  if (m_client) m_client->SetAllIntensity(theme.radar_intensity);
  Refresh();
}

void RadarDisplayPanel::OnSize(wxSizeEvent& event) { event.Skip(); }

void RadarDisplayPanel::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(*wxBLACK_BRUSH);
  dc.Clear();

  RadarState* state = m_client ? m_client->StateAt(m_index) : nullptr;
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

  // Extra layers over the picture (rings, heading/COG lines, north, AIS).
  const uint32_t range_m = state ? state->RangeMeters() : 0;
  DrawLayers(dc, wxPoint(sz.x / 2, sz.y / 2), side / 2.0, range_m);

  DrawLozenges(dc, sz);

  wxString status =
      m_client ? wxString::FromUTF8(m_client->StatusLine().c_str())
               : wxString("no client");
  dc.SetTextForeground(m_theme.text);
  dc.DrawText(status, 8, sz.y - 20);
}

void RadarDisplayPanel::DrawLozenges(wxDC& dc, const wxSize& sz) {
  m_menu_rect = wxRect();
  m_power_rect = wxRect();
  m_range_minus_rect = wxRect();
  m_range_plus_rect = wxRect();

  // Hamburger button (top-right), painted so it themes.
  {
    const int bw = 32, bh = 26, x = sz.x - bw - 8, y = 8;
    LozengeBg(dc, wxRect(x, y, bw, bh), 5, m_theme);
    dc.SetPen(wxPen(m_theme.text, 2));
    const int lx0 = x + 8, lx1 = x + bw - 8, cy = y + bh / 2;
    dc.DrawLine(lx0, cy - 5, lx1, cy - 5);
    dc.DrawLine(lx0, cy, lx1, cy);
    dc.DrawLine(lx0, cy + 5, lx1, cy + 5);
    m_menu_rect = wxRect(x, y, bw, bh);
  }

  if (!m_client) return;
  RadarControls* controls = m_client->ControlsAt(m_index);
  if (!controls) return;  // no radar connected yet

  // --- Power lozenge (top-left) with a clickable power icon ---
  ControlValue pw = controls->Value("power");
  {
    const int p = pw.has_value ? static_cast<int>(pw.value) : 0;
    const bool tx = p >= 2;  // Transmit
    const wxColour fg = tx ? m_theme.accent : m_theme.accent_dim;
    const wxString label =
        pw.has_value ? PowerLabel(controls, p) : wxString(_("Power"));
    wxCoord tw, th;
    dc.GetTextExtent(label, &tw, &th);
    const int padx = 8, gap = 8, icon = 16;
    const int h = std::max<int>(th, icon) + 12;
    const int w = padx + icon + gap + tw + padx;
    const int x = 10, y = 10;
    LozengeBg(dc, wxRect(x, y, w, h), h / 2, m_theme);

    // Power glyph: ring + top stem.
    const int ix = x + padx + icon / 2, iy = y + h / 2, r = icon / 2 - 1;
    dc.SetPen(wxPen(fg, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(ix, iy, r);
    dc.SetBrush(wxBrush(m_theme.lozenge_bg));  // erase the top gap
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(ix - 2, iy - r - 2, 4, 5);
    dc.SetPen(wxPen(fg, 2));
    dc.DrawLine(ix, iy - r - 2, ix, iy);

    dc.SetTextForeground(fg);
    dc.DrawText(label, x + padx + icon + gap, y + (h - th) / 2);
    m_power_rect = wxRect(x, y, w, h);
  }

  // --- Range lozenge (left edge, vertically centred) with - / + ---
  RadarState* state = m_client->StateAt(m_index);
  ControlValue rv = controls->Value("range");
  const double cur =
      rv.has_value ? rv.value : (state ? state->RangeMeters() : 0.0);
  const wxString rlabel = RangeLabel(static_cast<uint32_t>(cur));
  if (!rlabel.IsEmpty()) {
    wxCoord tw, th;
    dc.GetTextExtent(rlabel, &tw, &th);
    const int w = std::max<int>(tw + 16, 56);
    const int plus_h = 26, minus_h = 26, val_h = th + 12;
    const int h = plus_h + val_h + minus_h;
    const int x = 10, y = (sz.y - h) / 2;
    LozengeBg(dc, wxRect(x, y, w, h), 12, m_theme);

    const wxColour fg = m_theme.text;
    const int cx = x + w / 2;
    dc.SetPen(wxPen(fg, 2));
    // plus (top)
    const int pcy = y + plus_h / 2;
    dc.DrawLine(cx - 7, pcy, cx + 7, pcy);
    dc.DrawLine(cx, pcy - 7, cx, pcy + 7);
    // minus (bottom)
    const int mcy = y + plus_h + val_h + minus_h / 2;
    dc.DrawLine(cx - 7, mcy, cx + 7, mcy);
    // value (middle)
    dc.SetTextForeground(fg);
    dc.DrawText(rlabel, x + (w - tw) / 2, y + plus_h + (val_h - th) / 2);

    m_range_plus_rect = wxRect(x, y, w, plus_h);
    m_range_minus_rect = wxRect(x, y + plus_h + val_h, w, minus_h);
  }
}

namespace {
// Head-up screen point for a target at true bearing `brg_deg` and pixel radius
// `r`, given own-ship true `heading`. Screen 0deg = straight up (bow).
wxPoint PolarPoint(wxPoint c, double r, double brg_deg, double heading) {
  const double a = (brg_deg - heading) * M_PI / 180.0;
  return wxPoint(c.x + static_cast<int>(std::lround(r * std::sin(a))),
                 c.y - static_cast<int>(std::lround(r * std::cos(a))));
}

wxString RingLabel(double m) {
  if (m >= 1852.0) return wxString::Format("%.2f NM", m / 1852.0);
  return wxString::Format("%.0f m", m);
}
}  // namespace

void RadarDisplayPanel::DrawLayers(wxDC& dc, wxPoint c, double radius,
                                   uint32_t range_m) {
  if (radius < 8) return;
  RadarState* st = m_client ? m_client->StateAt(m_index) : nullptr;

  NavState nav;
  if (m_nav) nav = m_nav();
  double heading = 0.0;
  bool has_heading = false;
  if (nav.has_hdt) {
    heading = nav.hdt;
    has_heading = true;
  } else if (st) {
    double h = 0.0;
    if (st->Heading(h)) {
      heading = h;
      has_heading = true;
    }
  }

  dc.SetBrush(*wxTRANSPARENT_BRUSH);

  // Range rings, with a distance label on the outer three.
  if (m_layers.range_rings) {
    dc.SetPen(wxPen(m_theme.dim_text, 1));
    dc.SetTextForeground(m_theme.dim_text);
    const int rings = 4;
    for (int i = 1; i <= rings; ++i) {
      const double rr = radius * i / rings;
      dc.DrawCircle(c.x, c.y, static_cast<int>(rr));
      if (range_m > 0 && i < rings) {
        const wxString lbl = RingLabel(static_cast<double>(range_m) * i / rings);
        dc.DrawText(lbl, c.x + 3, c.y - static_cast<int>(rr) - 2);
      }
    }
  }

  // Heading line: bow is straight up on the head-up picture.
  if (m_layers.heading_line) {
    dc.SetPen(wxPen(m_theme.accent, 2));
    dc.DrawLine(c.x, c.y, c.x, c.y - static_cast<int>(radius));
  }

  // North marker: a small "N" at the true-north bearing.
  if (m_layers.north_marker && has_heading) {
    const wxPoint n = PolarPoint(c, radius - 12, 0.0, heading);
    dc.SetTextForeground(m_theme.text);
    wxCoord tw, th;
    dc.GetTextExtent("N", &tw, &th);
    dc.DrawText("N", n.x - tw / 2, n.y - th / 2);
  }

  // COG line: dashed, from the centre out to the ring edge.
  if (m_layers.cog_line && has_heading && nav.has_cog) {
    wxPen pen(wxColour(0, 200, 255), 2, wxPENSTYLE_LONG_DASH);
    dc.SetPen(pen);
    const wxPoint e = PolarPoint(c, radius, nav.cog, heading);
    dc.DrawLine(c.x, c.y, e.x, e.y);
  }

  // AIS targets: a bearing/range-placed triangle pointing along its COG.
  if (m_layers.ais && has_heading && range_m > 0) {
    ArrayOfPlugIn_AIS_Targets* arr = GetAISTargetArray();
    if (arr) {
      for (size_t i = 0; i < arr->GetCount(); ++i) {
        PlugIn_AIS_Target* t = arr->Item(i);
        if (!t) continue;
        const double rng_m = t->Range_NM * 1852.0;
        if (rng_m <= 0 || rng_m > range_m) continue;  // outside the ring
        const double r = radius * rng_m / range_m;
        const wxPoint p = PolarPoint(c, r, t->Brg, heading);
        // Small triangle oriented to COG (fallback: bearing).
        const double course =
            (t->COG >= 0 && t->COG < 360) ? t->COG : t->Brg;
        const double a = (course - heading) * M_PI / 180.0;
        const double ca = std::cos(a), sa = std::sin(a);
        auto rot = [&](double dx, double dy) {
          return wxPoint(p.x + static_cast<int>(std::lround(dx * ca - dy * sa)),
                         p.y + static_cast<int>(std::lround(dx * sa + dy * ca)));
        };
        // Triangle: nose forward (screen up = -y before rotation).
        wxPoint tri[3] = {rot(0, -7), rot(-4, 5), rot(4, 5)};
        const bool danger = t->bCPA_Valid && t->CPA < 0.5 && t->TCPA > 0;
        dc.SetBrush(wxBrush(danger ? wxColour(255, 80, 80)
                                   : wxColour(0, 220, 120)));
        dc.SetPen(wxPen(m_theme.text, 1));
        dc.DrawPolygon(3, tri);
        delete t;  // core allocates fresh copies per call
      }
      delete arr;
    }
  }
}

void RadarDisplayPanel::OnLeftDown(wxMouseEvent& event) {
  const wxPoint p = event.GetPosition();
  if (m_menu_rect.Contains(p)) {
    if (m_on_menu) m_on_menu();
  } else if (m_power_rect.Contains(p))
    TogglePower();
  else if (m_range_minus_rect.Contains(p))
    StepRange(-1);
  else if (m_range_plus_rect.Contains(p))
    StepRange(+1);
  else {
    if (m_on_focus) m_on_focus();
    event.Skip();
  }
}

void RadarDisplayPanel::TogglePower() {
  RadarControls* c = m_client ? m_client->ControlsAt(m_index) : nullptr;
  if (!c) return;
  ControlValue pw = c->Value("power");
  const int target = (pw.has_value && static_cast<int>(pw.value) >= 2) ? 1 : 2;
  m_client->SetControlAt(m_index, "power",
                         "{\"value\":" + std::to_string(target) + "}");
}

void RadarDisplayPanel::StepRange(int direction) {
  RadarControls* c = m_client ? m_client->ControlsAt(m_index) : nullptr;
  if (!c) return;
  std::vector<int> vals = RangeValues(c);
  if (vals.empty()) return;
  const double cur = c->Value("range").value;
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
  m_client->SetControlAt(m_index, "range",
                         "{\"value\":" + std::to_string(vals[idx]) + "}");
}
