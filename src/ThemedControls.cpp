/******************************************************************************
 * mayara_pi - owner-drawn, theme-aware controls.
 *****************************************************************************/
#include "ThemedControls.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/popupwin.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

namespace {

wxColour Blend(const wxColour& a, const wxColour& b, double t) {
  return wxColour(
      static_cast<unsigned char>(a.Red() + (b.Red() - a.Red()) * t),
      static_cast<unsigned char>(a.Green() + (b.Green() - a.Green()) * t),
      static_cast<unsigned char>(a.Blue() + (b.Blue() - a.Blue()) * t));
}

}  // namespace

// The dropdown list ThemedChoice opens: owner-drawn so it looks like the rest
// of the panel instead of a native popup menu (a checklist on every platform,
// and on Windows in plain system colours no matter what theme is active).
// Not in the anonymous namespace above: ThemedChoice needs to forward-declare
// and hold a pointer to it (see ThemedControls.h for why).
class ThemedChoicePopup : public wxPopupTransientWindow {
 public:
  ThemedChoicePopup(wxWindow* parent, const MayaraTheme& theme,
                    std::vector<wxString> items, int selection,
                    std::function<void(int)> on_pick)
      : wxPopupTransientWindow(parent, wxBORDER_NONE),
        m_theme(theme),
        m_items(std::move(items)),
        m_selection(selection),
        m_on_pick(std::move(on_pick)) {
    m_hover = selection >= 0 ? selection : 0;
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &ThemedChoicePopup::OnPaint, this);
    Bind(wxEVT_MOTION, &ThemedChoicePopup::OnMouseMove, this);
    Bind(wxEVT_LEFT_UP, &ThemedChoicePopup::OnLeftUp, this);
    Bind(wxEVT_MOUSEWHEEL, &ThemedChoicePopup::OnMouseWheel, this);
    Bind(wxEVT_LEAVE_WINDOW, &ThemedChoicePopup::OnLeaveWindow, this);
    // Not Bind(wxEVT_KEY_DOWN, ...): Popup()'s SetFocus() call doesn't
    // reliably move real keyboard focus onto this kind of borderless popup
    // in this embedding (same underlying issue as the outside-click dismiss
    // below) -- ThemedChoice reliably gets the key instead and forwards it
    // to HandleKey() while we're open. See ThemedChoice::OnKeyDown.
    //
    // wxPopupTransientWindow's own dismiss-on-outside-click relies on mouse
    // capture and focus-loss, and this app's owner-drawn controls never call
    // SetFocus() on click the way native ones do -- so that path silently
    // never fires here. Watch the real, global button/cursor state instead:
    // it doesn't care what has capture or focus. A short-interval timer,
    // not wxEVT_IDLE with RequestMore() -- that forces a fresh idle event
    // right after every one it handles, which pins the event loop at 100% of
    // a core for as long as the popup stays open.
    m_watch_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &ThemedChoicePopup::OnWatchTimer, this);
  }

  // Fires exactly once, on every dismissal path -- lets the owning
  // ThemedChoice drop its pointer to this popup before it's gone, so it never
  // holds a dangling one. The bool is true only when OnWatchTimer dismissed
  // us mid-press (the mouse button was still down, just outside): the
  // matching LEFT_UP is still coming and would otherwise reach
  // ThemedChoice::OnClick() and reopen a fresh popup for the same press.
  void SetOnClose(std::function<void(bool reopen_suppressed)> cb) {
    m_on_close = std::move(cb);
  }

  // ThemedChoice forwards its own key events here while we're open -- see the
  // ctor comment on why we don't rely on receiving them directly.
  void HandleKey(wxKeyEvent& e) { OnKeyDown(e); }

  // A user-initiated close (clicking the button again to toggle the list
  // shut): goes through OnDismiss(), unlike the destructor's bare Dismiss(),
  // so it actually gets destroyed rather than just hidden. DismissAndNotify()
  // is protected -- this is the public door to it.
  void RequestClose() { DismissAndNotify(); }

  // Sizes itself to the anchor's width (or the widest item, if wider), directly
  // under the anchor -- or above it, if it wouldn't fit on screen -- then shows
  // itself. Long lists (a radar's range list can run to 20-30 entries) scroll
  // by wheel rather than growing past a fraction of the screen.
  void ShowNear(wxWindow* anchor) {
    wxClientDC dc(anchor);
    dc.SetFont(anchor->GetFont());
    int max_w = 0;
    for (const wxString& s : m_items) {
      wxCoord tw, th;
      dc.GetTextExtent(s, &tw, &th);
      max_w = std::max(max_w, static_cast<int>(tw));
    }
    m_row_h = FromDIP(24);
    const int w = std::max(anchor->GetSize().x, max_w + FromDIP(30));
    const int display_h = wxGetClientDisplayRect().GetHeight();
    const int max_h = std::max(m_row_h, display_h * 3 / 5);
    const int content_h = static_cast<int>(m_items.size()) * m_row_h;
    const int h = std::min(content_h, max_h);
    SetSize(w, h);

    // wxPopupWindowBase::Position() is built for a cascading submenu (it
    // offsets by `size` again on top of `ptOrigin`, and to the right rather
    // than staying left-aligned) -- wrong shape for a dropdown, so this
    // places it directly, left-aligned under the anchor, flipping above and
    // clamping sideways only if it wouldn't otherwise fit on screen.
    const wxPoint top_left = anchor->ClientToScreen(wxPoint(0, 0));
    const wxRect screen = wxDisplay(anchor).GetGeometry();
    wxPoint pos(top_left.x, top_left.y + anchor->GetSize().y);
    if (pos.y + h > screen.GetBottom()) {
      const int above_y = top_left.y - h;
      if (above_y >= screen.GetTop()) pos.y = above_y;
    }
    pos.x = std::clamp(pos.x, screen.GetLeft(),
                       std::max(screen.GetLeft(), screen.GetRight() - w));
    Move(pos);
    Popup();
    m_watch_timer.Start(60);
  }

 protected:
  // Reached both from our own DismissAndNotify() calls below and from the
  // base class's own (unreliable, here) dismiss paths -- idempotent so
  // whichever gets there first doesn't leave the other to double-destroy.
  void OnDismiss() override {
    if (m_dismissed) return;
    m_dismissed = true;
    m_watch_timer.Stop();
    if (wxWindow* p = GetParent()) p->SetFocus();  // back to the closed button
    if (m_on_close) m_on_close(m_reopen_suppressed);
    Destroy();
  }

 private:
  int RowAt(const wxPoint& pt) const {
    const int i = (pt.y + m_scroll) / m_row_h;
    return (i >= 0 && i < static_cast<int>(m_items.size())) ? i : -1;
  }

  void SetHover(int i) {
    if (m_items.empty()) return;
    i = std::clamp(i, 0, static_cast<int>(m_items.size()) - 1);
    if (i == m_hover) return;
    m_hover = i;
    EnsureVisible(i);
    Refresh();
  }

  void EnsureVisible(int i) {
    const int y = i * m_row_h;
    const int ch = GetClientSize().y;
    if (y < m_scroll)
      m_scroll = y;
    else if (y + m_row_h > m_scroll + ch)
      m_scroll = y + m_row_h - ch;
  }

  void Pick(int i) {
    // Deferred: cb(i) fires wxEVT_CHOICE, and a handler for that can reach
    // ControlsPanel::Rebuild() (e.g. the radar selector), whose
    // DestroyChildren() deletes this popup and its owning ThemedChoice
    // immediately -- while this is still a frame on the call stack (inside
    // one of THIS object's own event handlers). Running cb after unwinding
    // means nothing is still executing on freed memory when that happens.
    std::function<void(int)> cb = m_on_pick;
    DismissAndNotify();
    if (i >= 0 && cb) {
      if (wxWindow* p = GetParent())
        p->CallAfter([cb, i]() { cb(i); });
      else
        cb(i);
    }
  }

  void OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    const wxSize cs = GetClientSize();
    dc.SetPen(wxPen(m_theme.lozenge_border));
    dc.SetBrush(wxBrush(m_theme.panel_bg));
    dc.DrawRectangle(0, 0, cs.x, cs.y);
    dc.SetFont(GetFont());
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
      const int y = i * m_row_h - m_scroll;
      if (y + m_row_h < 0 || y > cs.y) continue;
      if (i == m_hover) {
        dc.SetBrush(wxBrush(Blend(m_theme.panel_bg, m_theme.text, 0.15)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, y, cs.x, m_row_h);
      }
      dc.SetTextForeground(m_theme.text);
      wxCoord tw, th;
      dc.GetTextExtent(m_items[i], &tw, &th);
      const int ty = y + (m_row_h - th) / 2;
      // Explicit code point, not a literal "✓": MSVC decodes the source with
      // the active code page unless /utf-8 is passed, which mangles it.
      if (i == m_selection)
        dc.DrawText(wxString(wxUniChar(0x2713)), FromDIP(8), ty);
      dc.DrawText(m_items[i], FromDIP(24), ty);
    }
  }

  void OnMouseMove(wxMouseEvent& e) {
    const int i = RowAt(e.GetPosition());
    if (i != m_hover) {
      m_hover = i;
      Refresh();
    }
  }

  void OnLeaveWindow(wxMouseEvent&) {
    if (m_hover != -1) {
      m_hover = -1;
      Refresh();
    }
  }

  void OnLeftUp(wxMouseEvent& e) { Pick(RowAt(e.GetPosition())); }

  void OnMouseWheel(wxMouseEvent& e) {
    const int content_h = static_cast<int>(m_items.size()) * m_row_h;
    const int max_scroll = std::max(0, content_h - GetClientSize().y);
    if (max_scroll <= 0) return;
    const int notches = e.GetWheelRotation() / std::max(1, e.GetWheelDelta());
    m_scroll = std::clamp(m_scroll - notches * m_row_h, 0, max_scroll);
    Refresh();
  }

  void OnKeyDown(wxKeyEvent& e) {
    switch (e.GetKeyCode()) {
      case WXK_ESCAPE:
        DismissAndNotify();
        return;
      case WXK_UP:
        SetHover(m_hover - 1);
        return;
      case WXK_DOWN:
        SetHover(m_hover + 1);
        return;
      case WXK_HOME:
        SetHover(0);
        return;
      case WXK_END:
        SetHover(static_cast<int>(m_items.size()) - 1);
        return;
      case WXK_RETURN:
      case WXK_NUMPAD_ENTER:
      case WXK_SPACE:
        Pick(m_hover);
        return;
      default:
        break;
    }
    // Typeahead: jump to the next item starting with the typed letter.
    const int uc = e.GetUnicodeKey();
    if (uc != WXK_NONE && !m_items.empty()) {
      const wxUniChar want = wxToupper(wxUniChar(uc));
      for (size_t n = 1; n <= m_items.size(); ++n) {
        const size_t i = (m_hover + n) % m_items.size();
        if (!m_items[i].empty() && wxToupper(m_items[i][0]) == want) {
          SetHover(static_cast<int>(i));
          break;
        }
      }
      return;
    }
    e.Skip();
  }

  // wxPopupTransientWindow's own outside-click dismissal depends on mouse
  // capture (only wired on macOS by the base class) and on focus loss (which
  // never happens here -- see the comment in the ctor); watching the real
  // global button state sidesteps both and works the same on every platform.
  void OnWatchTimer(wxTimerEvent&) {
    if (wxGetMouseState().LeftIsDown() &&
        !GetScreenRect().Contains(wxGetMousePosition())) {
      // The button that's down now still owes a LEFT_UP somewhere -- if the
      // cursor is back over the anchor by then, that event reaches
      // ThemedChoice::OnClick() and would otherwise open a brand new popup
      // for what the user experiences as one continuous press.
      m_reopen_suppressed = true;
      DismissAndNotify();
    }
  }

  MayaraTheme m_theme;
  std::vector<wxString> m_items;
  int m_selection;
  int m_hover = -1;
  wxTimer m_watch_timer;
  int m_row_h = 24;
  int m_scroll = 0;
  bool m_dismissed = false;
  bool m_reopen_suppressed = false;
  std::function<void(int)> m_on_pick;
  std::function<void(bool)> m_on_close;
};

