/******************************************************************************
 * mayara_pi - client for mayara-server (REST + WebSocket).
 *
 * Discovers and streams ALL radars the server exposes. Each radar has its own
 * spoke stream, image state and control model; a single Signal K control stream
 * routes updates to the right radar by id. The UI picks an "active" radar.
 *****************************************************************************/
#ifndef MAYARA_CLIENT_H_
#define MAYARA_CLIENT_H_

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RadarControls.h"
#include "RadarState.h"

namespace ix {
class WebSocket;
}

struct Radar {
  Radar();
  ~Radar();
  std::string id;
  std::string name;
  std::string spoke_url;
  RadarState state;
  RadarControls controls;
  std::unique_ptr<ix::WebSocket> spoke_ws;
  std::atomic<bool> streaming{false};
  std::atomic<bool> ws_error{false};
  // Server-tracked ARPA targets, keyed by target id. Guarded by the client's
  // m_radars_mutex (membership is stable once up).
  std::map<uint64_t, RadarTarget> targets;
};

class MayaraClient {
 public:
  // The Radar API version this client speaks. The server reports its own in the
  // `version` field of GET /radars; a mismatch on any JSON error is fatal (see
  // MayaraClient.cpp). Bump this only once the parsing matches that version.
  static constexpr const char* kRadarApiVersion = "3.4.0";

  MayaraClient(std::string explicit_url, std::string fallback_url);
  ~MayaraClient();

  void Start();
  void Stop();

  // Last-known-good server base URL, tried first (before mDNS) for a fast
  // reconnect. Set before Start().
  void SetRememberedUrl(std::string url);
  // A server URL the user entered manually (Signal K :3000 or Mayara :6502);
  // tried in preference to discovery. Thread-safe; takes effect on the next
  // discovery attempt.
  void SetServerUrl(std::string url);
  // A mayara-server this plugin runs itself, on loopback. Set only while the
  // user has chosen to run one here, and then it is used exclusively: that
  // choice is one side of a radio pair in Settings, so a mayara advertising on
  // the network must not take over from it. Empty clears it, which is what
  // choosing "use a server on the network" does.
  void SetLocalUrl(std::string url);
  // Drop the current connection and reconsider every candidate from scratch.
  // Needed whenever the server configuration changes: the discovery thread
  // exits once it has connected, so nothing would otherwise notice.
  void Rescan();
  // A Signal K server OpenCPN itself is configured to talk to. A guess, not a
  // promise that mayara runs there, so it is tried after discovery -- but it
  // works where mDNS does not (routed networks, mDNS blocked by the AP).
  void SetHintUrl(std::string url);
  // The base URL currently streaming (empty until connected), for persisting.
  std::string ConnectedUrl();
  bool Connected();  // at least one radar is streaming

  // A guard-zone alarm as the server reports it. The server does the
  // detection and publishes notifications.radar.<key>.guardZone.<n>; the
  // plugin only has to notice and say so.
  struct GuardAlarm {
    std::string radar_id;
    int zone = 0;          // 1 or 2
    bool active = false;   // anything but the "normal" state
    std::string message;
  };
  // Every zone we have heard about, active or not.
  std::vector<GuardAlarm> Alarms();

  // --- Signal K access -----------------------------------------------------
  // A Signal K server with security enabled answers 401 to every control write
  // until the plugin holds a device token. Signal K issues one through its
  // access-request flow: POST a request, then someone approves it in the
  // server's admin UI (Security -> Access Requests) and the reply carries the
  // token. Reads are unaffected, which is why radar data arrives either way.
  enum class AuthState {
    kUnknown,      // no control write attempted yet
    kNotNeeded,    // writes are accepted without a token
    kNeeded,       // a write was refused and we hold no usable token
    kRequesting,   // posting the access request
    kPending,      // waiting for approval in the Signal K admin UI
    kApproved,     // token in hand
    kDenied,       // approval refused
    kUnavailable   // this server does not take device access requests
  };
  // The device identity we ask under. Set before Start(); the plugin persists
  // it so an approval survives a restart.
  void SetClientId(std::string id);
  // A token obtained earlier and the server that issued it. The token is only
  // sent while connected to that same server (it is signed by it).
  void SetAuthToken(std::string server, std::string token);
  std::string AuthToken();        // non-empty once approved, for persisting
  std::string AuthTokenServer();
  AuthState Auth();
  std::string AuthMessage();      // human-readable detail for the dialog
  void RequestAccess();           // start the access-request flow
  // Resume polling a request posted in an earlier session, so approval does
  // not have to happen within one run of OpenCPN.
  void ResumeAccessRequest(std::string server, std::string href);
  // The request currently awaiting approval, for persisting ("" if none).
  std::string PendingHref();
  std::string PendingServer();

  std::string StatusLine();
  // Radar API version handshake. ServerApiVersion is empty until GET /radars is
  // read; ApiVersionMismatch is true once it is known and differs from ours.
  std::string ServerApiVersion();
  bool ApiVersionMismatch();

