/******************************************************************************
 * mayara_pi - radar-image panel.
 *****************************************************************************/
#include "RadarDisplayPanel.h"

#include <algorithm>
#include <cmath>
#include <numeric>
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

// Defined further down; also used by the range/ring labels.
wxString FormatRange(double m, bool metric);

void LozengeBg(wxDC& dc, const wxRect& r, int radius, const MayaraTheme& t) {
  dc.SetBrush(wxBrush(t.lozenge_bg));
  dc.SetPen(wxPen(t.lozenge_border));
  dc.DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);
}

// Fraction (0..1) of a 0..max control, or -1 if unknown. Sets is_auto.
double GaugeFrac(RadarControls* c, const std::string& id, bool& is_auto) {
  is_auto = false;
  if (!c) return -1.0;
  ControlValue v = c->Value(id);
  if (!v.has_value) return -1.0;
  is_auto = v.auto_;
  double mn = 0, mx = 0;
  bool has_max = false;
  for (const auto& d : c->Schema())
    if (d.id == id) {
      mn = d.has_min ? d.minValue : 0.0;
      if (d.has_max) {
        mx = d.maxValue;
        has_max = true;
      }
      break;
    }
  if (!has_max || mx <= mn) return -1.0;
  const double f = (v.value - mn) / (mx - mn);
  return f < 0 ? 0 : (f > 1 ? 1 : f);
}

// A small hand-drawn semicircle gauge with a value arc and a letter beneath.
void DrawGauge(wxDC& dc, wxPoint c, const wxString& letter, double frac,
               bool is_auto, const wxColour& ink, const wxColour& accent) {
  const int R = 11, SEG = 16, yoff = 2;
  auto pt = [&](double a) {
    return wxPoint(c.x + static_cast<int>(std::lround(R * std::cos(a))),
                   c.y - static_cast<int>(std::lround(R * std::sin(a))) + yoff);
  };
  dc.SetPen(wxPen(wxColour(90, 90, 96), 2));  // background arc 180 -> 0
  wxPoint prev = pt(M_PI);
  for (int i = 1; i <= SEG; ++i) {
    wxPoint q = pt(M_PI - M_PI * i / SEG);
    dc.DrawLine(prev, q);
    prev = q;
  }
  if (frac >= 0) {
    dc.SetPen(wxPen(is_auto ? wxColour(0, 200, 255) : accent, 2));
    const int segN = static_cast<int>(std::lround(SEG * frac));
    prev = pt(M_PI);
    for (int i = 1; i <= segN; ++i) {
      wxPoint q = pt(M_PI - M_PI * i / SEG);
      dc.DrawLine(prev, q);
      prev = q;
    }
  }
  wxFont f = dc.GetFont();
  f.SetPointSize(8);
  dc.SetFont(f);
  dc.SetTextForeground(ink);
  wxCoord tw, th;
  dc.GetTextExtent(letter, &tw, &th);
  dc.DrawText(letter, c.x - tw / 2, c.y + yoff + 3);
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
  // Centre the picture in the space not covered by the open menu.
  const int avail_w = std::max(16, sz.x - m_obscured_right);
  const int side = std::max(16, std::min(avail_w, sz.y));
  const wxPoint pctr(avail_w / 2, sz.y / 2);

  // The reported (nominal, round) range fills the window; the larger spoke
  // range spills past the edge as overzoom (zoom = spoke / report).
  const uint32_t spoke_m = state ? state->RangeMeters() : 0;
  double report_m = spoke_m;
  bool metric = false;
  EffectiveRange(report_m, metric);
  const double zoom =
      (report_m > 0 && spoke_m > 0) ? spoke_m / report_m : 1.0;

  double up_bearing = 0, raster_rot = 0, heading = 0;
  bool has_heading = false;
  ResolveOrientation(up_bearing, raster_rot, heading, has_heading);

  if (state) {
    std::vector<uint8_t> rgb(static_cast<size_t>(side) * side * 3);
    if (state->RenderPPI(rgb.data(), side, side, zoom, raster_rot)) {
      wxImage img(side, side, rgb.data(), true);
      wxBitmap bmp(img);
      dc.DrawBitmap(bmp, pctr.x - side / 2, pctr.y - side / 2, false);
    }
  }

  // Extra layers over the picture. Radius maps to the reported range; layers
  // are placed relative to whatever bearing is shown at screen-up.
  DrawLayers(dc, pctr, side / 2.0, report_m, metric);

  DrawLozenges(dc, sz);

  // Connection status only until a radar is up (no "streaming N radar(s)").
  if (!m_client || !m_client->ControlsAt(m_index)) {
    const wxString status =
        m_client ? wxString::FromUTF8(m_client->StatusLine().c_str())
                 : wxString("no client");
    dc.SetTextForeground(m_theme.text);
    dc.DrawText(status, 8, sz.y - 20);
  }
}

