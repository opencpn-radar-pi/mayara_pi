/******************************************************************************
 * mayara_pi - mDNS discovery of a Signal K / mayara endpoint.
 *****************************************************************************/
#include "MayaraDiscovery.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#endif

#include <chrono>
#include <cstring>
#include <string>

#include "mdns.h"

#ifdef __APPLE__
#include <dns_sd.h>
#endif

namespace {

const char* kMayaraService = "_mayara-http._tcp.local.";
const char* kSignalKService = "_signalk-http._tcp.local.";

struct Found {
  std::string mayara;   // a mayara-server answered directly
  std::string signalk;  // a Signal K server answered (may or may not host us)
};

int RecordCallback(int /*sock*/, const struct sockaddr* from, size_t /*addrlen*/,
                   mdns_entry_type_t /*entry*/, uint16_t /*query_id*/,
                   uint16_t rtype, uint16_t /*rclass*/, uint32_t /*ttl*/,
                   const void* data, size_t size, size_t name_offset,
                   size_t name_length, size_t record_offset,
                   size_t record_length, void* user_data) {
  Found* f = static_cast<Found*>(user_data);
  if (rtype != MDNS_RECORDTYPE_SRV) return 0;

  // Both services are asked for on one socket, so the answer has to say which
  // it is. An SRV's owner name is "instance._service._tcp.local.".
  char ownerbuf[256];
  size_t offset = name_offset;
  mdns_string_t owner =
      mdns_string_extract(data, size, &offset, ownerbuf, sizeof(ownerbuf));
  const std::string name(owner.str, owner.length);
  (void)name_length;
  const bool is_mayara = name.find("_mayara-http") != std::string::npos;
  const bool is_signalk = name.find("_signalk-http") != std::string::npos;
  if (!is_mayara && !is_signalk) return 0;

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

  const std::string url =
      "http://" + std::string(ip) + ":" + std::to_string(srv.port);
  std::string& slot = is_mayara ? f->mayara : f->signalk;
  if (slot.empty()) slot = url;  // first answer of its kind wins
  return 0;
}

#ifdef __APPLE__
// macOS answers mDNS through mDNSResponder, which owns port 5353. A socket of
// our own therefore sends its query fine but never sees the multicast answer --
// verified with a passive listener, which saw only our own outgoing question
// while `dns-sd -L` resolved the same service instantly. So on this platform
// the browse goes through the system responder instead of the raw socket.
struct BrowseCtx {
  std::string name, regtype, domain;
  uint32_t iface = 0;
  bool got = false;
};

void DNSSD_API BrowseCb(DNSServiceRef, DNSServiceFlags flags, uint32_t iface,
                        DNSServiceErrorType err, const char* name,
                        const char* regtype, const char* domain, void* ctx) {
  auto* b = static_cast<BrowseCtx*>(ctx);
  if (err != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd) || b->got)
    return;
  b->name = name ? name : "";
  b->regtype = regtype ? regtype : "";
  b->domain = domain ? domain : "";
  b->iface = iface;
  b->got = true;
}

struct ResolveCtx {
  std::string url;
  bool got = false;
};

void DNSSD_API ResolveCb(DNSServiceRef, DNSServiceFlags, uint32_t,
                         DNSServiceErrorType err, const char*,
                         const char* hosttarget, uint16_t port, uint16_t,
                         const unsigned char*, void* ctx) {
  auto* r = static_cast<ResolveCtx*>(ctx);
  if (err != kDNSServiceErr_NoError || !hosttarget || r->got) return;
  std::string host(hosttarget);
  if (!host.empty() && host.back() == '.') host.pop_back();  // "mayara.local."
  r->url = "http://" + host + ":" + std::to_string(ntohs(port));
  r->got = true;
}

// Drive `ref` until `flag` is set or the deadline passes.
void Pump(DNSServiceRef ref, const bool& flag,
          std::chrono::steady_clock::time_point deadline) {
  const int fd = DNSServiceRefSockFD(ref);
  if (fd < 0) return;
  while (!flag) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return;
    const auto remain =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - now)
            .count();
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(remain / 1000000);
    tv.tv_usec = static_cast<long>(remain % 1000000);
    if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) return;
    if (DNSServiceProcessResult(ref) != kDNSServiceErr_NoError) return;
  }
}

std::string FindViaSystem(const char* regtype,
                          std::chrono::steady_clock::time_point deadline) {
  BrowseCtx b;
  DNSServiceRef browse = nullptr;
  if (DNSServiceBrowse(&browse, 0, 0, regtype, nullptr, BrowseCb, &b) !=
      kDNSServiceErr_NoError)
    return "";
  Pump(browse, b.got, deadline);
  DNSServiceRefDeallocate(browse);
  if (!b.got) return "";

  ResolveCtx r;
  DNSServiceRef resolve = nullptr;
  if (DNSServiceResolve(&resolve, 0, b.iface, b.name.c_str(),
                        b.regtype.c_str(), b.domain.c_str(), ResolveCb,
                        &r) != kDNSServiceErr_NoError)
    return "";
  Pump(resolve, r.got, deadline);
  DNSServiceRefDeallocate(resolve);
  return r.url;
}
#endif  // __APPLE__

}  // namespace

namespace MayaraDiscovery {

std::string FindServer(int timeout_ms) {
#ifdef __APPLE__
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  // A direct mayara wins; fall back to a Signal K server that may host it.
  std::string url = FindViaSystem("_mayara-http._tcp", deadline);
  if (url.empty()) url = FindViaSystem("_signalk-http._tcp", deadline);
  return url;
#else
  return FindViaSocket(timeout_ms);
#endif
}

std::string FindViaSocket(int timeout_ms) {
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
  for (const char* service : {kMayaraService, kSignalKService})
    mdns_query_send(sock, MDNS_RECORDTYPE_PTR, service, std::strlen(service),
                    buffer, sizeof(buffer), 0);

  Found found;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (found.mayara.empty()) {
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
  // A mayara answer is certain to serve the radar API; a Signal K one only
  // might, so it is the fallback rather than a tie.
  return !found.mayara.empty() ? found.mayara : found.signalk;
}

}  // namespace MayaraDiscovery
