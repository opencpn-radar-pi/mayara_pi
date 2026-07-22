/******************************************************************************
 * mayara_pi - schema-driven control panel.
 *****************************************************************************/
#include "ControlsPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include <wx/dcclient.h>
#include <wx/statline.h>
#include <wx/tglbtn.h>

#include "MayaraClient.h"
#include "ThemedControls.h"

enum { kControlsTimerId = wxID_HIGHEST + 20 };

namespace {

std::string Num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

std::string BodyValueAuto(double v, bool has_auto, bool a) {
  std::string s = "{\"value\":" + Num(v);
  if (has_auto) s += a ? ",\"auto\":true" : ",\"auto\":false";
  return s + "}";
}
std::string BodyValue(double v) { return "{\"value\":" + Num(v) + "}"; }
std::string BodyAuto(bool a) {
  return std::string("{\"auto\":") + (a ? "true" : "false") + "}";
}

// Human-friendly value string. SI on the wire; convert a couple of common units
// for display.
wxString FormatVal(double value, const std::string& units) {
  if (units == "rad")
    return wxString::Format("%.0f°", value * 180.0 / M_PI);
  if (units == "m") {
    if (value >= 1852.0)
      return wxString::Format("%.2f NM", value / 1852.0);
    return wxString::Format("%.0f m", value);
  }
  if (units == "s") return wxString::Format("%.0f s", value);
  return wxString::FromUTF8(Num(value).c_str());
}

int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Sector/zone angles travel as radians over -180..180 degrees; the slider is
// 0..1000. Distances are metres over 0..maxDist.
double SliderToDeg(int s) { return -180.0 + (s / 1000.0) * 360.0; }
int DegToSlider(double deg) {
  return Clampi(static_cast<int>(std::lround((deg + 180.0) / 360.0 * 1000.0)),
                0, 1000);
}
double RadToDeg(double r) { return r * 180.0 / M_PI; }
double DegToRad(double d) { return d * M_PI / 180.0; }
long RoundL(double v) { return std::lround(v); }

// Recursively apply the theme: panel-coloured background everywhere, themed
// text on labels. (Native controls on macOS may ignore colour changes.)
void ThemeWindow(wxWindow* w, const MayaraTheme& t) {
  w->SetBackgroundColour(t.panel_bg);
  if (wxDynamicCast(w, wxStaticText)) w->SetForegroundColour(t.text);
  for (wxWindow* c : w->GetChildren()) ThemeWindow(c, t);
}

// Owner-drawn collapsible section header: a themed bar with a disclosure
// triangle and title; clicking fires a callback.
class SectionHeader : public wxPanel {
 public:
  SectionHeader(wxWindow* parent, const wxString& title,
                const MayaraTheme& theme)
      : wxPanel(parent, wxID_ANY), m_title(title), m_theme(theme) {
    SetMinSize(wxSize(-1, 24));
    Bind(wxEVT_PAINT, &SectionHeader::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN,
         [this](wxMouseEvent&) { if (m_onclick) m_onclick(); });
  }
  void SetCollapsed(bool c) { m_collapsed = c; Refresh(); }
  void SetOnClick(std::function<void()> f) { m_onclick = std::move(f); }

 private:
  void OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    const wxSize sz = GetClientSize();
    dc.SetBackground(wxBrush(m_theme.lozenge_bg));
    dc.Clear();
    const int cy = sz.y / 2, cx = 10;
    dc.SetBrush(wxBrush(m_theme.text));
    dc.SetPen(*wxTRANSPARENT_PEN);
    wxPoint tri[3];
    if (m_collapsed) {
      tri[0] = wxPoint(cx - 3, cy - 5);
      tri[1] = wxPoint(cx - 3, cy + 5);
      tri[2] = wxPoint(cx + 4, cy);
    } else {
      tri[0] = wxPoint(cx - 5, cy - 3);
      tri[1] = wxPoint(cx + 5, cy - 3);
      tri[2] = wxPoint(cx, cy + 4);
    }
    dc.DrawPolygon(3, tri);
    wxFont f = GetFont();
    f.MakeBold();
    dc.SetFont(f);
    dc.SetTextForeground(m_theme.text);
    wxCoord tw, th;
    dc.GetTextExtent(m_title, &tw, &th);
    dc.DrawText(m_title, 22, (sz.y - th) / 2);
  }

  wxString m_title;
  MayaraTheme m_theme;
  bool m_collapsed = true;
  std::function<void()> m_onclick;
};

}  // namespace

