/******************************************************************************
 * mayara_pi - client for mayara-server (REST + WebSocket).
 *
 * Phase 1a: discovery only. Grows to own the spoke + control websockets.
 *****************************************************************************/
#ifndef MAYARA_CLIENT_H_
#define MAYARA_CLIENT_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class MayaraClient {
 public:
  explicit MayaraClient(std::string base_url);
  ~MayaraClient();

  void Start();  // begin discovery on a background thread
  void Stop();

  // Thread-safe one-line status for display in the UI.
  std::string StatusLine();

 private:
  void Run();
  void SetStatus(const std::string& s);

  std::string m_base_url;
  std::thread m_thread;
  std::atomic<bool> m_stop{false};

  std::mutex m_status_mutex;
  std::string m_status{"not connected"};
};

#endif  // MAYARA_CLIENT_H_
