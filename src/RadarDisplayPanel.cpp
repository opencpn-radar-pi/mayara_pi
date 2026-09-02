/******************************************************************************
 * mayara_pi - radar-image panel.
 *****************************************************************************/
#include "RadarDisplayPanel.h"

#include <chrono>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include <memory>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
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

void LozengeBg(wxDC& dc, const wxWindow* win, const wxRect& r, int radius,
               const MayaraTheme& t) {
  dc.SetBrush(wxBrush(t.lozenge_bg));
  // A default one-pixel pen is a hairline on a scaled display, which left the
  // outlines far fainter than the type they enclose.
  dc.SetPen(wxPen(t.lozenge_border, win->FromDIP(1)));
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
void DrawGauge(wxDC& dc, const wxWindow* win, wxPoint c,
               const wxString& letter, double frac, bool is_auto,
               const wxColour& ink, const wxColour& accent) {
  // Device-independent: see the note in DrawIconBar.
  const int R = win->FromDIP(11), SEG = 16, yoff = win->FromDIP(2);
  const int pen = win->FromDIP(2);
  auto pt = [&](double a) {
    return wxPoint(c.x + static_cast<int>(std::lround(R * std::cos(a))),
                   c.y - static_cast<int>(std::lround(R * std::sin(a))) + yoff);
  };
  dc.SetPen(wxPen(wxColour(90, 90, 96), pen));  // background arc 180 -> 0
  wxPoint prev = pt(M_PI);
  for (int i = 1; i <= SEG; ++i) {
    wxPoint q = pt(M_PI - M_PI * i / SEG);
    dc.DrawLine(prev, q);
    prev = q;
  }
  if (frac >= 0) {
    dc.SetPen(wxPen(is_auto ? wxColour(0, 200, 255) : accent, pen));
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
  // Tight under the arc: the glyph sits well below the top of its text box,
  // so a nominal gap here reads as a much wider one.
  dc.DrawText(letter, c.x - tw / 2, c.y + yoff + win->FromDIP(1));
}

// The two VRM/EBL markers, as in the mayara GUI.
const wxColour kVrmEblColours[2] = {wxColour(0, 255, 255), wxColour(255, 0, 255)};

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
    EVT_LEFT_UP(RadarDisplayPanel::OnLeftUp)
    EVT_MOTION(RadarDisplayPanel::OnMotion)
    EVT_LEAVE_WINDOW(RadarDisplayPanel::OnLeave)
    EVT_LEFT_DCLICK(RadarDisplayPanel::OnLeftDClick)
    EVT_MOUSEWHEEL(RadarDisplayPanel::OnMouseWheel)
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

static int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
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
  const PpiGeometry g = Geometry();

  // The reported (nominal, round) range fills the window; the larger spoke
  // range spills past the edge as overzoom (zoom = spoke / report).
  const uint32_t spoke_m = state ? state->RangeMeters() : 0;
  // Base zoom fits the reported range to the window edge; the user's free
  // display zoom magnifies about the sweep origin on top of that.
  const double base_zoom =
      (g.report_m > 0 && spoke_m > 0) ? spoke_m / g.report_m : 1.0;
  const double zoom = base_zoom * g.zoom;

  if (state) {
    const int rw = std::max(16, sz.x - m_obscured_right), rh = sz.y;
    // The picture only changes when the radar sends spokes or the operator
    // moves the view, but the timer repaints regardless -- on a still boat at
    // 10 Hz with a 2 s rotation that is nineteen frames in twenty rasterising
    // the same image. Rasterise on a change, blit the rest of the time.
    PpiCacheKey key;
    key.generation = state->Generation();
    key.w = rw;
    key.h = rh;
    key.zoom = zoom;
    key.rot = g.raster_rot;
    key.off_x = g.offset.x;  // int in a wxPoint, double here
    key.off_y = g.offset.y;
    key.intensity = m_theme.radar_intensity;
    key.threshold = state->Threshold();
    if (!m_picture.IsOk() || !(key == m_picture_key)) {
      const int64_t t0 = NowUs();
      std::vector<uint8_t> rgb(static_cast<size_t>(rw) * rh * 3);
      if (state->RenderPPI(rgb.data(), rw, rh, zoom, g.raster_rot, g.offset.x,
                           g.offset.y)) {
        const int64_t t1 = NowUs();
        // wxImage copies here (the `true` is static_data: it does not take
        // ownership), and wxBitmap converts again -- both are part of the cost
        // this cache exists to avoid, so both are inside the timing.
        m_picture = wxBitmap(wxImage(rw, rh, rgb.data(), true));
        m_picture_key = key;
        m_render_us = t1 - t0;
        m_convert_us = NowUs() - t1;
        ++m_render_count;
      } else {
        m_picture = wxBitmap();
      }
    } else {
      ++m_blit_count;
    }
    if (m_picture.IsOk()) dc.DrawBitmap(m_picture, 0, 0, false);
    if (m_perf_log) {
      // Once every 100 paints, so the log says what the picture costs without
      // becoming the thing that costs.
      if ((m_render_count + m_blit_count) % 100 == 0)
        m_perf_log(wxString::Format(
            "PPI radar %d: %dx%d, rasterise %.1f ms + convert %.1f ms, "
            "%llu rasterised / %llu blitted",
            m_index, rw, rh, m_render_us / 1000.0, m_convert_us / 1000.0,
            static_cast<unsigned long long>(m_render_count),
            static_cast<unsigned long long>(m_blit_count)));
    }
  }

  // Extra layers over the picture. Radius maps to the reported range; layers
  // are placed relative to whatever bearing is shown at screen-up.
  DrawLayers(dc, g);

  // The lozenges and the icon bar are hand-drawn: arcs built from short line
  // segments, rounded rectangles, small triangles. On macOS a plain wxDC is
  // backed by Core Graphics and antialiases all of that for free; on MSW it is
  // GDI, which does not antialias at all, so the same code came out faceted and
  // hard-edged, and the unblended edges made the fills read a shade harsher.
  // Draw them through a graphics context so both platforms agree. The layers
  // above make their own where they need it.
  {
    std::unique_ptr<wxGraphicsContext> gc(
        wxGraphicsContext::CreateFromUnknownDC(dc));
    if (gc) {
      gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
      wxGCDC gdc;
      gdc.SetGraphicsContext(gc.release());
      gdc.SetFont(dc.GetFont());  // sizes are derived from it
      DrawLozenges(gdc, sz);
    } else {
      DrawLozenges(dc, sz);
    }
  }

  if (m_client && m_client->ApiVersionMismatch()) {
    // Loud, centred, two-line warning at menu-item point size.
    const wxString l1 = _("Radar API version mismatch");
    const wxString l2 = wxString::Format(
        // ASCII hyphen, not an em dash: msgids are kept ASCII-only.
        _("server %s, plugin %s - update the plugin"),
        wxString::FromUTF8(m_client->ServerApiVersion().c_str()),
        wxString::FromUTF8(MayaraClient::kRadarApiVersion));
    dc.SetFont(GetFont());  // same point size as the menu items
    dc.SetTextForeground(wxColour(255, 90, 90));
    wxCoord w1, h1, w2, h2;
    dc.GetTextExtent(l1, &w1, &h1);
    dc.GetTextExtent(l2, &w2, &h2);
    const int cx = (sz.x - m_obscured_right) / 2, cy = sz.y / 2;
    dc.DrawText(l1, cx - w1 / 2, cy - h1);
    dc.DrawText(l2, cx - w2 / 2, cy + 2);
  } else if (!m_client || !m_client->ControlsAt(m_index)) {
    // Connection status only until a radar is up (no "streaming N radar(s)").
    const wxString status =
        m_client ? wxString::FromUTF8(m_client->StatusLine().c_str())
                 : wxString(L"no client");
    dc.SetTextForeground(m_theme.text);
    dc.DrawText(status, 8, sz.y - 20);
  }
}

void RadarDisplayPanel::DrawIconBar(wxDC& dc, const wxSize& sz) {
  RadarControls* controls = m_client ? m_client->ControlsAt(m_index) : nullptr;
  // Every measurement here is in device-independent pixels. On a 200% display
  // the raw values render at half their intended size, while text -- sized in
  // points, a physical unit -- comes out full size, so the type outgrows the
  // boxes drawn for it. macOS never showed this because its backing scale is
  // applied for us.
  const int cell = FromDIP(40), bw = FromDIP(40);
  // Against the right edge of the *visible* picture, not the panel: with the
  // controls open the full-width position puts the bar underneath them.
  const int avail_w = std::max(16, sz.x - m_obscured_right);
  const int bx = avail_w - bw - FromDIP(6), by = FromDIP(6), barH = 6 * cell;

  // Very dark grey rounded background.
  dc.SetBrush(wxBrush(wxColour(22, 22, 24)));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRoundedRectangle(bx, by, bw, barH, FromDIP(8));

  const wxColour ink(220, 220, 225), dim(115, 115, 122);
  auto cellRect = [&](int i) { return wxRect(bx, by + i * cell, bw, cell); };
  // A caption under an icon whose glyph does not say what it is. Tiny, and
  // centred in the cell so it reads as part of the icon.
  auto caption = [&](int i, const wxString& text, const wxColour& col) {
    wxFont f = dc.GetFont();
    f.SetPointSize(6);
    dc.SetFont(f);
    dc.SetTextForeground(col);
    wxCoord tw, th;
    dc.GetTextExtent(text, &tw, &th);
    dc.DrawText(text, bx + (bw - tw) / 2,
                by + i * cell + cell - th - FromDIP(2));
  };
  auto ctr = [&](int i) {
    return wxPoint(bx + bw / 2, by + i * cell + cell / 2);
  };

  // 0: Menu (hamburger).
  {
    wxPoint c = ctr(0);
    dc.SetPen(wxPen(ink, FromDIP(2)));
    const int hw = FromDIP(9), hgap = FromDIP(5);
    for (int k = -1; k <= 1; ++k)
      dc.DrawLine(c.x - hw, c.y + k * hgap, c.x + hw, c.y + k * hgap);
    m_menu_rect = cellRect(0);
  }
  // 1: AIS on/off (filled triangle when on).
  {
    wxPoint c = ctr(1);
    const wxColour col = m_layers.ais ? wxColour(0, 220, 120) : dim;
    dc.SetPen(wxPen(col, FromDIP(2)));
    dc.SetBrush(m_layers.ais ? wxBrush(col) : *wxTRANSPARENT_BRUSH);
    const int tup = FromDIP(9), tside = FromDIP(7), tdn = FromDIP(8);
    wxPoint tri[3] = {wxPoint(c.x, c.y - tup), wxPoint(c.x - tside, c.y + tdn),
                      wxPoint(c.x + tside, c.y + tdn)};
    dc.DrawPolygon(3, tri);
    m_icon_ais = cellRect(1);
    caption(1, "AIS", m_layers.ais ? col : dim);
  }
  // 2,3,4: Gain / Sea / Rain gauges.
  {
    bool a = false;
    DrawGauge(dc, this, ctr(2), "G", GaugeFrac(controls, "gain", a), a, ink,
              m_theme.accent);
    m_icon_gain = cellRect(2);
    DrawGauge(dc, this, ctr(3), "S", GaugeFrac(controls, "sea", a), a, ink,
              m_theme.accent);
    m_icon_sea = cellRect(3);
    DrawGauge(dc, this, ctr(4), "R", GaugeFrac(controls, "rain", a), a, ink,
              m_theme.accent);
    m_icon_rain = cellRect(4);
  }
  // 5: VRM/EBL (ring + radial). Takes the colour of the marker a click would
  // place, and carries its number, so which one is armed is never a guess.
  {
    wxPoint c = ctr(5);
    const bool armed = m_ebl_arm > 0;
    const wxColour col = armed ? kVrmEblColours[m_ebl_arm - 1] : dim;
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(col, FromDIP(2)));
    dc.DrawCircle(c.x, c.y, FromDIP(9));
    dc.DrawLine(c.x, c.y, c.x + FromDIP(8), c.y - FromDIP(5));
    if (armed) {
      wxFont f = dc.GetFont();
      f.SetPointSize(8);
      f.MakeBold();
      dc.SetFont(f);
      dc.SetTextForeground(col);
      const wxString n = wxString::Format("%d", m_ebl_arm);
      wxCoord tw, th;
      dc.GetTextExtent(n, &tw, &th);
      // Left of centre: the radial line leaves the middle towards the upper
      // right, and the digit was sitting under it.
      dc.DrawText(n, c.x - tw / 2 - FromDIP(4), c.y - th / 2 + FromDIP(1));
    }
    m_icon_ebl = cellRect(5);
    caption(5, "EBL/VRM", col);
  }
}

