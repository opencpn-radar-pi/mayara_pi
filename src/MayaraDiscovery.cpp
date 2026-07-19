/******************************************************************************
 * mayara_pi - mDNS discovery of a Signal K / mayara endpoint.
 *****************************************************************************/
#include "MayaraDiscovery.h"

#include <arpa/inet.h>
#include <sys/select.h>

#include <chrono>
#include <cstring>
#include <string>

#include "mdns.h"

namespace {

struct Found {
  std::string url;
  bool done = false;
};

int RecordCallback(int /*sock*/, const struct sockaddr* from, size_t /*addrlen*/,
                   mdns_entry_type_t /*entry*/, uint16_t /*query_id*/,
                   uint16_t rtype, uint16_t /*rclass*/, uint32_t /*ttl*/,
                   const void* data, size_t size, size_t /*name_offset*/,
                   size_t /*name_length*/, size_t record_offset,
                   size_t record_length, void* user_data) {
  Found* f = static_cast<Found*>(user_data);
  if (rtype != MDNS_RECORDTYPE_SRV) return 0;

  char namebuf[256];
  mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset,
                                                record_length, namebuf,
                                                sizeof(namebuf));
  if (srv.port == 0) return 0;

  // The responder's source address is the server itself.
  char ip[INET6_ADDRSTRLEN] = {0};
  if (from->sa_family == AF_INET) {
    inet_ntop(AF_INET,
              &reinterpret_cast<const struct sockaddr_in*>(from)->sin_addr, ip,
              sizeof(ip));
  }
  if (ip[0] == '\0') return 0;

  f->url = "http://" + std::string(ip) + ":" + std::to_string(srv.port);
  f->done = true;
  return 0;
}

}  // namespace

namespace MayaraDiscovery {

std::string FindSignalK(int timeout_ms) {
  struct sockaddr_in saddr;
  std::memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = INADDR_ANY;
  saddr.sin_port = 0;  // ephemeral -> unicast responses
#ifdef __APPLE__
  saddr.sin_len = sizeof(saddr);
#endif

  int sock = mdns_socket_open_ipv4(&saddr);
  if (sock < 0) return "";

  alignas(8) uint8_t buffer[2048];
  const char* service = "_signalk-http._tcp.local.";
  mdns_query_send(sock, MDNS_RECORDTYPE_PTR, service, std::strlen(service),
                  buffer, sizeof(buffer), 0);

  Found found;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!found.done) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) break;
    const auto remain =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - now)
            .count();

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(remain / 1000000);
    tv.tv_usec = static_cast<long>(remain % 1000000);
    if (select(sock + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
    if (FD_ISSET(sock, &fds))
      mdns_query_recv(sock, buffer, sizeof(buffer), RecordCallback, &found, 0);
  }

  mdns_socket_close(sock);
  return found.url;
}

}  // namespace MayaraDiscovery
