/******************************************************************************
 * mayara_pi - client for mayara-server (multi-radar).
 *****************************************************************************/
#include "MayaraClient.h"

#include <chrono>
#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "MayaraDiscovery.h"
#include "RadarMessage.h"

using json = nlohmann::json;

Radar::Radar() = default;
Radar::~Radar() = default;

namespace {

void StripTrailingSlash(std::string& s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
}

ControlValue ParseControlValue(const json& v) {
  ControlValue cv;
  if (v.contains("value") && !v["value"].is_null()) {
    cv.has_value = true;
    if (v["value"].is_string())
      cv.str_value = v["value"].get<std::string>();
    else
      cv.value = v["value"].get<double>();
  }
  if (v.contains("auto")) {
    cv.has_auto = true;
    cv.auto_ = v["auto"].get<bool>();
  }
  if (v.contains("enabled")) {
    cv.has_enabled = true;
    cv.enabled = v["enabled"].get<bool>();
  }
  cv.allowed = v.value("allowed", true);
  cv.autoValue = v.value("autoValue", 0.0);
  cv.endValue = v.value("endValue", 0.0);
  cv.startDistance = v.value("startDistance", 0.0);
  cv.endDistance = v.value("endDistance", 0.0);
  cv.error = v.value("error", std::string());
  return cv;
}

ControlDef ParseControlDef(const std::string& id, const json& c) {
  ControlDef d;
  d.id = id;
  d.numeric_id = c.value("id", 0);
  d.name = c.value("name", id);
  d.description = c.value("description", std::string());
  d.category = c.value("category", std::string());
  d.dataType = c.value("dataType", std::string());
  d.units = c.value("units", std::string());
  d.isReadOnly = c.value("isReadOnly", false);
  d.hasEnabled = c.value("hasEnabled", false);
  d.hasAuto = c.value("hasAuto", false) || c.contains("automatic");
  d.hasAutoAdjustable = c.value("hasAutoAdjustable", false);
  d.autoAdjustMin = c.value("autoAdjustMinValue", 0.0);
  d.autoAdjustMax = c.value("autoAdjustMaxValue", 0.0);
  if (c.contains("minValue")) {
    d.has_min = true;
    d.minValue = c["minValue"].get<double>();
  }
  if (c.contains("maxValue")) {
    d.has_max = true;
    d.maxValue = c["maxValue"].get<double>();
  }
  if (c.contains("stepValue")) {
    d.has_step = true;
    d.stepValue = c["stepValue"].get<double>();
  }
  d.maxDistance = c.value("maxDistance", 0.0);
  if (c.contains("descriptions") && c["descriptions"].is_object())
    for (auto dit = c["descriptions"].begin(); dit != c["descriptions"].end();
         ++dit) {
      try {
        d.descriptions[std::stoi(dit.key())] = dit.value().get<std::string>();
      } catch (...) {
      }
    }
  if (c.contains("validValues") && c["validValues"].is_array())
    for (const auto& v : c["validValues"]) d.validValues.push_back(v.get<int>());
  return d;
}

std::string WsBase(const std::string& base) {
  if (base.rfind("https://", 0) == 0) return "wss://" + base.substr(8);
  if (base.rfind("http://", 0) == 0) return "ws://" + base.substr(7);
  return base;
}

std::string WsUrl(const std::string& base, const std::string& radar_id) {
  return WsBase(base) + "/signalk/v2/api/vessels/self/radars/" + radar_id +
         "/spokes";
}

}  // namespace

MayaraClient::MayaraClient(std::string explicit_url, std::string fallback_url)
    : m_explicit(std::move(explicit_url)),
      m_fallback(std::move(fallback_url)) {
  StripTrailingSlash(m_explicit);
  StripTrailingSlash(m_fallback);
}

MayaraClient::~MayaraClient() { Stop(); }

void MayaraClient::Start() {
  ix::initNetSystem();
  m_stop = false;
  m_thread = std::thread([this] { Run(); });
}

// Run() returns as soon as it connects, so nothing is left watching the
// configuration: without this, changing which server to use had no effect until
// OpenCPN was restarted, and the URL we reported stayed at the old one forever.
void MayaraClient::Rescan() {
  Stop();
  {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_connected_url.clear();
    m_server_api_version.clear();
  }
  SetStatus("reconnecting...");
  Start();
}

void MayaraClient::Stop() {
  m_stop = true;
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    for (auto& r : m_radars)
      if (r->spoke_ws) r->spoke_ws->stop();
  }
  if (m_control_ws) m_control_ws->stop();
  if (m_thread.joinable()) m_thread.join();
  if (m_auth_thread.joinable()) m_auth_thread.join();
  if (m_targets_thread.joinable()) m_targets_thread.join();
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    m_radars.clear();
  }
  m_control_ws.reset();
}

std::string MayaraClient::StatusLine() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_status;
}

// Capped: nobody reads a log nobody drains, and an unbounded one on a plugin
// left running for a week is a leak.
void MayaraClient::LogLine(int level, const std::string& msg) {
  std::lock_guard<std::mutex> lock(m_log_mutex);
  // Evict the oldest rather than refuse the newest: a burst that overruns the
  // cap between two drains ends in the lines that explain it.
  if (m_log.size() >= 500) m_log.erase(m_log.begin());
  m_log.push_back({level, msg});
}