// ---------------------------------------------------------------- ThemedButton
ThemedButton::ThemedButton(wxWindow* parent, const wxString& label,
                           const MayaraTheme& theme, bool toggle)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE),
      m_theme(theme),
      m_toggle(toggle) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  // Before SetLabel() below measures text against it, so it's sized for the
  // font it will actually be painted in.
  if (theme.menu_font_pt > 0) {
    wxFont f = GetFont();
    f.SetPointSize(theme.menu_font_pt);
    SetFont(f);
  }
  SetLabel(label);  // sets the min size from the text; see below
  Bind(wxEVT_PAINT, &ThemedButton::OnPaint, this);
  // On LEFT_UP, not LEFT_DOWN -- same reasoning as ThemedChoice's popup
  // below: firing mid-click let the close button's own action (hiding the
  // Controls panel) expose whatever sits underneath before this same
  // click's LEFT_UP arrived, which the PPI window's radar picture read as a
  // click on its own hamburger menu hitbox at that spot -- reopening the
  // panel that had just been closed.
  Bind(wxEVT_LEFT_UP, &ThemedButton::OnClick, this);
}

// The min size follows the label rather than being fixed at construction:
// "Edit" turns into "Cancel" in a guard zone, and measuring only the first
// one left the wider label squeezed into the narrower one's width.
void ThemedButton::SetLabel(const wxString& label) {
  wxControl::SetLabel(label);
  wxCoord tw, th;
  GetTextExtent(label, &tw, &th);  // uses the window font; no DC needed
  SetMinSize(wxSize(tw + FromDIP(14), th + FromDIP(12)));
  Refresh();
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
  dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, FromDIP(5));
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
  if (theme.menu_font_pt > 0) {
    wxFont f = GetFont();
    f.SetPointSize(theme.menu_font_pt);
    SetFont(f);
  }
  SetMinSize(wxSize(FromDIP(90), FromDIP(24)));
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
  const int m = FromDIP(9), ty = sz.y / 2;
  const int x0 = m, x1 = sz.x - m;
  if (x1 <= x0) return;
  const int pos = x0 + (x1 - x0) * m_value / 1000;
  const bool en = IsEnabled();
  dc.SetPen(wxPen(m_theme.lozenge_border, FromDIP(3)));
  dc.DrawLine(x0, ty, x1, ty);
  dc.SetPen(wxPen(en ? m_theme.text : m_theme.dim_text, FromDIP(3)));
  dc.DrawLine(x0, ty, pos, ty);
  dc.SetBrush(wxBrush(en ? m_theme.text : m_theme.dim_text));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawCircle(pos, ty, FromDIP(6));
}

