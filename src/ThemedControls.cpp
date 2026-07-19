/******************************************************************************
 * mayara_pi - owner-drawn, theme-aware controls.
 *****************************************************************************/
#include "ThemedControls.h"

#include <algorithm>

#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/tglbtn.h>

namespace {

wxColour Blend(const wxColour& a, const wxColour& b, double t) {
  return wxColour(
      static_cast<unsigned char>(a.Red() + (b.Red() - a.Red()) * t),
      static_cast<unsigned char>(a.Green() + (b.Green() - a.Green()) * t),
      static_cast<unsigned char>(a.Blue() + (b.Blue() - a.Blue()) * t));
}

}  // namespace

// ---------------------------------------------------------------- ThemedButton
ThemedButton::ThemedButton(wxWindow* parent, const wxString& label,
                           const MayaraTheme& theme, bool toggle)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE),
      m_theme(theme),
      m_toggle(toggle) {
  SetLabel(label);
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  wxClientDC dc(this);
  dc.SetFont(GetFont());
  wxCoord tw, th;
  dc.GetTextExtent(label, &tw, &th);
  SetMinSize(wxSize(tw + 22, th + 12));
  Bind(wxEVT_PAINT, &ThemedButton::OnPaint, this);
  Bind(wxEVT_LEFT_DOWN, &ThemedButton::OnClick, this);
}

void ThemedButton::SetTheme(const MayaraTheme& t) {
  m_theme = t;
  Refresh();
}

void ThemedButton::SetValue(bool v) {
  m_pressed = v;
  Refresh();
}

void ThemedButton::OnPaint(wxPaintEvent&) {
  wxPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(wxBrush(m_theme.panel_bg));
  dc.Clear();
  const bool active = m_pressed;  // set via toggle or SetValue()
  const wxColour bg =
      active ? (m_has_active ? m_active : Blend(m_theme.lozenge_bg, m_theme.text,
                                                0.35))
             : m_theme.lozenge_bg;
  dc.SetBrush(wxBrush(bg));
  dc.SetPen(wxPen(active ? m_theme.text : m_theme.lozenge_border));
  dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, 5);
  dc.SetTextForeground(IsEnabled() ? m_theme.text : m_theme.dim_text);
  dc.SetFont(GetFont());
  wxCoord tw, th;
  dc.GetTextExtent(GetLabel(), &tw, &th);
  dc.DrawText(GetLabel(), (sz.x - tw) / 2, (sz.y - th) / 2);
}

void ThemedButton::OnClick(wxMouseEvent&) {
  if (!IsEnabled()) return;
  if (m_toggle) {
    m_pressed = !m_pressed;
    Refresh();
    wxCommandEvent e(wxEVT_TOGGLEBUTTON, GetId());
    e.SetEventObject(this);
    e.SetInt(m_pressed);
    ProcessWindowEvent(e);
  } else {
    wxCommandEvent e(wxEVT_BUTTON, GetId());
    e.SetEventObject(this);
    ProcessWindowEvent(e);
  }
}

// ---------------------------------------------------------------- ThemedSlider
ThemedSlider::ThemedSlider(wxWindow* parent, const MayaraTheme& theme)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE),
      m_theme(theme) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(90, 24));
  Bind(wxEVT_PAINT, &ThemedSlider::OnPaint, this);
  Bind(wxEVT_LEFT_DOWN, &ThemedSlider::OnMouse, this);
  Bind(wxEVT_LEFT_UP, &ThemedSlider::OnMouse, this);
  Bind(wxEVT_MOTION, &ThemedSlider::OnMouse, this);
  Bind(wxEVT_MOUSE_CAPTURE_LOST,
       [this](wxMouseCaptureLostEvent&) { m_dragging = false; });
}

void ThemedSlider::SetTheme(const MayaraTheme& t) {
  m_theme = t;
  Refresh();
}

void ThemedSlider::SetValue(int v) {
  m_value = std::max(0, std::min(1000, v));
  if (!m_dragging) Refresh();
}

void ThemedSlider::OnPaint(wxPaintEvent&) {
  wxPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(wxBrush(m_theme.panel_bg));
  dc.Clear();
  const int m = 9, ty = sz.y / 2;
  const int x0 = m, x1 = sz.x - m;
  if (x1 <= x0) return;
  const int pos = x0 + (x1 - x0) * m_value / 1000;
  const bool en = IsEnabled();
  dc.SetPen(wxPen(m_theme.lozenge_border, 3));
  dc.DrawLine(x0, ty, x1, ty);
  dc.SetPen(wxPen(en ? m_theme.text : m_theme.dim_text, 3));
  dc.DrawLine(x0, ty, pos, ty);
  dc.SetBrush(wxBrush(en ? m_theme.text : m_theme.dim_text));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawCircle(pos, ty, 6);
}

