/******************************************************************************
 * mayara_pi - the mayara-server wire protocol (see MayaraProtocol.h).
 *****************************************************************************/
#include "MayaraProtocol.h"

#include <cmath>
#include <cstdlib>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using json = nlohmann::json;

namespace MayaraProtocol {

void StripTrailingSlash(std::string& s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
}

ControlValue ParseControlValue(const json& v) {
  ControlValue cv;
  if (v.contains("value") && !v["value"].is_null()) {
    cv.has_value = true;
    if (v["value"].is_string())
      cv.str_value = v["value"].get<std::string>();
    else
      cv.value = v["value"].get<double>();
  }
  if (v.contains("auto")) {
    cv.has_auto = true;
    cv.auto_ = v["auto"].get<bool>();
  }
  if (v.contains("enabled")) {
    cv.has_enabled = true;
    cv.enabled = v["enabled"].get<bool>();
  }
  cv.allowed = v.value("allowed", true);
  cv.autoValue = v.value("autoValue", 0.0);
  cv.endValue = v.value("endValue", 0.0);
  cv.startDistance = v.value("startDistance", 0.0);
  cv.endDistance = v.value("endDistance", 0.0);
  cv.error = v.value("error", std::string());
  return cv;
}

ControlDef ParseControlDef(const std::string& id, const json& c) {
  ControlDef d;
  d.id = id;
  d.numeric_id = c.value("id", 0);
  d.name = c.value("name", id);
  d.description = c.value("description", std::string());
  d.category = c.value("category", std::string());
  d.dataType = c.value("dataType", std::string());
  d.units = c.value("units", std::string());
  d.isReadOnly = c.value("isReadOnly", false);
  d.hasEnabled = c.value("hasEnabled", false);
  d.hasAuto = c.value("hasAuto", false) || c.contains("automatic");
  d.hasAutoAdjustable = c.value("hasAutoAdjustable", false);
  d.autoAdjustMin = c.value("autoAdjustMinValue", 0.0);
  d.autoAdjustMax = c.value("autoAdjustMaxValue", 0.0);
  if (c.contains("minValue")) {
    d.has_min = true;
    d.minValue = c["minValue"].get<double>();
  }
  if (c.contains("maxValue")) {
    d.has_max = true;
    d.maxValue = c["maxValue"].get<double>();
  }
  if (c.contains("stepValue")) {
    d.has_step = true;
    d.stepValue = c["stepValue"].get<double>();
  }
  d.maxDistance = c.value("maxDistance", 0.0);
  if (c.contains("descriptions") && c["descriptions"].is_object())
    for (auto dit = c["descriptions"].begin(); dit != c["descriptions"].end();
         ++dit) {
      try {
        d.descriptions[std::stoi(dit.key())] = dit.value().get<std::string>();
      } catch (...) {
      }
    }
  if (c.contains("validValues") && c["validValues"].is_array())
    for (const auto& v : c["validValues"]) d.validValues.push_back(v.get<int>());
  return d;
}

std::string WsBase(const std::string& base) {
  if (base.rfind("https://", 0) == 0) return "wss://" + base.substr(8);
  if (base.rfind("http://", 0) == 0) return "ws://" + base.substr(7);
  return base;
}

std::string WsUrl(const std::string& base, const std::string& radar_id) {
  return WsBase(base) + "/signalk/v2/api/vessels/self/radars/" + radar_id +
         "/spokes";
}

bool LooksLikeRadarApi(const json& j) {
  if (j.is_array()) return true;
  if (!j.is_object()) return false;
  if (j.contains("radars") && j["radars"].is_object()) return true;
  // Older shape: radars keyed at the top level, anything but "version".
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "version" && it.value().is_object()) return true;
  return false;
}

std::string OnMayaraPort(const std::string& base) {
  const size_t sep = base.find("://");
  if (sep == std::string::npos) return "";
  const size_t start = sep + 3;
  size_t end = base.find('/', start);
  if (end == std::string::npos) end = base.size();
  std::string host = base.substr(start, end - start);
  if (host.empty()) return "";
  // An IPv6 literal keeps its brackets, and its colons are not the port's.
  size_t colon = host.rfind(':');
  const size_t bracket = host.rfind(']');
  if (colon != std::string::npos && bracket != std::string::npos &&
      colon < bracket)
    colon = std::string::npos;
  int port = base.rfind("https://", 0) == 0 ? 443 : 80;
  if (colon != std::string::npos) {
    port = std::atoi(host.c_str() + colon + 1);
    host.erase(colon);
  }
  if (port == kMayaraPort) return "";
  return base.substr(0, start) + host + ":" + std::to_string(kMayaraPort);
}


RadarTarget ParseTarget(uint64_t id, const json& value) {
  RadarTarget t;
  t.id = id;
  const std::string st = value.value("status", std::string());
  t.status = st == "tracking" ? RadarTarget::kTracking
             : st == "lost"   ? RadarTarget::kLost
                              : RadarTarget::kAcquiring;
  t.manual = value.value("acquisition", std::string()) == "manual";
  if (value.contains("position") && value["position"].is_object()) {
    const auto& p = value["position"];
    t.bearing_deg = p.value("bearing", 0.0) * 180.0 / M_PI;
    t.distance_m = p.value("distance", 0.0);
  }
  if (value.contains("motion") && value["motion"].is_object()) {
    const auto& m = value["motion"];
    t.has_motion = true;
    t.course_deg = m.value("course", 0.0) * 180.0 / M_PI;
    t.speed_kn = m.value("speed", 0.0) * 1.9438445;  // m/s -> kn
  }
  if (value.contains("danger") && value["danger"].is_object()) {
    const auto& d = value["danger"];
    t.has_danger = true;
    t.cpa_m = d.value("cpa", 0.0);
    t.tcpa_s = d.value("tcpa", 0.0);
    t.is_dangerous = d.value("isDangerous", d.value("is_dangerous", false));
  }
  return t;
}

}  // namespace MayaraProtocol