  // Active-radar accessors (the one shown/controlled in the UI).
  RadarState* State();
  RadarControls* Controls();
  int RadarCount();
  std::vector<std::string> RadarNames();
  int ActiveIndex();
  void SetActive(int index);
  void SetAllIntensity(float f);
  // Echo colours, applied to every radar now and to any that appear later.
  void SetPalette(const RadarPalette& p);

  // Diagnostic lines, drained by the UI thread. Not written straight to wxLog:
  // wxLog defers a worker thread's messages to the main thread's next flush,
  // which can land after this dylib has been unloaded. Level 1 is a problem,
  // 2 is chatter.
  std::vector<std::pair<int, std::string>> TakeLog();

  // Per-radar access (for the composite overlay and multi-PPI windows).
  RadarState* StateAt(int index);
  RadarControls* ControlsAt(int index);
  std::string RadarId(int index);
  // Snapshot of the server-tracked ARPA targets for a radar (empty if none or
  // out of range). Safe to call from the UI thread.
  std::vector<RadarTarget> TargetsAt(int index);
  // Acquire a target at a true bearing (deg) and distance (m) from the radar.
  // POSTs to the server; the tracker confirms it via the target stream.
  void AcquireTargetAt(int index, double bearing_deg, double distance_m);
  // Stop tracking a target (DELETE .../targets/{id}).
  void CancelTargetAt(int index, uint64_t target_id);

  // Radars currently composited on the chart overlay. Defaults to all radars.
  std::vector<int> ShownRadars();
  void SetShown(std::vector<int> indices);

  // Set a control value. `json_body` is the BareControlValue JSON
  // (e.g. {"value":75}). Sent via REST PUT. SetControl targets the active
  // radar; SetControlAt targets a specific radar index.
  void SetControl(const std::string& control_id, const std::string& json_body);
  void SetControlAt(int index, const std::string& control_id,
                    const std::string& json_body);

 private:
  void Run();                 // background: discover + connect, retry
  // Outcome of one attempt at m_base_url.
  enum class Attempt {
    kFailed,     // no answer, or an answer we cannot use
    kNoRadars,   // the radar API answered, but lists nothing
    kConnected,  // at least one radar is streaming
  };
  Attempt DiscoverAndConnect();
  bool FetchCapabilities(Radar* radar);
  void FetchControlValues(Radar* radar);
  // Surface a JSON error. If the server's API version was seen and differs from
  // kRadarApiVersion, this throws a loud "version mismatch" tantrum instead.
  void JsonError(const std::string& context, const char* what);
  bool ConnectSpokes(Radar* radar);  // true if it opens
  void ConnectControlStream();
  void SetStatus(const std::string& s);
  // The bearer token to send to `base`, or empty when we hold none for it.
  std::string TokenFor(const std::string& base);
  void SetAuth(AuthState s, std::string message);
  // Fold a control write's HTTP status into the auth state: 401/403 is the
  // server telling us we need a token.
  void NoteWriteStatus(int status_code, const std::string& base);
  void PollAccessRequest(std::string base, std::string href);
  void RunAccessRequest();  // background: post the request, then poll

  std::string m_explicit;
  std::string m_fallback;
  std::string m_base_url;
  std::thread m_thread;
  std::atomic<bool> m_stop{false};

  std::mutex m_status_mutex;
  std::string m_status{"not connected"};
  std::string m_server_api_version;  // from GET /radars `version`, if present
  std::string m_manual;              // user-entered server URL (guarded)
  std::string m_local;               // our own local server, if any (guarded)
  std::string m_hint;                // OpenCPN's Signal K connection (guarded)
  std::mutex m_alarm_mutex;
  std::vector<GuardAlarm> m_alarms;  // keyed by radar id + zone
  std::string m_connected_url;       // base URL that connected (guarded)
  std::string m_remembered;          // last-known-good URL (set before Start)

  // Signal K device access. Strings guarded by m_status_mutex.
  std::string m_client_id;
  std::string m_token;
  std::string m_token_server;
  std::string m_pending_href;
  std::string m_pending_server;
  std::string m_auth_message;
  std::atomic<AuthState> m_auth{AuthState::kUnknown};
  std::atomic<bool> m_auth_busy{false};
  std::thread m_auth_thread;
  void LogLine(int level, const std::string& msg);
  std::mutex m_log_mutex;
  std::vector<std::pair<int, std::string>> m_log;
  std::mutex m_palette_mutex;
  RadarPalette m_palette;
  // Targets over REST, for servers whose delta stream does not carry them.
  void PollTargets();
  std::thread m_targets_thread;
  std::atomic<int64_t> m_last_target_delta_ms{0};

  std::mutex m_radars_mutex;  // guards m_radars membership (stable once up)
  std::vector<std::unique_ptr<Radar>> m_radars;
  std::atomic<int> m_active{0};
  std::atomic<float> m_intensity{1.0f};
  std::vector<int> m_shown;  // <= 2 displayed radar indices (empty = default)

  std::unique_ptr<ix::WebSocket> m_control_ws;
};

#endif  // MAYARA_CLIENT_H_
