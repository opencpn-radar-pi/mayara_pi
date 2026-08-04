/******************************************************************************
 * mayara_pi - optional local copy of mayara-server.
 *****************************************************************************/
#include "MayaraServer.h"

#include <fstream>
#include <memory>

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/zstream.h>

#if wxUSE_TARSTREAM
#include <wx/tarstrm.h>
#endif

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <nlohmann/json.hpp>

#include "ocpn_plugin.h"

using json = nlohmann::json;

namespace {

// mayara-server publishes one asset per platform on every release, and the tag
// is part of the file name, so the URL always comes from the release JSON
// rather than being composed here.
const char* kLatestReleaseUrl =
    "https://api.github.com/repos/MarineYachtRadar/mayara-server/releases/"
    "latest";

const char* kConfigGroup = "/PlugIns/mayara";
const int kCheckIntervalSecs = 7 * 24 * 3600;  // "once a week"
const int kRetryIntervalSecs = 3600;  // after a failed check (offline at boot)
const int kWatchdogMs = 30000;        // a check silent this long is dead
const int kTimerId = 4711;
const int kWrap = 380;  // px; keeps the explanatory text a readable column

#ifdef _WIN32
const char* kBinaryName = "mayara-server.exe";
#else
const char* kBinaryName = "mayara-server";
#endif

// The release-asset suffix for this platform, or empty when mayara-server ships
// no binary we could run. On Windows we ask the OS rather than looking at our
// own bitness: OpenCPN itself may be a 32-bit build on a 64-bit machine.
std::string AssetSuffix() {
#if defined(__APPLE__)
  return "universal-apple-darwin.tar.gz";
#elif defined(_WIN32)
  return wxIsPlatform64Bit() ? "x86_64-pc-windows.zip" : std::string();
#elif defined(__linux__) && defined(__x86_64__)
  return "x86_64-unknown-linux-musl.tar.gz";
#elif defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
  return "aarch64-unknown-linux-musl.tar.gz";
#else
  return "";
#endif
}

bool EndsWith(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() &&
         s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

wxString HumanSize(long bytes) {
  if (bytes <= 0) return wxEmptyString;
  return wxString::Format("%.0f MB", bytes / (1024.0 * 1024.0));
}

// Copy the single `mayara-server` entry out of an archive. wxBase gives us gzip
// and zip readers, so this needs no extra dependency.
bool ExtractBinary(const wxString& archive, const wxString& dest,
                   wxString* error) {
  wxFFileInputStream file(archive);
  if (!file.IsOk()) {
    *error = _("Cannot read the downloaded archive.");
    return false;
  }

  // The entry is deliberately kept alive until its bytes have been copied.
  auto copy_matching = [&](wxArchiveInputStream& in) {
    while (true) {
      std::unique_ptr<wxArchiveEntry> entry(in.GetNextEntry());
      if (!entry) return false;
      if (!entry->GetName().AfterLast('/').IsSameAs(kBinaryName,
                                                    /*caseSensitive=*/false))
        continue;
      wxFFileOutputStream out(dest);
      if (!out.IsOk()) {
        *error = wxString::Format(_("Cannot write %s."), dest);
        return false;
      }
      in.Read(out);
      out.Close();
      return true;
    }
  };

  bool found = false;
  if (archive.Lower().EndsWith(".zip")) {
    wxZipInputStream zip(file);
    found = copy_matching(zip);
#if wxUSE_TARSTREAM
  } else {
    wxZlibInputStream gz(file, wxZLIB_GZIP);
    wxTarInputStream tar(gz);
    found = copy_matching(tar);
#endif
  }
  if (!found && error->IsEmpty())
    *error = _("The downloaded archive does not contain mayara-server.");
  return found;
}

}  // namespace

MayaraServer::MayaraServer(opencpn_plugin* plugin) : m_plugin(plugin) {
  Bind(wxEVT_DOWNLOAD_EVENT, &MayaraServer::OnDownloadEvent, this);
  m_watchdog.SetOwner(this, kTimerId);
  Bind(wxEVT_TIMER, &MayaraServer::OnWatchdog, this, kTimerId);
  if (!PlatformSupported()) m_state = CheckState::kUnsupported;
}

MayaraServer::~MayaraServer() {
  m_watchdog.Stop();
  if (m_state == CheckState::kChecking && m_dl_handle)
    OCPN_cancelDownloadFileBackground(m_dl_handle);
  Stop();
}

bool MayaraServer::PlatformSupported() { return !AssetSuffix().empty(); }

std::string MayaraServer::LocalUrl() { return "http://127.0.0.1:6502"; }

// --- Configuration ---------------------------------------------------------

void MayaraServer::LoadConfig() {
  wxFileConfig* cfg = GetOCPNConfigObject();
  if (!cfg) return;
  cfg->SetPath(kConfigGroup);
  cfg->Read("LocalServerEnabled", &m_enabled, false);
  cfg->Read("LocalServerVersion", &m_installed_version);
  cfg->Read("LocalServerAllowWifi", &m_opts.allow_wifi, false);
  wxString brand;
  cfg->Read("LocalServerBrand", &brand);
  m_opts.brand = std::string(brand.mb_str());
  long last = 0;
  cfg->Read("LocalServerLastCheck", &last, 0);
  m_last_check = static_cast<time_t>(last);
  // A binary deleted behind our back must not look installed.
  if (!Installed()) m_installed_version.Clear();
}

void MayaraServer::SaveConfig() {
  wxFileConfig* cfg = GetOCPNConfigObject();
  if (!cfg) return;
  cfg->SetPath(kConfigGroup);
  cfg->Write("LocalServerEnabled", m_enabled);
  cfg->Write("LocalServerVersion", m_installed_version);
  cfg->Write("LocalServerAllowWifi", m_opts.allow_wifi);
  cfg->Write("LocalServerBrand",
             wxString::FromUTF8(m_opts.brand.c_str()));
  cfg->Write("LocalServerLastCheck", static_cast<long>(m_last_check));
  cfg->Flush();
}

// --- Release check ---------------------------------------------------------

void MayaraServer::CheckLatest() {
  if (!PlatformSupported() || m_state == CheckState::kChecking) return;
  m_json_path = wxFileName::CreateTempFileName("mayara_rel");
  if (m_json_path.IsEmpty()) return;
  m_last_attempt = wxDateTime::Now().GetTicks();
  SetState(CheckState::kChecking);
  m_dl_handle = 0;
  // The return value is deliberately ignored: OpenCPN reports OCPN_DL_STARTED
  // on some failure paths too, so the watchdog below is what really decides.
  OCPN_downloadFileBackground(kLatestReleaseUrl, m_json_path, this,
                              &m_dl_handle);
  m_watchdog.StartOnce(kWatchdogMs);
}

void MayaraServer::OnDownloadEvent(wxEvent& e) {
  auto& ev = static_cast<OCPN_downloadEvent&>(e);
  if (ev.getDLEventCondition() != OCPN_DL_EVENT_TYPE_END) return;
  if (m_state != CheckState::kChecking) return;
  m_watchdog.Stop();
  m_dl_handle = 0;
  if (ev.getDLEventStatus() != OCPN_DL_NO_ERROR) {
    // Offline, DNS failure, rate limited... say nothing, just don't offer it.
    if (!m_json_path.IsEmpty()) wxRemoveFile(m_json_path);
    m_json_path.Clear();
    SetState(CheckState::kFailed);
    return;
  }
  FinishCheck();
}

void MayaraServer::OnWatchdog(wxTimerEvent&) {
  if (m_state != CheckState::kChecking) return;
  if (m_dl_handle) OCPN_cancelDownloadFileBackground(m_dl_handle);
  m_dl_handle = 0;
  if (!m_json_path.IsEmpty()) wxRemoveFile(m_json_path);
  m_json_path.Clear();
  SetState(CheckState::kFailed);
}

void MayaraServer::FinishCheck() {
  Release rel;
  const std::string suffix = AssetSuffix();
  try {
    std::ifstream in(std::string(m_json_path.mb_str()));
    json j = json::parse(in);
    rel.tag = j.value("tag_name", std::string());
    if (j.contains("assets") && j["assets"].is_array())
      for (const auto& a : j["assets"]) {
        const std::string name = a.value("name", std::string());
        if (!EndsWith(name, suffix)) continue;
        rel.name = name;
        rel.url = a.value("browser_download_url", std::string());
        rel.size = a.value("size", 0L);
        break;
      }
  } catch (const std::exception&) {
    rel = Release();
  }
  wxRemoveFile(m_json_path);
  m_json_path.Clear();

  if (rel.tag.empty() || rel.url.empty()) {
    SetState(CheckState::kFailed);
    return;
  }
  m_latest = rel;
  m_last_check = wxDateTime::Now().GetTicks();
  SaveConfig();
  SetState(CheckState::kDone);
}

void MayaraServer::Poll() {
  // The child died on its own (crash, or the port was already taken): stop
  // claiming it runs, but leave the "run it here" preference alone.
  if (m_pid && !wxProcess::Exists(m_pid)) {
    m_pid = 0;
    Notify();
  }
  if (m_state == CheckState::kChecking || !PlatformSupported()) return;
  const time_t now = wxDateTime::Now().GetTicks();
  if (m_state == CheckState::kFailed) {
    // Offline at start-up is normal on a boat; try again later in the session.
    if (now - m_last_attempt >= kRetryIntervalSecs) CheckLatest();
  } else if (m_last_check != 0 && now - m_last_check >= kCheckIntervalSecs) {
    CheckLatest();
  }
}

bool MayaraServer::UpdateAvailable() const {
  return m_state == CheckState::kDone && Installed() &&
         !m_installed_version.IsEmpty() &&
         m_installed_version != wxString::FromUTF8(m_latest.tag.c_str());
}

// --- Installation ----------------------------------------------------------

wxString MayaraServer::InstallDir() const {
  // Next to the plugin library, as long as we may write there. Package-managed
  // installs (Program Files, /usr/lib) are read-only, so fall back to the
  // per-user OpenCPN data directory.
  if (m_plugin) {
    const wxString lib = GetPlugInPath(m_plugin);
    if (!lib.IsEmpty()) {
      const wxString dir = wxFileName(lib).GetPath();
      if (!dir.IsEmpty() && wxFileName::IsDirWritable(dir)) return dir;
    }
  }
  wxString* priv = GetpPrivateApplicationDataLocation();
  wxFileName fallback = wxFileName::DirName(
      priv && !priv->IsEmpty() ? *priv
                               : wxStandardPaths::Get().GetUserDataDir());
  fallback.AppendDir("mayara");
  return fallback.GetPath();
}

wxString MayaraServer::BinaryPath() const {
  return wxFileName(InstallDir(), kBinaryName).GetFullPath();
}

bool MayaraServer::Installed() const { return wxFileExists(BinaryPath()); }

bool MayaraServer::DownloadAndInstall(wxWindow* parent, wxString* error) {
  wxString ignored;
  if (!error) error = &ignored;
  error->Clear();
  if (m_state != CheckState::kDone || m_latest.url.empty()) {
    *error = _("No mayara-server download is available.");
    return false;
  }

  const wxString dir = InstallDir();
  if (!wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    *error = wxString::Format(_("Cannot create %s."), dir);
    return false;
  }

  const wxString url = wxString::FromUTF8(m_latest.url.c_str());
  const wxString archive =
      wxFileName(wxFileName::GetTempDir(),
                 wxString::FromUTF8(m_latest.name.c_str()))
          .GetFullPath();
  const _OCPN_DLStatus st = OCPN_downloadFile(
      url, archive, _("Downloading mayara-server"),
      wxString::Format(_("mayara-server %s (%s) from "),
                       wxString::FromUTF8(m_latest.tag.c_str()),
                       HumanSize(m_latest.size)),
      wxNullBitmap, parent,
      OCPN_DLDS_ELAPSED_TIME | OCPN_DLDS_ESTIMATED_TIME | OCPN_DLDS_SPEED |
          OCPN_DLDS_SIZE | OCPN_DLDS_URL | OCPN_DLDS_CAN_ABORT |
          OCPN_DLDS_AUTO_CLOSE,
      0);
  if (st != OCPN_DL_NO_ERROR) {
    if (wxFileExists(archive)) wxRemoveFile(archive);
    if (st != OCPN_DL_ABORTED) *error = _("The download did not complete.");
    return false;
  }

  // Windows cannot replace a running executable, so the old one goes first.
  const bool was_running = Running();
  Stop();

  const wxString staged = BinaryPath() + ".new";
  const bool ok = ExtractBinary(archive, staged, error);
  wxRemoveFile(archive);
  if (!ok) {
    if (wxFileExists(staged)) wxRemoveFile(staged);
    if (was_running) Start();
    return false;
  }
#ifndef _WIN32
  chmod(staged.fn_str(), 0755);
#endif
  if (wxFileExists(BinaryPath())) wxRemoveFile(BinaryPath());
  if (!wxRenameFile(staged, BinaryPath())) {
    wxRemoveFile(staged);
    *error = wxString::Format(_("Cannot install %s."), BinaryPath());
    if (was_running) Start();
    return false;
  }

  m_installed_version = wxString::FromUTF8(m_latest.tag.c_str());
  m_enabled = true;  // downloading it is the act of choosing to run it here
  SaveConfig();
  Start();
  Notify();
  return true;
}

// --- Child process ---------------------------------------------------------

bool MayaraServer::Running() const {
  return m_pid != 0 && wxProcess::Exists(m_pid);
}

bool MayaraServer::Start() {
  if (Running()) return true;
  m_pid = 0;
  if (!Installed()) return false;
  // No wxProcess callback object on purpose: that would be a vtable in this
  // plugin's shared library which OpenCPN's event loop could call into after we
  // are unloaded. Liveness is polled with wxProcess::Exists() instead.
  wxString cmd = "\"" + BinaryPath() + "\"";
  // --parent: run as our helper. mayara-server then binds localhost only and
  // stays off mDNS (this copy is ours, not the network's -- we reach it through
  // LocalUrl()), and it exits once we are gone. Stop() cannot do that job alone:
  // if OpenCPN crashes or is force-quit our destructor never runs, and the
  // orphan keeps holding port 6502 against the next session.
  cmd += wxString::Format(" --parent %lu", wxGetProcessId());
  // mayara-server skips WiFi interfaces unless asked: right on a wired boat
  // server, wrong on the laptop this whole option exists for -- hence the
  // checkbox rather than a fixed choice either way.
  if (m_opts.allow_wifi) cmd += " --allow-wifi";
  // The emulator is offered as a brand, but it is not one to the server:
  // --brand only narrows the search for real hardware, while the fake radar is
  // created solely for --emulator.
  if (m_opts.brand == kEmulatorBrand) {
    cmd += " --emulator";
  } else if (!m_opts.brand.empty()) {
    cmd += " --brand " + wxString::FromUTF8(m_opts.brand.c_str());
  }
  const long pid = wxExecute(cmd, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE);
  if (pid <= 0) return false;
  m_pid = pid;
  Notify();
  return true;
}

void MayaraServer::Stop() {
  if (!m_pid) return;
  const long pid = m_pid;
  m_pid = 0;
  if (wxProcess::Exists(pid)) {
    wxKill(pid, wxSIGTERM, nullptr, wxKILL_CHILDREN);
    for (int i = 0; i < 20 && wxProcess::Exists(pid); ++i) wxMilliSleep(50);
    if (wxProcess::Exists(pid))
      wxKill(pid, wxSIGKILL, nullptr, wxKILL_CHILDREN);
  }
  Notify();
}

const char* MayaraServer::kEmulatorBrand = "emulator";

const std::vector<std::string>& MayaraServer::Brands() {
  static const std::vector<std::string> kBrands = {
      "navico", "furuno", "garmin", "koden", "raymarine", kEmulatorBrand};
  return kBrands;
}

void MayaraServer::SetOptions(const LocalOptions& o) {
  if (o.allow_wifi == m_opts.allow_wifi && o.brand == m_opts.brand) return;
  m_opts = o;
  SaveConfig();
  // These are command-line arguments, so they only take effect on a restart.
  if (Running()) {
    Stop();
    Start();
  }
  Notify();
}

void MayaraServer::SetEnabled(bool on) {
  if (on == m_enabled) return;
  m_enabled = on;
  SaveConfig();
  if (on)
    Start();
  else
    Stop();
  Notify();
}

// --- Observers -------------------------------------------------------------

void MayaraServer::AddObserver(void* owner, std::function<void()> cb) {
  RemoveObserver(owner);
  m_observers.emplace_back(owner, std::move(cb));
}

void MayaraServer::RemoveObserver(void* owner) {
  for (size_t i = 0; i < m_observers.size(); ++i)
    if (m_observers[i].first == owner) {
      m_observers.erase(m_observers.begin() + i);
      return;
    }
}

void MayaraServer::SetState(CheckState s) {
  if (s == m_state) return;
  m_state = s;
  Notify();
}

void MayaraServer::Notify() {
  // Copy first: a callback may add or drop observers.
  auto observers = m_observers;
  for (auto& o : observers)
    if (o.second) o.second();
}

// ---------------------------------------------------------------------------
// MayaraServerPanel
// ---------------------------------------------------------------------------

MayaraServerPanel::MayaraServerPanel(wxWindow* parent, MayaraServer* server,
                                     std::function<void()> on_changed)
    : wxPanel(parent, wxID_ANY),
      m_server(server),
      m_on_changed(std::move(on_changed)) {
  auto* box = new wxStaticBoxSizer(wxVERTICAL, this,
                                   _("Run mayara-server on this computer"));
  wxWindow* sbox = box->GetStaticBox();

  auto* advice = new wxStaticText(
      sbox, wxID_ANY,
      _("If you already run Signal K on a boat server, install the Mayara "
        "plugin there: one server then serves every device on board, "
        "and it keeps running when this computer is off.\n\n"
        "Otherwise mayara-server can be downloaded and run here, next to "
        "OpenCPN."));
  advice->Wrap(kWrap);
  box->Add(advice, 0, wxALL, 8);

  m_status = new wxStaticText(sbox, wxID_ANY, wxEmptyString);
  box->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  m_download = new wxButton(sbox, wxID_ANY, _("Download mayara locally"));
  m_download->Bind(wxEVT_BUTTON, &MayaraServerPanel::OnDownload, this);
  row->Add(m_download, 0, wxRIGHT, 12);
  m_run = new wxCheckBox(sbox, wxID_ANY, _("Run it here"));
  m_run->Bind(wxEVT_CHECKBOX, &MayaraServerPanel::OnRunLocally, this);
  row->Add(m_run, 0, wxALIGN_CENTER_VERTICAL);
  box->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  SetSizer(box);
  if (m_server) m_server->AddObserver(this, [this]() { Sync(); });
  Sync();
}

MayaraServerPanel::~MayaraServerPanel() {
  if (m_server) m_server->RemoveObserver(this);
}

void MayaraServerPanel::Sync() {
  if (!m_server) return;
  const wxString installed = m_server->InstalledVersion();
  const MayaraServer::Release& latest = m_server->Latest();
  const wxString tag = wxString::FromUTF8(latest.tag.c_str());
  const bool have = m_server->Installed();

  wxString status;
  if (have)
    status = wxString::Format(
        m_server->Running()
            ? _("mayara-server %s is installed here and running.")
            : _("mayara-server %s is installed here but not running."),
        installed);

  bool can_download = false;
  wxString label = _("Download mayara locally");
  switch (m_server->State()) {
    case MayaraServer::CheckState::kUnsupported:
      if (!have)
        status = _("There is no mayara-server download for this platform.");
      break;
    case MayaraServer::CheckState::kIdle:
    case MayaraServer::CheckState::kChecking:
      if (!have) status = _("Looking for a mayara-server download…");
      break;
    case MayaraServer::CheckState::kFailed:
      // Offline is normal on a boat, and a server on the LAN is still found
      // without github.com; say it once, quietly, and only when it matters.
      if (!have)
        status = _("github.com cannot be reached, so there is nothing to "
                   "download right now. A mayara-server already on the network "
                   "is still found normally.");
      break;
    case MayaraServer::CheckState::kDone:
      if (!have) {
        can_download = true;
        status = wxString::Format(_("mayara-server %s can be downloaded (%s)."),
                                  tag, HumanSize(latest.size));
      } else if (m_server->UpdateAvailable()) {
        can_download = true;
        label = wxString::Format(_("Update to %s"), tag);
        status += wxString::Format(_("  %s is available (%s)."), tag,
                                   HumanSize(latest.size));
      } else {
        status += _("  It is up to date.");
      }
      break;
  }

  m_status->SetLabel(status);
  m_status->Wrap(kWrap);
  m_download->SetLabel(label);
  m_download->Show(can_download);
  m_run->Show(have);
  m_run->SetValue(m_server->Enabled());
  Layout();
  if (m_on_changed) m_on_changed();
}

void MayaraServerPanel::OnDownload(wxCommandEvent&) {
  if (!m_server) return;
  wxString error;
  if (!m_server->DownloadAndInstall(this, &error) && !error.IsEmpty())
    wxMessageBox(error, _("Mayara"), wxOK | wxICON_WARNING, this);
  Sync();
}

void MayaraServerPanel::OnRunLocally(wxCommandEvent&) {
  if (m_server) m_server->SetEnabled(m_run->GetValue());
  Sync();
}
