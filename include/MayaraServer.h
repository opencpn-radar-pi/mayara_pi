/******************************************************************************
 * mayara_pi - optional local copy of mayara-server.
 *
 * Normally mayara-server runs on a boat server (usually as a Signal K plugin)
 * and the plugin just talks to it over the network. For people who have no such
 * server, this downloads the official mayara-server release for this platform
 * from GitHub, installs it next to the plugin library and runs it as a child
 * process on the loopback interface.
 *
 * Everything here is best-effort and silent about failure: with no route to
 * github.com the download is simply never offered, and nothing blocks the UI
 * thread waiting to find that out.
 *****************************************************************************/
#ifndef MAYARA_SERVER_H_
#define MAYARA_SERVER_H_

#include <functional>
#include <string>
#include <vector>

#include <wx/event.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/timer.h>

class opencpn_plugin;
class wxButton;
class wxCheckBox;
class wxStaticText;

class MayaraServer : public wxEvtHandler {
 public:
  explicit MayaraServer(opencpn_plugin* plugin);
  ~MayaraServer() override;

  // The release this platform can run, once CheckLatest() has succeeded.
  struct Release {
    std::string tag;   // "v3.7.0"
    std::string url;   // browser_download_url of our platform's asset
    std::string name;  // asset file name (tells .tar.gz from .zip)
    long size = 0;     // asset size in bytes
  };

  enum class CheckState {
    kIdle,         // not looked yet
    kUnsupported,  // no mayara-server binary is published for this platform
    kChecking,     // request in flight
    kDone,         // Latest() is valid
    kFailed        // no answer from github.com (offline, DNS, rate limit, ...)
  };

  // False when mayara-server publishes no binary we could run here (32-bit ARM
  // Linux, 32-bit Windows, ...). The download is then never offered.
  static bool PlatformSupported();

  // Base URL of the server we run ourselves.
  static std::string LocalUrl();

  void LoadConfig();

  // --- Release check -------------------------------------------------------
  // Asks github.com for the latest release, in the background. Harmless to call
  // when offline: the state just ends up kFailed. Called at start-up and, via
  // Poll(), once a week for long-running sessions.
  void CheckLatest();
  // Drive from the plugin's 1 Hz heartbeat: re-checks weekly and notices when
  // the child process has died.
  void Poll();
  CheckState State() const { return m_state; }
  const Release& Latest() const { return m_latest; }
  bool UpdateAvailable() const;

  // --- Installation --------------------------------------------------------
  bool Installed() const;
  wxString InstalledVersion() const { return m_installed_version; }
  wxString InstallDir() const;   // plugin library dir, or the private data dir
  wxString BinaryPath() const;
  // Downloads and unpacks Latest() (modal, with OpenCPN's progress dialog),
  // then starts it. Returns false and fills `error` if anything went wrong.
  bool DownloadAndInstall(wxWindow* parent, wxString* error);

  // --- Child process -------------------------------------------------------
  // How to launch the copy we run ourselves. mayara-server reads these only at
  // start-up, so changing them restarts a running server.
  struct LocalOptions {
    bool allow_wifi = false;  // also search WiFi interfaces (server default:
                              // off, since a boat server is wired)
    // "" = look for any brand; otherwise one of Brands(). kEmulatorBrand is a
    // brand as far as the user is concerned, but the server takes it as its own
    // flag rather than a --brand value (--brand only filters the locator; the
    // fake radar is created only for --emulator).
    std::string brand;
    // Answers mayara-server's "inform developers of successful deploy?"
    // question for it via MAYARA_TELEMETRY, so its own GUI never has to ask:
    // this plugin's checkbox is the only place a user sees that question when
    // running mayara-server through it. Default on.
    bool telemetry = true;
  };
  // What can be asked for, in menu order. "playback" is absent: it needs a
  // recording to play, which there is nowhere to choose here.
  static const std::vector<std::string>& Brands();
  static const char* kEmulatorBrand;  // "emulator"

  bool Enabled() const { return m_enabled; }
  void SetEnabled(bool on);  // persisted; starts or stops the server
  const LocalOptions& Options() const { return m_opts; }
  void SetOptions(const LocalOptions& o);  // persisted; restarts if running
  bool Running() const;
  bool Start();
  // Stops whatever is serving on the local port, not merely the child we
  // launched: see RequestQuit().
  void Stop();

  // Panels refresh through this; the panel unregisters itself when destroyed.
  void AddObserver(void* owner, std::function<void()> cb);
  void RemoveObserver(void* owner);

 private:
  void OnDownloadEvent(wxEvent& e);
  void OnWatchdog(wxTimerEvent&);
  void FinishCheck();          // parse the downloaded JSON, set the state
  void SetState(CheckState s);
  void Notify();               // tell the observers something changed
  void SaveConfig();

  opencpn_plugin* m_plugin = nullptr;
  CheckState m_state = CheckState::kIdle;
  Release m_latest;
  wxString m_installed_version;
  bool m_enabled = false;
  LocalOptions m_opts;
  long m_pid = 0;
  long m_dl_handle = 0;
  wxString m_json_path;   // temp file the release JSON lands in
  wxTimer m_watchdog;     // gives up on a check that never answers
  time_t m_last_check = 0;    // last successful check (persisted)
  time_t m_last_attempt = 0;  // last check started, successful or not
  std::vector<std::pair<void*, std::function<void()>>> m_observers;
};

// The "run mayara-server here" box, shared by the search dialog and Settings.
// Explains why a Signal K server is the better home for mayara-server before
// offering the download, so there is no second "are you sure?" dialog.
class MayaraServerPanel : public wxPanel {
 public:
  // `on_changed` is called after anything that may resize the panel, so the
  // hosting dialog can re-fit itself.
  MayaraServerPanel(wxWindow* parent, MayaraServer* server,
                    std::function<void()> on_changed = nullptr);
  ~MayaraServerPanel() override;

  void Sync();  // re-read the server state into the widgets

 private:
  void OnDownload(wxCommandEvent&);
  void OnRunLocally(wxCommandEvent&);

  MayaraServer* m_server;
  std::function<void()> m_on_changed;
  wxStaticText* m_status = nullptr;
  wxButton* m_download = nullptr;
  wxCheckBox* m_run = nullptr;
};

#endif  // MAYARA_SERVER_H_
