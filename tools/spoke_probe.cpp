/******************************************************************************
 * mayara_pi - spoke stream probe (diagnostic).
 *
 * Connects to a mayara spoke WebSocket, decodes a few RadarMessages with the
 * plugin's own decoder, and prints what it sees. Exercises the protobuf decode
 * and permessage-deflate path end to end against a live server.
 *
 * Build (after `make`):
 *   clang++ -std=c++17 -Iinclude -Ithird_party/IXWebSocket \
 *     tools/spoke_probe.cpp src/RadarMessage.cpp \
 *     build/third_party/IXWebSocket/libixwebsocket.a -lz -o /tmp/spoke_probe
 *   /tmp/spoke_probe [ws-url]
 *****************************************************************************/
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include "RadarMessage.h"

int main(int argc, char** argv) {
  ix::initNetSystem();
  std::string url =
      argc > 1 ? argv[1]
               : "ws://10.56.0.1:6502/signalk/v2/api/vessels/self/radars/"
                 "nav1034A/spokes";
  printf("connecting: %s\n", url.c_str());

  ix::WebSocket ws;
  ws.setUrl(url);
  ws.disableAutomaticReconnection();

  std::atomic<int> msgs{0};
  std::atomic<long> total_spokes{0};
  ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& m) {
    if (m->type == ix::WebSocketMessageType::Open) {
      printf("OPEN (ws-extensions header present=%zu)\n",
             m->openInfo.headers.count("Sec-WebSocket-Extensions"));
    } else if (m->type == ix::WebSocketMessageType::Error) {
      printf("ERROR: %s\n", m->errorInfo.reason.c_str());
    } else if (m->type == ix::WebSocketMessageType::Message && m->binary) {
      std::vector<MayaraSpoke> sp;
      bool ok = DecodeRadarMessage(
          reinterpret_cast<const uint8_t*>(m->str.data()), m->str.size(), sp);
      total_spokes += static_cast<long>(sp.size());
      if (msgs < 3) {
        printf("MSG %zu bytes decode=%d spokes=%zu", m->str.size(), ok,
               sp.size());
        if (!sp.empty()) {
          const auto& s = sp.front();
          printf("  [first: angle=%u range=%um datalen=%zu bearing=%s]",
                 s.angle, s.range, s.data_len,
                 s.has_bearing ? std::to_string(s.bearing).c_str() : "none");
        }
        printf("\n");
      }
      ++msgs;
    }
  });

  ws.start();
  std::this_thread::sleep_for(std::chrono::seconds(4));
  ws.stop();
  printf("total: %d messages, %ld spokes\n", msgs.load(), total_spokes.load());
  return 0;
}
