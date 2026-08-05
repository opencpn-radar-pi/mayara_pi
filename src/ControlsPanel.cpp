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

// Offered PPI refresh rates, in the order the buttons appear.
const int kRates[] = {1, 2, 5, 10};
const int kRateCount = static_cast<int>(sizeof(kRates) / sizeof(kRates[0]));

std::string Num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

// How many distinct values a number control can take. A slider is fine for a
// handful of steps and turns into guesswork for hundreds: over a 90 px track,
// one pixel of travel can be several units, so the value you actually want is
// unreachable. Above kStepperFrom the slider gets - / + buttons for the last
// few units; above kEntryFrom it is replaced by a field you can type into.
const double kStepperFrom = 20.0;
const double kEntryFrom = 100.0;

double NumberSteps(const ControlDef& d) {
  const double mn = d.has_min ? d.minValue : 0.0;
  const double mx = d.has_max ? d.maxValue : 100.0;
  const double step = (d.has_step && d.stepValue > 0) ? d.stepValue : 1.0;
  if (mx <= mn) return 1.0;
  return (mx - mn) / step + 1.0;
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

  bool vrm_done = false;
  for (const char* cat : categories) {
    // Straight after Base, as the operator's own measuring tools rather than
    // anything the radar reports.
    if (!vrm_done && std::string(cat) != "base") {
      AddCollapsibleSection(root, _("EBL/VRM"), "eblvrm",
                            [this](wxSizer* c) { FillVrmEblSection(c); });
      vrm_done = true;
    }
    std::vector<const ControlDef*> group;
    for (const auto& d : defs)
      if (d.category == cat && !quick.count(d.id)) group.push_back(&d);
    // Info is always built, even when the radar reports nothing of its own: it
    // carries the server we are actually talking to, which is the first thing
    // worth knowing precisely when there is no picture to explain.
    const bool is_info = std::string(cat) == "info";
    if (group.empty() && !is_info) continue;
    std::sort(group.begin(), group.end(),
              [](const ControlDef* a, const ControlDef* b) {
                return a->numeric_id < b->numeric_id;
              });
    AddCollapsibleSection(root, wxString(cat).Capitalize(), cat,
                          [this, group, is_info](wxSizer* c) {
                            if (is_info) AddServerRow(c);
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
    // That Show is recursive, so it un-hides children a control had
    // deliberately hidden -- a guard zone's Save button, say. Re-push the model
    // into the widgets so each control restates what it wants to be visible.
    // The timer cannot do it: it only calls ApplyValues() when the radar sends
    // a control update, which may be never.
    ApplyValues();
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
  // Where the radar picture appears. Independent toggles rather than one
  // exclusive choice: overlay and PPI can both be up, and "docked" is a
  // property of the PPI window rather than a third place to put the picture.
  if (m_set_overlay || m_set_ppi || m_set_dock) {
    content->Add(new wxStaticText(this, wxID_ANY, _("Views")), 0,
                 wxLEFT | wxTOP, 4);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto add = [&](const wxString& label, std::function<void(bool)> set) {
      auto* b = new ThemedButton(this, label, m_theme, /*toggle=*/true);
      row->Add(b, 1, wxALL, 2);
      b->Bind(wxEVT_TOGGLEBUTTON,
              [b, set](wxCommandEvent&) { if (set) set(b->GetValue()); });
      return b;
    };
    ThemedButton* overlay = m_set_overlay ? add(_("Overlay"), m_set_overlay)
                                          : nullptr;
    ThemedButton* ppi = m_set_ppi ? add(_("PPI"), m_set_ppi) : nullptr;
    ThemedButton* dock = m_set_dock ? add(_("Docked"), m_set_dock) : nullptr;
    content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
    m_updaters.push_back([this, overlay, ppi, dock]() {
      if (overlay && m_get_overlay) overlay->SetValue(m_get_overlay());
      if (ppi) {
        if (m_get_ppi) ppi->SetValue(m_get_ppi());
        // The PPI may only be hidden while the overlay still shows the radar.
        ppi->Enable(m_get_overlay && m_get_overlay());
      }
      if (dock && m_get_dock) dock->SetValue(m_get_dock());
    });
  }
  if (m_get_orientation && m_set_orientation)
    AddChoiceRow(content, _("Orientation"),
                 {_("Head up"), _("North up"), _("Course up")},
                 m_get_orientation, m_set_orientation);
  if (m_get_threshold && m_set_threshold)
    AddChoiceRow(content, _("Echo threshold"),
                 {_("All"), _("Medium"), _("Strong")}, m_get_threshold,
                 m_set_threshold);
  if (m_get_prefs && m_set_prefs) {
    // Refresh rate: presets rather than a slider. The picture is CPU-rendered,
    // so this is the one display setting that costs something to raise.
    AddChoiceRow(
        content, _("Refresh rate"), {"1 Hz", "2 Hz", "5 Hz", "10 Hz"},
        [this]() {
          const int hz = m_get_prefs().refresh_hz;
          int best = 0;
          for (int i = 0; i < kRateCount; ++i)
            if (std::abs(kRates[i] - hz) < std::abs(kRates[best] - hz)) best = i;
          return best;
        },
        [this](int i) {
          PpiPrefs p = m_get_prefs();
          p.refresh_hz = kRates[i];
          m_set_prefs(p);
        });
    AddChoiceRow(
        content, _("Menu auto-hide"), {_("Never"), "10 s", "30 s"},
        [this]() { return m_get_prefs().menu_autohide; },
        [this](int i) {
          PpiPrefs p = m_get_prefs();
          p.menu_autohide = i;
          m_set_prefs(p);
        });
    auto* rz = new ThemedButton(this, _("Reverse zoom wheel"), m_theme, true);
    content->Add(rz, 0, wxEXPAND | wxALL, 4);
    rz->Bind(wxEVT_TOGGLEBUTTON, [this, rz](wxCommandEvent&) {
      PpiPrefs p = m_get_prefs();
      p.reverse_zoom = rz->GetValue();
      m_set_prefs(p);
    });
    m_updaters.push_back(
        [this, rz]() { rz->SetValue(m_get_prefs().reverse_zoom); });
  }
  if (m_on_autolayout) {
    auto* b = new ThemedButton(this, _("Auto layout windows"), m_theme,
                               /*toggle=*/false);
    content->Add(b, 0, wxEXPAND | wxALL, 4);
    b->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      if (m_on_autolayout) m_on_autolayout();
    });
  }
}

void ControlsPanel::AddChoiceRow(wxSizer* content, const wxString& label,
                                 const std::vector<wxString>& labels,
                                 std::function<int()> get,
                                 std::function<void(int)> set) {
  content->Add(new wxStaticText(this, wxID_ANY, label), 0, wxLEFT | wxTOP, 4);
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto btns = std::make_shared<std::vector<ThemedButton*>>();
  for (size_t i = 0; i < labels.size(); ++i) {
    auto* b = new ThemedButton(this, labels[i], m_theme, /*toggle=*/false);
    row->Add(b, 1, wxALL, 2);
    btns->push_back(b);
    const int idx = static_cast<int>(i);
    b->Bind(wxEVT_BUTTON, [set, idx, btns](wxCommandEvent&) {
      if (set) set(idx);
      for (size_t j = 0; j < btns->size(); ++j)
        (*btns)[j]->SetValue(static_cast<int>(j) == idx);
    });
  }
  content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);
  m_updaters.push_back([get, btns]() {
    const int v = get ? get() : 0;
    for (size_t j = 0; j < btns->size(); ++j)
      (*btns)[j]->SetValue(static_cast<int>(j) == v);
  });
}

