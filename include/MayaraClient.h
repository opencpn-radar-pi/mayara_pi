/******************************************************************************
 * mayara_pi - client for mayara-server (REST + WebSocket).
 *
 * Phase 1a/1b: discover a radar, fetch its capabilities (geometry + legend),
 * and stream the binary protobuf spokes into a RadarState.
 *****************************************************************************/
#ifndef MAYARA_CLIENT_H_
#define MAYARA_CLIENT_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "RadarState.h"

namespace ix {
class WebSocket;
}

class MayaraClient {
 public:
  // explicit_url: forced address (e.g. from MAYARA_SERVER), or "" to
  // auto-discover via mDNS. fallback_url: used if discovery finds nothing.
  MayaraClient(std::string explicit_url, std::string fallback_url);
  ~MayaraClient();

  void Start();
  void Stop();

  std::string StatusLine();
  RadarState* State() { return &m_state; }

 private:
  void Run();                          // background: discover + connect, retry
  bool DiscoverAndConnect();           // one attempt; true once streaming
  bool FetchCapabilities(const std::string& radar_id);
  bool ConnectSpokes(const std::string& spoke_url);  // true if it opens
  void SetStatus(const std::string& s);

  std::string m_explicit;   // forced address, or empty to discover
  std::string m_fallback;   // used when discovery finds nothing
  std::string m_base_url;   // resolved address in use
  std::thread m_thread;
  std::atomic<bool> m_stop{false};

  std::mutex m_status_mutex;
  std::string m_status{"not connected"};
  std::atomic<bool> m_streaming{false};  // spoke WS actually opened
  std::atomic<bool> m_ws_error{false};   // spoke WS failed to connect

  RadarState m_state;
  std::unique_ptr<ix::WebSocket> m_spoke_ws;
  std::string m_radar_id;
};

#endif  // MAYARA_CLIENT_H_