void ThemedSlider::SetFromX(int x, bool dragging) {
  const wxSize sz = GetClientSize();
  const int m = FromDIP(9), x0 = m, x1 = sz.x - m;
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
                wxBORDER_NONE | wxWANTS_CHARS),
      m_theme(theme) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  // Before SetMinSize below and any later Append(), both of which measure
  // text against the window's current font.
  if (theme.menu_font_pt > 0) {
    wxFont f = GetFont();
    f.SetPointSize(theme.menu_font_pt);
    SetFont(f);
  }
  SetMinSize(wxSize(FromDIP(60), FromDIP(26)));
  Bind(wxEVT_PAINT, &ThemedChoice::OnPaint, this);
  // On LEFT_UP, not LEFT_DOWN: opening the popup mid-click left it holding
  // mouse capture when the same click's LEFT_UP arrived a moment later, which
  // it read as a click on whatever row happened to be under the pointer --
  // dismissing the list before the user had a chance to look at it.
  Bind(wxEVT_LEFT_UP, &ThemedChoice::OnClick, this);
  Bind(wxEVT_KEY_DOWN, &ThemedChoice::OnKeyDown, this);
  Bind(wxEVT_SET_FOCUS, &ThemedChoice::OnFocusChange, this);
  Bind(wxEVT_KILL_FOCUS, &ThemedChoice::OnFocusChange, this);
}