void ThemedSlider::SetFromX(int x, bool dragging) {
  const wxSize sz = GetClientSize();
  const int m = 9, x0 = m, x1 = sz.x - m;
  if (x1 <= x0) return;
  m_value = std::max(0, std::min(1000, (x - x0) * 1000 / (x1 - x0)));
  Refresh();
  wxScrollEvent e(dragging ? wxEVT_SCROLL_THUMBTRACK : wxEVT_SCROLL_THUMBRELEASE,
                  GetId());
  e.SetEventObject(this);
  e.SetPosition(m_value);
  ProcessWindowEvent(e);
}

void ThemedSlider::OnMouse(wxMouseEvent& event) {
  if (!IsEnabled()) return;
  if (event.LeftDown()) {
    m_dragging = true;
    if (!HasCapture()) CaptureMouse();
    SetFromX(event.GetX(), true);
  } else if (event.Dragging() && m_dragging && event.LeftIsDown()) {
    SetFromX(event.GetX(), true);
  } else if (event.LeftUp() && m_dragging) {
    m_dragging = false;
    if (HasCapture()) ReleaseMouse();
    SetFromX(event.GetX(), false);
  }
}

// ---------------------------------------------------------------- ThemedChoice
ThemedChoice::ThemedChoice(wxWindow* parent, const MayaraTheme& theme)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE),
      m_theme(theme) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(60, 26));
  Bind(wxEVT_PAINT, &ThemedChoice::OnPaint, this);
  Bind(wxEVT_LEFT_DOWN, &ThemedChoice::OnClick, this);
}

void ThemedChoice::SetTheme(const MayaraTheme& t) {
  m_theme = t;
  Refresh();
}

void ThemedChoice::Append(const wxString& label, int data) {
  m_items.push_back({label, data});
  wxClientDC dc(this);
  dc.SetFont(GetFont());
  wxCoord tw, th;
  dc.GetTextExtent(label, &tw, &th);
  const wxSize cur = GetMinSize();
  SetMinSize(wxSize(std::max(cur.x, tw + 34), std::max<int>(cur.y, th + 12)));
}

void ThemedChoice::Clear() {
  m_items.clear();
  m_selection = -1;
  Refresh();
}

void ThemedChoice::SetSelection(int i) {
  m_selection = i;
  Refresh();
}

int ThemedChoice::GetItemData(int i) const {
  return (i >= 0 && i < static_cast<int>(m_items.size())) ? m_items[i].data : 0;
}

void ThemedChoice::OnPaint(wxPaintEvent&) {
  wxPaintDC dc(this);
  const wxSize sz = GetClientSize();
  dc.SetBackground(wxBrush(m_theme.panel_bg));
  dc.Clear();
  dc.SetBrush(wxBrush(m_theme.lozenge_bg));
  dc.SetPen(wxPen(m_theme.lozenge_border));
  dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, 5);
  dc.SetTextForeground(IsEnabled() ? m_theme.text : m_theme.dim_text);
  dc.SetFont(GetFont());
  const wxString label =
      (m_selection >= 0 && m_selection < static_cast<int>(m_items.size()))
          ? m_items[m_selection].label
          : wxString();
  wxCoord tw, th;
  dc.GetTextExtent(label, &tw, &th);
  dc.DrawText(label, 8, (sz.y - th) / 2);
  // disclosure triangle
  const int ax = sz.x - 14, ay = sz.y / 2;
  wxPoint tri[3] = {{ax - 4, ay - 2}, {ax + 4, ay - 2}, {ax, ay + 3}};
  dc.SetBrush(wxBrush(m_theme.text));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawPolygon(3, tri);
}

void ThemedChoice::OnClick(wxMouseEvent&) {
  if (!IsEnabled() || m_items.empty()) return;
  wxMenu menu;
  for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
    menu.AppendCheckItem(1000 + i, m_items[i].label);
  if (m_selection >= 0) menu.Check(1000 + m_selection, true);
  const int sel = GetPopupMenuSelectionFromUser(menu);
  if (sel == wxID_NONE) return;
  SetSelection(sel - 1000);
  wxCommandEvent e(wxEVT_CHOICE, GetId());
  e.SetEventObject(this);
  e.SetInt(m_selection);
  ProcessWindowEvent(e);
}