wxBEGIN_EVENT_TABLE(ControlsPanel, wxScrolledWindow)
    EVT_TIMER(kControlsTimerId, ControlsPanel::OnTimer)
wxEND_EVENT_TABLE()

ControlsPanel::ControlsPanel(wxWindow* parent, MayaraClient* client,
                             int radar_index)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL),
      m_client(client),
      m_index(radar_index),
      m_timer(this, kControlsTimerId) {
  SetMinSize(wxSize(300, -1));
  SetScrollRate(0, 12);
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(MakeCloseRow(), 0, wxEXPAND);
  sizer->Add(new wxStaticText(this, wxID_ANY, _("Waiting for radar…")), 0,
             wxALL, 8);
  SetSizer(sizer);
  m_timer.Start(400);
}

wxSizer* ControlsPanel::MakeCloseRow() {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* title = new wxStaticText(this, wxID_ANY, _("Controls"));
  wxFont f = title->GetFont();
  f.MakeBold();
  title->SetFont(f);
  row->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
  auto* gear = new ThemedButton(this, wxT("⚙"), m_theme, /*toggle=*/false);
  gear->SetToolTip(_("Settings"));
  gear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (m_on_settings) m_on_settings();
  });
  row->Add(gear, 0, wxTOP | wxBOTTOM | wxLEFT, 4);
  auto* close = new ThemedButton(this, wxT("✕"), m_theme, /*toggle=*/false);
  close->SetToolTip(_("Hide controls"));
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (m_on_close) m_on_close();
  });
  row->Add(close, 0, wxALL, 4);
  return row;
}

void ControlsPanel::Set(const std::string& id, const std::string& body) {
  if (m_client) m_client->SetControlAt(m_index, id, body);
}

RadarControls* ControlsPanel::controls() {
  return m_client ? m_client->ControlsAt(m_index) : nullptr;
}

void ControlsPanel::SetRadarIndex(int index) {
  if (index == m_index) return;
  m_index = index;
  m_schema_gen = ~0ull;  // force a rebuild against the new radar's schema
  if (m_built) Rebuild();
}

void ControlsPanel::SetViewMode(bool view_only) {
  if (view_only == m_view_only && m_single_id.empty()) return;
  m_view_only = view_only;
  m_single_id.clear();
  if (m_built) Rebuild();
}

void ControlsPanel::SetSingleControl(const std::string& id) {
  if (id == m_single_id && !m_view_only) return;
  m_single_id = id;
  m_view_only = false;
  if (m_built) Rebuild();
}

void ControlsPanel::ApplyTheme(const MayaraTheme& theme) {
  m_theme = theme;
  if (m_built)
    Rebuild();  // re-theme owner-drawn section headers too
  else
    ThemeChildren();
  Refresh();
}

void ControlsPanel::ThemeChildren() {
  SetBackgroundColour(m_theme.panel_bg);
  for (wxWindow* c : GetChildren()) ThemeWindow(c, m_theme);
}

void ControlsPanel::OnTimer(wxTimerEvent&) {
  RadarControls* c = controls();
  if (!c || !c->HasSchema()) return;
  const uint64_t sgen = c->SchemaGeneration();
  if (!m_built || sgen != m_schema_gen) {
    Rebuild();  // first build, or the bound radar's schema changed
    m_built = true;
    m_schema_gen = sgen;
    m_last_gen = c->Generation();
    return;
  }
  const uint64_t gen = c->Generation();
  if (gen != m_last_gen) {
    m_last_gen = gen;
    ApplyValues();
  }
}

void ControlsPanel::ApplyValues() {
  if (!m_client || !controls()) return;
  for (auto& u : m_updaters) u();
}

