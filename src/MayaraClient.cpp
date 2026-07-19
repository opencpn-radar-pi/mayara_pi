/******************************************************************************
 * mayara_pi - client for mayara-server (multi-radar).
 *****************************************************************************/
#include "MayaraClient.h"

#include <chrono>
#include <cstdint>

#include <wx/log.h>

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

void MayaraClient::Stop() {
  m_stop = true;
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    for (auto& r : m_radars)
      if (r->spoke_ws) r->spoke_ws->stop();
  }
  if (m_control_ws) m_control_ws->stop();
  if (m_thread.joinable()) m_thread.join();
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

void MayaraClient::SetStatus(const std::string& s) {
  {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_status = s;
  }
  wxLogMessage("mayara_pi: %s", s.c_str());
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

std::vector<int> MayaraClient::ShownRadars() {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  const int n = static_cast<int>(m_radars.size());
  std::vector<int> out;
  for (int i : m_shown)
    if (i >= 0 && i < n) out.push_back(i);
  if (out.empty())
    for (int i = 0; i < n && i < 2; ++i) out.push_back(i);  // default first two
  return out;
}

void MayaraClient::SetShown(std::vector<int> indices) {
  std::lock_guard<std::mutex> lock(m_radars_mutex);
  if (indices.size() > 2) indices.resize(2);
  m_shown = std::move(indices);
}

void MayaraClient::Run() {
  while (!m_stop) {
    std::vector<std::string> candidates;
    if (!m_explicit.empty()) {
      candidates.push_back(m_explicit);
    } else {
      SetStatus("searching for mayara-server (mDNS)…");
      std::string found = MayaraDiscovery::FindSignalK(2000);
      if (!found.empty()) candidates.push_back(found);
      if (!m_fallback.empty() && m_fallback != found)
        candidates.push_back(m_fallback);
    }
    for (auto& base : candidates) {
      if (m_stop) return;
      m_base_url = base;
      StripTrailingSlash(m_base_url);
      if (DiscoverAndConnect()) return;
    }
    for (int i = 0; i < 30 && !m_stop; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool MayaraClient::DiscoverAndConnect() {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;

  const std::string url = m_base_url + "/signalk/v2/api/vessels/self/radars";
  auto resp = http.get(url, args);
  if (!resp || resp->statusCode == 0) {
    SetStatus("no server at " + m_base_url);
    return false;
  }
  if (resp->statusCode != 200) {
    SetStatus("GET radars -> HTTP " + std::to_string(resp->statusCode));
    return false;
  }

  // Build the radar list from either shape (keyed object or array).
  std::vector<std::unique_ptr<Radar>> radars;
  try {
    auto j = json::parse(resp->body);
    auto add = [&](const std::string& id, const json& info) {
      auto r = std::make_unique<Radar>();
      r->id = id;
      r->name = info.value("name", id);
      r->spoke_url = info.value("spokeDataUrl", std::string());
      radars.push_back(std::move(r));
    };
    if (j.is_object())
      for (auto it = j.begin(); it != j.end(); ++it) add(it.key(), it.value());
    else if (j.is_array())
      for (const auto& e : j) add(e.value("id", std::string()), e);
  } catch (const std::exception& e) {
    SetStatus(std::string("radars JSON error: ") + e.what());
    return false;
  }
  if (radars.empty()) {
    SetStatus("connected; no radars transmitting");
    return false;
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
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    m_radars = std::move(live);
    m_active = 0;
  }
  ConnectControlStream();
  SetStatus("streaming " + std::to_string(RadarCount()) + " radar(s)");
  return true;
}

bool MayaraClient::FetchCapabilities(Radar* radar) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;
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
        const std::string col = px.value("color", "#00000000");
        Rgba c;
        auto hex = [](char ch) -> int {
          if (ch >= '0' && ch <= '9') return ch - '0';
          if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
          if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
          return 0;
        };
        const size_t i = (!col.empty() && col[0] == '#') ? 1 : 0;
        auto byte = [&](size_t off) -> uint8_t {
          return (off + 1 < col.size())
                     ? static_cast<uint8_t>(hex(col[off]) * 16 + hex(col[off + 1]))
                     : 0;
        };
        c.r = byte(i);
        c.g = byte(i + 2);
        c.b = byte(i + 4);
        c.a = (col.size() >= i + 8) ? byte(i + 6) : 255;
        legend.push_back(c);
      }
    radar->state.Configure(spokes, maxlen, std::move(legend));
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
  } catch (const std::exception&) {
    return false;
  }
}

void MayaraClient::FetchControlValues(Radar* radar) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;
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
  } catch (const std::exception&) {
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

void MayaraClient::ConnectControlStream() {
  const std::string ctrl_url =
      WsBase(m_base_url) + "/signalk/v1/stream?subscribe=none";
  m_control_ws = std::make_unique<ix::WebSocket>();
  m_control_ws->setUrl(ctrl_url);

  m_control_ws->setOnMessageCallback([this](
                                         const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      m_control_ws->send(
          "{\"subscribe\":[{\"path\":\"radars.*.controls.*\",\"period\":1000}]}");
      return;
    }
    if (msg->type != ix::WebSocketMessageType::Message || msg->binary) return;
    try {
      auto j = json::parse(msg->str);
      if (!j.contains("updates")) return;
      for (const auto& upd : j["updates"]) {
        auto route = [&](const std::string& path, const json& value,
                         bool is_meta) {
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
              if (is_meta)
                r->controls.UpdateDef(ParseControlDef(ctrl, value));
              else
                r->controls.SetValue(ctrl, ParseControlValue(value));
              return;
            }
        };
        if (upd.contains("meta"))
          for (const auto& mv : upd["meta"])
            if (mv.contains("value") && mv["value"].is_object())
              route(mv.value("path", std::string()), mv["value"], true);
        if (upd.contains("values"))
          for (const auto& v : upd["values"])
            if (v.contains("value") && v["value"].is_object())
              route(v.value("path", std::string()), v["value"], false);
      }
    } catch (const std::exception&) {
    }
  });
  m_control_ws->start();
}

void MayaraClient::SetControl(const std::string& control_id,
                              const std::string& json_body) {
  std::string radar_id;
  {
    std::lock_guard<std::mutex> lock(m_radars_mutex);
    const int i = m_active;
    if (i < 0 || i >= static_cast<int>(m_radars.size())) return;
    radar_id = m_radars[i]->id;
  }
  const std::string base = m_base_url;
  std::thread([base, radar_id, control_id, json_body] {
    ix::HttpClient http(/*async=*/false);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = 5;
    args->extraHeaders["Content-Type"] = "application/json";
    const std::string url = base + "/signalk/v2/api/vessels/self/radars/" +
                            radar_id + "/controls/" + control_id;
    http.put(url, json_body, args);
  }).detach();
}
