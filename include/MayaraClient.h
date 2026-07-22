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
  // The base URL currently streaming (empty until connected), for persisting.
  std::string ConnectedUrl();
  bool Connected();  // at least one radar is streaming

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
  void SetAllIntensity(float f);  // apply echo dimming to every radar

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
  bool DiscoverAndConnect();  // one attempt; true once at least one streams
  bool FetchCapabilities(Radar* radar);
  void FetchControlValues(Radar* radar);
  // Surface a JSON error. If the server's API version was seen and differs from
  // kRadarApiVersion, this throws a loud "version mismatch" tantrum instead.
  void JsonError(const std::string& context, const char* what);
  bool ConnectSpokes(Radar* radar);  // true if it opens
  void ConnectControlStream();
  void SetStatus(const std::string& s);

  std::string m_explicit;
  std::string m_fallback;
  std::string m_base_url;
  std::thread m_thread;
  std::atomic<bool> m_stop{false};

  std::mutex m_status_mutex;
  std::string m_status{"not connected"};
  std::string m_server_api_version;  // from GET /radars `version`, if present
  std::string m_manual;              // user-entered server URL (guarded)
  std::string m_connected_url;       // base URL that connected (guarded)
  std::string m_remembered;          // last-known-good URL (set before Start)

  std::mutex m_radars_mutex;  // guards m_radars membership (stable once up)
  std::vector<std::unique_ptr<Radar>> m_radars;
  std::atomic<int> m_active{0};
  std::atomic<float> m_intensity{1.0f};
  std::vector<int> m_shown;  // <= 2 displayed radar indices (empty = default)

  std::unique_ptr<ix::WebSocket> m_control_ws;
};

#endif  // MAYARA_CLIENT_H_