void ControlsPanel::Rebuild() {
  DestroyChildren();
  m_updaters.clear();

  RadarControls* c = controls();
  if (!c) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(MakeCloseRow(), 0, wxEXPAND);
    SetSizer(sizer);
    return;
  }
  std::vector<ControlDef> defs = c->Schema();
  std::vector<int> ranges = c->SupportedRanges();

  std::map<std::string, const ControlDef*> by_id;
  for (const auto& d : defs) by_id[d.id] = &d;

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(MakeCloseRow(), 0, wxEXPAND);

  // Single-control mode: just one control (opened by a gauge icon).
  if (!m_single_id.empty()) {
    for (const auto& d : defs)
      if (d.id == m_single_id) {
        AddControl(root, d);
        break;
      }
    SetSizer(root);
    FitInside();
    Layout();
    ThemeChildren();
    ApplyValues();
    return;
  }

  // View-only mode: just the View controls (opened by the View icon).
  if (m_view_only) {
    auto* content = new wxBoxSizer(wxVERTICAL);
    FillViewSection(content);
    root->Add(content, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
    SetSizer(root);
    FitInside();
    Layout();
    ThemeChildren();
    ApplyValues();
    return;
  }

  // Radar selector: switches which of this window's radars these controls
  // drive. Shown only when the window hosts more than one radar.
  if (m_radar_list.size() > 1) {
    std::vector<std::string> names = m_client->RadarNames();
    root->Add(new wxStaticText(this, wxID_ANY, _("Radar")), 0, wxLEFT | wxTOP,
              4);
    auto* sel = new ThemedChoice(this, m_theme);
    int selected = 0;
    for (size_t i = 0; i < m_radar_list.size(); ++i) {
      const int ri = m_radar_list[i];
      const wxString nm = (ri >= 0 && ri < static_cast<int>(names.size()))
                              ? wxString::FromUTF8(names[ri].c_str())
                              : wxString::Format("Radar %d", ri);
      sel->Append(nm, ri);
      if (ri == m_index) selected = static_cast<int>(i);
    }
    sel->SetSelection(selected);
    sel->Bind(wxEVT_CHOICE, [this, sel](wxCommandEvent&) {
      int s = sel->GetSelection();
      if (s != wxNOT_FOUND) SetRadarIndex(sel->GetItemData(s));
    });
    root->Add(sel, 0, wxEXPAND | wxALL, 4);
  }

  // --- Quick controls: prominent, fixed placement ---
  if (by_id.count("power")) AddEnum(root, *by_id["power"], /*buttons=*/true);
  if (by_id.count("range")) AddRange(root, *by_id["range"], ranges);
  if (by_id.count("rangeUnits"))
    AddEnum(root, *by_id["rangeUnits"], /*buttons=*/true);
  root->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 4);

  // (The View controls live in their own menu now, opened by the View icon.)
  const std::set<std::string> quick = {"power", "range", "rangeUnits"};
  const char* categories[] = {"base",         "targets",      "trails",
                              "advanced",     "installation", "info"};

  for (const char* cat : categories) {
    std::vector<const ControlDef*> group;
    for (const auto& d : defs)
      if (d.category == cat && !quick.count(d.id)) group.push_back(&d);
    if (group.empty()) continue;
    std::sort(group.begin(), group.end(),
              [](const ControlDef* a, const ControlDef* b) {
                return a->numeric_id < b->numeric_id;
              });
    AddCollapsibleSection(
        root, wxString(cat).Capitalize(), cat, [this, group](wxSizer* c) {
          for (const ControlDef* d : group) AddControl(c, *d);
        });
  }

  SetSizer(root);  // deletes the previous sizer
  FitInside();
  Layout();
  ThemeChildren();  // theme the freshly created widgets
  ApplyValues();
}

void ControlsPanel::AddCollapsibleSection(wxSizer* root, const wxString& title,
                                          const std::string& key,
                                          std::function<void(wxSizer*)> fill) {
  const bool collapsed = m_collapsed.count(key) ? m_collapsed[key] : true;
  m_collapsed[key] = collapsed;

  auto* content = new wxBoxSizer(wxVERTICAL);
  fill(content);

  auto* header = new SectionHeader(this, title, m_theme);
  header->SetCollapsed(collapsed);
  root->Add(header, 0, wxEXPAND | wxTOP, 4);
  root->Add(content, 0, wxEXPAND | wxLEFT, 8);

  header->SetOnClick([this, root, content, header, key]() {
    const bool c = !m_collapsed[key];
    m_collapsed[key] = c;
    header->SetCollapsed(c);
    root->Show(content, !c, true);
    Layout();
    FitInside();
  });
  root->Show(content, !collapsed, true);
}

void ControlsPanel::AddControl(wxSizer* content, const ControlDef& d) {
  if (d.isReadOnly)
    AddReadonly(content, d);
  else if (d.dataType == "number")
    AddNumber(content, d);
  else if (d.dataType == "enum")
    AddEnum(content, d, /*buttons=*/false);
  else if (d.dataType == "button")
    AddButton(content, d);
  else if (d.dataType == "string")
    AddReadonly(content, d);
  else if (d.dataType == "sector")
    AddSector(content, d);
  else if (d.dataType == "zone")
    AddZone(content, d);
  else
    AddPlaceholder(content, d);  // rect: editor later
}

