/******************************************************************************
 * mayara_pi - the mayara-server wire protocol, without the client around it.
 *
 * The pure half of MayaraClient: turning the server's JSON into the plugin's
 * own structs, and working out which URL to talk to. No sockets, no threads,
 * no state -- so it can be unit-tested against captured server responses
 * instead of only against a radar on the other end of a LAN.
 *
 * Everything here is a function of its arguments alone. Anything that needs a
 * connection, a thread or a Radar belongs in MayaraClient.
 *****************************************************************************/
#ifndef MAYARA_PROTOCOL_H_
#define MAYARA_PROTOCOL_H_

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "RadarControls.h"
#include "RadarState.h"

namespace MayaraProtocol {

// mayara-server's own port. The radar API can answer anywhere -- a Signal K
// server hosting the plugin answers it on the Signal K port -- but mayara's
// web GUI only ever lives here.
constexpr int kMayaraPort = 6502;

// Trailing slashes off a base URL, so "http://h:6502/" and "http://h:6502"
// are the same server and not two.
void StripTrailingSlash(std::string& s);

// One control's current value, from a values response or a control delta.
ControlValue ParseControlValue(const nlohmann::json& v);

// One control's schema. `id` is the key it arrived under; the server repeats
// it as a numeric "id" field, which is not the same thing.
ControlDef ParseControlDef(const std::string& id, const nlohmann::json& c);

// http(s) -> ws(s), leaving anything else alone.
std::string WsBase(const std::string& base);

// The spoke stream URL for one radar on one server.
std::string WsUrl(const std::string& base, const std::string& radar_id);

// Is this an answer from the radar API, or merely a 200 from a server that
// happens to route that path? A Signal K server without mayara installed is
// the case that matters: reaching Signal K is no proof that the radar plugin
// is there, and treating the two alike made the plugin settle on a server that
// could never produce a radar. Every shape we accept carries either the 3.4.0
// envelope or at least one radar; a bare {} carries neither and is rejected.
// A lone "version" is not enough either -- every mayara that reports one puts
// it beside a radars object, so on its own it is somebody else's field.
bool LooksLikeRadarApi(const nlohmann::json& j);

// The same host as `base` but on mayara-server's port, or "" when `base` is
// already there (or is not a URL we can take apart).
std::string OnMayaraPort(const std::string& base);

// One target, from either shape the server offers: the delta value under
// `radars.<id>.targets.<tid>`, or an element of the REST target list. They
// agree on everything except the spelling of isDangerous. Angles arrive in
// radians and distances in metres per the Signal K convention; the plugin
// works in degrees and knots, and the conversion happens here.
RadarTarget ParseTarget(uint64_t id, const nlohmann::json& value);

}  // namespace MayaraProtocol

#endif  // MAYARA_PROTOCOL_H_
