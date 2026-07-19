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

#include "RadarMessage.h"

using json = nlohmann::json;

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

MayaraClient::MayaraClient(std::string base_url)
    : m_base_url(std::move(base_url)) {
  while (!m_base_url.empty() && m_base_url.back() == '/') m_base_url.pop_back();
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
  // Retry discovery until a radar is streaming or we are told to stop.
  while (!m_stop) {
    if (DiscoverAndConnect()) return;
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
    if (j.empty()) {
      SetStatus("connected; no radars transmitting");
      return false;
    }
    // Pick the first radar for now (multi-radar selection is a later phase).
    auto it = j.begin();
    m_radar_id = it.key();
    spoke_url = it.value().value("spokeDataUrl", "");
  } catch (const std::exception& e) {
    SetStatus(std::string("radars JSON error: ") + e.what());
    return false;
  }

  if (!FetchCapabilities(m_radar_id)) return false;
  if (spoke_url.empty())
    spoke_url = m_base_url + "/signalk/v2/api/vessels/self/radars/" +
                m_radar_id + "/spokes";
  ConnectSpokes(spoke_url);
  return true;
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
    return true;
  } catch (const std::exception& e) {
    SetStatus(std::string("capabilities JSON error: ") + e.what());
    return false;
  }
}

void MayaraClient::ConnectSpokes(const std::string& spoke_url) {
  m_spoke_ws = std::make_unique<ix::WebSocket>();
  m_spoke_ws->setUrl(spoke_url);
  m_spoke_ws->disableAutomaticReconnection();

  RadarState* state = &m_state;
  m_spoke_ws->setOnMessageCallback([this, state](
                                       const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message && msg->binary) {
      const auto* buf =
          reinterpret_cast<const uint8_t*>(msg->str.data());
      std::vector<MayaraSpoke> spokes;
      if (DecodeRadarMessage(buf, msg->str.size(), spokes)) {
        for (const auto& s : spokes)
          state->WriteSpoke(s.angle, s.data, s.data_len, s.range);
      }
    } else if (msg->type == ix::WebSocketMessageType::Open) {
      SetStatus("streaming radar " + m_radar_id);
    } else if (msg->type == ix::WebSocketMessageType::Error) {
      SetStatus("spoke stream error: " + msg->errorInfo.reason);
    }
  });

  m_spoke_ws->start();
  SetStatus("connecting spokes for " + m_radar_id);
}