void RadarDisplayPanel::DrawLozenges(wxDC& dc, const wxSize& sz) {
  m_menu_rect = m_icon_ais = m_icon_gain = m_icon_sea = m_icon_rain =
      m_icon_ebl = m_orient_rect = m_alarm_rect = wxRect();
  m_power_rect = wxRect();
  m_range_minus_rect = wxRect();
  m_range_plus_rect = wxRect();

  DrawIconBar(dc, sz);

  if (!m_client) return;
  RadarControls* controls = m_client->ControlsAt(m_index);
  if (!controls) return;  // no radar connected yet

  // Range lozenge geometry, needed early: it is vertically centred on the
  // panel and can land anywhere, including inside the Power/Orientation/
  // Alarm column stacked from the top on a short (multi-radar grid) panel.
  // Computed again, identically, where it is actually drawn below -- this
  // is geometry only, cheap, and keeps that block unchanged.
  wxRect range_reserved;
  {
    RadarState* rs = m_client->StateAt(m_index);
    ControlValue rrv = controls->Value("range");
    const double rcur =
        rrv.has_value ? rrv.value : (rs ? rs->RangeMeters() : 0.0);
    double rreport_m = rcur;
    bool rmetric = false;
    EffectiveRange(rreport_m, rmetric);
    const wxString rrlabel =
        rcur > 0 ? FormatRange(rcur, rmetric) : wxString();
    if (!rrlabel.IsEmpty()) {
      wxFont big = GetFont();
      big.SetPointSize(big.GetPointSize() + 2);
      dc.SetFont(big);
      wxCoord tw, th;
      dc.GetTextExtent(rrlabel, &tw, &th);
      dc.SetFont(GetFont());
      const int w = std::max<int>(tw + 18, 60);
      const int h = 26 + (th + 12) + 26;
      range_reserved = wxRect(10, (sz.y - h) / 2, w, h);
    }
  }

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
    const wxFont& name_font = base;  // ("small" is a Windows macro)  // name line == ring-label size

    wxCoord tw, th, nw = 0, nh = 0;
    dc.SetFont(big);
    dc.GetTextExtent(label, &tw, &th);
    if (!name.IsEmpty()) {
      dc.SetFont(name_font);
      dc.GetTextExtent(name, &nw, &nh);
    }

    const int padx = 9, gap = 8, icon = th, vgap = 1;
    const int textW = std::max<int>(tw, nw);
    const int textH = name.IsEmpty() ? th : nh + vgap + th;
    const int h = std::max<int>(textH, icon) + 12;
    const int w = padx + icon + gap + textW + padx;
    const int x = 10, y = 10;
    LozengeBg(dc, this, wxRect(x, y, w, h), std::min(h / 2, 12), m_theme);

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
      dc.SetFont(name_font);
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

  // --- Orientation lozenge, under the power one ---
  // Which way is up is a per-radar decision, and on a two-radar window the
  // controls drive whichever picture has focus -- so the picture itself has to
  // say what it is doing. Click to cycle.
  {
    double up = 0.0, rot = 0.0, hdg = 0.0;
    bool has_heading = false;
    ResolveOrientation(up, rot, hdg, has_heading);
    // Head-up asks nothing of anyone: the picture is bow-relative as it
    // arrives. North-up needs a heading to rotate by, and course-up needs a
    // course as well -- without either it is really head-up, and the lozenge
    // says which input is missing rather than just going quiet.
    NavState nav;
    if (m_nav) nav = m_nav();
    const bool needs_heading = m_orientation != kHeadUp;
    const bool needs_cog = m_orientation == kCourseUp;
    const bool honoured = (!needs_heading || has_heading) &&
                          (!needs_cog || nav.has_cog);
    const wxString missing = !has_heading && needs_heading ? _("no heading")
                             : needs_cog && !nav.has_cog   ? _("no course")
                                                           : wxString();
    const wxString label = m_orientation == kNorthUp   ? _("North up")
                           : m_orientation == kCourseUp ? _("Course up")
                                                        : _("Head up");
    const wxColour fg = honoured ? m_theme.text : m_theme.accent_dim;

    wxFont f = GetFont();
    dc.SetFont(f);
    wxCoord tw, th;
    dc.GetTextExtent(label, &tw, &th);
    const int w = tw + 22, h = th + 12;
    const int x = 10, y = m_power_rect.IsEmpty() ? 10
                                                 : m_power_rect.GetBottom() + 8;
    LozengeBg(dc, this, wxRect(x, y, w, h), std::min(h / 2, 12), m_theme);

    // A small arrow for what is at the top of the picture: north gets a
    // needle, course and heading get a bow.
    dc.SetTextForeground(fg);
    dc.DrawText(label, x + 11, y + 6);
    m_orient_rect = wxRect(x, y, w, h);
    if (!honoured) {
      // Whatever it says, the picture is head-up until the missing input
      // arrives. Folded into the hit-test rect too, so the alarm-sound
      // lozenge stacked below still clears it.
      dc.SetFont(f);
      dc.SetTextForeground(m_theme.accent_dim);
      dc.DrawText(missing, x, y + h + 2);
      m_orient_rect.SetHeight(h + 2 + th);
    }
  }

  // --- Alarm-sound lozenge, under the orientation one -----------------------
  // A plugin-wide setting, not a per-radar one -- same idea and the same
  // colours as the web GUI's speaker lozenge (layout.css), hand-drawn as a
  // bell rather than a speaker to match how the operator asked for it. Icon
  // only, no label -- there is no room to spare stacked under the other two.
  // Click to toggle.
  if (m_alarm_sound_get) {
    const int side = FromDIP(36);
    const int x = 10, y = m_orient_rect.IsEmpty() ? (m_power_rect.IsEmpty()
                                                         ? 10
                                                         : m_power_rect.GetBottom() + 8)
                                                  : m_orient_rect.GetBottom() + 8;
    const wxRect rect(x, y, side, side);
    // On a short multi-radar panel the vertically-centred range lozenge
    // (same x) can land inside this column. Range wins -- it is always
    // present and essential; the bell just does not draw or hit-test that
    // frame (m_alarm_rect stays empty, cleared at the top of this function).
    if (range_reserved.IsEmpty() || !rect.Intersects(range_reserved)) {
      const bool on = m_alarm_sound_get();
      // .myr_speaker_lozenge: background rgba(0,0,0,0.7), border #446.
      const wxColour bg(0, 0, 0, 178), border(0x44, 0x44, 0x66);
      // .myr_speaker_lozenge_button on/off background, .myr_speaker_icon
      // stroke/fill on/off -- lifted from web/gui/layout.css as-is.
      const wxColour btn_bg =
          on ? wxColour(0x1a, 0x3d, 0x4d) : wxColour(0x2a, 0x2a, 0x2a);
      const wxColour fg =
          on ? wxColour(0x4d, 0xc8, 0xff) : wxColour(0x66, 0x66, 0x66);

      dc.SetBrush(wxBrush(bg));
      dc.SetPen(wxPen(border, FromDIP(2)));
      dc.DrawRoundedRectangle(rect, side / 2);
      dc.SetBrush(wxBrush(btn_bg));
      dc.SetPen(*wxTRANSPARENT_PEN);
      const int inset = FromDIP(2);
      dc.DrawRoundedRectangle(wxRect(x + inset, y + inset, side - 2 * inset,
                                     side - 2 * inset),
                              side / 2 - inset);

      // A small bell: dome, flared sides, a rim, and a clapper. Muted draws
      // a diagonal strike through it, same convention as the web GUI's mute
      // icon.
      const int cx = x + side / 2, cy = y + side / 2 - FromDIP(2);
      const int r = FromDIP(7);
      dc.SetPen(wxPen(fg, FromDIP(2)));
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.DrawEllipticArc(cx - r, cy - r, r * 2, r * 2, 0, 180);
      dc.DrawLine(cx - r, cy, cx - r - FromDIP(2), cy + r);
      dc.DrawLine(cx + r, cy, cx + r + FromDIP(2), cy + r);
      dc.DrawLine(cx - r - FromDIP(2), cy + r, cx + r + FromDIP(2), cy + r);
      dc.SetBrush(wxBrush(fg));
      dc.DrawCircle(cx, cy + r + FromDIP(3), FromDIP(2));
      if (!on) {
        dc.SetPen(wxPen(fg, FromDIP(2)));
        dc.DrawLine(cx - r - FromDIP(3), cy - r - FromDIP(1),
                   cx + r + FromDIP(3), cy + r + FromDIP(5));
      }

      m_alarm_rect = rect;
    }
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
    LozengeBg(dc, this, wxRect(x, y, w, h), 12, m_theme);

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
        return wxString::Format(L"%ld km", std::lround(km));
      return wxString::Format(L"%.1f km", km);
    }
    return wxString::Format(L"%.0f m", m);
  }
  const double nm = m / 1852.0;
  if (nm >= 0.95) {
    if (std::fabs(nm - std::lround(nm)) < 0.03)
      return wxString::Format(L"%ld NM", std::lround(nm));
    return wxString::Format(L"%.1f NM", nm);
  }
  for (long d : {16L, 8L, 4L, 2L}) {
    const double num = nm * d;
    const long n = std::lround(num);
    if (n >= 1 && std::fabs(num - n) < 0.03) {
      const long g = std::gcd(n, d);
      const long nn = n / g, dd = d / g;
      if (dd == 1) return wxString::Format(L"%ld NM", nn);
      return wxString::Format(L"%ld/%ld NM", nn, dd);
    }
  }
  return wxString::Format(L"%.0f m", m);
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