std::vector<std::pair<int, std::string>> MayaraClient::TakeLog() {
  std::lock_guard<std::mutex> lock(m_log_mutex);
  std::vector<std::pair<int, std::string>> out;
  out.swap(m_log);
  return out;
}

void MayaraClient::SetStatus(const std::string& s) {
  // Deliberately wx-free (this TU also pulls in IXWebSocket, whose ssize_t
  // typedef clashes with wxWidgets' on Win32). The status is surfaced via
  // StatusLine(); we never call wxLog from these worker threads (its deferred
  // cross-thread flush can dereference our unloaded dylib), so diagnostics go
  // into a queue the UI thread drains.
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    changed = s != m_status;
    m_status = s;
  }
  // Every state this went through, in order, which is the whole point when a
  // connection is flapping.
  if (changed) LogLine(2, "status: " + s);
}

std::string MayaraClient::ServerApiVersion() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_server_api_version;
}

bool MayaraClient::ApiVersionMismatch() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return !m_server_api_version.empty() &&
         m_server_api_version != kRadarApiVersion;
}

void MayaraClient::JsonError(const std::string& context, const char* what) {
  LogLine(1, "JSON error in " + context + ": " + (what ? what : "?"));
  const std::string sv = ServerApiVersion();
  if (!sv.empty() && sv != kRadarApiVersion) {
    SetStatus("!!!!!! RADAR API VERSION MISMATCH !!!!!!  server speaks " + sv +
              " but this plugin only understands " +
              std::string(kRadarApiVersion) +
              " -- REFUSING TO CONTINUE. The plugin must be updated for the new "
              "radar API. (" + context + ": " + what + ")");
  } else {
    SetStatus(context + " JSON error: " + what);
  }
}

RadarState* MayaraClient::State() {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  const int i = m_active;
  if (i < 0 || i >= static_cast<int>(m_radars.size())) return nullptr;
  return &m_radars[i]->state;
}

RadarControls* MayaraClient::Controls() {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  const int i = m_active;
  if (i < 0 || i >= static_cast<int>(m_radars.size())) return nullptr;
  return &m_radars[i]->controls;
}

int MayaraClient::RadarCount() {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  return static_cast<int>(m_radars.size());
}

std::vector<std::string> MayaraClient::RadarNames() {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  std::vector<std::string> names;
  for (auto& r : m_radars) names.push_back(r->name.empty() ? r->id : r->name);
  return names;
}

int MayaraClient::ActiveIndex() { return m_active; }

void MayaraClient::SetActive(int index) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  if (index >= 0 && index < static_cast<int>(m_radars.size())) m_active = index;
}

// One palette for every radar: it is a display preference, not a property of
// a particular set.
void MayaraClient::SetPalette(const RadarPalette& p) {
  {
    std::lock_guard<std::mutex> lock(m_palette_mutex);
    m_palette = p;
  }
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  for (auto& r : m_radars) r->state.SetPalette(p);
}

void MayaraClient::SetAllIntensity(float f) {
  m_intensity = f;
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  for (auto& r : m_radars) r->state.SetIntensity(f);
}

RadarState* MayaraClient::StateAt(int index) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  if (index < 0 || index >= static_cast<int>(m_radars.size())) return nullptr;
  return &m_radars[index]->state;
}

RadarControls* MayaraClient::ControlsAt(int index) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  if (index < 0 || index >= static_cast<int>(m_radars.size())) return nullptr;
  return &m_radars[index]->controls;
}

std::string MayaraClient::RadarId(int index) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  if (index < 0 || index >= static_cast<int>(m_radars.size())) return {};
  return m_radars[index]->id;
}

std::vector<RadarTarget> MayaraClient::TargetsAt(int index) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  if (index < 0 || index >= static_cast<int>(m_radars.size())) return {};
  std::vector<RadarTarget> out;
  out.reserve(m_radars[index]->targets.size());
  for (const auto& kv : m_radars[index]->targets) out.push_back(kv.second);
  return out;
}

std::vector<int> MayaraClient::ShownRadars() {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  const int n = static_cast<int>(m_radars.size());
  std::vector<int> out;
  for (int i : m_shown)
    if (i >= 0 && i < n) out.push_back(i);
  if (out.empty())
    for (int i = 0; i < n; ++i) out.push_back(i);  // default: all radars
  return out;
}

void MayaraClient::SetShown(std::vector<int> indices) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  m_shown = std::move(indices);
}

void MayaraClient::SetRememberedUrl(std::string url) {
  StripTrailingSlash(url);
  m_remembered = std::move(url);
}

void MayaraClient::SetServerUrl(std::string url) {
  StripTrailingSlash(url);
  std::lock_guard<std::mutex> lock(m_status_mutex);
  m_manual = std::move(url);
}

void MayaraClient::SetLocalUrl(std::string url) {
  StripTrailingSlash(url);
  std::lock_guard<std::mutex> lock(m_status_mutex);
  m_local = std::move(url);
}

std::vector<MayaraClient::GuardAlarm> MayaraClient::Alarms() {
  std::lock_guard<std::mutex> lock(m_alarm_mutex);
  return m_alarms;
}

void MayaraClient::SetHintUrl(std::string url) {
  StripTrailingSlash(url);
  std::lock_guard<std::mutex> lock(m_status_mutex);
  m_hint = std::move(url);
}