void ControlsPanel::FillViewSection(wxSizer* content) {
  if (m_set_overlay) {
    auto* cb =
        new ThemedButton(this, _("Radar overlay on chart"), m_theme, true);
    content->Add(cb, 0, wxEXPAND | wxALL, 4);
    cb->Bind(wxEVT_TOGGLEBUTTON, [this, cb](wxCommandEvent&) {
      if (m_set_overlay) m_set_overlay(cb->GetValue());
    });
    m_updaters.push_back([this, cb]() {
      if (m_get_overlay) cb->SetValue(m_get_overlay());
    });
  }
  if (m_set_ppi) {
    auto* cb = new ThemedButton(this, _("Show PPI"), m_theme, true);
    content->Add(cb, 0, wxEXPAND | wxALL, 4);
    cb->Bind(wxEVT_TOGGLEBUTTON, [this, cb](wxCommandEvent&) {
      if (m_set_ppi) m_set_ppi(cb->GetValue());
    });
    m_updaters.push_back([this, cb]() {
      if (m_get_ppi) cb->SetValue(m_get_ppi());
      cb->Enable(m_get_overlay && m_get_overlay());  // hide PPI only w/ overlay
    });
  }
  if (m_get_orientation && m_set_orientation) {
    content->Add(new wxStaticText(this, wxID_ANY, _("Orientation")), 0,
                 wxLEFT | wxTOP, 4);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const wxString labels[3] = {_("Head up"), _("North up"), _("Course up")};
    auto btns = std::make_shared<std::vector<ThemedButton*>>();
    for (int i = 0; i < 3; ++i) {
      auto* b = new ThemedButton(this, labels[i], m_theme, /*toggle=*/false);
      row->Add(b, 1, wxALL, 2);
      btns->push_back(b);
      b->Bind(wxEVT_BUTTON, [this, i, btns](wxCommandEvent&) {
        if (m_set_orientation) m_set_orientation(i);
        for (size_t j = 0; j < btns->size(); ++j)
          (*btns)[j]->SetValue(static_cast<int>(j) == i);
      });
    }
    content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
    m_updaters.push_back([this, btns]() {
      const int o = m_get_orientation ? m_get_orientation() : 0;
      for (size_t j = 0; j < btns->size(); ++j)
        (*btns)[j]->SetValue(static_cast<int>(j) == o);
    });
  }
  if (m_on_autolayout) {
    auto* b = new ThemedButton(this, _("Auto layout windows"), m_theme,
                               /*toggle=*/false);
    content->Add(b, 0, wxEXPAND | wxALL, 4);
    b->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      if (m_on_autolayout) m_on_autolayout();
    });
  }
  if (m_get_dock && m_set_dock) {
    auto* cb = new ThemedButton(this, _("Dock in OpenCPN"), m_theme, true);
    content->Add(cb, 0, wxEXPAND | wxALL, 4);
    cb->Bind(wxEVT_TOGGLEBUTTON, [this, cb](wxCommandEvent&) {
      if (m_set_dock) m_set_dock(cb->GetValue());
    });
    m_updaters.push_back([this, cb]() {
      if (m_get_dock) cb->SetValue(m_get_dock());
    });
  }
}

void ControlsPanel::SetOrientationControl(std::function<int()> get,
                                          std::function<void(int)> set) {
  m_get_orientation = std::move(get);
  m_set_orientation = std::move(set);
  if (m_built) Rebuild();
}

void ControlsPanel::SetDockControl(std::function<bool()> get,
                                   std::function<void(bool)> set) {
  m_get_dock = std::move(get);
  m_set_dock = std::move(set);
  if (m_built) Rebuild();
}

void ControlsPanel::SetViewControls(std::function<bool()> get_overlay,
                                    std::function<void(bool)> set_overlay,
                                    std::function<bool()> get_ppi,
                                    std::function<void(bool)> set_ppi) {
  m_get_overlay = std::move(get_overlay);
  m_set_overlay = std::move(set_overlay);
  m_get_ppi = std::move(get_ppi);
  m_set_ppi = std::move(set_ppi);
  if (m_built) Rebuild();  // add the View section now that we can drive it
}