void RadarDisplayPanel::DrawIconBar(wxDC& dc, const wxSize& sz) {
  RadarControls* controls = m_client ? m_client->ControlsAt(m_index) : nullptr;
  const int cell = 40, bw = 40;
  const int bx = sz.x - bw - 6, by = 6, barH = 7 * cell;

  // Very dark grey rounded background.
  dc.SetBrush(wxBrush(wxColour(22, 22, 24)));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRoundedRectangle(bx, by, bw, barH, 8);

  const wxColour ink(220, 220, 225), dim(115, 115, 122);
  auto cellRect = [&](int i) { return wxRect(bx, by + i * cell, bw, cell); };
  auto ctr = [&](int i) {
    return wxPoint(bx + bw / 2, by + i * cell + cell / 2);
  };

  // 0: Menu (hamburger).
  {
    wxPoint c = ctr(0);
    dc.SetPen(wxPen(ink, 2));
    for (int k = -1; k <= 1; ++k)
      dc.DrawLine(c.x - 9, c.y + k * 5, c.x + 9, c.y + k * 5);
    m_menu_rect = cellRect(0);
  }
  // 1: AIS on/off (filled triangle when on).
  {
    wxPoint c = ctr(1);
    const wxColour col = m_layers.ais ? wxColour(0, 220, 120) : dim;
    dc.SetPen(wxPen(col, 2));
    dc.SetBrush(m_layers.ais ? wxBrush(col) : *wxTRANSPARENT_BRUSH);
    wxPoint tri[3] = {wxPoint(c.x, c.y - 9), wxPoint(c.x - 7, c.y + 8),
                      wxPoint(c.x + 7, c.y + 8)};
    dc.DrawPolygon(3, tri);
    m_icon_ais = cellRect(1);
  }
  // 2,3,4: Gain / Sea / Rain gauges.
  {
    bool a = false;
    DrawGauge(dc, ctr(2), "G", GaugeFrac(controls, "gain", a), a, ink,
              m_theme.accent);
    m_icon_gain = cellRect(2);
    DrawGauge(dc, ctr(3), "S", GaugeFrac(controls, "sea", a), a, ink,
              m_theme.accent);
    m_icon_sea = cellRect(3);
    DrawGauge(dc, ctr(4), "R", GaugeFrac(controls, "rain", a), a, ink,
              m_theme.accent);
    m_icon_rain = cellRect(4);
  }
  // 5: EBL/VRM (ring + radial). Placeholder toggle.
  {
    wxPoint c = ctr(5);
    const wxColour col = m_ebl_on ? m_theme.accent : dim;
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(col, 2));
    dc.DrawCircle(c.x, c.y, 9);
    dc.DrawLine(c.x, c.y, c.x + 8, c.y - 5);
    m_icon_ebl = cellRect(5);
  }
  // 6: View (mini hamburger over an eye).
  {
    wxPoint c = ctr(6);
    dc.SetPen(wxPen(ink, 2));
    for (int k = 0; k < 2; ++k)
      dc.DrawLine(c.x - 8, c.y - 11 + k * 4, c.x + 8, c.y - 11 + k * 4);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawEllipse(c.x - 9, c.y - 1, 18, 12);
    dc.SetBrush(wxBrush(ink));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawCircle(c.x, c.y + 5, 2);
    m_icon_view = cellRect(6);
  }
}

