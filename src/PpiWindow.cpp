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
    EVT_SIZE(MayaraPpiWindow::OnSize)
wxEND_EVENT_TABLE()

MayaraPpiWindow::MayaraPpiWindow(wxWindow* parent, MayaraClient* client,
                                 std::vector<int> radar_indices)
    : wxDialog(parent, wxID_ANY, _("Mayara Radar"), wxDefaultPosition,
               wxSize(880, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
  SetMinSize(wxSize(480, 320));
  if (radar_indices.empty()) radar_indices.push_back(0);
  m_client = client;
  SetTitle(WindowTitle(client, radar_indices));

  // The radar pictures live in m_grid; BuildGrid() arranges them by the
  // window's aspect (see DesiredCols).
  m_grid = new wxPanel(this, wxID_ANY);
  for (int ri : radar_indices)
    m_radars.push_back(new RadarDisplayPanel(m_grid, client, ri));
  BuildGrid();

  // One control panel, bound to the focused radar (the first by default). It
  // floats over the pictures (not in the sizer) so it can pop up next to
  // whichever picture's hamburger was clicked.
  m_controls = new ControlsPanel(this, client, radar_indices.front());
  m_controls->SetRadarList(radar_indices);
  m_controls->Hide();  // opened via a picture's hamburger button

  auto* sizer = new wxBoxSizer(wxHORIZONTAL);
  sizer->Add(m_grid, 1, wxEXPAND);
  SetSizer(sizer);

  ControlsPanel* controls = m_controls;
  // Open (or toggle) the controls next to picture `p`, in full or view-only
  // mode. In a multi-radar window the picture is soloed while its menu is open.
  auto open = [this, controls](RadarDisplayPanel* p, bool view_only) {
    if (controls->IsShown() && controls->RadarIndex() == p->RadarIndex() &&
        controls->IsViewMode() == view_only) {
      controls->Hide();
      if (m_solo) BuildGrid();
      return;
    }
    controls->SetViewMode(view_only);
    controls->SetRadarIndex(p->RadarIndex());
    if (m_radars.size() > 1) SoloPicture(p);
    PositionControls(p);
  };
  for (RadarDisplayPanel* p : m_radars) {
    p->SetMenuCallback([open, p]() { open(p, /*view_only=*/false); });
    p->SetViewCallback([open, p]() { open(p, /*view_only=*/true); });
    // A gauge icon opens just that one control as a small top-right popup.
    p->SetControlCallback([this, controls, p](const std::string& id) {
      if (controls->IsShown() && controls->SingleControl() == id &&
          controls->RadarIndex() == p->RadarIndex()) {
        controls->Hide();
        if (m_solo) BuildGrid();
        return;
      }
      if (m_solo) BuildGrid();  // the popup doesn't solo the picture
      controls->SetRadarIndex(p->RadarIndex());
      controls->SetSingleControl(id);
      PositionControlsTopRight();
    });
    // Clicking a picture re-homes the open controls next to it.
    p->SetFocusCallback([this, controls, p]() {
      if (controls->IsShown()) {
        controls->SetRadarIndex(p->RadarIndex());
        PositionControls(p);
      }
    });
  }
  controls->SetCloseCallback([this, controls]() {
    controls->Hide();
    if (m_solo) BuildGrid();  // restore the other pictures
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
        const int h = GetSize().y;
        if (show) {
          Layout();
          SetSize(wxSize(std::max(GetSize().x, 820), h));
        } else if (m_controls) {
          // Show only the menu: dock the floating controls to fill a narrow
          // window.
          const int ctrl_w =
              std::max(320, m_controls->GetEffectiveMinSize().x);
          SetClientSize(ctrl_w, GetClientSize().y);
          m_controls->SetSize(0, 0, ctrl_w, GetClientSize().y);
          m_controls->Show(true);
          m_controls->Raise();
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

RadarDisplayPanel* MayaraPpiWindow::FocusedPanel() {
  const int ri = m_controls ? m_controls->RadarIndex()
                            : (m_radars.empty() ? 0 : m_radars[0]->RadarIndex());
  for (RadarDisplayPanel* p : m_radars)
    if (p->RadarIndex() == ri) return p;
  return m_radars.empty() ? nullptr : m_radars[0];
}

void MayaraPpiWindow::PositionControls(RadarDisplayPanel* focused,
                                       bool allow_grow) {
  if (!m_controls || !focused) return;
  const int ctrl_w = std::max(320, m_controls->GetEffectiveMinSize().x);
  const int kMinPicture = 240;  // keep at least this much picture beside it
  // Right edge of the focused picture, in this window's client coordinates.
  wxPoint tr = focused->GetScreenPosition();
  tr.x += focused->GetSize().x;
  int x = ScreenToClient(tr).x;
  wxSize cs = GetClientSize();
  if (x + ctrl_w > cs.x) {
    // No room to the right of the picture: dock the controls at the right edge
    // (overlaying the picture). Only widen the window if that would leave too
    // little picture -- so repeatedly opening the menu doesn't keep growing it.
    x = cs.x - ctrl_w;
    if (allow_grow && x < kMinPicture) {
      GrowForControls(kMinPicture - x);
      cs = GetClientSize();
      x = cs.x - ctrl_w;
    }
    x = std::max(0, x);
  }
  m_controls->SetSize(x, 0, ctrl_w, cs.y);
  m_controls->Show(true);
  m_controls->Raise();
}

int MayaraPpiWindow::DesiredCols() const {
  const int n = static_cast<int>(m_radars.size());
  if (n <= 1) return 1;
  const wxSize cs = GetClientSize();
  if (cs.y > cs.x) return 1;  // narrow window: stack top-to-bottom
  return std::max(1, static_cast<int>(std::ceil(std::sqrt((double)n))));
}

void MayaraPpiWindow::BuildGrid() {
  m_solo = false;
  for (RadarDisplayPanel* p : m_radars) p->Show(true);
  const int n = static_cast<int>(m_radars.size());
  m_grid_cols = DesiredCols();
  const int rows = (n + m_grid_cols - 1) / m_grid_cols;
  auto* g = new wxGridSizer(rows, m_grid_cols, 2, 2);
  for (RadarDisplayPanel* p : m_radars) g->Add(p, 1, wxEXPAND);
  m_grid->SetSizer(g);  // deletes the previous sizer
  m_grid->Layout();
}

void MayaraPpiWindow::SoloPicture(RadarDisplayPanel* only) {
  m_solo = true;
  m_grid_cols = -1;
  for (RadarDisplayPanel* p : m_radars) p->Show(p == only);
  auto* s = new wxBoxSizer(wxHORIZONTAL);
  s->Add(only, 1, wxEXPAND);
  m_grid->SetSizer(s);  // deletes the previous sizer
  m_grid->Layout();
}

void MayaraPpiWindow::PositionControlsTopRight() {
  if (!m_controls) return;
  const int w = 300, h = 120, barW = 52;  // clear of the icon bar
  const wxSize cs = GetClientSize();
  const int x = std::max(6, cs.x - barW - w);
  m_controls->SetSize(x, 6, w, std::min(h, cs.y - 12));
  m_controls->Show(true);
  m_controls->Raise();
}

void MayaraPpiWindow::OnSize(wxSizeEvent& event) {
  event.Skip();
  // Re-flow the (non-soloed) grid when the window's aspect flips.
  if (!m_solo && m_radars.size() > 1 && DesiredCols() != m_grid_cols)
    BuildGrid();
  if (m_controls && m_controls->IsShown()) {
    if (!m_controls->SingleControl().empty())
      PositionControlsTopRight();
    else
      PositionControls(FocusedPanel(), /*allow_grow=*/false);
  }
}

void MayaraPpiWindow::SetOrientationHandlers(
    std::function<int(const std::string&)> get_mode,
    std::function<void(const std::string&, int)> set_mode) {
  m_orient_get = std::move(get_mode);
  m_orient_set = std::move(set_mode);
  // Initialise each picture from its radar's persisted orientation.
  for (RadarDisplayPanel* p : m_radars) {
    const std::string id = m_client ? m_client->RadarId(p->RadarIndex()) : "";
    p->SetOrientation(m_orient_get ? m_orient_get(id) : 0);
  }
  // Drive the View toggle from/into the focused radar's picture.
  if (m_controls)
    m_controls->SetOrientationControl(
        [this]() {
          RadarDisplayPanel* p = FocusedPanel();
          return p ? p->Orientation() : 0;
        },
        [this](int o) {
          RadarDisplayPanel* p = FocusedPanel();
          if (!p) return;
          p->SetOrientation(o);
          const std::string id = m_client ? m_client->RadarId(p->RadarIndex())
                                          : "";
          if (m_orient_set) m_orient_set(id, o);
        });
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
