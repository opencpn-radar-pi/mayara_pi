/******************************************************************************
 * mayara_pi - HTTP calls against our own mayara-server. No wx here; see the
 * header for why.
 *****************************************************************************/
#include "LocalServerHttp.h"

#include <ixwebsocket/IXHttpClient.h>

namespace LocalServerHttp {

// Short timeouts: this runs on the UI thread, and the reply ("bye") comes back
// before the shutdown it triggers.
void RequestQuit(const std::string& base_url) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 1;
  args->transferTimeout = 2;
  http.get(base_url + "/quit", args);  // result ignored: best effort
}

bool Responds(const std::string& base_url) {
  ix::HttpClient http(/*async=*/false);
  auto args = http.createRequest();
  args->connectTimeout = 1;
  args->transferTimeout = 1;
  auto resp = http.get(base_url + "/signalk", args);
  return resp && resp->statusCode > 0;  // 0 == could not connect
}

}  // namespace LocalServerHttp