void ControlsPanel::SetPrefsControl(std::function<PpiPrefs()> get,
                                    std::function<void(const PpiPrefs&)> set) {
  m_get_prefs = std::move(get);
  m_set_prefs = std::move(set);
  if (m_built) Rebuild();
}

void ControlsPanel::SetOrientationControl(std::function<int()> get,
                                          std::function<void(int)> set) {
  m_get_orientation = std::move(get);
  m_set_orientation = std::move(set);
  if (m_built) Rebuild();
}

void ControlsPanel::SetThresholdControl(std::function<int()> get,
                                        std::function<void(int)> set) {
  m_get_threshold = std::move(get);
  m_set_threshold = std::move(set);
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

  // How the value is edited depends on how many values there are; see
  // NumberSteps. Layouts: [slider | value | Auto], [slider | - | value | + |
  // Auto], or [field | Auto].
  const double steps = NumberSteps(def);
  const bool use_entry = steps > kEntryFrom;
  const bool use_steppers = !use_entry && steps > kStepperFrom;

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  ThemedSlider* slider = nullptr;
  wxTextCtrl* entry = nullptr;
  wxStaticText* valtext = nullptr;
  ThemedButton *minus = nullptr, *plus = nullptr;

  if (use_entry) {
    entry = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxSize(80, -1), wxTE_PROCESS_ENTER);
    row->Add(entry, 0, wxALIGN_CENTER_VERTICAL);
    if (!def.units.empty())
      row->Add(new wxStaticText(this, wxID_ANY,
                                wxString::FromUTF8(def.units.c_str())),
               0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  } else {
    slider = new ThemedSlider(this, m_theme);
    slider->SetMinSize(wxSize(90, 24));
    row->Add(slider, 1, wxALIGN_CENTER_VERTICAL);
    if (use_steppers) {
      minus = new ThemedButton(this, "-", m_theme, /*toggle=*/false);
      minus->SetMinSize(wxSize(24, 24));
      row->Add(minus, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    }
    valtext = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition,
                               wxSize(46, -1),
                               wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
    row->Add(valtext, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
    if (use_steppers) {
      plus = new ThemedButton(this, "+", m_theme, /*toggle=*/false);
      plus->SetMinSize(wxSize(24, 24));
      row->Add(plus, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    }
  }
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
      [slider, entry, valtext, minus, plus, autobtn, def, mn, mx, units,
       dragging](const ControlValue& v) {
        const bool adj = def.hasAutoAdjustable && v.auto_;
        const double lo = adj ? def.autoAdjustMin : mn;
        const double hi = adj ? def.autoAdjustMax : mx;
        const double cur = adj ? v.autoValue : v.value;
        // *dragging also covers "the field has focus": overwriting what
        // someone is halfway through typing is worse than a stale display.
        if (slider && !*dragging && hi > lo)
          slider->SetValue(Clampi(
              static_cast<int>((cur - lo) / (hi - lo) * 1000.0 + 0.5), 0,
              1000));
        if (entry && !*dragging)
          entry->ChangeValue(wxString::FromUTF8(Num(cur).c_str()));
        if (valtext) {
          if (adj) {
            const long a = std::lround(cur);
            valtext->SetLabel(a == 0 ? wxString("A")
                                     : wxString::Format("A%+ld", a));
          } else {
            valtext->SetLabel(FormatVal(v.value, units));
          }
        }
        if (autobtn) {
          autobtn->SetValue(v.auto_);
          const bool on = !v.auto_ || def.hasAutoAdjustable;
          if (slider) slider->Enable(on);
          if (entry) entry->Enable(on);
          if (minus) minus->Enable(on);
          if (plus) plus->Enable(on);
        }
      };

  // One place that writes a value, whatever the widget was.
  auto send_value = [this, id, mn, mx, def](double val) {
    ControlValue v = controls()->Value(id);
    const bool adj = def.hasAutoAdjustable && v.auto_;
    const double lo = adj ? def.autoAdjustMin : mn;
    const double hi = adj ? def.autoAdjustMax : mx;
    if (val < lo) val = lo;
    if (val > hi) val = hi;
    if (adj)
      Set(id, "{\"auto\":true,\"autoValue\":" + Num(val) + "}");
    else
      Set(id, BodyValueAuto(val, def.hasAuto, false));  // manual -> auto off
  };

  if (slider) {
    auto send = [this, id, mn, mx, def, slider, send_value]() {
      ControlValue v = controls()->Value(id);
      const bool adj = def.hasAutoAdjustable && v.auto_;
      const double lo = adj ? def.autoAdjustMin : mn;
      const double hi = adj ? def.autoAdjustMax : mx;
      send_value(lo + (hi - lo) * slider->GetValue() / 1000.0);
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
  }

  // Steppers move by the schema's own step, which is the whole point: the
  // slider cannot reliably land on one when there are hundreds of them.
  const double stepv = (def.has_step && def.stepValue > 0) ? def.stepValue : 1.0;
  auto nudge = [this, id, def, stepv, send_value](int dir) {
    ControlValue v = controls()->Value(id);
    const bool adj = def.hasAutoAdjustable && v.auto_;
    send_value((adj ? v.autoValue : v.value) + dir * stepv);
  };
  if (minus)
    minus->Bind(wxEVT_BUTTON, [nudge](wxCommandEvent&) { nudge(-1); });
  if (plus) plus->Bind(wxEVT_BUTTON, [nudge](wxCommandEvent&) { nudge(+1); });

  if (entry) {
    // Typing holds off the updater until the value is committed or the field
    // loses focus, so a 400 ms refresh cannot eat a half-typed number.
    entry->Bind(wxEVT_SET_FOCUS, [dragging](wxFocusEvent& e) {
      *dragging = true;
      e.Skip();
    });
    auto commit = [entry, dragging, send_value]() {
      double d = 0;
      if (entry->GetValue().ToDouble(&d)) send_value(d);
      *dragging = false;
    };
    entry->Bind(wxEVT_TEXT_ENTER,
                [commit](wxCommandEvent&) { commit(); });
    entry->Bind(wxEVT_KILL_FOCUS, [commit](wxFocusEvent& e) {
      commit();
      e.Skip();
    });
  }

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

// The server this plugin is talking to. Not a radar control, but it belongs
// with the other "what am I actually looking at" facts, and it is what you want
// to see when the radar is silent: whether we are on the copy we run ourselves
// (loopback) or one on the network.
// The two VRM/EBL markers. They are the plugin's own -- the radar has no such
// control -- so this section is built by hand rather than from the schema, and
// sits with the other things you look at rather than set.
void ControlsPanel::FillVrmEblSection(wxSizer* content) {
  for (int i = 0; i < kVrmEblCount; ++i) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(
        this, wxID_ANY, wxString::Format(_("VRM/EBL %d"), i + 1),
        wxDefaultPosition, wxSize(78, -1));
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    auto* val = new wxStaticText(this, wxID_ANY, wxEmptyString);
    row->Add(val, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    content->Add(row, 0, wxEXPAND | wxALL, 4);
    // Its own full-width row, added at proportion 1 -- the same shape as the
    // guard-zone button row, which lays out reliably. Sharing a row with a
    // proportion-1 label left it with no room to appear in.
    auto* brow = new wxBoxSizer(wxHORIZONTAL);
    auto* off = new ThemedButton(this, _("Clear"), m_theme, /*toggle=*/false);
    brow->Add(off, 1, wxALL, 2);
    content->Add(brow, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

    off->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
      if (!m_vrm_get || !m_vrm_set) return;
      VrmEbl m = m_vrm_get(i);
      m.enabled = false;
      m_vrm_set(i, m);
    });

    m_updaters.push_back([this, i, val, off]() {
      if (!m_vrm_get) return;
      const VrmEbl m = m_vrm_get(i);
      if (!m.enabled) {
        val->SetLabel(_("not set"));
        off->Enable(false);
        return;
      }
      off->Enable(true);
      double deg = m.bearing_rad * 180.0 / M_PI;
      while (deg < 0) deg += 360.0;
      val->SetLabel(wxString::Format("%.1f°  %s", deg,
                                     FormatVal(m.distance_m, "m")));
    });
  }
  content->Add(new wxStaticText(
                   this, wxID_ANY,
                   _("The EBL icon cycles 1 / 2 / off and takes the colour of "
                     "the marker it will place; click the picture to place it, "
                     "and Clear to remove it.")),
               0, wxALL, 4);
}

void ControlsPanel::AddServerRow(wxSizer* outer) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(this, wxID_ANY, _("Server:")), 0, wxRIGHT, 6);
  auto* val = new wxStaticText(this, wxID_ANY, wxEmptyString);
  row->Add(val, 1);
  outer->Add(row, 0, wxEXPAND | wxALL, 4);
  m_updaters.push_back([this, val]() {
    const std::string url = m_client ? m_client->ConnectedUrl() : std::string();
    val->SetLabel(url.empty() ? wxString(_("not connected"))
                              : wxString::FromUTF8(url.c_str()));
  });
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
  // Edit puts drag handles on the picture. Off by default: a zone you can move
  // by brushing the display is a zone you will move by accident.
  auto* edit = new ThemedButton(this, _("Edit"), m_theme, /*toggle=*/true);
  brow->Add(edit, 1, wxALL, 2);
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

  // Typed fields, not sliders. An angle has 360 useful values and a distance
  // thousands; over a 90 px track a pixel is several degrees or tens of metres,
  // so a slider cannot express "start at 47 degrees, 250 m out" at all.
  // Two rows rather than four: a zone is a pair of ranges, so the pairs read as
  // pairs.  Angles: <from> - <to>   Range: <from> - <to>
  wxTextCtrl *fStart, *fEnd, *fIn, *fOut;
  auto make_pair = [&](const wxString& label, const wxString& unit,
                       wxTextCtrl** lo, wxTextCtrl** hi) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(this, wxID_ANY, label, wxDefaultPosition,
                              wxSize(52, -1)),
             0, wxALIGN_CENTER_VERTICAL);
    auto field = [this]() {
      return new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxSize(54, -1),
                            wxTE_PROCESS_ENTER | wxTE_RIGHT);
    };
    *lo = field();
    row->Add(*lo, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(new wxStaticText(this, wxID_ANY, wxT(" – ")), 0,
             wxALIGN_CENTER_VERTICAL);
    *hi = field();
    row->Add(*hi, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(new wxStaticText(this, wxID_ANY, unit), 0,
             wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    box->Add(row, 0, wxEXPAND | wxLEFT | wxBOTTOM, 4);
  };
  make_pair(_("Angles:"), wxT("°"), &fStart, &fEnd);
  make_pair(_("Range:"), _("m"), &fIn, &fOut);
  wxTextCtrl* fields[4] = {fStart, fEnd, fIn, fOut};
  // Which field is being typed in, or -1. Tracked by hand: FindFocus() reports
  // the native text view inside a wxTextCtrl on macOS, never the wxTextCtrl,
  // so comparing pointers never matched and the updater overwrote the field
  // under the caret four times a second -- which looked like being unable to
  // type at all.
  auto focused = std::make_shared<int>(-1);
  for (int i = 0; i < 4; ++i) {
    fields[i]->Bind(wxEVT_SET_FOCUS, [focused, i](wxFocusEvent& e) {
      *focused = i;
      e.Skip();
    });
    fields[i]->Bind(wxEVT_KILL_FOCUS, [focused, i](wxFocusEvent& e) {
      if (*focused == i) *focused = -1;
      e.Skip();
    });
  }

  auto* brow = new wxBoxSizer(wxHORIZONTAL);
  // Enabled acts immediately and independently of an edit: switching a zone on
  // or off is not a change of shape, and should not need Save.
  auto* en = new ThemedButton(this, _("Enabled"), m_theme, /*toggle=*/true);
  brow->Add(en, 1, wxALL, 2);
  // One button with two jobs: it starts an edit, and abandons one. Save only
  // exists while an edit does, so there is never a Save that means nothing.
  auto* edit = new ThemedButton(this, _("Edit"), m_theme, /*toggle=*/false);
  brow->Add(edit, 1, wxALL, 2);
  auto* save = new ThemedButton(this, _("Save"), m_theme, /*toggle=*/false);
  brow->Add(save, 1, wxALL, 2);
  box->Add(brow, 0, wxEXPAND);
  outer->Add(box, 0, wxEXPAND | wxALL, 4);

  auto num = [](wxTextCtrl* f, double fallback) {
    double d = 0;
    return f->GetValue().ToDouble(&d) ? d : fallback;
  };

  // Read-only until Edit is pressed: these four numbers aim a guard zone, and
  // half-typed ones should not look like the radar's own.
  auto editing = std::make_shared<bool>(false);
  auto apply_mode = [fields, edit, save, editing, this]() {
    for (wxTextCtrl* f : fields) {
      f->SetEditable(*editing);
      // Read-only has to look read-only: SetEditable alone changes nothing you
      // can see, which is how the fields came across as broken.
      f->SetBackgroundColour(*editing ? m_theme.lozenge_bg : m_theme.panel_bg);
      f->SetForegroundColour(*editing ? m_theme.text : m_theme.dim_text);
      f->Refresh();
    }
    edit->SetLabel(*editing ? _("Cancel") : _("Edit"));
    save->Show(*editing);
    Layout();
  };
  apply_mode();

  // The zone as the fields currently read it.
  auto from_fields = [this, id, num, fStart, fEnd, fIn, fOut, en]() {
    const ControlValue cur = controls()->Value(id);
    ZoneEdit z;
    z.active = true;
    z.radar_index = m_index;
    z.id = id;
    z.start_rad = DegToRad(num(fStart, RadToDeg(cur.value)));
    z.end_rad = DegToRad(num(fEnd, RadToDeg(cur.endValue)));
    z.start_m = num(fIn, cur.startDistance);
    z.end_m = num(fOut, cur.endDistance);
    return z;
  };

  en->Bind(wxEVT_TOGGLEBUTTON, [this, id, en](wxCommandEvent&) {
    // Send the zone back unchanged except for the flag, so flipping it cannot
    // quietly reshape the zone.
    const ControlValue v = controls()->Value(id);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\"value\":%g,\"endValue\":%g,\"startDistance\":%g,"
                  "\"endDistance\":%g,\"enabled\":%s}",
                  v.value, v.endValue, v.startDistance, v.endDistance,
                  en->GetValue() ? "true" : "false");
    Set(id, buf);
  });

  // The shared edit is the only source of truth for whether we are editing:
  // pushing it runs the updater, which sets *editing and applies the mode.
  // Toggling *editing here as well used to undo what the updater had just
  // done, leaving the handles up but the fields still read-only.
  edit->Bind(wxEVT_BUTTON, [this, id, editing](wxCommandEvent&) {
    if (!m_zone_set) return;
    if (*editing) {
      m_zone_set(ZoneEdit(), /*commit=*/false);  // Cancel: drop the edit
    } else {
      const ControlValue v = controls()->Value(id);
      ZoneEdit z;
      z.active = true;
      z.radar_index = m_index;
      z.id = id;
      z.start_rad = v.value;
      z.end_rad = v.endValue;
      z.start_m = v.startDistance;
      z.end_m = v.endDistance;
      m_zone_set(z, /*commit=*/false);
    }
  });

  auto commit = [this, id, en, maxDist, from_fields]() {
    ZoneEdit z = from_fields();
    if (z.start_m < 0) z.start_m = 0;
    if (z.end_m > maxDist) z.end_m = maxDist;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\"value\":%g,\"endValue\":%g,\"startDistance\":%g,"
                  "\"endDistance\":%g,\"enabled\":%s}",
                  z.start_rad, z.end_rad, z.start_m, z.end_m,
                  en->GetValue() ? "true" : "false");
    Set(id, buf);
    if (m_zone_set) m_zone_set(ZoneEdit(), /*commit=*/false);  // edit is over
  };
  save->Bind(wxEVT_BUTTON, [commit](wxCommandEvent&) { commit(); });
  for (wxTextCtrl* f : fields)
    f->Bind(wxEVT_TEXT_ENTER, [commit](wxCommandEvent&) { commit(); });

  // Every keystroke feeds the shared edit, so the handles on the picture follow
  // the numbers. Nothing reaches the radar until Save.
  for (wxTextCtrl* f : fields)
    f->Bind(wxEVT_TEXT, [this, editing, from_fields](wxCommandEvent& e) {
      if (*editing && m_zone_set) m_zone_set(from_fields(), /*commit=*/false);
      e.Skip();
    });

  m_updaters.push_back(
      [this, id, fields, en, save, editing, focused, apply_mode]() {
    // While editing, the shared edit is the truth -- a dragged handle writes it
    // and the numbers must follow. Otherwise the radar's own values are.
    const ZoneEdit z = m_zone_get ? m_zone_get() : ZoneEdit();
    const bool mine = z.active && z.radar_index == m_index && z.id == id;
    // Re-assert rather than assume: expanding this control's section calls
    // Show() recursively over every child (which un-hides Save), and a rebuild
    // re-themes the fields, so the mode has to be reapplied when it has drifted.
    if (mine != *editing || save->IsShown() != *editing ||
        fields[0]->IsEditable() != *editing) {
      *editing = mine;
      apply_mode();
    }
    double vals[4];
    if (mine) {
      vals[0] = RadToDeg(z.start_rad);
      vals[1] = RadToDeg(z.end_rad);
      vals[2] = z.start_m;
      vals[3] = z.end_m;
    } else {
      const ControlValue v = controls()->Value(id);
      vals[0] = RadToDeg(v.value);
      vals[1] = RadToDeg(v.endValue);
      vals[2] = v.startDistance;
      vals[3] = v.endDistance;
    }
    // Enabled belongs to the radar, not to the edit, so it tracks either way.
    const ControlValue cv = controls()->Value(id);
    if (cv.has_enabled) en->SetValue(cv.enabled);
    // Never overwrite the field being typed in; every other one follows.
    for (int i = 0; i < 4; ++i)
      if (i != *focused)
        fields[i]->ChangeValue(wxString::Format("%ld", RoundL(vals[i])));
  });
}

void ControlsPanel::AddPlaceholder(wxSizer* outer, const ControlDef& def) {
  outer->Add(new wxStaticText(
                 this, wxID_ANY,
                 wxString::FromUTF8((def.name + " (" + def.dataType + ")").c_str())),
             0, wxALL, 4);
}
