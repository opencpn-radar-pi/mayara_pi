/******************************************************************************
 * mayara_pi - zero-config discovery of a mayara / Signal K endpoint via mDNS.
 *****************************************************************************/
#ifndef MAYARA_DISCOVERY_H_
#define MAYARA_DISCOVERY_H_

#include <string>

namespace MayaraDiscovery {

// Browse mDNS/Bonjour for something that serves the radar API, for up to
// timeout_ms. Returns a base URL like "http://10.56.0.1:6502", or "" if nothing
// answered.
//
// Two services are asked for at once:
//   _mayara-http._tcp   mayara-server itself (3.8.0 and later)
//   _signalk-http._tcp  a Signal K server, which may host mayara as a plugin
//
// A direct mayara answer wins: it is certain to serve the radar API, whereas a
// Signal K server only might. The browse therefore stops early on a mayara
// reply but keeps listening out the full timeout for a Signal K one, in case a
// mayara answer is still coming.
// On macOS this goes through the system responder (mDNSResponder owns port
// 5353, so our own socket sends queries but never sees the answers); elsewhere
// it uses the raw socket browse in FindViaSocket.
std::string FindServer(int timeout_ms = 2000);

// The raw-socket browse. Exposed only so it can be tested directly; callers
// want FindServer.
std::string FindViaSocket(int timeout_ms = 2000);

}  // namespace MayaraDiscovery

#endif  // MAYARA_DISCOVERY_H_
