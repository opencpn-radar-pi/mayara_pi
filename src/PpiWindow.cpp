/******************************************************************************
 * mayara_pi - radar window (a grid of radar pictures + a shared control panel).
 *****************************************************************************/
#include "PpiWindow.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <wx/display.h>

#include "ControlsPanel.h"
#include "MayaraClient.h"
#include "RadarDisplayPanel.h"

namespace {
// "Mayara radar Halo A" / "Mayara radars Halo A + Halo B".
wxString WindowTitle(MayaraClient* client, const std::vector<int>& idx) {
  std::vector<std::string> names = client ? client->RadarNames() : std::vector<std::string>();
  wxString joined;
  for (size_t i = 0; i < idx.size(); ++i) {
    const int ri = idx[i];
    wxString nm = (ri >= 0 && ri < static_cast<int>(names.size()) &&
                   !names[ri].empty())
                      ? wxString::FromUTF8(names[ri].c_str())
                      : wxString::Format("Radar %d", ri + 1);
    if (i) joined += " + ";
    joined += nm;
  }
  if (idx.size() == 1) return "Mayara radar " + joined;
  return "Mayara radars " + joined;
}
}  // namespace

wxBEGIN_EVENT_TABLE(MayaraPpiWindow, wxDialog)
    EVT_CLOSE(MayaraPpiWindow::OnClose)
wxEND_EVENT_TABLE()

MayaraPpiWindow::MayaraPpiWindow(wxWindow* parent, MayaraClient* client,
                                 std::vector<int> radar_indices)
    : wxDialog(parent, wxID_ANY, _("Mayara Radar"), wxDefaultPosition,
               wxSize(880, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  SetMinSize(wxSize(480, 320));
  if (radar_indices.empty()) radar_indices.push_back(0);
  SetTitle(WindowTitle(client, radar_indices));

  // The radar pictures, laid out in a near-square grid.
  m_grid = new wxPanel(this, wxID_ANY);
  const int n = static_cast<int>(radar_indices.size());
  int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt((double)n))));
  int rows = (n + cols - 1) / cols;
  auto* grid = new wxGridSizer(rows, cols, 2, 2);
  for (int ri : radar_indices) {
    auto* panel = new RadarDisplayPanel(m_grid, client, ri);
    m_radars.push_back(panel);
    grid->Add(panel, 1, wxEXPAND);
  }
  m_grid->SetSizer(grid);

  // One control panel, bound to the focused radar (the first by default).
  m_controls = new ControlsPanel(this, client, radar_indices.front());
  m_controls->SetRadarList(radar_indices);
  m_controls->Hide();  // opened via a picture's hamburger button

  auto* sizer = new wxBoxSizer(wxHORIZONTAL);
  sizer->Add(m_grid, 1, wxEXPAND);
  sizer->Add(m_controls, 0, wxEXPAND);
  SetSizer(sizer);

  ControlsPanel* controls = m_controls;
  for (RadarDisplayPanel* p : m_radars) {
    const int ri = p->RadarIndex();
    // Hamburger: toggle the controls, bound to this picture's radar.
    p->SetMenuCallback([this, controls, ri]() {
      const int ctrl_w = std::max(320, controls->GetEffectiveMinSize().x);
      if (controls->IsShown() && controls->RadarIndex() == ri) {
        controls->Hide();
        Layout();
        const wxSize cs = GetClientSize();  // give the width back
        SetClientSize(std::max(360, cs.x - ctrl_w), cs.y);
      } else {
        const bool was_hidden = !controls->IsShown();
        controls->SetRadarIndex(ri);
        controls->Show(true);
        if (was_hidden) GrowForControls(ctrl_w);
        Layout();
      }
    });
    // Clicking a picture focuses its radar in the (already open) controls.
    p->SetFocusCallback([controls, ri]() {
      if (controls->IsShown()) controls->SetRadarIndex(ri);
    });
  }
  controls->SetCloseCallback([this, controls]() {
    controls->Hide();
    Layout();
  });
}

void MayaraPpiWindow::ApplyTheme(const MayaraTheme& theme) {
  SetBackgroundColour(theme.panel_bg);
  for (RadarDisplayPanel* p : m_radars) p->ApplyTheme(theme);
  if (m_controls) m_controls->ApplyTheme(theme);
  Refresh();
}

void MayaraPpiWindow::SetOverlayControl(std::function<bool()> get,
                                        std::function<void(bool)> set) {
  if (!m_controls) return;
  m_controls->SetViewControls(
      std::move(get), std::move(set),
      [this]() { return m_grid && m_grid->IsShown(); },
      [this](bool show) {
        if (m_grid) m_grid->Show(show);
        if (!show && m_controls) m_controls->Show(true);  // keep menu visible
        Layout();
        // Narrow to just the menu when the pictures are hidden; widen to show.
        const int h = GetSize().y;
        if (show) {
          SetSize(wxSize(std::max(GetSize().x, 820), h));
        } else {
          Fit();
          SetSize(wxSize(GetSize().x, h));
        }
      });
}

void MayaraPpiWindow::SetSettingsControl(std::function<void()> open) {
  if (m_controls) m_controls->SetSettingsCallback(std::move(open));
}

void MayaraPpiWindow::SetAutoLayoutControl(std::function<void()> cb) {
  if (m_controls) m_controls->SetAutoLayoutCallback(std::move(cb));
}

void MayaraPpiWindow::SetNavProvider(std::function<NavState()> provider) {
  for (RadarDisplayPanel* p : m_radars) p->SetNavProvider(provider);
}

void MayaraPpiWindow::SetOrientation(int o) {
  for (RadarDisplayPanel* p : m_radars) p->SetOrientation(o);
}

void MayaraPpiWindow::SetOrientationControl(std::function<int()> get,
                                            std::function<void(int)> set) {
  if (m_controls) m_controls->SetOrientationControl(std::move(get),
                                                    std::move(set));
}

void MayaraPpiWindow::GrowForControls(int extra) {
  const wxSize cs = GetClientSize();
  SetClientSize(cs.x + extra, cs.y);
  // Keep the (now wider) window within the display it is on.
  int disp = wxDisplay::GetFromWindow(this);
  if (disp == wxNOT_FOUND) disp = 0;
  const wxRect area = wxDisplay(static_cast<unsigned>(disp)).GetClientArea();
  const wxRect r = GetScreenRect();
  int x = r.x, y = r.y;
  if (r.GetRight() > area.GetRight())
    x = std::max(area.x, area.GetRight() - r.width);
  if (r.GetBottom() > area.GetBottom())
    y = std::max(area.y, area.GetBottom() - r.height);
  if (x != r.x || y != r.y) Move(x, y);
}

void MayaraPpiWindow::OnClose(wxCloseEvent& event) {
  // The plugin owns this window's lifetime; hide instead of destroying.
  Hide();
  event.Veto();
}
