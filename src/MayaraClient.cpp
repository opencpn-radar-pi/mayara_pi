/******************************************************************************
 * mayara_pi - client for mayara-server.
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

// Convert an http(s) base URL to its ws(s) equivalent.
std::string WsBase(const std::string& base) {
  if (base.rfind("https://", 0) == 0) return "wss://" + base.substr(8);
  if (base.rfind("http://", 0) == 0) return "ws://" + base.substr(7);
  return base;
}

// Build the spoke WebSocket URL from an http(s) base + radar id (spec fallback
// when spokeDataUrl is absent).
std::string WsUrl(const std::string& base, const std::string& radar_id) {
  return WsBase(base) + "/signalk/v2/api/vessels/self/radars/" + radar_id +
         "/spokes";
}
}  // namespace

namespace {

// Parse "#RRGGBBAA" (or "#RRGGBB") into an Rgba.
Rgba ParseColor(const std::string& s) {
  Rgba c;
  auto hex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 0;
  };
  size_t i = (!s.empty() && s[0] == '#') ? 1 : 0;
  auto byte = [&](size_t off) -> uint8_t {
    if (off + 1 >= s.size()) return 0;
    return static_cast<uint8_t>(hex(s[off]) * 16 + hex(s[off + 1]));
  };
  c.r = byte(i);
  c.g = byte(i + 2);
  c.b = byte(i + 4);
  c.a = (s.size() >= i + 8) ? byte(i + 6) : 255;
  return c;
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
  if (m_spoke_ws) {
    m_spoke_ws->stop();
    m_spoke_ws.reset();
  }
  if (m_control_ws) {
    m_control_ws->stop();
    m_control_ws.reset();
  }
  if (m_thread.joinable()) m_thread.join();
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

void MayaraClient::Run() {
  // Each round, build an ordered candidate list and keep the first that
  // actually streams spokes. A forced address (MAYARA_SERVER) wins; otherwise
  // try the mDNS-discovered Signal K endpoint, then the known fallback.
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
      if (DiscoverAndConnect()) return;  // streaming
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
    SetStatus("no mayara-server at " + m_base_url);
    return false;
  }
  if (resp->statusCode != 200) {
    SetStatus("GET radars -> HTTP " + std::to_string(resp->statusCode));
    return false;
  }

  std::string spoke_url;
  try {
    auto j = json::parse(resp->body);
    // Two list shapes: keyed object {id:{...}} (mayara) or array [{id,...}]
    // (Signal K server). Pick the first radar (selection is a later phase).
    const json* radar = nullptr;
    if (j.is_object() && !j.empty()) {
      auto it = j.begin();
      m_radar_id = it.key();
      radar = &it.value();
    } else if (j.is_array() && !j.empty()) {
      radar = &j[0];
      m_radar_id = radar->value("id", std::string());
    }
    if (!radar || m_radar_id.empty()) {
      SetStatus("connected; no radars transmitting");
      return false;
    }
    spoke_url = radar->value("spokeDataUrl", std::string());
  } catch (const std::exception& e) {
    SetStatus(std::string("radars JSON error: ") + e.what());
    return false;
  }

  if (!FetchCapabilities(m_radar_id)) return false;

  // Signal K Radar API: if spokeDataUrl is absent, construct it from this host.
  if (spoke_url.empty()) spoke_url = WsUrl(m_base_url, m_radar_id);

  if (!ConnectSpokes(spoke_url)) {
    SetStatus("no spoke stream at " + m_base_url);
    return false;
  }
  ConnectControlStream();  // live control values (best-effort)
  return true;
}

void MayaraClient::ConnectControlStream() {
  const std::string url =
      WsBase(m_base_url) + "/signalk/v1/stream?subscribe=none";
  m_control_ws = std::make_unique<ix::WebSocket>();
  m_control_ws->setUrl(url);

  const std::string prefix = "radars." + m_radar_id + ".controls.";
  m_control_ws->setOnMessageCallback([this, prefix](
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
        if (!upd.contains("values")) continue;
        for (const auto& v : upd["values"]) {
          const std::string path = v.value("path", std::string());
          if (path.rfind(prefix, 0) != 0) continue;
          const std::string ctrl = path.substr(prefix.size());
          if (v.contains("value") && v["value"].is_object())
            m_controls.SetValue(ctrl, ParseControlValue(v["value"]));
        }
      }
    } catch (const std::exception&) {
    }
  });
  m_control_ws->start();
}

bool MayaraClient::FetchCapabilities(const std::string& radar_id) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;

  const std::string url = m_base_url +
                          "/signalk/v2/api/vessels/self/radars/" + radar_id +
                          "/capabilities";
  auto resp = http.get(url, args);
  if (!resp || resp->statusCode != 200) {
    SetStatus("capabilities fetch failed");
    return false;
  }
  try {
    auto j = json::parse(resp->body);
    int spokes = j.value("spokesPerRevolution", 2048);
    int maxlen = j.value("maxSpokeLength", 1024);
    std::vector<Rgba> legend;
    if (j.contains("legend") && j["legend"].contains("pixels")) {
      for (const auto& px : j["legend"]["pixels"]) {
        legend.push_back(ParseColor(px.value("color", "#00000000")));
      }
    }
    m_state.Configure(spokes, maxlen, std::move(legend));

    // Control schema (self-describing) + supported ranges, for the UI.
    std::vector<ControlDef> defs;
    if (j.contains("controls") && j["controls"].is_object()) {
      for (auto it = j["controls"].begin(); it != j["controls"].end(); ++it) {
        const auto& c = it.value();
        ControlDef d;
        d.id = it.key();
        d.numeric_id = c.value("id", 0);
        d.name = c.value("name", d.id);
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
        if (c.contains("descriptions") && c["descriptions"].is_object()) {
          for (auto dit = c["descriptions"].begin();
               dit != c["descriptions"].end(); ++dit) {
            try {
              d.descriptions[std::stoi(dit.key())] =
                  dit.value().get<std::string>();
            } catch (...) {
            }
          }
        }
        if (c.contains("validValues") && c["validValues"].is_array())
          for (const auto& v : c["validValues"])
            d.validValues.push_back(v.get<int>());
        defs.push_back(std::move(d));
      }
    }
    std::vector<int> ranges;
    if (j.contains("supportedRanges") && j["supportedRanges"].is_array())
      for (const auto& r : j["supportedRanges"])
        ranges.push_back(r.get<int>());
    m_controls.SetSchema(std::move(defs), std::move(ranges));

    FetchControlValues(radar_id);
    return true;
  } catch (const std::exception& e) {
    SetStatus(std::string("capabilities JSON error: ") + e.what());
    return false;
  }
}

void MayaraClient::FetchControlValues(const std::string& radar_id) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;
  const std::string url = m_base_url +
                          "/signalk/v2/api/vessels/self/radars/" + radar_id +
                          "/controls";
  auto resp = http.get(url, args);
  if (!resp || resp->statusCode != 200) return;
  try {
    auto j = json::parse(resp->body);
    if (j.is_object())
      for (auto it = j.begin(); it != j.end(); ++it)
        m_controls.SetValue(it.key(), ParseControlValue(it.value()));
  } catch (const std::exception&) {
  }
}

void MayaraClient::SetControl(const std::string& control_id,
                              const std::string& json_body) {
  const std::string base = m_base_url;
  const std::string radar = m_radar_id;
  std::thread([base, radar, control_id, json_body] {
    ix::HttpClient http(/*async=*/false);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = 5;
    args->extraHeaders["Content-Type"] = "application/json";
    const std::string url = base + "/signalk/v2/api/vessels/self/radars/" +
                            radar + "/controls/" + control_id;
    http.put(url, json_body, args);
  }).detach();
}