void ControlsPanel::AddNumber(wxSizer* outer, const ControlDef& def) {
  auto* box = new wxBoxSizer(wxVERTICAL);
  box->Add(new wxStaticText(this, wxID_ANY,
                            wxString::FromUTF8(def.name.c_str())),
           0, wxLEFT, 2);

  // slider | value | Auto  — value has a fixed width so it never clips.
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* slider = new ThemedSlider(this, m_theme);
  slider->SetMinSize(wxSize(90, 24));
  row->Add(slider, 1, wxALIGN_CENTER_VERTICAL);
  auto* valtext = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition,
                                   wxSize(46, -1),
                                   wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
  row->Add(valtext, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
  ThemedButton* autobtn = nullptr;
  if (def.hasAuto) {
    autobtn = new ThemedButton(this, _("Auto"), m_theme, /*toggle=*/true);
    row->Add(autobtn, 0, wxALIGN_CENTER_VERTICAL);
  }
  box->Add(row, 0, wxEXPAND);
  outer->Add(box, 0, wxEXPAND | wxALL, 4);

  const std::string id = def.id;
  const std::string units = def.units;
  const double mn = def.has_min ? def.minValue : 0.0;
  const double mx = def.has_max ? def.maxValue : 100.0;

  // The slider's range/meaning depends on the live auto state:
  //   - auto + hasAutoAdjustable -> adjusts autoValue over autoAdjust{Min,Max}
  //   - otherwise               -> the manual value over min..max
  // Track dragging explicitly (a toggle-button click doesn't move focus off the
  // slider on macOS, so a HasFocus() guard would leave it stuck).
  auto dragging = std::make_shared<bool>(false);

  std::function<void(const ControlValue&)> refresh =
      [slider, valtext, autobtn, def, mn, mx, units,
       dragging](const ControlValue& v) {
        const bool adj = def.hasAutoAdjustable && v.auto_;
        const double lo = adj ? def.autoAdjustMin : mn;
        const double hi = adj ? def.autoAdjustMax : mx;
        const double cur = adj ? v.autoValue : v.value;
        if (!*dragging && hi > lo)
          slider->SetValue(Clampi(
              static_cast<int>((cur - lo) / (hi - lo) * 1000.0 + 0.5), 0,
              1000));
        if (adj) {
          const long a = std::lround(cur);
          valtext->SetLabel(a == 0 ? wxString("A")
                                   : wxString::Format("A%+ld", a));
        } else {
          valtext->SetLabel(FormatVal(v.value, units));
        }
        if (autobtn) {
          autobtn->SetValue(v.auto_);
          slider->Enable(!v.auto_ || def.hasAutoAdjustable);
        }
      };

  auto send = [this, id, mn, mx, def, slider]() {
    ControlValue v = controls()->Value(id);
    const bool adj = def.hasAutoAdjustable && v.auto_;
    const double lo = adj ? def.autoAdjustMin : mn;
    const double hi = adj ? def.autoAdjustMax : mx;
    const double val = lo + (hi - lo) * slider->GetValue() / 1000.0;
    if (adj)
      Set(id, "{\"auto\":true,\"autoValue\":" + Num(val) + "}");
    else
      Set(id, BodyValueAuto(val, def.hasAuto, false));  // manual -> auto off
  };

  slider->Bind(wxEVT_SCROLL_THUMBTRACK,
               [dragging](wxScrollEvent&) { *dragging = true; });
  slider->Bind(wxEVT_SCROLL_THUMBRELEASE, [dragging, send](wxScrollEvent&) {
    *dragging = false;
    send();
  });
  slider->Bind(wxEVT_SCROLL_CHANGED, [dragging, send](wxScrollEvent&) {
    *dragging = false;
    send();
  });

  if (autobtn) {
    autobtn->Bind(wxEVT_TOGGLEBUTTON, [this, id, refresh,
                                       autobtn](wxCommandEvent&) {
      const bool a = autobtn->GetValue();
      Set(id, BodyAuto(a));
      // Reflect the new mode immediately; the stream confirms shortly after.
      ControlValue v = controls()->Value(id);
      v.auto_ = a;
      v.has_auto = true;
      refresh(v);
    });
  }

  m_updaters.push_back(
      [this, id, refresh]() { refresh(controls()->Value(id)); });
}

void ControlsPanel::AddEnum(wxSizer* outer, const ControlDef& def,
                            bool as_buttons) {
  outer->Add(new wxStaticText(this, wxID_ANY,
                              wxString::FromUTF8(def.name.c_str())),
             0, wxLEFT | wxTOP, 4);

  const std::string id = def.id;
  std::vector<int> values = def.validValues;
  if (values.empty())
    for (const auto& kv : def.descriptions) values.push_back(kv.first);

  if (as_buttons) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    std::vector<std::pair<int, ThemedButton*>> buttons;
    for (int v : values) {
      auto dit = def.descriptions.find(v);
      wxString label = dit != def.descriptions.end()
                           ? wxString::FromUTF8(dit->second.c_str())
                           : wxString::Format("%d", v);
      auto* b = new ThemedButton(this, label, m_theme, /*toggle=*/false);
      row->Add(b, 1, wxALL, 2);
      buttons.emplace_back(v, b);
      b->Bind(wxEVT_BUTTON, [this, id, v](wxCommandEvent&) {
        Set(id, BodyValue(v));
      });
    }
    outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
    m_updaters.push_back([this, id, buttons]() {
      ControlValue val = controls()->Value(id);
      for (auto& pb : buttons)
        pb.second->SetValue(val.has_value &&
                            static_cast<int>(val.value) == pb.first);
    });
  } else {
    auto* choice = new ThemedChoice(this, m_theme);
    for (int v : values) {
      auto dit = def.descriptions.find(v);
      wxString label = dit != def.descriptions.end()
                           ? wxString::FromUTF8(dit->second.c_str())
                           : wxString::Format("%d", v);
      choice->Append(label, v);
    }
    outer->Add(choice, 0, wxEXPAND | wxALL, 4);
    choice->Bind(wxEVT_CHOICE, [this, id, choice](wxCommandEvent&) {
      int sel = choice->GetSelection();
      if (sel == wxNOT_FOUND) return;
      Set(id, BodyValue(choice->GetItemData(sel)));
    });
    m_updaters.push_back([this, id, choice, values]() {
      ControlValue val = controls()->Value(id);
      if (!val.has_value) return;
      for (unsigned i = 0; i < values.size(); ++i)
        if (values[i] == static_cast<int>(val.value)) {
          if (choice->GetSelection() != static_cast<int>(i)) choice->SetSelection(i);
          break;
        }
    });
  }
}