// One place decides how an angle reads, because the alternative is what this
// replaced: the picture's own readout printing a marker as "57.1 T" while the
// controls panel printed the same marker as a bare "17.6", the heading being
// the whole of the difference and neither of them saying so.
double SignedRelative(double deg) {
  while (deg < 0) deg += 360.0;
  while (deg >= 360.0) deg -= 360.0;
  return deg > 180.0 ? deg - 360.0 : deg;
}

wxString FormatBearing(double relative_deg, const BearingRef& ref) {
  // Head-up already points the bow at the top of the screen, so a bearing
  // read off it is relative whether or not a heading is available.
  const bool absolute = ref.has_heading && ref.orientation != kHeadUp;
  if (!absolute)
    return wxString::Format(L"%.1f°R", SignedRelative(relative_deg));
  double t = relative_deg + ref.heading_deg;
  while (t < 0) t += 360.0;
  while (t >= 360.0) t -= 360.0;
  return wxString::Format(L"%.1f°T", t);
}

BearingRef RadarDisplayPanel::BearingReference() const {
  BearingRef ref;
  double up = 0, rot = 0;
  ResolveOrientation(up, rot, ref.heading_deg, ref.has_heading);
  ref.orientation = m_orientation;
  return ref;
}

void RadarDisplayPanel::ResolveOrientation(double& up_bearing,
                                           double& raster_rot, double& heading,
                                           bool& has_heading) const {
  NavState nav;
  if (m_nav) nav = m_nav();
  heading = 0.0;
  has_heading = false;
  if (m_heading_provider) {
    has_heading = m_heading_provider(m_index, heading);
  } else if (nav.has_hdt) {
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

PpiGeometry RadarDisplayPanel::Geometry() const {
  PpiGeometry g;
  const wxSize sz = GetClientSize();
  // The picture fills the visible rectangle (the part not covered by the open
  // menu). Range rings use the smaller half-dimension so they stay circular.
  const int avail_w = std::max(16, sz.x - m_obscured_right);
  const int side = std::max(16, std::min(avail_w, sz.y));
  g.offset = wxPoint(m_off_center.x + m_drag.x, m_off_center.y + m_drag.y);
  g.center = wxPoint(avail_w / 2 + g.offset.x, sz.y / 2 + g.offset.y);
  g.radius = side / 2.0;
  g.zoom = m_display_zoom > 0 ? m_display_zoom : 1.0;

  RadarState* st = m_client ? m_client->StateAt(m_index) : nullptr;
  g.spoke_m = st ? st->RangeMeters() : 0.0;
  g.report_m = g.spoke_m;
  EffectiveRange(g.report_m, g.metric);
  ResolveOrientation(g.up_bearing, g.raster_rot, g.heading, g.has_heading);
  g.valid = g.radius >= 8 && g.report_m > 0;
  return g;
}

void RadarDisplayPanel::DrawLayers(wxDC& dc, const PpiGeometry& g) {
  const wxPoint c = g.center;
  const double radius = g.radius, report_m = g.report_m,
               disp_zoom = g.zoom;
  const bool metric = g.metric;
  if (radius < 8) return;
  // Geographic layers (rings, AIS, ARPA) scale with the free display zoom;
  // window-fixed decorations (compass, heading, north, COG) do not. `geo`
  // maps a true geographic distance to screen pixels.
  const double geo = report_m > 0 ? disp_zoom / report_m * radius : 0.0;

  NavState nav;
  if (m_nav) nav = m_nav();
  // `up_bearing` is the true bearing shown at screen-up for the current
  // orientation; every true-referenced layer is placed relative to it.
  const double up_bearing = g.up_bearing, heading = g.heading;
  const bool has_heading = g.has_heading;

  dc.SetBrush(*wxTRANSPARENT_BRUSH);

  // The reported range fills the window (the picture is zoomed to it), so the
  // outer ring sits at the window edge and the picture beyond is overzoom.
  if (m_layers.range_rings && report_m > 0) {
    const int rings = RingCount(report_m);
    dc.SetPen(wxPen(m_theme.dim_text, 1));
    dc.SetTextForeground(m_theme.dim_text);
    const double k = 0.70710678;  // cos/sin 45 deg (top-right diagonal)
    for (int i = 1; i <= rings; ++i) {
      const double rr = radius * i / rings * disp_zoom;
      if (rr > radius * 1.5) continue;  // ring past the picture when zoomed in
      dc.DrawCircle(c.x, c.y, static_cast<int>(rr));
      const wxString lbl = FormatRange(report_m * i / rings, metric);
      wxCoord tw, th;
      dc.GetTextExtent(lbl, &tw, &th);
      // Text starts just outside the ring on the upper-right diagonal.
      dc.DrawText(lbl, c.x + static_cast<int>((rr + 3) * k),
                  c.y - static_cast<int>((rr + 3) * k) - th / 2);
    }
  }

  // Extreme range: where the spoke data actually stops, which is further out
  // than the reported range (the difference is the overzoom filling the
  // corners). Beyond this ring there is no radar, only black.
  if (m_layers.extreme_range && geo > 0 && g.spoke_m > g.report_m) {
    const double er = g.spoke_m * geo;
    if (er <= radius * 1.45) {  // otherwise it is off-picture anyway
      dc.SetPen(wxPen(wxColour(200, 40, 40), 1));
      dc.DrawCircle(c.x, c.y, static_cast<int>(er));
    }
  }

  // Guard zones sit under the targets: they are context, not contacts.
  if (m_layers.guard_zones && geo > 0) DrawGuardZones(dc, g, geo);

  // The zone being edited is drawn from the uncommitted values, so a drag or a
  // typed number shows immediately rather than after a round trip.
  m_zone_pts_valid = false;
  if (geo > 0 && m_zone_get) {
    const ZoneEdit z = m_zone_get();
    if (z.active && z.radar_index == m_index) DrawZoneHandles(dc, g, geo, z);
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
      // OpenCPN's AIS palette (matches the active colour scheme).
      auto gcol = [](const char* name, const wxColour& fallback) {
        wxColour col;
        if (GetGlobalColor(wxString::FromUTF8(name), &col) && col.IsOk())
          return col;
        return fallback;
      };
      const wxColour col_outline = gcol("UBLCK", wxColour(20, 20, 20));
      const wxColour col_active = gcol("TEAL1", wxColour(0, 150, 150));
      const wxColour col_noname = gcol("CHYLW", wxColour(255, 220, 0));
      const wxColour col_danger = gcol("URED", wxColour(210, 0, 0));
      for (size_t i = 0; i < arr->GetCount(); ++i) {
        PlugIn_AIS_Target* t = arr->Item(i);
        if (!t) continue;
        const double rng_m = t->Range_NM * 1852.0;
        const double r = rng_m * geo;
        // Show targets within the picture (window edge plus the overzoom
        // corners, ~1.45x), honouring the free display zoom.
        if (rng_m <= 0 || r > radius * 1.45) {
          delete t;
          continue;
        }
        const wxPoint p = PolarPoint(c, r, t->Brg, up_bearing);
        const double course =
            (t->COG >= 0 && t->COG < 360) ? t->COG : t->Brg;
        const double a = (course - up_bearing) * M_PI / 180.0;
        const double ca = std::cos(a), sa = std::sin(a);
        auto rot = [&](double dx, double dy) {
          return wxPoint(p.x + static_cast<int>(std::lround(dx * ca - dy * sa)),
                         p.y + static_cast<int>(std::lround(dx * sa + dy * ca)));
        };
        const bool danger = t->bCPA_Valid && t->CPA < 0.5 && t->TCPA > 0;
        const bool moving = t->SOG >= 0.5 && t->SOG < 102.2;

        wxString name = wxString::FromUTF8(t->ShipName);
        name.Replace("@", " ");  // AIS pads names with '@'
        name.Trim().Trim(false);
        const wxColour fill =
            danger ? col_danger : (name.IsEmpty() ? col_noname : col_active);

        // COG/SOG predictor vector (10 minutes), like OpenCPN's.
        if (moving) {
          const double pred_m = (t->SOG / 6.0) * 1852.0;  // 10 min = SOG/6 NM
          const double pred_px = pred_m * geo;
          const wxPoint e2(p.x + static_cast<int>(std::lround(pred_px * sa)),
                           p.y - static_cast<int>(std::lround(pred_px * ca)));
          dc.SetPen(wxPen(fill, 1));
          dc.DrawLine(p.x, p.y, e2.x, e2.y);
        }

        dc.SetBrush(wxBrush(fill));
        dc.SetPen(wxPen(col_outline, 1));
        if (moving) {
          // Directional AIS "kite": long nose along COG, base astern. Class B
          // gets a notched back; Class A a flat back.
          const int back = t->Class == 1 ? 1 : 6;
          wxPoint kite[4] = {rot(-5, 6), rot(0, -13), rot(5, 6), rot(0, back)};
          dc.DrawPolygon(4, kite);
        } else {
          dc.DrawCircle(p, 5);  // stationary/anchored target
        }

        // Label: ship name (or MMSI), plus COG/SOG when moving.
        if (name.IsEmpty() && t->MMSI) name = wxString::Format("%d", t->MMSI);
        wxString line2;
        if (moving)
          line2 = wxString::Format(L"%03.0f° %.1fkn",
                                   (t->COG >= 0 && t->COG < 360) ? t->COG : 0.0,
                                   t->SOG);
        dc.SetTextForeground(fill);
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

  // ARPA targets: the server tracks them (guard-zone auto-acquire or manual);
  // the plugin just renders what it streams, placed by true bearing/distance
  // from the radar. A distinct look from AIS: a ringed marker with a velocity
  // vector along COG.
  if (m_layers.arpa && m_client && report_m > 0) {
    std::vector<RadarTarget> targets = m_client->TargetsAt(m_index);
    if (!targets.empty()) {
      wxFont label_font = dc.GetFont();
      label_font.SetPointSize(std::max(7, label_font.GetPointSize() - 2));
      dc.SetFont(label_font);
      auto gcol = [](const char* name, const wxColour& fallback) {
        wxColour col;
        if (GetGlobalColor(wxString::FromUTF8(name), &col) && col.IsOk())
          return col;
        return fallback;
      };
      const wxColour col_track = gcol("UGREN", wxColour(0, 230, 0));
      const wxColour col_danger = gcol("URED", wxColour(210, 0, 0));
      const wxColour col_acq = gcol("CHYLW", wxColour(255, 210, 0));
      const wxColour col_lost = gcol("UGRY1", wxColour(140, 140, 140));
      for (const RadarTarget& t : targets) {
        const double r = t.distance_m * geo;
        if (t.distance_m <= 0 || r > radius * 1.45) continue;
        const wxPoint p = PolarPoint(c, r, t.bearing_deg, up_bearing);
        const bool dangerous = t.has_danger && t.is_dangerous;
        const wxColour col = t.status == RadarTarget::kLost ? col_lost
                             : dangerous                    ? col_danger
                             : t.status == RadarTarget::kAcquiring ? col_acq
                                                    : col_track;

        // Velocity vector (6 min), along true COG.
        if (t.has_motion && t.speed_kn >= 0.2 &&
            t.status == RadarTarget::kTracking) {
          const double pred_nm = t.speed_kn * (6.0 / 60.0);
          const double pred_px = (pred_nm * 1852.0) * geo;
          const double a = (t.course_deg - up_bearing) * M_PI / 180.0;
          const wxPoint e(p.x + static_cast<int>(std::lround(pred_px * std::sin(a))),
                          p.y - static_cast<int>(std::lround(pred_px * std::cos(a))));
          dc.SetPen(wxPen(col, 2));
          dc.DrawLine(p.x, p.y, e.x, e.y);
        }

        // Marker: solid ring when tracking, dashed while acquiring, an X when
        // lost. A manually acquired target gets a small centre dot.
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        if (t.status == RadarTarget::kLost) {
          dc.SetPen(wxPen(col, 2));
          dc.DrawLine(p.x - 6, p.y - 6, p.x + 6, p.y + 6);
          dc.DrawLine(p.x - 6, p.y + 6, p.x + 6, p.y - 6);
        } else {
          const int style = t.status == RadarTarget::kAcquiring
                                ? wxPENSTYLE_SHORT_DASH
                                : wxPENSTYLE_SOLID;
          dc.SetPen(wxPen(col, 2, style));
          dc.DrawCircle(p, 8);
          if (t.manual) {
            dc.SetBrush(wxBrush(col));
            dc.SetPen(wxPen(col, 1));
            dc.DrawCircle(p, 2);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
          }
        }

        // Label: id, then COG/SOG, then CPA/TCPA when in danger.
        dc.SetTextForeground(col);
        int ty = p.y + 10;
        const int lh = label_font.GetPixelSize().y + 1;
        dc.DrawText(wxString::Format("T%llu", (unsigned long long)t.id),
                    p.x + 10, ty);
        ty += lh;
        if (t.has_motion && t.speed_kn >= 0.2) {
          double cog = t.course_deg;
          while (cog < 0) cog += 360;
          while (cog >= 360) cog -= 360;
          dc.DrawText(wxString::Format(L"%03.0f° %.1fkn", cog, t.speed_kn),
                      p.x + 10, ty);
          ty += lh;
        }
        if (dangerous) {
          const wxString cpa = FormatRange(t.cpa_m, metric);
          dc.DrawText(wxString::Format("CPA %s %.0fmin", cpa, t.tcpa_s / 60.0),
                      p.x + 10, ty);
        }
      }
    }
  }

  // Own-ship marker at the sweep origin. Always drawn: once the picture can be
  // dragged off centre, "where is the boat" stops being obvious.
  dc.SetPen(wxPen(m_theme.text, 1));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawCircle(c.x, c.y, 4);
  dc.DrawLine(c.x - 7, c.y, c.x - 5, c.y);
  dc.DrawLine(c.x + 5, c.y, c.x + 7, c.y);
  dc.DrawLine(c.x, c.y - 7, c.x, c.y - 5);
  dc.DrawLine(c.x, c.y + 5, c.x, c.y + 7);

  // EBL/VRM and the cursor readout go on top of everything geographic.
  if (geo > 0) DrawVrmEbl(dc, g, geo);
  if (m_cursor_in && !m_dragging) DrawCursor(dc, g);

  // Zoom/recentre chip, only while magnified or panned. Clicking it undoes
  // both, which is the only way back from a pan that ran off the window.
  m_recenter_rect = wxRect();
  if (IsOffCenter()) {
    dc.SetFont(GetFont());
    const wxString z =
        std::fabs(disp_zoom - 1.0) > 0.02
            ? wxString::Format(L"%.1f× ⌖", disp_zoom)
            : wxString(L"⌖");
    wxCoord tw, th;
    dc.GetTextExtent(z, &tw, &th);
    const wxSize sz = GetClientSize();
    const int avail_w = std::max(16, sz.x - m_obscured_right);
    // Window-fixed, not picture-fixed: a pan must not carry it off-screen.
    const wxRect chip(avail_w / 2 - tw / 2 - 8, sz.y - th - 14, tw + 16,
                      th + 8);
    LozengeBg(dc, this, chip, (th + 8) / 2, m_theme);
    dc.SetTextForeground(m_theme.text);
    dc.DrawText(z, chip.x + 8, chip.y + 4);
    m_recenter_rect = chip;
  }
}

// Guard zones as the server reports them: bow-relative angles in radians and
// distances in metres (see mayara-server config::GuardZone). Bow-relative means
// they rotate with the boat, so each edge is drawn at heading + zone angle.
void RadarDisplayPanel::DrawGuardZones(wxDC& dc, const PpiGeometry& g,
                                       double geo) {
  RadarControls* ctrl = m_client ? m_client->ControlsAt(m_index) : nullptr;
  if (!ctrl) return;

  // Styling copied from the mayara web GUI (web/gui/ppi.js #drawGuardZone): a
  // hairline stroke over a barely-there fill, green for zone 1 and blue for
  // zone 2. It reads as a tint over the picture rather than something drawn on
  // top of it, which is the point -- a guard zone is context, and the echoes
  // underneath are what you are actually looking at.
  //
  // That needs real translucency, which plain wxDC cannot do portably (hence
  // the hatching this replaces), so the zones go through a graphics context.
  struct Style {
    const char* id;
    wxColour fill;    // GUI: rgba(..., 0.25)
    wxColour stroke;  // GUI: rgba(..., 0.6)
  };
  const Style styles[] = {
      {"guardZone1", wxColour(144, 238, 144, 64), wxColour(0, 128, 0, 153)},
      {"guardZone2", wxColour(173, 216, 230, 64), wxColour(0, 0, 255, 153)},
  };

  std::unique_ptr<wxGraphicsContext> gc(
      wxGraphicsContext::CreateFromUnknownDC(dc));
  if (!gc) return;  // no antialiased backend: better nothing than the old slab
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

  const ZoneEdit edit = m_zone_get ? m_zone_get() : ZoneEdit();
  for (const Style& s : styles) {
    ControlValue v = ctrl->Value(s.id);
    // While a zone is being edited, draw the edit rather than the stored value
    // -- otherwise the old shape shows through the new one.
    const bool editing =
        edit.active && edit.radar_index == m_index && edit.id == s.id;
    if (editing) {
      v.has_enabled = true;
      v.enabled = true;
      v.value = edit.start_rad;
      v.endValue = edit.end_rad;
      v.startDistance = edit.start_m;
      v.endDistance = edit.end_m;
    }
    if (!v.has_enabled || !v.enabled) continue;
    const double r_in = v.startDistance * geo, r_out = v.endDistance * geo;
    if (v.endDistance <= v.startDistance || r_out < 2) continue;

    // Zone angles are bow-relative radians. Screen angle is measured from the
    // +x axis with y downwards, so it trails the bearing by 90 degrees -- the
    // same "- PI/2" the GUI applies.
    const double base =
        (g.heading - g.up_bearing) * M_PI / 180.0 - M_PI / 2.0;
    const double a0 = base + v.value;
    const double a1 = base + v.endValue;
    const double cx = g.center.x, cy = g.center.y;

    wxGraphicsPath path = gc->CreatePath();
    // Equal angles mean the whole circle, as in the GUI. Drawn as two full
    // circles filled odd-even so the hole is a hole, not a seam.
    const bool whole_circle = std::fabs(v.endValue - v.value) < 0.001;
    if (whole_circle) {
      path.AddCircle(cx, cy, r_out);
      if (r_in > 0) path.AddCircle(cx, cy, r_in);
    } else {
      path.AddArc(cx, cy, r_out, a0, a1, /*clockwise=*/true);
      if (r_in > 0)
        path.AddArc(cx, cy, r_in, a1, a0, /*clockwise=*/false);
      else
        path.AddLineToPoint(cx, cy);  // a sector, not a degenerate annulus
      path.CloseSubpath();
    }

    gc->SetBrush(gc->CreateBrush(wxBrush(s.fill)));
    gc->FillPath(path, whole_circle ? wxODDEVEN_RULE : wxWINDING_RULE);
    gc->SetPen(gc->CreatePen(wxPen(s.stroke, 1)));
    gc->StrokePath(path);
  }
}

// Handle order: 0 start angle, 1 end angle, 2 inner distance, 3 outer distance.
// Angle handles sit mid-depth on their edge, distance handles mid-sweep on
// their arc -- the same places the mayara GUI puts them.
bool RadarDisplayPanel::ZoneHandlePoints(const PpiGeometry& g, double geo,
                                         const ZoneEdit& z,
                                         wxPoint out[4]) const {
  const double r_in = z.start_m * geo, r_out = z.end_m * geo;
  if (r_out < 4) return false;
  const double r_mid = (r_in + r_out) / 2.0;
  const double a0 = g.heading + z.start_rad * 180.0 / M_PI;
  double sweep = (z.end_rad - z.start_rad) * 180.0 / M_PI;
  while (sweep < 0) sweep += 360.0;
  const double a_mid = a0 + sweep / 2.0;
  out[0] = PolarPoint(g.center, r_mid, a0, g.up_bearing);
  out[1] = PolarPoint(g.center, r_mid, a0 + sweep, g.up_bearing);
  out[2] = PolarPoint(g.center, r_in, a_mid, g.up_bearing);
  out[3] = PolarPoint(g.center, r_out, a_mid, g.up_bearing);
  return true;
}

void RadarDisplayPanel::DrawZoneHandles(wxDC& dc, const PpiGeometry& g,
                                        double geo, const ZoneEdit& z) {
  wxPoint pts[4];
  if (!ZoneHandlePoints(g, geo, z, pts)) return;
  for (int i = 0; i < 4; ++i) m_zone_pts[i] = pts[i];
  m_zone_pts_valid = true;

  // Translucent white discs with a grey rim, as in the GUI; the one being
  // dragged is brighter so it is obvious which value is moving.
  std::unique_ptr<wxGraphicsContext> gc(
      wxGraphicsContext::CreateFromUnknownDC(dc));
  if (!gc) return;
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  const double kR = 9.0;
  for (int i = 0; i < 4; ++i) {
    const unsigned char a = (i == m_zone_drag) ? 230 : 128;
    gc->SetBrush(gc->CreateBrush(wxBrush(wxColour(255, 255, 255, a))));
    gc->SetPen(gc->CreatePen(wxPen(wxColour(100, 100, 100, 204), 2)));
    wxGraphicsPath path = gc->CreatePath();
    path.AddCircle(pts[i].x, pts[i].y, kR);
    gc->FillPath(path);
    gc->StrokePath(path);
  }
}

// Move the grabbed handle to the pointer. Angles follow the bearing under the
// cursor, distances its range; each handle moves exactly one of the zone's four
// numbers, so a drag cannot quietly change something you were not aiming at.
void RadarDisplayPanel::DragZoneHandle(const wxPoint& p, bool commit) {
  if (!m_zone_get || !m_zone_set) return;
  ZoneEdit z = m_zone_get();
  if (!z.active || z.radar_index != m_index) return;

  double brg = 0, dist = 0;
  if (!PointToPolar(p, brg, dist)) return;
  const PpiGeometry g = Geometry();
  // Bearing is true; the zone speaks bow-relative.
  double rel = brg - g.heading;
  while (rel < -180.0) rel += 360.0;
  while (rel > 180.0) rel -= 360.0;
  const double rad = rel * M_PI / 180.0;

  switch (m_zone_drag) {
    case 0: z.start_rad = rad; break;
    case 1: z.end_rad = rad; break;
    case 2: z.start_m = std::max(0.0, dist); break;
    case 3: z.end_m = std::max(0.0, dist); break;
    default: return;
  }
  // Keep the ring the right way round rather than refusing the drag.
  if (z.end_m < z.start_m) std::swap(z.start_m, z.end_m);
  m_zone_set(z, commit);
  Refresh(false);
}

int RadarDisplayPanel::ZoneHandleHit(const wxPoint& p) const {
  if (!m_zone_pts_valid) return -1;
  const double kHit = 14.0;  // a little larger than the disc, for fat fingers
  for (int i = 0; i < 4; ++i)
    if (std::hypot(p.x - m_zone_pts[i].x, p.y - m_zone_pts[i].y) <= kHit)
      return i;
  return -1;
}

// Styled after the mayara GUI (web/gui/ppi.js #drawVrmEblMarker): a dashed
// bearing line run out to the picture edge so it stays visible when the ring is
// off-screen, a range ring, a filled dot where they cross, and a boxed readout
// set out along the bearing.
void RadarDisplayPanel::DrawVrmEbl(wxDC& dc, const PpiGeometry& g, double geo) {
  std::unique_ptr<wxGraphicsContext> gc(
      wxGraphicsContext::CreateFromUnknownDC(dc));
  if (!gc) return;
  gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

  for (int i = 0; i < kVrmEblCount; ++i) {
    const VrmEbl& m = m_vrmebl[i];
    if (!m.enabled) continue;
    const wxColour col = kVrmEblColours[i];
    const double brg = g.heading + m.bearing_rad * 180.0 / M_PI;
    const double r = m.distance_m * geo;
    const double line_r = std::max(r, g.radius * 1.42);

    wxPen pen(col, 2, wxPENSTYLE_SHORT_DASH);
    gc->SetPen(gc->CreatePen(pen));
    const wxPoint e = PolarPoint(g.center, line_r, brg, g.up_bearing);
    gc->StrokeLine(g.center.x, g.center.y, e.x, e.y);

    if (r > 0 && r < g.radius * 3) {
      gc->SetPen(gc->CreatePen(wxPen(col, 2)));
      gc->SetBrush(*wxTRANSPARENT_BRUSH);
      wxGraphicsPath ring = gc->CreatePath();
      ring.AddCircle(g.center.x, g.center.y, r);
      gc->StrokePath(ring);
    }

    // The point the marker actually measures.
    const wxPoint p = PolarPoint(g.center, r, brg, g.up_bearing);
    gc->SetBrush(gc->CreateBrush(wxBrush(col)));
    gc->SetPen(gc->CreatePen(wxPen(*wxBLACK, 1)));
    wxGraphicsPath dot = gc->CreatePath();
    dot.AddCircle(p.x, p.y, 6);
    gc->FillPath(dot);
    gc->StrokePath(dot);

    // Readout: which marker, the bearing (true when we know the heading,
    // otherwise relative) and the range.
    wxFont f = GetFont();
    f.SetPointSize(std::max(7, f.GetPointSize() - 1));
    f.MakeBold();
    dc.SetFont(f);
    const wxString lines[3] = {
        wxString::Format(_("VRM/EBL %d"), i + 1),
        FormatBearing(m.bearing_rad * 180.0 / M_PI, BearingReference()),
        FormatRange(m.distance_m, g.metric)};
    wxCoord tw = 0, th = 0, w1, h1;
    for (const wxString& l : lines) {
      dc.GetTextExtent(l, &w1, &h1);
      tw = std::max(tw, w1);
      th = h1;
    }
    const int padx = 6, pady = 4, lh = th + 2;
    const int bw = tw + padx * 2, bh = lh * 3 + pady * 2;
    // Anchored past the crossing point, then pushed clear of the bearing line
    // so the box never sits on top of what it describes.
    const wxPoint a = PolarPoint(g.center, r + 24, brg, g.up_bearing);
    int bx = a.x - bw / 2, by = a.y - bh / 2;
    bx += (a.x >= g.center.x) ? 12 : -(bw / 2 + 12);
    by += (a.y >= g.center.y) ? 12 : -12;
    const wxSize sz = GetClientSize();
    bx = std::max(4, std::min(sz.x - bw - 4, bx));
    by = std::max(4, std::min(sz.y - bh - 4, by));

    gc->SetBrush(gc->CreateBrush(wxBrush(wxColour(0, 0, 0, 191))));
    gc->SetPen(gc->CreatePen(wxPen(col, 1)));
    wxGraphicsPath box = gc->CreatePath();
    box.AddRectangle(bx, by, bw, bh);
    gc->FillPath(box);
    gc->StrokePath(box);
    dc.SetTextForeground(col);
    for (int k = 0; k < 3; ++k)
      dc.DrawText(lines[k], bx + padx, by + pady + k * lh);
  }
}

void RadarDisplayPanel::DrawCursor(wxDC& dc, const PpiGeometry& g) {
  double brg = 0, dist = 0;
  if (!PointToPolar(m_cursor, brg, dist)) return;
  const wxColour col = m_theme.dim_text;
  dc.SetPen(wxPen(col, 1));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawLine(m_cursor.x - 7, m_cursor.y, m_cursor.x - 2, m_cursor.y);
  dc.DrawLine(m_cursor.x + 2, m_cursor.y, m_cursor.x + 7, m_cursor.y);
  dc.DrawLine(m_cursor.x, m_cursor.y - 7, m_cursor.x, m_cursor.y - 2);
  dc.DrawLine(m_cursor.x, m_cursor.y + 2, m_cursor.x, m_cursor.y + 7);

  wxFont f = GetFont();
  f.SetPointSize(std::max(7, f.GetPointSize() - 1));
  dc.SetFont(f);
  wxString txt = wxString::Format(L"%03.0f°T  %s", brg,
                                  FormatRange(dist, g.metric));
  // This one shows both when it can, rather than letting the orientation
  // pick -- it is a free measurement, not a placed marker. The relative half
  // still takes the signed convention FormatBearing uses, so a bearing does
  // not change sign depending on which readout you happen to read it from.
  if (g.has_heading)
    txt = wxString::Format(L"%03.0f°T %+.0f°R  %s", brg,
                           SignedRelative(brg - g.heading),
                           FormatRange(dist, g.metric));
  wxCoord tw, th;
  dc.GetTextExtent(txt, &tw, &th);
  // Bottom-left, window-fixed: a readout that follows the pointer covers the
  // echoes being measured.
  const wxRect chip(8, GetClientSize().y - th - 12, tw + 12, th + 6);
  LozengeBg(dc, this, chip, (th + 6) / 2, m_theme);
  dc.SetTextForeground(m_theme.text);
  dc.DrawText(txt, chip.x + 6, chip.y + 3);
}

// A press only arms a possible drag; the action is decided on release, so
// dragging the picture around does not also trigger whatever was under the
// press. Matches how radar_pi separates the two.
void RadarDisplayPanel::OnLeftDown(wxMouseEvent& event) {
  m_mouse_down = event.GetPosition();
  m_dragging = false;
  m_drag = wxPoint(0, 0);
  // A zone handle takes precedence over panning: it is a much smaller target,
  // and the press that grabs it would otherwise start dragging the picture.
  m_zone_drag = ZoneHandleHit(m_mouse_down);
  event.Skip();
}

// Slop before a press counts as a drag: below this it is a click with a shaky
// hand, which is common on a boat.
static const int kDragSlop = 6;

void RadarDisplayPanel::OnMotion(wxMouseEvent& event) {
  const wxPoint p = event.GetPosition();
  if (m_zone_drag >= 0 && event.Dragging() && event.LeftIsDown()) {
    DragZoneHandle(p, /*commit=*/false);
    return;
  }
  if (event.Dragging() && event.LeftIsDown()) {
    const wxPoint d(p.x - m_mouse_down.x, p.y - m_mouse_down.y);
    if (!m_dragging && std::abs(d.x) + std::abs(d.y) > kDragSlop)
      m_dragging = true;
    if (m_dragging) {
      m_drag = d;
      Refresh(false);
    }
    return;
  }
  // Cursor readout. A repaint re-renders the whole picture, so hover is
  // throttled to a few pixels of travel -- far below what the readout can
  // resolve, and it keeps a slow drift across the window from pinning a core.
  if (!m_cursor_in || std::abs(p.x - m_cursor.x) + std::abs(p.y - m_cursor.y) >= 3) {
    m_cursor = p;
    m_cursor_in = true;
    Refresh(false);
  }
  event.Skip();
}

void RadarDisplayPanel::OnLeave(wxMouseEvent& event) {
  if (m_cursor_in) {
    m_cursor_in = false;
    Refresh(false);
  }
  event.Skip();
}

void RadarDisplayPanel::OnLeftUp(wxMouseEvent& event) {
  if (m_zone_drag >= 0) {
    // One write per drag, on release: the radar does not need a control
    // update for every mouse move.
    DragZoneHandle(event.GetPosition(), /*commit=*/true);
    m_zone_drag = -1;
    Refresh(false);
    return;
  }
  if (m_dragging) {  // commit the pan; not a click
    m_off_center += m_drag;
    m_drag = wxPoint(0, 0);
    m_dragging = false;
    Refresh(false);
    return;
  }
  HandleClick(event.GetPosition());
  event.Skip();
}

void RadarDisplayPanel::CenterView() {
  m_off_center = wxPoint(0, 0);
  m_drag = wxPoint(0, 0);
  m_display_zoom = 1.0;
  Refresh(false);
}

bool RadarDisplayPanel::IsOffCenter() const {
  return m_off_center.x != 0 || m_off_center.y != 0 || m_drag.x != 0 ||
         m_drag.y != 0 || std::fabs(m_display_zoom - 1.0) > 0.02;
}

void RadarDisplayPanel::HandleClick(const wxPoint& p) {
  if (m_recenter_rect.Contains(p)) {
    CenterView();
  } else if (m_menu_rect.Contains(p)) {
    if (m_on_menu) m_on_menu();
  } else if (m_orient_rect.Contains(p)) {
    SetOrientation((m_orientation + 1) % 3);
    if (m_on_orientation) m_on_orientation(m_orientation);
  } else if (m_alarm_rect.Contains(p)) {
    if (m_alarm_sound_toggle) m_alarm_sound_toggle();
  } else if (m_icon_ais.Contains(p)) {
    m_layers.ais = !m_layers.ais;
    Refresh(false);
  } else if (m_icon_ebl.Contains(p)) {
    // Cycle: off -> arm marker 1 -> arm marker 2 -> off. One icon for both,
    // and an armed marker keeps the other one's placement untouched.
    m_ebl_arm = (m_ebl_arm + 1) % (kVrmEblCount + 1);
    NotifyMarkers();
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
    // A click in the picture places the armed marker. Bearings are stored
    // bow-relative so a marker stays on the target as the boat turns.
    if (m_ebl_arm > 0) {
      double brg = 0, dist = 0;
      if (PointToPolar(p, brg, dist)) {
        const PpiGeometry g = Geometry();
        // Clicking where the marker already is takes it away: placing and
        // clearing are then the same gesture, and the marker itself is the
        // target, which is easier to hit than a button in another panel.
        VrmEbl& cur = m_vrmebl[m_ebl_arm - 1];
        const double geo =
            g.report_m > 0 ? g.zoom / g.report_m * g.radius : 0.0;
        if (cur.enabled && geo > 0) {
          const wxPoint mp =
              PolarPoint(g.center, cur.distance_m * geo,
                         g.heading + cur.bearing_rad * 180.0 / M_PI,
                         g.up_bearing);
          if (std::hypot(p.x - mp.x, p.y - mp.y) <= 10) {
            cur.enabled = false;
            NotifyMarkers();
            if (m_on_focus) m_on_focus();
            return;
          }
        }
        double rel = brg - g.heading;
        while (rel < -180.0) rel += 360.0;
        while (rel > 180.0) rel -= 360.0;
        VrmEbl& m = m_vrmebl[m_ebl_arm - 1];
        m.enabled = true;
        m.bearing_rad = rel * M_PI / 180.0;
        m.distance_m = dist;
        NotifyMarkers();
      }
    }
    if (m_on_focus) m_on_focus();
  }
}

void RadarDisplayPanel::OnLeftDClick(wxMouseEvent& event) {
  const wxPoint p = event.GetPosition();
  // Double-clicking a control/lozenge is not an acquire gesture.
  if (m_menu_rect.Contains(p) || m_orient_rect.Contains(p) ||
      m_alarm_rect.Contains(p) || m_icon_ais.Contains(p) ||
      m_icon_ebl.Contains(p) || m_icon_gain.Contains(p) ||
      m_icon_sea.Contains(p) || m_icon_rain.Contains(p) ||
      m_power_rect.Contains(p) || m_range_minus_rect.Contains(p) ||
      m_range_plus_rect.Contains(p)) {
    event.Skip();
    return;
  }
  if (!m_client) return;

  // Double-clicking on (or very near) a tracked target drops it; empty space
  // acquires a new one.
  const PpiGeometry g = Geometry();
  if (g.valid) {
    for (const RadarTarget& t : m_client->TargetsAt(m_index)) {
      if (t.distance_m <= 0 || t.status == RadarTarget::kLost) continue;
      const double r = g.radius * t.distance_m / g.report_m * g.zoom;
      const wxPoint tp = PolarPoint(g.center, r, t.bearing_deg, g.up_bearing);
      if (std::hypot(p.x - tp.x, p.y - tp.y) <= 12) {
        m_client->CancelTargetAt(m_index, t.id);
        return;
      }
    }
  }

  double bearing_deg = 0, distance_m = 0;
  if (PointToPolar(p, bearing_deg, distance_m))
    m_client->AcquireTargetAt(m_index, bearing_deg, distance_m);
}

bool RadarDisplayPanel::PointToPolar(const wxPoint& p, double& bearing_deg,
                                     double& distance_m) const {
  const PpiGeometry g = Geometry();
  if (!g.valid) return false;

  const double dx = p.x - g.center.x, dy = p.y - g.center.y;
  const double pix = std::sqrt(dx * dx + dy * dy);
  if (pix > g.radius * 1.45) return false;  // outside the visible picture

  // The reported range maps to `radius` at 1x; the free display zoom magnifies
  // about the sweep origin, so undo it to recover the geographic distance.
  distance_m = (pix / (g.radius * g.zoom)) * g.report_m;

  // Inverse of PolarPoint: screen up is `up_bearing`, x grows east.
  bearing_deg = g.up_bearing + std::atan2(dx, -dy) * 180.0 / M_PI;
  while (bearing_deg < 0) bearing_deg += 360;
  while (bearing_deg >= 360) bearing_deg -= 360;
  return true;
}

void RadarDisplayPanel::SetPrefs(const PpiPrefs& p) {
  m_reverse_zoom = p.reverse_zoom;
  const int hz = std::max(1, std::min(15, p.refresh_hz));
  const int ms = 1000 / hz;
  if (!m_timer.IsRunning() || m_timer.GetInterval() != ms) m_timer.Start(ms);
}

void RadarDisplayPanel::OnMouseWheel(wxMouseEvent& event) {
  // Free PPI magnification, independent of the radar range control. Each notch
  // is ~15%; clamped to 0.5x - 5x. A notch back to ~1.0 snaps to exactly 1x.
  const bool up = (event.GetWheelRotation() > 0) != m_reverse_zoom;
  const double factor = up ? 1.15 : 1.0 / 1.15;
  double z = m_display_zoom * factor;
  if (z < 0.5) z = 0.5;
  if (z > 5.0) z = 5.0;
  if (std::fabs(z - 1.0) < 0.05) z = 1.0;  // detent at 1x
  if (z != m_display_zoom) {
    m_display_zoom = z;
    Refresh(false);
  }
}

void RadarDisplayPanel::SetThreshold(int level) {
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  m_threshold = level;
  if (RadarState* s = m_client ? m_client->StateAt(m_index) : nullptr)
    s->SetThreshold(level);
  Refresh(false);
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