ThemedChoice::~ThemedChoice() {
  // Bare Dismiss(), not ClosePopup()'s RequestClose(): that runs
  // OnDismiss(), which would SetFocus() this control and fire m_on_close's
  // callback into it -- both unsafe while this destructor is running.
  if (m_open_popup) {
    m_open_popup->Dismiss();
    m_open_popup = nullptr;
  }
}

void ThemedChoice::SetTheme(const MayaraTheme& t) {
  m_theme = t;
  Refresh();
}

void ThemedChoice::Append(const wxString& label, int data) {
  m_items.push_back({label, data});
  wxCoord tw, th;
  GetTextExtent(label, &tw, &th);
  const wxSize cur = GetMinSize();
  SetMinSize(wxSize(std::max(cur.x, tw + FromDIP(34)),
                    std::max<int>(cur.y, th + FromDIP(12))));
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
  dc.SetPen(wxPen(HasFocus() ? m_theme.text : m_theme.lozenge_border,
                  HasFocus() ? 2 : 1));
  dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, FromDIP(5));
  dc.SetTextForeground(IsEnabled() ? m_theme.text : m_theme.dim_text);
  dc.SetFont(GetFont());
  const wxString label =
      (m_selection >= 0 && m_selection < static_cast<int>(m_items.size()))
          ? m_items[m_selection].label
          : wxString();
  wxCoord tw, th;
  dc.GetTextExtent(label, &tw, &th);
  dc.DrawText(label, FromDIP(8), (sz.y - th) / 2);
  // disclosure triangle
  const int ax = sz.x - FromDIP(14), ay = sz.y / 2;
  wxPoint tri[3] = {{ax - FromDIP(4), ay - FromDIP(2)},
                    {ax + FromDIP(4), ay - FromDIP(2)},
                    {ax, ay + FromDIP(3)}};
  dc.SetBrush(wxBrush(m_theme.text));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawPolygon(3, tri);
}