void ControlsPanel::AddRange(wxSizer* outer, const ControlDef& def,
                             const std::vector<int>& supported) {
  outer->Add(new wxStaticText(this, wxID_ANY, _("Range")), 0, wxLEFT | wxTOP, 4);
  auto* choice = new ThemedChoice(this, m_theme);

  // Use the range control's own validValues (the settable ranges, with nice
  // "1 nm"/"500 m" descriptions), not capabilities.supportedRanges which
  // includes intermediate values the control does not accept.
  std::vector<int> values = def.validValues.empty() ? supported : def.validValues;
  for (int v : values) {
    auto dit = def.descriptions.find(v);
    choice->Append(dit != def.descriptions.end()
                       ? wxString::FromUTF8(dit->second.c_str())
                       : FormatVal(v, "m"));
  }
  outer->Add(choice, 0, wxEXPAND | wxALL, 4);

  const std::string id = def.id;
  choice->Bind(wxEVT_CHOICE, [this, id, choice, values](wxCommandEvent&) {
    int sel = choice->GetSelection();
    if (sel >= 0 && sel < static_cast<int>(values.size()))
      Set(id, BodyValue(values[sel]));
  });
  m_updaters.push_back([this, id, choice, values]() {
    ControlValue val = controls()->Value(id);
    if (!val.has_value || values.empty()) return;
    int best = 0;
    double bestd = 1e18;
    for (unsigned i = 0; i < values.size(); ++i) {
      double d = std::fabs(values[i] - val.value);
      if (d < bestd) {
        bestd = d;
        best = static_cast<int>(i);
      }
    }
    if (choice->GetSelection() != best) choice->SetSelection(best);
  });
}

void ControlsPanel::AddButton(wxSizer* outer, const ControlDef& def) {
  auto* b = new ThemedButton(this, wxString::FromUTF8(def.name.c_str()),
                             m_theme, /*toggle=*/false);
  outer->Add(b, 0, wxEXPAND | wxALL, 4);
  const std::string id = def.id;
  b->Bind(wxEVT_BUTTON, [this, id](wxCommandEvent&) { Set(id, "{}"); });
}

void ControlsPanel::AddReadonly(wxSizer* outer, const ControlDef& def) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(this, wxID_ANY,
                            wxString::FromUTF8((def.name + ":").c_str())),
           0, wxRIGHT, 6);
  auto* val = new wxStaticText(this, wxID_ANY, "");
  row->Add(val, 1);
  outer->Add(row, 0, wxEXPAND | wxALL, 4);

  const std::string id = def.id;
  const std::string units = def.units;
  m_updaters.push_back([this, id, val, units]() {
    ControlValue v = controls()->Value(id);
    if (!v.str_value.empty())
      val->SetLabel(wxString::FromUTF8(v.str_value.c_str()));
    else if (v.has_value)
      val->SetLabel(FormatVal(v.value, units));
  });
}