std::string MayaraClient::ConnectedUrl() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_connected_url;
}

// --- Signal K device access ------------------------------------------------

void MayaraClient::SetClientId(std::string id) {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  m_client_id = std::move(id);
}

void MayaraClient::SetAuthToken(std::string server, std::string token) {
  StripTrailingSlash(server);
  std::lock_guard<std::mutex> lock(m_status_mutex);
  m_token_server = std::move(server);
  m_token = std::move(token);
  if (!m_token.empty()) m_auth = AuthState::kApproved;
}

std::string MayaraClient::AuthToken() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_token;
}

std::string MayaraClient::AuthTokenServer() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_token_server;
}

MayaraClient::AuthState MayaraClient::Auth() { return m_auth; }

std::string MayaraClient::AuthMessage() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_auth_message;
}

std::string MayaraClient::PendingHref() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_pending_href;
}

std::string MayaraClient::PendingServer() {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  return m_pending_server;
}

void MayaraClient::SetAuth(AuthState s, std::string message) {
  m_auth = s;
  std::lock_guard<std::mutex> lock(m_status_mutex);
  m_auth_message = std::move(message);
}

// The token is signed by one specific server, so it is only offered back to
// that server; against any other it would just be rejected.
std::string MayaraClient::TokenFor(const std::string& base) {
  std::lock_guard<std::mutex> lock(m_status_mutex);
  if (m_token.empty()) return std::string();
  if (!m_token_server.empty() && m_token_server != base) return std::string();
  return m_token;
}

void MayaraClient::NoteWriteStatus(int status_code, const std::string& base) {
  if (status_code == 401 || status_code == 403) {
    SetAuth(AuthState::kNeeded,
            TokenFor(base).empty()
                ? "the server refuses radar control without permission"
                : "the server rejected our token");
  } else if (status_code >= 200 && status_code < 300) {
    // Only clear an unproven state: an approved token stays approved.
    if (m_auth == AuthState::kUnknown || m_auth == AuthState::kNeeded)
      SetAuth(AuthState::kNotNeeded, std::string());
  }
}

void MayaraClient::RequestAccess() {
  if (m_auth_busy.exchange(true)) return;  // one flow at a time
  if (m_auth_thread.joinable()) m_auth_thread.join();
  if (m_targets_thread.joinable()) m_targets_thread.join();
  m_auth_thread = std::thread([this] {
    RunAccessRequest();
    m_auth_busy = false;
  });
}

void MayaraClient::ResumeAccessRequest(std::string server, std::string href) {
  StripTrailingSlash(server);
  if (server.empty() || href.empty()) return;
  if (m_auth_busy.exchange(true)) return;
  if (m_auth_thread.joinable()) m_auth_thread.join();
  if (m_targets_thread.joinable()) m_targets_thread.join();
  {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_pending_server = server;
    m_pending_href = href;
  }
  SetAuth(AuthState::kPending, "waiting for approval on " + server);
  m_auth_thread = std::thread([this, server, href] {
    PollAccessRequest(server, href);
    m_auth_busy = false;
  });
}

void MayaraClient::RunAccessRequest() {
  std::string base, client_id;
  {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    base = m_connected_url.empty() ? m_base_url : m_connected_url;
    client_id = m_client_id;
  }
  if (base.empty() || client_id.empty()) {
    SetAuth(AuthState::kNeeded, "not connected to a server yet");
    return;
  }
  SetAuth(AuthState::kRequesting, "asking " + base + " for permission...");

  json body = {{"clientId", client_id},
               {"description", "Mayara radar plugin for OpenCPN"},
               {"permissions", "readwrite"}};
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 10;
  args->extraHeaders["Content-Type"] = "application/json";
  auto resp = http.post(base + "/signalk/v1/access/requests", body.dump(),
                        args);
  if (!resp || resp->statusCode == 0) {
    SetAuth(AuthState::kNeeded, "no answer from " + base);
    return;
  }
  if (resp->statusCode == 404) {
    SetAuth(AuthState::kUnavailable,
            "this server does not offer access requests; its security may be "
            "configured some other way");
    return;
  }

  std::string href, message;
  int state_code = resp->statusCode;
  bool pending = false;
  try {
    auto j = json::parse(resp->body);
    href = j.value("href", std::string());
    message = j.value("message", std::string());
    pending = j.value("state", std::string()) == "PENDING";
    state_code = j.value("statusCode", resp->statusCode);
  } catch (const std::exception&) {
    // Some errors come back as plain text; the status code still tells us.
  }

  if (!pending || href.empty()) {
    if (state_code == 403)
      SetAuth(AuthState::kUnavailable,
              "this server does not allow device access requests" +
                  (message.empty() ? std::string() : " (" + message + ")"));
    else
      SetAuth(AuthState::kNeeded,
              message.empty() ? "the server refused the request" : message);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_pending_server = base;
    m_pending_href = href;
  }
  SetAuth(AuthState::kPending, "waiting for approval on " + base);
  PollAccessRequest(base, href);
}