void RadarDisplayPanel::DrawLozenges(wxDC& dc, const wxSize& sz) {
  m_menu_rect = m_icon_ais = m_icon_gain = m_icon_sea = m_icon_rain =
      m_icon_ebl = m_icon_view = wxRect();
  m_power_rect = wxRect();
  m_range_minus_rect = wxRect();
  m_range_plus_rect = wxRect();

  DrawIconBar(dc, sz);

  if (!m_client) return;
  RadarControls* controls = m_client->ControlsAt(m_index);
  if (!controls) return;  // no radar connected yet

  // --- Power lozenge (top-left): radar name above the transmit state ---
  ControlValue pw = controls->Value("power");
  {
    const int p = pw.has_value ? static_cast<int>(pw.value) : 0;
    const bool tx = p >= 2;  // Transmit
    const wxColour fg = tx ? m_theme.accent : m_theme.accent_dim;
    const wxString label =
        pw.has_value ? PowerLabel(controls, p) : wxString(_("Power"));

    std::vector<std::string> names = m_client->RadarNames();
    wxString name;
    if (m_index >= 0 && m_index < static_cast<int>(names.size()))
      name = wxString::FromUTF8(names[m_index].c_str());

    // One size larger than the range-ring labels (which use the panel font).
    const wxFont base = GetFont();
    wxFont big = base;
    big.SetPointSize(base.GetPointSize() + 2);
    const wxFont& small = base;  // name line == ring-label size

    wxCoord tw, th, nw = 0, nh = 0;
    dc.SetFont(big);
    dc.GetTextExtent(label, &tw, &th);
    if (!name.IsEmpty()) {
      dc.SetFont(small);
      dc.GetTextExtent(name, &nw, &nh);
    }

    const int padx = 9, gap = 8, icon = th, vgap = 1;
    const int textW = std::max<int>(tw, nw);
    const int textH = name.IsEmpty() ? th : nh + vgap + th;
    const int h = std::max<int>(textH, icon) + 12;
    const int w = padx + icon + gap + textW + padx;
    const int x = 10, y = 10;
    LozengeBg(dc, wxRect(x, y, w, h), std::min(h / 2, 12), m_theme);

    // Power glyph: ring + top stem, vertically centred.
    const int ix = x + padx + icon / 2, iy = y + h / 2, r = icon / 2 - 1;
    dc.SetPen(wxPen(fg, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(ix, iy, r);
    dc.SetBrush(wxBrush(m_theme.lozenge_bg));  // erase the top gap
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(ix - 2, iy - r - 2, 4, 5);
    dc.SetPen(wxPen(fg, 2));
    dc.DrawLine(ix, iy - r - 2, ix, iy);

    const int textX = x + padx + icon + gap;
    const int textY = y + (h - textH) / 2;
    if (!name.IsEmpty()) {
      dc.SetFont(small);
      dc.SetTextForeground(m_theme.text);
      dc.DrawText(name, textX, textY);
      dc.SetFont(big);
      dc.SetTextForeground(fg);
      dc.DrawText(label, textX, textY + nh + vgap);
    } else {
      dc.SetFont(big);
      dc.SetTextForeground(fg);
      dc.DrawText(label, textX, textY);
    }
    dc.SetFont(base);
    m_power_rect = wxRect(x, y, w, h);
  }

  // --- Range lozenge (left edge, vertically centred) with - / + ---
  RadarState* state = m_client->StateAt(m_index);
  ControlValue rv = controls->Value("range");
  const double cur =
      rv.has_value ? rv.value : (state ? state->RangeMeters() : 0.0);
  double report_m = cur;
  bool metric = false;
  EffectiveRange(report_m, metric);  // for the unit; report_m is the range value
  const wxString rlabel =
      cur > 0 ? FormatRange(cur, metric) : wxString();  // same as ring labels
  if (!rlabel.IsEmpty()) {
    wxFont big = GetFont();
    big.SetPointSize(big.GetPointSize() + 2);  // one size up from ring labels
    dc.SetFont(big);
    wxCoord tw, th;
    dc.GetTextExtent(rlabel, &tw, &th);
    const int w = std::max<int>(tw + 18, 60);
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
    dc.SetFont(GetFont());

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

// Concise range label. Nautical: whole NM without decimals ("4 NM"), a single
// decimal otherwise ("1.5 NM"), and fractions below 1 NM ("1/8 NM"). Metric:
// metres, or km above 1000 m.
wxString FormatRange(double m, bool metric) {
  if (metric) {
    if (m >= 1000.0) {
      const double km = m / 1000.0;
      if (std::fabs(km - std::lround(km)) < 0.05)
        return wxString::Format("%ld km", std::lround(km));
      return wxString::Format("%.1f km", km);
    }
    return wxString::Format("%.0f m", m);
  }
  const double nm = m / 1852.0;
  if (nm >= 0.95) {
    if (std::fabs(nm - std::lround(nm)) < 0.03)
      return wxString::Format("%ld NM", std::lround(nm));
    return wxString::Format("%.1f NM", nm);
  }
  for (long d : {16L, 8L, 4L, 2L}) {
    const double num = nm * d;
    const long n = std::lround(num);
    if (n >= 1 && std::fabs(num - n) < 0.03) {
      const long g = std::gcd(n, d);
      const long nn = n / g, dd = d / g;
      if (dd == 1) return wxString::Format("%ld NM", nn);
      return wxString::Format("%ld/%ld NM", nn, dd);
    }
  }
  return wxString::Format("%.0f m", m);
}

// A step is "nice" if its leading digits read cleanly (1, 1.5, 2, 2.5, 3, ...).
bool NiceStep(double x) {
  if (x <= 0) return false;
  double m = x;
  while (m >= 10.0) m /= 10.0;
  while (m < 1.0) m *= 10.0;
  const double nice[] = {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0};
  for (double n : nice)
    if (std::fabs(m - n) < 0.02) return true;
  return false;
}

// Three rings when the reported range divides into nice thirds (1.5/3/6/12 NM,
// 750 m, ...), otherwise four. Tested in both NM and metres so either unit's
// round ranges get thirds.
int RingCount(double report_m) {
  if (NiceStep(report_m / 1852.0 / 3.0) || NiceStep(report_m / 3.0)) return 3;
  return 4;
}
}  // namespace

void RadarDisplayPanel::ResolveOrientation(double& up_bearing,
                                           double& raster_rot, double& heading,
                                           bool& has_heading) const {
  NavState nav;
  if (m_nav) nav = m_nav();
  heading = 0.0;
  has_heading = false;
  if (nav.has_hdt) {
    heading = nav.hdt;
    has_heading = true;
  } else if (RadarState* st = m_client ? m_client->StateAt(m_index) : nullptr) {
    double h = 0.0;
    if (st->Heading(h)) {
      heading = h;
      has_heading = true;
    }
  }
  up_bearing = has_heading ? heading : 0.0;  // head-up: bow at screen-up
  raster_rot = 0.0;
  if (m_orientation == kNorthUp && has_heading) {
    up_bearing = 0.0;  // north at screen-up
    raster_rot = heading;
  } else if (m_orientation == kCourseUp && has_heading && nav.has_cog) {
    up_bearing = nav.cog;  // course at screen-up
    raster_rot = heading - nav.cog;
  }
}

void RadarDisplayPanel::EffectiveRange(double& report_m, bool& metric) const {
  RadarControls* ctrl = m_client ? m_client->ControlsAt(m_index) : nullptr;
  if (!ctrl) return;
  ControlValue rv = ctrl->Value("range");
  if (rv.has_value && rv.value > 0) report_m = rv.value;
  ControlValue ru = ctrl->Value("rangeUnits");
  if (ru.has_value)
    for (const auto& d : ctrl->Schema())
      if (d.id == "rangeUnits") {
        auto it = d.descriptions.find(static_cast<int>(ru.value));
        if (it != d.descriptions.end())
          metric = wxString::FromUTF8(it->second.c_str()).Lower().Contains(
              "metric");
        break;
      }
}

void RadarDisplayPanel::DrawLayers(wxDC& dc, wxPoint c, double radius,
                                   double report_m, bool metric) {
  if (radius < 8) return;

  NavState nav;
  if (m_nav) nav = m_nav();
  // `up_bearing` is the true bearing shown at screen-up for the current
  // orientation; every true-referenced layer is placed relative to it.
  double up_bearing = 0, raster_rot = 0, heading = 0;
  bool has_heading = false;
  ResolveOrientation(up_bearing, raster_rot, heading, has_heading);

  dc.SetBrush(*wxTRANSPARENT_BRUSH);

  // The reported range fills the window (the picture is zoomed to it), so the
  // outer ring sits at the window edge and the picture beyond is overzoom.
  if (m_layers.range_rings && report_m > 0) {
    const int rings = RingCount(report_m);
    dc.SetPen(wxPen(m_theme.dim_text, 1));
    dc.SetTextForeground(m_theme.dim_text);
    const double k = 0.70710678;  // cos/sin 45 deg (top-right diagonal)
    for (int i = 1; i <= rings; ++i) {
      const double rr = radius * i / rings;
      dc.DrawCircle(c.x, c.y, static_cast<int>(rr));
      const wxString lbl = FormatRange(report_m * i / rings, metric);
      wxCoord tw, th;
      dc.GetTextExtent(lbl, &tw, &th);
      // Text starts just outside the ring on the upper-right diagonal.
      dc.DrawText(lbl, c.x + static_cast<int>((rr + 3) * k),
                  c.y - static_cast<int>((rr + 3) * k) - th / 2);
    }
  }

  // Compass ring: bearing ticks every 10 deg, major ticks + labels every 30.
  // Placed by true bearing so (head-up) the labels stay geographic.
  if (m_layers.compass) {
    const double ringR = radius - 16;
    if (ringR > 20) {
      const wxColour green(0, 210, 0);
      dc.SetPen(wxPen(green, 1));
      dc.SetTextForeground(green);
      wxFont f = dc.GetFont();
      f.SetPointSize(std::max(7, f.GetPointSize() - 1));
      dc.SetFont(f);
      for (int deg = 0; deg < 360; deg += 10) {
        const bool major = (deg % 30) == 0;
        const int tick = major ? 9 : 5;
        dc.DrawLine(PolarPoint(c, ringR - tick, deg, up_bearing),
                    PolarPoint(c, ringR, deg, up_bearing));
        if (major) {
          const wxString lbl = wxString::Format("%d", deg);
          wxCoord tw, th;
          dc.GetTextExtent(lbl, &tw, &th);
          const wxPoint lp = PolarPoint(c, ringR - tick - 9, deg, up_bearing);
          dc.DrawText(lbl, lp.x - tw / 2, lp.y - th / 2);
        }
      }
    }
  }

  // Heading line: from the centre towards the bow (true heading). On head-up
  // that is straight up; on north/course-up it points where the bow is.
  if (m_layers.heading_line) {
    dc.SetPen(wxPen(m_theme.accent, 1));
    const wxPoint e = has_heading ? PolarPoint(c, radius, heading, up_bearing)
                                  : wxPoint(c.x, c.y - static_cast<int>(radius));
    dc.DrawLine(c.x, c.y, e.x, e.y);
  }

  // North marker: a small "N" at the true-north bearing.
  if (m_layers.north_marker && has_heading) {
    const wxPoint n = PolarPoint(c, radius - 12, 0.0, up_bearing);
    dc.SetTextForeground(m_theme.text);
    wxCoord tw, th;
    dc.GetTextExtent("N", &tw, &th);
    dc.DrawText("N", n.x - tw / 2, n.y - th / 2);
  }

  // COG line: dashed, from the centre out to the ring edge.
  if (m_layers.cog_line && has_heading && nav.has_cog) {
    wxPen pen(wxColour(0, 200, 255), 2, wxPENSTYLE_LONG_DASH);
    dc.SetPen(pen);
    const wxPoint e = PolarPoint(c, radius, nav.cog, up_bearing);
    dc.DrawLine(c.x, c.y, e.x, e.y);
  }

  // AIS targets: a bearing/range-placed triangle pointing along its COG, with
  // a small name + course/speed label.
  if (m_layers.ais && has_heading && report_m > 0) {
    ArrayOfPlugIn_AIS_Targets* arr = GetAISTargetArray();
    if (arr) {
      wxFont label_font = dc.GetFont();
      label_font.SetPointSize(std::max(7, label_font.GetPointSize() - 2));
      dc.SetFont(label_font);
      const wxColour green(0, 220, 120), red(255, 80, 80);
      for (size_t i = 0; i < arr->GetCount(); ++i) {
        PlugIn_AIS_Target* t = arr->Item(i);
        if (!t) continue;
        const double rng_m = t->Range_NM * 1852.0;
        // Show targets within the picture (report range plus the overzoom
        // corners, ~1.45x).
        if (rng_m <= 0 || rng_m > report_m * 1.45) {
          delete t;
          continue;
        }
        const double r = radius * rng_m / report_m;
        const wxPoint p = PolarPoint(c, r, t->Brg, up_bearing);
        // Small triangle oriented to COG (fallback: bearing).
        const double course =
            (t->COG >= 0 && t->COG < 360) ? t->COG : t->Brg;
        const double a = (course - up_bearing) * M_PI / 180.0;
        const double ca = std::cos(a), sa = std::sin(a);
        auto rot = [&](double dx, double dy) {
          return wxPoint(p.x + static_cast<int>(std::lround(dx * ca - dy * sa)),
                         p.y + static_cast<int>(std::lround(dx * sa + dy * ca)));
        };
        const bool danger = t->bCPA_Valid && t->CPA < 0.5 && t->TCPA > 0;

        // Course predictor ("extension line"): where the target will be in
        // 10 minutes at its current SOG, along its COG.
        if (t->SOG > 0.1 && t->SOG < 102.2) {
          const double pred_m = (t->SOG / 6.0) * 1852.0;  // 10 min = SOG/6 NM
          const double pred_px = radius * pred_m / report_m;
          const wxPoint e2(p.x + static_cast<int>(std::lround(pred_px * sa)),
                           p.y - static_cast<int>(std::lround(pred_px * ca)));
          dc.SetPen(wxPen(danger ? red : green, 1));
          dc.DrawLine(p.x, p.y, e2.x, e2.y);
        }

        // Triangle: nose forward (screen up = -y before rotation).
        wxPoint tri[3] = {rot(0, -8), rot(-5, 6), rot(5, 6)};
        dc.SetBrush(wxBrush(danger ? red : green));
        dc.SetPen(wxPen(m_theme.text, 1));
        dc.DrawPolygon(3, tri);

        // Label: ship name (or MMSI), plus COG/SOG when moving.
        wxString name = wxString::FromUTF8(t->ShipName).Trim().Trim(false);
        if (name.IsEmpty() && t->MMSI) name = wxString::Format("%d", t->MMSI);
        wxString line2;
        if (t->SOG >= 0 && t->SOG < 102.2)
          line2 = wxString::Format("%03.0f° %.1fkn",
                                   (t->COG >= 0 && t->COG < 360) ? t->COG : 0.0,
                                   t->SOG);
        dc.SetTextForeground(danger ? red : green);
        int ty = p.y + 8;
        if (!name.IsEmpty()) {
          dc.DrawText(name, p.x + 8, ty);
          ty += label_font.GetPixelSize().y + 1;
        }
        if (!line2.IsEmpty()) dc.DrawText(line2, p.x + 8, ty);
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
  } else if (m_icon_view.Contains(p)) {
    if (m_on_view) m_on_view();
  } else if (m_icon_ais.Contains(p)) {
    m_layers.ais = !m_layers.ais;
    Refresh(false);
  } else if (m_icon_ebl.Contains(p)) {
    m_ebl_on = !m_ebl_on;  // placeholder until EBL/VRM is implemented
    Refresh(false);
  } else if (m_icon_gain.Contains(p)) {
    if (m_on_control) m_on_control("gain");
  } else if (m_icon_sea.Contains(p)) {
    if (m_on_control) m_on_control("sea");
  } else if (m_icon_rain.Contains(p)) {
    if (m_on_control) m_on_control("rain");
  } else if (m_power_rect.Contains(p))
    TogglePower();
  else if (m_range_minus_rect.Contains(p))
    StepRange(+1);  // "-" zooms out to a longer range
  else if (m_range_plus_rect.Contains(p))
    StepRange(-1);  // "+" zooms in to a shorter range
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