bool MayaraClient::ConnectSpokes(const std::string& spoke_url) {
  m_streaming = false;
  m_ws_error = false;
  m_spoke_ws = std::make_unique<ix::WebSocket>();
  m_spoke_ws->setUrl(spoke_url);
  m_spoke_ws->disableAutomaticReconnection();

  RadarState* state = &m_state;
  m_spoke_ws->setOnMessageCallback([this, state](
                                       const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message && msg->binary) {
      m_streaming = true;
      const auto* buf = reinterpret_cast<const uint8_t*>(msg->str.data());
      std::vector<MayaraSpoke> spokes;
      if (DecodeRadarMessage(buf, msg->str.size(), spokes)) {
        double plat = 0, plon = 0;
        bool have_pos = false;
        uint32_t hb_angle = 0, hb_bearing = 0;
        bool have_heading = false;
        for (const auto& s : spokes) {
          state->WriteSpoke(s.angle, s.data, s.data_len, s.range);
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
        if (have_pos) state->SetPosition(plat, plon);
        if (have_heading) state->SetHeadingFromBearing(hb_angle, hb_bearing);
      }
    } else if (msg->type == ix::WebSocketMessageType::Open) {
      m_streaming = true;
      SetStatus("streaming radar " + m_radar_id);
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      m_ws_error = true;
      SetStatus("spoke stream error: " + msg->errorInfo.reason);
    }
  });

  m_spoke_ws->start();
  SetStatus("connecting spokes for " + m_radar_id);

  // Verify the stream actually opens; otherwise the caller tries the next
  // candidate endpoint (breaking early once a connection error is reported).
  for (int i = 0; i < 50 && !m_stop; ++i) {
    if (m_streaming) return true;
    if (m_ws_error) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  m_spoke_ws->stop();
  m_spoke_ws.reset();
  return false;
}