// A no-transmit sector: start/end angles (degrees) + Enabled + Save.
void ControlsPanel::AddSector(wxSizer* outer, const ControlDef& def) {
  const std::string id = def.id;
  auto* box = new wxBoxSizer(wxVERTICAL);
  box->Add(new wxStaticText(this, wxID_ANY,
                            wxString::FromUTF8(def.name.c_str())),
           0, wxLEFT | wxTOP, 4);
  auto dirty = std::make_shared<bool>(false);

  auto make_angle = [&](const wxString& label) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, label, wxDefaultPosition,
                              wxSize(52, -1)),
             0, wxALIGN_CENTER_VERTICAL);
    auto* sl = new ThemedSlider(this, m_theme);
    sl->SetMinSize(wxSize(90, 24));
    row->Add(sl, 1, wxALIGN_CENTER_VERTICAL);
    auto* val = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition,
                                 wxSize(52, -1),
                                 wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
    row->Add(val, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    box->Add(row, 0, wxEXPAND | wxLEFT, 8);
    auto self = [sl, val]() {
      val->SetLabel(wxString::Format("%ld°", RoundL(SliderToDeg(sl->GetValue()))));
    };
    sl->Bind(wxEVT_SCROLL_THUMBTRACK,
             [dirty, self](wxScrollEvent&) { *dirty = true; self(); });
    sl->Bind(wxEVT_SCROLL_CHANGED,
             [dirty, self](wxScrollEvent&) { *dirty = true; self(); });
    return std::make_pair(sl, val);
  };

  ThemedSlider *sStart, *sEnd;
  wxStaticText *vStart, *vEnd;
  std::tie(sStart, vStart) = make_angle(_("Start°"));
  std::tie(sEnd, vEnd) = make_angle(_("End°"));

  auto* brow = new wxBoxSizer(wxHORIZONTAL);
  auto* en = new ThemedButton(this, _("Enabled"), m_theme, /*toggle=*/true);
  en->Bind(wxEVT_TOGGLEBUTTON, [dirty](wxCommandEvent&) { *dirty = true; });
  brow->Add(en, 1, wxALL, 2);
  auto* save = new ThemedButton(this, _("Save"), m_theme, /*toggle=*/false);
  brow->Add(save, 1, wxALL, 2);
  box->Add(brow, 0, wxEXPAND);
  outer->Add(box, 0, wxEXPAND | wxALL, 4);

  save->Bind(wxEVT_BUTTON, [this, id, dirty, sStart, sEnd, en](wxCommandEvent&) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "{\"value\":%g,\"endValue\":%g,\"enabled\":%s}",
                  DegToRad(SliderToDeg(sStart->GetValue())),
                  DegToRad(SliderToDeg(sEnd->GetValue())),
                  en->GetValue() ? "true" : "false");
    Set(id, buf);
    *dirty = false;
  });

  m_updaters.push_back(
      [this, id, dirty, sStart, vStart, sEnd, vEnd, en]() {
        if (*dirty) return;
        ControlValue v = controls()->Value(id);
        const double sd = RadToDeg(v.value), ed = RadToDeg(v.endValue);
        sStart->SetValue(DegToSlider(sd));
        vStart->SetLabel(wxString::Format("%ld°", RoundL(sd)));
        sEnd->SetValue(DegToSlider(ed));
        vEnd->SetLabel(wxString::Format("%ld°", RoundL(ed)));
        if (v.has_enabled) en->SetValue(v.enabled);
      });
}