void ThemedChoice::OnClick(wxMouseEvent&) {
  SetFocus();
  if (m_suppress_next_open) {
    // This LEFT_UP belongs to the same press whose LEFT_DOWN the popup's
    // watchdog already reacted to by dismissing -- not a fresh click.
    m_suppress_next_open = false;
    return;
  }
  OpenPopup();
}

void ThemedChoice::OpenPopup() {
  if (!IsEnabled() || m_items.empty()) return;
  if (m_open_popup) {  // clicked/pressed again while open: toggle it closed
    ClosePopup();
    return;
  }
  std::vector<wxString> labels;
  labels.reserve(m_items.size());
  for (const Item& it : m_items) labels.push_back(it.label);
  auto* popup = new ThemedChoicePopup(
      this, m_theme, std::move(labels), m_selection, [this](int i) {
        SetSelection(i);
        wxCommandEvent e(wxEVT_CHOICE, GetId());
        e.SetEventObject(this);
        e.SetInt(m_selection);
        ProcessWindowEvent(e);
      });
  m_open_popup = popup;
  popup->SetOnClose([this](bool reopen_suppressed) {
    m_open_popup = nullptr;
    if (reopen_suppressed) m_suppress_next_open = true;
  });
  popup->ShowNear(this);
}

// A user-initiated close (toggling the button while the list is open). Goes
// through RequestClose() -> OnDismiss(), so the popup is actually destroyed,
// not just hidden -- unlike the destructor's own bare Dismiss(), below, which
// this control being mid-teardown makes unsafe to route the same way.
void ThemedChoice::ClosePopup() {
  if (!m_open_popup) return;
  m_open_popup->RequestClose();
  m_open_popup = nullptr;
}

void ThemedChoice::MoveSelection(int delta) {
  if (m_items.empty()) return;
  const int i = std::clamp(m_selection + delta, 0,
                           static_cast<int>(m_items.size()) - 1);
  if (i == m_selection) return;
  SetSelection(i);
  wxCommandEvent e(wxEVT_CHOICE, GetId());
  e.SetEventObject(this);
  e.SetInt(m_selection);
  ProcessWindowEvent(e);
}

void ThemedChoice::OnKeyDown(wxKeyEvent& e) {
  if (m_open_popup) {
    // The list is open: Up/Down/Home/End/typing move its highlighted row,
    // Enter/Space pick it, Escape cancels -- none of that should reach the
    // closed-control cases below (which would change the value immediately
    // instead of just moving the highlight).
    m_open_popup->HandleKey(e);
    return;
  }
  switch (e.GetKeyCode()) {
    case WXK_UP:
      MoveSelection(-1);
      return;
    case WXK_DOWN:
      MoveSelection(1);
      return;
    case WXK_SPACE:
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
    case WXK_F4:
      OpenPopup();
      return;
    default:
      e.Skip();
  }
}

void ThemedChoice::OnFocusChange(wxFocusEvent& e) {
  Refresh();
  e.Skip();
}