// Poll until someone approves or denies in the Signal K admin UI. Approval can
// take a while -- the person doing it may be walking to another device -- so
// this waits a long time, and the href is persisted for the next session.
void MayaraClient::PollAccessRequest(std::string base, std::string href) {
  const int kPollSeconds = 3;
  const int kGiveUpSeconds = 3600;
  for (int waited = 0; waited < kGiveUpSeconds && !m_stop;
       waited += kPollSeconds) {
    for (int i = 0; i < kPollSeconds * 10 && !m_stop; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (m_stop) return;

    ix::HttpClient http(/*async=*/false);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = 10;
    auto resp = http.get(base + href, args);
    if (!resp || resp->statusCode != 200) continue;  // transient; keep waiting

    std::string permission, token, message;
    bool completed = false;
    try {
      auto j = json::parse(resp->body);
      completed = j.value("state", std::string()) == "COMPLETED";
      message = j.value("message", std::string());
      if (j.contains("accessRequest") && j["accessRequest"].is_object()) {
        permission = j["accessRequest"].value("permission", std::string());
        token = j["accessRequest"].value("token", std::string());
      }
    } catch (const std::exception&) {
      continue;
    }
    if (!completed) continue;

    {
      std::lock_guard<std::mutex> lock(m_status_mutex);
      m_pending_href.clear();
      m_pending_server.clear();
    }
    if (permission == "APPROVED" && !token.empty()) {
      {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_token = token;
        m_token_server = base;
      }
      SetAuth(AuthState::kApproved, "approved by " + base);
    } else if (permission == "DENIED") {
      SetAuth(AuthState::kDenied, "the request was refused");
    } else {
      SetAuth(AuthState::kNeeded,
              message.empty() ? "the request ended without a token" : message);
    }
    return;
  }
  if (!m_stop)
    SetAuth(AuthState::kPending,
            "still waiting for approval; it will be picked up again next time");
}

bool MayaraClient::Connected() { return RadarCount() > 0; }

void MayaraClient::Run() {
  while (!m_stop) {
    std::string manual, local, hint;
    {
      std::lock_guard<std::mutex> lock(m_status_mutex);
      manual = m_manual;
      local = m_local;
      hint = m_hint;
    }
    std::vector<std::string> candidates;
    if (!manual.empty()) {
      candidates.push_back(manual);  // user-entered server wins
    } else if (!m_explicit.empty()) {
      candidates.push_back(m_explicit);
    } else if (!local.empty()) {
      // "Run it here" is a choice, not a fallback. Settings offers it as one
      // side of a radio pair against "use a server on the network", so having a
      // mayara advertising on the LAN quietly take over would contradict what
      // the user just picked. It is exclusive for the same reason a manually
      // entered address is: both say which server, not merely that one exists.
      candidates.push_back(local);
    } else {
      // Try the last-known-good server first (fast reconnect), then discover.
      if (!m_remembered.empty()) candidates.push_back(m_remembered);
      SetStatus("searching for a mayara or Signal K server...");
      std::string found = MayaraDiscovery::FindServer(2000);
      if (!found.empty() && found != m_remembered) candidates.push_back(found);
      // A Signal K server OpenCPN is already configured to talk to. Not proof
      // that mayara runs there, but a better guess than nothing, and it works
      // where mDNS does not (routed networks, mDNS blocked by the AP).
      if (!hint.empty() && hint != m_remembered && hint != found)
        candidates.push_back(hint);
      if (!m_fallback.empty() && m_fallback != m_remembered)
        candidates.push_back(m_fallback);
    }
    for (auto& base : candidates) {
      if (m_stop) return;
      m_base_url = base;
      StripTrailingSlash(m_base_url);
      Attempt r = DiscoverAndConnect();
      // A server whose radar API answers but lists nothing is still the right
      // server -- it just has nothing to show yet, which is what a local server
      // with no radar attached looks like. Stay on it and re-poll instead of
      // falling through to the remaining candidates and starting discovery
      // over, which walked the status line through "searching...", stale
      // addresses and back every few seconds while nothing was actually wrong.
      while (r == Attempt::kNoRadars && !m_stop) {
        // Remember it meanwhile: otherwise a stale address that never answers
        // stays first in line for every future session.
        {
          std::lock_guard<std::mutex> lock(m_status_mutex);
          m_connected_url = m_base_url;
        }
        for (int i = 0; i < 30 && !m_stop; ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (m_stop) return;
        r = DiscoverAndConnect();
      }
      if (r == Attempt::kConnected) {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_connected_url = m_base_url;
        return;
      }
    }
    for (int i = 0; i < 30 && !m_stop; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

MayaraClient::Attempt MayaraClient::DiscoverAndConnect() {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;
  {  // Reads are usually public, but a locked-down server wants the token too.
    const std::string tok = TokenFor(m_base_url);
    if (!tok.empty()) args->extraHeaders["Authorization"] = "Bearer " + tok;
  }

  const std::string url = m_base_url + "/signalk/v2/api/vessels/self/radars";
  auto resp = http.get(url, args);
  if (!resp || resp->statusCode == 0) {
    SetStatus("no server at " + m_base_url);
    return Attempt::kFailed;
  }
  if (resp->statusCode != 200) {
    SetStatus("GET radars -> HTTP " + std::to_string(resp->statusCode));
    return Attempt::kFailed;
  }

  // Build the radar list from either shape (keyed object or array).
  std::vector<std::unique_ptr<Radar>> radars;
  try {
    auto j = json::parse(resp->body);
    // Note the server's Radar API version up front, so any JSON error below is
    // reported as a version mismatch when it applies.
    if (j.is_object() && j.contains("version") && j["version"].is_string()) {
      std::lock_guard<std::mutex> lock(m_status_mutex);
      m_server_api_version = j["version"].get<std::string>();
    }
    auto add = [&](const std::string& id, const json& info) {
      auto r = std::make_unique<Radar>();
      r->id = id;
      r->name = info.value("name", id);
      // 3.4.0 carries no stream URL (reached by convention); older shapes may.
      r->spoke_url = info.value("spokeDataUrl", std::string());
      radars.push_back(std::move(r));
    };
    if (j.is_object() && j.contains("radars") && j["radars"].is_object()) {
      // 3.4.0 envelope: { version, radars: { id: RadarInfo } }.
      for (auto it = j["radars"].begin(); it != j["radars"].end(); ++it)
        add(it.key(), it.value());
    } else if (j.is_object()) {
      // Older shape: radars keyed at the top level.
      for (auto it = j.begin(); it != j.end(); ++it)
        if (it.key() != "version") add(it.key(), it.value());
    } else if (j.is_array()) {
      for (const auto& e : j) add(e.value("id", std::string()), e);
    }
  } catch (const std::exception& e) {
    JsonError("radars", e.what());
    return Attempt::kFailed;
  }
  if (radars.empty()) {
    SetStatus("connected; no radars transmitting");
    return Attempt::kNoRadars;
  }

  // Fetch capabilities + connect the spoke stream for each radar; keep the ones
  // that actually stream.
  std::vector<std::unique_ptr<Radar>> live;
  for (auto& r : radars) {
    if (m_stop || r->id.empty()) continue;
    if (!FetchCapabilities(r.get())) continue;
    if (r->spoke_url.empty()) r->spoke_url = WsUrl(m_base_url, r->id);
    if (ConnectSpokes(r.get())) live.push_back(std::move(r));
  }
  if (live.empty()) {
    SetStatus("no spoke stream at " + m_base_url);
    return Attempt::kFailed;
  }

  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    m_radars = std::move(live);
    m_active = 0;
  }
  ConnectControlStream();
  if (!m_targets_thread.joinable())
    m_targets_thread = std::thread([this] { PollTargets(); });
  SetStatus("streaming " + std::to_string(RadarCount()) + " radar(s)");
  return Attempt::kConnected;
}

bool MayaraClient::FetchCapabilities(Radar* radar) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;
  {  // Reads are usually public, but a locked-down server wants the token too.
    const std::string tok = TokenFor(m_base_url);
    if (!tok.empty()) args->extraHeaders["Authorization"] = "Bearer " + tok;
  }
  const std::string url = m_base_url +
                          "/signalk/v2/api/vessels/self/radars/" + radar->id +
                          "/capabilities";
  auto resp = http.get(url, args);
  if (!resp || resp->statusCode != 200) return false;
  try {
    auto j = json::parse(resp->body);
    const int spokes = j.value("spokesPerRevolution", 2048);
    const int maxlen = j.value("maxSpokeLength", 1024);
    std::vector<Rgba> legend;
    if (j.contains("legend") && j["legend"].contains("pixels"))
      for (const auto& px : j["legend"]["pixels"]) {
        Rgba c;
        const auto cit = px.find("color");
        if (cit != px.end() && cit->is_object()) {
          // 3.4.0: color is an { r, g, b, a } object.
          c.r = cit->value("r", 0);
          c.g = cit->value("g", 0);
          c.b = cit->value("b", 0);
          c.a = cit->value("a", 255);
        } else {
          // Older shape: color is a "#RRGGBBAA" hex string.
          const std::string col =
              (cit != px.end() && cit->is_string()) ? cit->get<std::string>()
                                                    : "#00000000";
          auto hex = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return 0;
          };
          const size_t i = (!col.empty() && col[0] == '#') ? 1 : 0;
          auto byte = [&](size_t off) -> uint8_t {
            return (off + 1 < col.size())
                       ? static_cast<uint8_t>(hex(col[off]) * 16 +
                                              hex(col[off + 1]))
                       : 0;
          };
          c.r = byte(i);
          c.g = byte(i + 2);
          c.b = byte(i + 4);
          c.a = (col.size() >= i + 8) ? byte(i + 6) : 255;
        }
        legend.push_back(c);
      }
    radar->state.Configure(spokes, maxlen, std::move(legend));
    if (j.contains("legend") && j["legend"].is_object()) {
      const auto& lg = j["legend"];
      radar->state.SetLegendBands(lg.value("lowReturn", 0),
                                  lg.value("mediumReturn", 0),
                                  lg.value("strongReturn", 0));
      // What each legend index means, so a palette can re-colour by role
      // rather than by position. The Doppler entries come as [start, count].
      LegendLayout layout;
      layout.pixel_colors = lg.value("pixelColors", 0);
      layout.static_background = lg.value("staticBackground", -1);
      layout.history_start = lg.value("historyStart", -1);
      auto first = [&](const char* key) -> int {
        auto it = lg.find(key);
        if (it == lg.end() || !it->is_array() || it->empty()) return -1;
        return (*it)[0].get<int>();
      };
      layout.doppler_approaching = first("dopplerApproaching");
      layout.doppler_receding = first("dopplerReceding");
      radar->state.SetLegendLayout(layout);
    }
    {
      std::lock_guard<std::mutex> lock(m_palette_mutex);
      radar->state.SetPalette(m_palette);
    }
    radar->state.SetIntensity(m_intensity);

    std::vector<ControlDef> defs;
    if (j.contains("controls") && j["controls"].is_object())
      for (auto it = j["controls"].begin(); it != j["controls"].end(); ++it)
        defs.push_back(ParseControlDef(it.key(), it.value()));
    std::vector<int> ranges;
    if (j.contains("supportedRanges") && j["supportedRanges"].is_array())
      for (const auto& r : j["supportedRanges"]) ranges.push_back(r.get<int>());
    radar->controls.SetSchema(std::move(defs), std::move(ranges));

    FetchControlValues(radar);
    return true;
  } catch (const std::exception& e) {
    JsonError("capabilities", e.what());
    return false;
  }
}

void MayaraClient::FetchControlValues(Radar* radar) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;
  {  // Reads are usually public, but a locked-down server wants the token too.
    const std::string tok = TokenFor(m_base_url);
    if (!tok.empty()) args->extraHeaders["Authorization"] = "Bearer " + tok;
  }
  const std::string url = m_base_url +
                          "/signalk/v2/api/vessels/self/radars/" + radar->id +
                          "/controls";
  auto resp = http.get(url, args);
  if (!resp || resp->statusCode != 200) return;
  try {
    auto j = json::parse(resp->body);
    if (j.is_object())
      for (auto it = j.begin(); it != j.end(); ++it)
        radar->controls.SetValue(it.key(), ParseControlValue(it.value()));
  } catch (const std::exception& e) {
    JsonError("controls", e.what());
  }
}

bool MayaraClient::ConnectSpokes(Radar* radar) {
  radar->streaming = false;
  radar->ws_error = false;
  radar->spoke_ws = std::make_unique<ix::WebSocket>();
  radar->spoke_ws->setUrl(radar->spoke_url);
  radar->spoke_ws->disableAutomaticReconnection();

  Radar* r = radar;
  radar->spoke_ws->setOnMessageCallback([this, r](
                                            const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message && msg->binary) {
      r->streaming = true;
      const auto* buf = reinterpret_cast<const uint8_t*>(msg->str.data());
      std::vector<MayaraSpoke> spokes;
      if (DecodeRadarMessage(buf, msg->str.size(), spokes)) {
        double plat = 0, plon = 0;
        bool have_pos = false;
        uint32_t hb_angle = 0, hb_bearing = 0;
        bool have_heading = false;
        for (const auto& s : spokes) {
          r->state.WriteSpoke(s.angle, s.data, s.data_len, s.range);
          if (s.has_lat && s.has_lon) {
            plat = s.lat;
            plon = s.lon;
            have_pos = true;
          }
          if (s.has_bearing) {
            hb_angle = s.angle;
            hb_bearing = s.bearing;
            have_heading = true;
          }
        }
        if (have_pos) r->state.SetPosition(plat, plon);
        if (have_heading) r->state.SetHeadingFromBearing(hb_angle, hb_bearing);
      }
    } else if (msg->type == ix::WebSocketMessageType::Open) {
      r->streaming = true;
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      r->ws_error = true;
    }
  });

  radar->spoke_ws->start();
  for (int i = 0; i < 50 && !m_stop; ++i) {
    if (radar->streaming) return true;
    if (radar->ws_error) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  radar->spoke_ws->stop();
  radar->spoke_ws.reset();
  return false;
}

// One target, from either shape the server offers: the delta value under
// `radars.<id>.targets.<tid>`, or an element of the REST target list. They
// agree on everything except the spelling of isDangerous.
static RadarTarget ParseTarget(uint64_t id, const json& value) {
  RadarTarget t;
  t.id = id;
  const std::string st = value.value("status", std::string());
  t.status = st == "tracking" ? RadarTarget::kTracking
             : st == "lost"   ? RadarTarget::kLost
                              : RadarTarget::kAcquiring;
  t.manual = value.value("acquisition", std::string()) == "manual";
  if (value.contains("position") && value["position"].is_object()) {
    const auto& p = value["position"];
    t.bearing_deg = p.value("bearing", 0.0) * 180.0 / M_PI;
    t.distance_m = p.value("distance", 0.0);
  }
  if (value.contains("motion") && value["motion"].is_object()) {
    const auto& m = value["motion"];
    t.has_motion = true;
    t.course_deg = m.value("course", 0.0) * 180.0 / M_PI;
    t.speed_kn = m.value("speed", 0.0) * 1.9438445;  // m/s -> kn
  }
  if (value.contains("danger") && value["danger"].is_object()) {
    const auto& d = value["danger"];
    t.has_danger = true;
    t.cpa_m = d.value("cpa", 0.0);
    t.tcpa_s = d.value("tcpa", 0.0);
    t.is_dangerous = d.value("isDangerous", d.value("is_dangerous", false));
  }
  return t;
}

static int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Targets over the delta stream are the good case: they arrive as they change.
// But a Signal K server in front of mayara only republishes what its bridge
// knows how to publish, and today that is controls -- the targets are there,
// over the v2 REST API, just not in the data model. So when no target delta has
// arrived for a while, ask for the list instead. Costs nothing on a server that
// does stream them, since this never runs there.
void MayaraClient::PollTargets() {
  const int64_t kQuietMs = 5000;   // no deltas for this long -> ask directly
  const int64_t kPeriodMs = 1000;  // targets move about once a second
  bool polling = false;

  while (!m_stop) {
    for (int i = 0; i < kPeriodMs / 100 && !m_stop; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (m_stop) return;
    const bool quiet = NowMs() - m_last_target_delta_ms >= kQuietMs;
    if (quiet != polling) {
      polling = quiet;
      LogLine(2, quiet ? "no target deltas: polling the REST target list"
                       : "target deltas are arriving again: stopped polling");
    }
    if (!quiet) continue;

    std::vector<std::string> ids;
    {
      std::lock_guard<std::mutex> lock(m_radars_mutex);
      for (auto& r : m_radars) ids.push_back(r->id);
    }
    const std::string base = m_base_url;
    for (const std::string& id : ids) {
      if (m_stop) return;
      ix::HttpClient http(/*async=*/false);
      auto args = http.createRequest();
      args->connectTimeout = 5;
      args->transferTimeout = 5;
      const std::string tok = TokenFor(base);
      if (!tok.empty()) args->extraHeaders["Authorization"] = "Bearer " + tok;
      auto resp = http.get(
          base + "/signalk/v2/api/vessels/self/radars/" + id + "/targets", args);
      if (!resp || resp->statusCode != 200) continue;
      std::map<uint64_t, RadarTarget> fresh;
      try {
        auto j = json::parse(resp->body);
        if (!j.is_array()) continue;
        for (const auto& tv : j) {
          if (!tv.is_object() || !tv.contains("id")) continue;
          const uint64_t tid = tv["id"].get<uint64_t>();
          RadarTarget t = ParseTarget(tid, tv);
          if (t.status == RadarTarget::kLost) continue;
          fresh[tid] = t;
        }
      } catch (const std::exception& e) {
        JsonError("targets", e.what());
        continue;
      }
      // The list is the whole truth, so replace rather than merge: a target
      // that has gone must go here too.
      std::lock_guard<std::mutex> lock(m_radars_mutex);
      for (auto& r : m_radars)
        if (r->id == id) r->targets = std::move(fresh);
    }
  }
}

void MayaraClient::ConnectControlStream() {
  const std::string ctrl_url =
      WsBase(m_base_url) + "/signalk/v1/stream?subscribe=none";
  m_control_ws = std::make_unique<ix::WebSocket>();
  m_control_ws->setUrl(ctrl_url);

  m_control_ws->setOnMessageCallback([this](
                                         const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      m_control_ws->send(
          "{\"subscribe\":["
          "{\"path\":\"radars.*.controls.*\",\"period\":1000},"
          "{\"path\":\"radars.*.targets.*\",\"policy\":\"instant\"},"
          "{\"path\":\"notifications.*\",\"policy\":\"instant\"}]}");
      return;
    }
    if (msg->type != ix::WebSocketMessageType::Message || msg->binary) return;
    try {
      auto j = json::parse(msg->str);
      if (!j.contains("updates")) return;
      for (const auto& upd : j["updates"]) {
        auto route = [&](const std::string& path, const json& value,
                         bool is_meta) {
          // notifications.radar.<key>.guardZone.<n> -> a guard-zone alarm.
          // The server decides when one starts and stops; "normal" clears it.
          const std::string kNote = "notifications.radar.";
          if (path.rfind(kNote, 0) == 0) {
            const std::string rest = path.substr(kNote.size());
            const std::string kZone = ".guardZone.";
            const size_t z = rest.find(kZone);
            if (z == std::string::npos) return;
            GuardAlarm a;
            a.radar_id = rest.substr(0, z);
            a.zone = std::atoi(rest.substr(z + kZone.size()).c_str());
            if (value.is_object()) {
              const std::string st = value.value("state", std::string("normal"));
              a.active = st != "normal";
              a.message = value.value("message", std::string());
            }
            std::lock_guard<std::mutex> lock(m_alarm_mutex);
            for (auto& e : m_alarms)
              if (e.radar_id == a.radar_id && e.zone == a.zone) {
                e = a;
                return;
              }
            m_alarms.push_back(a);
            return;
          }
          // path = radars.{id}.controls.{ctrl}
          const std::string kPre = "radars.";
          const std::string kMid = ".controls.";
          if (path.rfind(kPre, 0) != 0) return;
          const size_t mid = path.find(kMid, kPre.size());
          if (mid == std::string::npos) return;
          const std::string rid = path.substr(kPre.size(), mid - kPre.size());
          const std::string ctrl = path.substr(mid + kMid.size());
          std::lock_guard<std::mutex> lock(m_radars_mutex);
          for (auto& r : m_radars)
            if (r->id == rid) {
              if (is_meta) {
                r->controls.UpdateDef(ParseControlDef(ctrl, value));
              } else {
                ControlValue cv = ParseControlValue(value);
                r->controls.SetValue(ctrl, cv);
                // Leaving Transmit (power < 2): erase the stale picture.
                if (ctrl == "power" && cv.has_value && cv.value < 2.0)
                  r->state.Clear();
              }
              return;
            }
        };
        // Apply a `radars.{id}.targets.{tid}` delta: parse+upsert the target,
        // or a null value removes it.
        auto route_target = [&](const std::string& path, const json& value) {
          const std::string kPre = "radars.";
          const std::string kMid = ".targets.";
          if (path.rfind(kPre, 0) != 0) return;
          const size_t mid = path.find(kMid, kPre.size());
          if (mid == std::string::npos) return;
          const std::string rid = path.substr(kPre.size(), mid - kPre.size());
          const std::string tid_s = path.substr(mid + kMid.size());
          uint64_t tid = 0;
          try {
            tid = std::stoull(tid_s);
          } catch (...) {
            return;
          }
          std::lock_guard<std::mutex> lock(m_radars_mutex);
          for (auto& r : m_radars) {
            if (r->id != rid) continue;
            if (value.is_null()) {  // target lost / removed
              r->targets.erase(tid);
              return;
            }
            if (!value.is_object()) return;
            r->targets[tid] = ParseTarget(tid, value);
            m_last_target_delta_ms = NowMs();
            return;
          }
        };

        if (upd.contains("meta"))
          for (const auto& mv : upd["meta"])
            if (mv.contains("value") && mv["value"].is_object())
              route(mv.value("path", std::string()), mv["value"], true);
        if (upd.contains("values"))
          for (const auto& v : upd["values"]) {
            if (!v.contains("path")) continue;
            const std::string path = v.value("path", std::string());
            if (path.find(".targets.") != std::string::npos)
              route_target(path, v.contains("value") ? v["value"] : json());
            else if (v.contains("value") && v["value"].is_object())
              route(path, v["value"], false);
          }
      }
    } catch (const std::exception& e) {
      JsonError("control stream", e.what());
    }
  });
  m_control_ws->start();
}

void MayaraClient::SetControl(const std::string& control_id,
                              const std::string& json_body) {
  SetControlAt(m_active, control_id, json_body);
}

void MayaraClient::SetControlAt(int index, const std::string& control_id,
                                const std::string& json_body) {
  std::string radar_id;
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    if (index < 0 || index >= static_cast<int>(m_radars.size())) return;
    radar_id = m_radars[index]->id;
  }
  const std::string base = m_base_url;
  const std::string token = TokenFor(base);
  std::thread([this, base, token, radar_id, control_id, json_body] {
    ix::HttpClient http(/*async=*/false);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = 5;
    args->extraHeaders["Content-Type"] = "application/json";
    if (!token.empty()) args->extraHeaders["Authorization"] = "Bearer " + token;
    const std::string url = base + "/signalk/v2/api/vessels/self/radars/" +
                            radar_id + "/controls/" + control_id;
    auto resp = http.put(url, json_body, args);
    LogLine(resp && resp->statusCode == 200 ? 2 : 1,
            "PUT " + radar_id + "/" + control_id + " " + json_body + " -> " +
                (resp ? std::to_string(resp->statusCode) : "no response"));
    // A refused control is otherwise invisible: the radar simply ignores the
    // click. Fold the status in so the UI can explain and ask for permission.
    if (resp) NoteWriteStatus(resp->statusCode, base);
  }).detach();
}

