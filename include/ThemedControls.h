/******************************************************************************
 * mayara_pi - owner-drawn, theme-aware controls.
 *
 * Native macOS controls ignore custom colours, so the control panel uses these
 * instead. They emit the same wx events as their native counterparts so the
 * panel logic is largely unchanged:
 *   ThemedButton  -> wxEVT_BUTTON, or wxEVT_TOGGLEBUTTON when toggle=true
 *   ThemedSlider  -> wxEVT_SCROLL_THUMBTRACK / _THUMBRELEASE (value 0..1000)
 *   ThemedChoice  -> wxEVT_CHOICE
 *****************************************************************************/
#ifndef MAYARA_THEMED_CONTROLS_H_
#define MAYARA_THEMED_CONTROLS_H_

#include <vector>

#include <wx/control.h>
#include <wx/wx.h>

#include "MayaraTheme.h"

class ThemedButton : public wxControl {
 public:
  ThemedButton(wxWindow* parent, const wxString& label, const MayaraTheme& theme,
               bool toggle = false);
  void SetTheme(const MayaraTheme& t);
  bool GetValue() const { return m_pressed; }       // toggle state
  void SetValue(bool v);
  void SetActiveColour(const wxColour& c) { m_active = c; m_has_active = true; }

 private:
  void OnPaint(wxPaintEvent&);
  void OnClick(wxMouseEvent&);

  MayaraTheme m_theme;
  bool m_toggle;
  bool m_pressed = false;
  bool m_has_active = false;
  wxColour m_active;
};

class ThemedSlider : public wxControl {
 public:
  ThemedSlider(wxWindow* parent, const MayaraTheme& theme);  // range 0..1000
  void SetTheme(const MayaraTheme& t);
  int GetValue() const { return m_value; }
  void SetValue(int v);

 private:
  void OnPaint(wxPaintEvent&);
  void OnMouse(wxMouseEvent&);
  void SetFromX(int x, bool dragging);

  MayaraTheme m_theme;
  int m_value = 0;
  bool m_dragging = false;
};

class ThemedChoice : public wxControl {
 public:
  ThemedChoice(wxWindow* parent, const MayaraTheme& theme);
  void SetTheme(const MayaraTheme& t);
  void Append(const wxString& label, int data = 0);
  void Clear();
  int GetCount() const { return static_cast<int>(m_items.size()); }
  int GetSelection() const { return m_selection; }
  void SetSelection(int i);
  int GetItemData(int i) const;

 private:
  void OnPaint(wxPaintEvent&);
  void OnClick(wxMouseEvent&);

  struct Item {
    wxString label;
    int data;
  };
  MayaraTheme m_theme;
  std::vector<Item> m_items;
  int m_selection = -1;
};

#endif  // MAYARA_THEMED_CONTROLS_H_
