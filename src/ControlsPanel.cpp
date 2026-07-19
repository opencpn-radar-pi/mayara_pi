/******************************************************************************
 * mayara_pi - schema-driven control panel.
 *****************************************************************************/
#include "ControlsPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include <wx/choice.h>
#include <wx/statline.h>
#include <wx/tglbtn.h>

#include "MayaraClient.h"

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
  auto* close = new wxButton(this, wxID_ANY, wxT("✕"), wxDefaultPosition,
                             wxSize(30, 26), wxBU_EXACTFIT);
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

void ControlsPanel::OnTimer(wxTimerEvent&) {
  if (!m_client) return;
  RadarControls* controls = m_client->Controls();
  if (!controls->HasSchema()) return;
  const uint64_t gen = controls->Generation();
  if (!m_built) {
    Rebuild();
    m_built = true;
    m_last_gen = gen;
    return;
  }
  if (gen != m_last_gen) {
    m_last_gen = gen;
    ApplyValues();  // schema is stable; only values change
  }
}

void ControlsPanel::ApplyValues() {
  for (auto& u : m_updaters) u();
}

void ControlsPanel::Rebuild() {
  DestroyChildren();
  m_updaters.clear();

  RadarControls* controls = m_client->Controls();
  std::vector<ControlDef> defs = controls->Schema();
  std::vector<int> ranges = controls->SupportedRanges();

  std::map<std::string, const ControlDef*> by_id;
  for (const auto& d : defs) by_id[d.id] = &d;

  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(MakeCloseRow(), 0, wxEXPAND);

  // --- Quick controls: prominent, fixed placement ---
  if (by_id.count("power")) AddEnum(root, *by_id["power"], /*buttons=*/true);
  if (by_id.count("range")) AddRange(root, *by_id["range"], ranges);
  if (by_id.count("rangeUnits"))
    AddEnum(root, *by_id["rangeUnits"], /*buttons=*/true);
  root->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 4);

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

    auto* header = new wxStaticText(this, wxID_ANY, wxString(cat).Capitalize());
    wxFont f = header->GetFont();
    f.MakeBold();
    header->SetFont(f);
    root->Add(header, 0, wxTOP | wxLEFT, 8);

    for (const ControlDef* d : group) {
      if (d->isReadOnly)
        AddReadonly(root, *d);
      else if (d->dataType == "number")
        AddNumber(root, *d);
      else if (d->dataType == "enum")
        AddEnum(root, *d, /*buttons=*/false);
      else if (d->dataType == "button")
        AddButton(root, *d);
      else if (d->dataType == "string")
        AddReadonly(root, *d);
      else
        AddPlaceholder(root, *d);  // sector/zone/rect: editors later
    }
  }

  SetSizer(root);  // deletes the previous sizer
  FitInside();
  Layout();
  ApplyValues();
}

void ControlsPanel::AddNumber(wxSizer* outer, const ControlDef& def) {
  auto* box = new wxBoxSizer(wxVERTICAL);
  box->Add(new wxStaticText(this, wxID_ANY,
                            wxString::FromUTF8(def.name.c_str())),
           0, wxLEFT, 2);

  // slider | value | Auto  — value has a fixed width so it never clips.
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* slider = new wxSlider(this, wxID_ANY, 0, 0, 1000);
  slider->SetMinSize(wxSize(80, -1));
  row->Add(slider, 1, wxALIGN_CENTER_VERTICAL);
  auto* valtext = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition,
                                   wxSize(46, -1),
                                   wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
  row->Add(valtext, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
  wxToggleButton* autobtn = nullptr;
  if (def.hasAuto) {
    autobtn = new wxToggleButton(this, wxID_ANY, _("Auto"), wxDefaultPosition,
                                 wxSize(52, -1));
    row->Add(autobtn, 0, wxALIGN_CENTER_VERTICAL);
  }
  box->Add(row, 0, wxEXPAND);
  outer->Add(box, 0, wxEXPAND | wxALL, 4);

  const std::string id = def.id;
  const std::string units = def.units;
  const double mn = def.has_min ? def.minValue : 0.0;
  const double mx = def.has_max ? def.maxValue : 100.0;

  slider->Bind(wxEVT_SCROLL_THUMBRELEASE, [this, id, mn, mx, def, slider](
                                              wxScrollEvent&) {
    const double v = mn + (mx - mn) * slider->GetValue() / 1000.0;
    Set(id, BodyValueAuto(v, def.hasAuto, false));
  });
  if (autobtn) {
    autobtn->Bind(wxEVT_TOGGLEBUTTON, [this, id, autobtn, slider](
                                          wxCommandEvent&) {
      const bool a = autobtn->GetValue();
      slider->Enable(!a);  // optimistic; the control stream confirms
      Set(id, BodyAuto(a));
    });
  }

  m_updaters.push_back([this, id, slider, valtext, autobtn, mn, mx, units]() {
    ControlValue v = m_client->Controls()->Value(id);
    if (v.has_value) {
      if (!slider->HasFocus() && mx > mn)
        slider->SetValue(Clampi(
            static_cast<int>((v.value - mn) / (mx - mn) * 1000.0 + 0.5), 0,
            1000));
      valtext->SetLabel(FormatVal(v.value, units));
    }
    if (autobtn) {
      autobtn->SetValue(v.auto_);
      slider->Enable(!v.auto_);
    }
  });
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
    std::vector<std::pair<int, wxToggleButton*>> buttons;
    for (int v : values) {
      auto dit = def.descriptions.find(v);
      wxString label = dit != def.descriptions.end()
                           ? wxString::FromUTF8(dit->second.c_str())
                           : wxString::Format("%d", v);
      auto* b = new wxToggleButton(this, wxID_ANY, label);
      row->Add(b, 1, wxALL, 2);
      buttons.emplace_back(v, b);
      b->Bind(wxEVT_TOGGLEBUTTON, [this, id, v](wxCommandEvent&) {
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
    auto* choice = new wxChoice(this, wxID_ANY);
    for (int v : values) {
      auto dit = def.descriptions.find(v);
      wxString label = dit != def.descriptions.end()
                           ? wxString::FromUTF8(dit->second.c_str())
                           : wxString::Format("%d", v);
      choice->Append(label, reinterpret_cast<void*>(static_cast<intptr_t>(v)));
    }
    outer->Add(choice, 0, wxEXPAND | wxALL, 4);
    choice->Bind(wxEVT_CHOICE, [this, id, choice](wxCommandEvent&) {
      int sel = choice->GetSelection();
      if (sel == wxNOT_FOUND) return;
      int v = static_cast<int>(reinterpret_cast<intptr_t>(
          choice->GetClientData(sel)));
      Set(id, BodyValue(v));
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
  auto* choice = new wxChoice(this, wxID_ANY);

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
  auto* b = new wxButton(this, wxID_ANY, wxString::FromUTF8(def.name.c_str()));
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
