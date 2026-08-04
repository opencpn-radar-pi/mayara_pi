/******************************************************************************
 * mayara_pi - HTTP calls against the mayara-server we run ourselves.
 *
 * These live in their own translation unit, free of wx, because IXWebSocket
 * typedefs ssize_t on MSVC (IXSocket.h) and wxWidgets typedefs it to a
 * different type: the two headers cannot meet in one file without a
 * redefinition error. MayaraServer.cpp is full of wx, so it calls through here.
 *****************************************************************************/
#ifndef MAYARA_LOCAL_SERVER_HTTP_H_
#define MAYARA_LOCAL_SERVER_HTTP_H_

#include <string>

namespace LocalServerHttp {

// GET <base_url>/quit: ask mayara-server to shut itself down. Best effort, and
// deliberately usable when we do not know the server's pid.
void RequestQuit(const std::string& base_url);

// True while something still answers on <base_url>.
bool Responds(const std::string& base_url);

}  // namespace LocalServerHttp

#endif  // MAYARA_LOCAL_SERVER_HTTP_H_
