/******************************************************************************
 * mayara_pi - client for mayara-server.
 *****************************************************************************/
#include "MayaraClient.h"

#include <wx/log.h>

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>

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
  const std::string url = m_base_url + "/signalk/v2/api/vessels/self/radars";

  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 5;
  args->transferTimeout = 5;

  auto resp = http.get(url, args);
  if (!resp || resp->statusCode == 0) {
    SetStatus("no mayara-server at " + m_base_url);
    return;
  }
  if (resp->statusCode != 200) {
    SetStatus("GET radars -> HTTP " + std::to_string(resp->statusCode));
    return;
  }

  try {
    auto j = nlohmann::json::parse(resp->body);
    std::string names;
    int count = 0;
    for (auto it = j.begin(); it != j.end(); ++it) {
      const auto& info = it.value();
      std::string name = info.value("name", it.key());
      names += (count ? ", " : "") + name;
      ++count;
    }
    if (count == 0) {
      SetStatus("connected; no radars reported");
    } else {
      SetStatus("connected; " + std::to_string(count) + " radar(s): " + names);
    }
  } catch (const std::exception& e) {
    SetStatus(std::string("JSON parse error: ") + e.what());
  }
}
