/******************************************************************************
 * mayara_pi - radar control schema + values.
 *
 * The self-describing control model from the Signal K Radar API: a schema
 * (ControlDefinition per control) plus current values. The UI generates widgets
 * from this, like the mayara web GUI. wx/JSON-free for testability.
 *****************************************************************************/
#ifndef MAYARA_RADAR_CONTROLS_H_
#define MAYARA_RADAR_CONTROLS_H_

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct ControlDef {
  std::string id;  // key, e.g. "gain"
  int numeric_id = 0;
  std::string name, description, category, dataType, units;
  bool isReadOnly = false, hasEnabled = false, hasAuto = false;
  bool has_min = false, has_max = false, has_step = false;
  double minValue = 0, maxValue = 0, stepValue = 0, maxDistance = 0;
  std::map<int, std::string> descriptions;  // enum value -> label
  std::vector<int> validValues;             // enum: settable values
};

struct ControlValue {
  bool has_value = false;
  double value = 0;
  std::string str_value;
  bool has_auto = false, auto_ = false;
  bool has_enabled = false, enabled = false;
  bool allowed = true;
  double autoValue = 0, endValue = 0, startDistance = 0, endDistance = 0;
  std::string error;
};

class RadarControls {
 public:
  void SetSchema(std::vector<ControlDef> defs, std::vector<int> ranges);
  void SetValue(const std::string& id, const ControlValue& v);

  bool HasSchema() const;
  std::vector<ControlDef> Schema() const;      // in received order
  std::vector<int> SupportedRanges() const;
  ControlValue Value(const std::string& id) const;  // default if unknown
  uint64_t Generation() const;  // bumps on any schema/value change

 private:
  mutable std::mutex m_;
  std::vector<ControlDef> defs_;
  std::vector<int> ranges_;
  std::map<std::string, ControlValue> values_;
  uint64_t generation_ = 0;
  bool has_schema_ = false;
};

#endif  // MAYARA_RADAR_CONTROLS_H_