void MayaraClient::AcquireTargetAt(int index, double bearing_deg,
                                   double distance_m) {
  std::string radar_id;
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    if (index < 0 || index >= static_cast<int>(m_radars.size())) return;
    radar_id = m_radars[index]->id;
  }
  double bearing_rad = bearing_deg * M_PI / 180.0;
  while (bearing_rad < 0) bearing_rad += 2 * M_PI;
  while (bearing_rad >= 2 * M_PI) bearing_rad -= 2 * M_PI;
  json body = {{"bearing", bearing_rad}, {"distance", distance_m}};
  const std::string json_body = body.dump();
  const std::string base = m_base_url;
  const std::string token = TokenFor(base);
  std::thread([this, base, token, radar_id, json_body] {
    ix::HttpClient http(/*async=*/false);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = 5;
    args->extraHeaders["Content-Type"] = "application/json";
    if (!token.empty()) args->extraHeaders["Authorization"] = "Bearer " + token;
    const std::string url = base + "/signalk/v2/api/vessels/self/radars/" +
                            radar_id + "/targets";
    auto resp = http.post(url, json_body, args);
    if (resp) NoteWriteStatus(resp->statusCode, base);
  }).detach();
}

void MayaraClient::CancelTargetAt(int index, uint64_t target_id) {
  std::string radar_id;
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    if (index < 0 || index >= static_cast<int>(m_radars.size())) return;
    radar_id = m_radars[index]->id;
  }
  const std::string base = m_base_url;
  const std::string tid = std::to_string(target_id);
  const std::string token = TokenFor(base);
  std::thread([this, base, token, radar_id, tid] {
    ix::HttpClient http(/*async=*/false);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = 5;
    if (!token.empty()) args->extraHeaders["Authorization"] = "Bearer " + token;
    const std::string url = base + "/signalk/v2/api/vessels/self/radars/" +
                            radar_id + "/targets/" + tid;
    auto resp =
        http.request(url, ix::HttpClient::kDelete, std::string(), args);
    if (resp) NoteWriteStatus(resp->statusCode, base);
  }).detach();
}
