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
  explicit MayaraClient(std::string base_url);
  ~MayaraClient();

  void Start();
  void Stop();

  std::string StatusLine();
  RadarState* State() { return &m_state; }

 private:
  void Run();                          // background: discover + connect, retry
  bool DiscoverAndConnect();           // one attempt; true once streaming
  bool FetchCapabilities(const std::string& radar_id);
  void ConnectSpokes(const std::string& spoke_url);
  void SetStatus(const std::string& s);

  std::string m_base_url;
  std::thread m_thread;
  std::atomic<bool> m_stop{false};

  std::mutex m_status_mutex;
  std::string m_status{"not connected"};

  RadarState m_state;
  std::unique_ptr<ix::WebSocket> m_spoke_ws;
  std::string m_radar_id;
};

#endif  // MAYARA_CLIENT_H_
