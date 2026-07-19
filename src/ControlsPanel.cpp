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

ControlsPanel::ControlsPanel(wxWindow* parent, MayaraClient* client)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL),
      m_client(client),
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
  if (m_client) m_client->SetControl(id, body);
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
  if (!m_client) return;
  RadarControls* controls = m_client->Controls();
  if (!controls || !controls->HasSchema()) return;
  const int active = m_client->ActiveIndex();
  const uint64_t sgen = controls->SchemaGeneration();
  if (!m_built || active != m_active_radar || sgen != m_schema_gen) {
    Rebuild();  // schema changed, or the active radar changed
    m_built = true;
    m_active_radar = active;
    m_schema_gen = sgen;
    m_last_gen = controls->Generation();
    return;
  }
  const uint64_t gen = controls->Generation();
  if (gen != m_last_gen) {
    m_last_gen = gen;
    ApplyValues();
  }
}

void ControlsPanel::ApplyValues() {
  if (!m_client || !m_client->Controls()) return;
  for (auto& u : m_updaters) u();
}

void ControlsPanel::Rebuild() {
  DestroyChildren();
  m_updaters.clear();

  RadarControls* controls = m_client->Controls();
  if (!controls) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(MakeCloseRow(), 0, wxEXPAND);
    SetSizer(sizer);
    return;
  }
  std::vector<ControlDef> defs = controls->Schema();
  std::vector<int> ranges = controls->SupportedRanges();

  std::map<std::string, const ControlDef*> by_id;
  for (const auto& d : defs) by_id[d.id] = &d;

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(MakeCloseRow(), 0, wxEXPAND);

  // Radar selector (only when more than one radar is present).
  if (m_client->RadarCount() > 1) {
    root->Add(new wxStaticText(this, wxID_ANY, _("Radar")), 0, wxLEFT | wxTOP,
              4);
    auto* sel = new ThemedChoice(this, m_theme);
    for (const auto& n : m_client->RadarNames())
      sel->Append(wxString::FromUTF8(n.c_str()));
    sel->SetSelection(m_client->ActiveIndex());
    sel->Bind(wxEVT_CHOICE, [this, sel](wxCommandEvent&) {
      m_client->SetActive(sel->GetSelection());
    });
    root->Add(sel, 0, wxEXPAND | wxALL, 4);
  }

  // --- Quick controls: prominent, fixed placement ---
  if (by_id.count("power")) AddEnum(root, *by_id["power"], /*buttons=*/true);
  if (by_id.count("range")) AddRange(root, *by_id["range"], ranges);
  if (by_id.count("rangeUnits"))
    AddEnum(root, *by_id["rangeUnits"], /*buttons=*/true);
  root->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 4);

  // View section (plugin/window toggles), placed before Base.
  AddCollapsibleSection(root, _("View"), "view",
                        [this](wxSizer* c) { FillViewSection(c); });

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
  else
    AddPlaceholder(content, d);  // sector/zone/rect: editors later
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
    ControlValue v = m_client->Controls()->Value(id);
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
      ControlValue v = m_client->Controls()->Value(id);
      v.auto_ = a;
      v.has_auto = true;
      refresh(v);
    });
  }

  m_updaters.push_back(
      [this, id, refresh]() { refresh(m_client->Controls()->Value(id)); });
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
      ControlValue val = m_client->Controls()->Value(id);
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
      ControlValue val = m_client->Controls()->Value(id);
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
    ControlValue val = m_client->Controls()->Value(id);
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
    ControlValue v = m_client->Controls()->Value(id);
    if (!v.str_value.empty())
      val->SetLabel(wxString::FromUTF8(v.str_value.c_str()));
    else if (v.has_value)
      val->SetLabel(FormatVal(v.value, units));
  });
}

void ControlsPanel::AddPlaceholder(wxSizer* outer, const ControlDef& def) {
  outer->Add(new wxStaticText(
                 this, wxID_ANY,
                 wxString::FromUTF8((def.name + " (" + def.dataType + ")").c_str())),
             0, wxALL, 4);
}
