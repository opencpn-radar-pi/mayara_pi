/******************************************************************************
 * mayara_pi - zero-config discovery of a Signal K / mayara endpoint via mDNS.
 *****************************************************************************/
#ifndef MAYARA_DISCOVERY_H_
#define MAYARA_DISCOVERY_H_

#include <string>

namespace MayaraDiscovery {

// Browse mDNS/Bonjour for a Signal K HTTP endpoint (_signalk-http._tcp) for up
// to timeout_ms. Returns a base URL like "http://10.56.0.1:6502", or "" if
// nothing answered. mayara-server advertises this service (standalone or as a
// Signal K provider), so the returned endpoint serves the radar REST API.
std::string FindSignalK(int timeout_ms = 2000);

}  // namespace MayaraDiscovery

#endif  // MAYARA_DISCOVERY_H_