// A guard zone: start/end angles (degrees), inner/outer distance (metres),
// Enabled + Save.
void ControlsPanel::AddZone(wxSizer* outer, const ControlDef& def) {
  const std::string id = def.id;
  const double maxDist = def.maxDistance > 0 ? def.maxDistance : 4000.0;
  auto* box = new wxBoxSizer(wxVERTICAL);
  box->Add(new wxStaticText(this, wxID_ANY,
                            wxString::FromUTF8(def.name.c_str())),
           0, wxLEFT | wxTOP, 4);
  auto dirty = std::make_shared<bool>(false);

  auto make_row = [&](const wxString& label, bool is_dist) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, label, wxDefaultPosition,
                              wxSize(52, -1)),
             0, wxALIGN_CENTER_VERTICAL);
    auto* sl = new ThemedSlider(this, m_theme);
    sl->SetMinSize(wxSize(90, 24));
    row->Add(sl, 1, wxALIGN_CENTER_VERTICAL);
    auto* val = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition,
                                 wxSize(52, -1),
                                 wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
    row->Add(val, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    box->Add(row, 0, wxEXPAND | wxLEFT, 8);
    auto self = [sl, val, is_dist, maxDist]() {
      if (is_dist)
        val->SetLabel(
            wxString::Format("%ld m", RoundL(sl->GetValue() / 1000.0 * maxDist)));
      else
        val->SetLabel(
            wxString::Format("%ld°", RoundL(SliderToDeg(sl->GetValue()))));
    };
    sl->Bind(wxEVT_SCROLL_THUMBTRACK,
             [dirty, self](wxScrollEvent&) { *dirty = true; self(); });
    sl->Bind(wxEVT_SCROLL_CHANGED,
             [dirty, self](wxScrollEvent&) { *dirty = true; self(); });
    return std::make_pair(sl, val);
  };

  ThemedSlider *sStart, *sEnd, *sIn, *sOut;
  wxStaticText *vStart, *vEnd, *vIn, *vOut;
  std::tie(sStart, vStart) = make_row(_("Start°"), false);
  std::tie(sEnd, vEnd) = make_row(_("End°"), false);
  std::tie(sIn, vIn) = make_row(_("Inner"), true);
  std::tie(sOut, vOut) = make_row(_("Outer"), true);

  auto* brow = new wxBoxSizer(wxHORIZONTAL);
  auto* en = new ThemedButton(this, _("Enabled"), m_theme, /*toggle=*/true);
  en->Bind(wxEVT_TOGGLEBUTTON, [dirty](wxCommandEvent&) { *dirty = true; });
  brow->Add(en, 1, wxALL, 2);
  auto* save = new ThemedButton(this, _("Save"), m_theme, /*toggle=*/false);
  brow->Add(save, 1, wxALL, 2);
  box->Add(brow, 0, wxEXPAND);
  outer->Add(box, 0, wxEXPAND | wxALL, 4);

  save->Bind(wxEVT_BUTTON, [this, id, dirty, sStart, sEnd, sIn, sOut, en,
                            maxDist](wxCommandEvent&) {
    char buf[256];
    std::snprintf(
        buf, sizeof(buf),
        "{\"value\":%g,\"endValue\":%g,\"startDistance\":%g,"
        "\"endDistance\":%g,\"enabled\":%s}",
        DegToRad(SliderToDeg(sStart->GetValue())),
        DegToRad(SliderToDeg(sEnd->GetValue())),
        sIn->GetValue() / 1000.0 * maxDist, sOut->GetValue() / 1000.0 * maxDist,
        en->GetValue() ? "true" : "false");
    Set(id, buf);
    *dirty = false;
  });

  m_updaters.push_back([this, id, dirty, sStart, vStart, sEnd, vEnd, sIn, vIn,
                        sOut, vOut, en, maxDist]() {
    if (*dirty) return;
    ControlValue v = controls()->Value(id);
    const double sd = RadToDeg(v.value), ed = RadToDeg(v.endValue);
    sStart->SetValue(DegToSlider(sd));
    vStart->SetLabel(wxString::Format("%ld°", RoundL(sd)));
    sEnd->SetValue(DegToSlider(ed));
    vEnd->SetLabel(wxString::Format("%ld°", RoundL(ed)));
    sIn->SetValue(Clampi(
        static_cast<int>(RoundL(v.startDistance / maxDist * 1000.0)), 0, 1000));
    vIn->SetLabel(wxString::Format("%ld m", RoundL(v.startDistance)));
    sOut->SetValue(Clampi(
        static_cast<int>(RoundL(v.endDistance / maxDist * 1000.0)), 0, 1000));
    vOut->SetLabel(wxString::Format("%ld m", RoundL(v.endDistance)));
    if (v.has_enabled) en->SetValue(v.enabled);
  });
}

void ControlsPanel::AddPlaceholder(wxSizer* outer, const ControlDef& def) {
  outer->Add(new wxStaticText(
                 this, wxID_ANY,
                 wxString::FromUTF8((def.name + " (" + def.dataType + ")").c_str())),
             0, wxALL, 4);
}
