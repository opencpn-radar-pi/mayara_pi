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
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// A double as JSON always writes it: with a full stop, whatever the process
// locale says. printf's %g follows LC_NUMERIC, so under a locale that uses a
// comma it emits 1,919 -- which turns {"value":1,919,...} into malformed JSON
// and loses the whole body, not just that field.
inline std::string JsonNum(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  for (char* p = buf; *p; ++p)
    if (*p == ',') *p = '.';
  return buf;
}

struct ControlDef {
  std::string id;  // key, e.g. "gain"
  int numeric_id = 0;
  std::string name, description, category, dataType, units;
  bool isReadOnly = false, hasEnabled = false, hasAuto = false;
  bool hasAutoAdjustable = false;  // auto mode also takes an offset (HALO Sea)
  bool has_min = false, has_max = false, has_step = false;
  double minValue = 0, maxValue = 0, stepValue = 0, maxDistance = 0;
  double autoAdjustMin = 0, autoAdjustMax = 0;
  std::map<int, std::string> descriptions;  // enum value -> label
  std::vector<int> validValues;             // enum: settable values

  bool operator==(const ControlDef& o) const {
    return id == o.id && numeric_id == o.numeric_id && name == o.name &&
           description == o.description && category == o.category &&
           dataType == o.dataType && units == o.units &&
           isReadOnly == o.isReadOnly && hasEnabled == o.hasEnabled &&
           hasAuto == o.hasAuto && hasAutoAdjustable == o.hasAutoAdjustable &&
           has_min == o.has_min && has_max == o.has_max &&
           has_step == o.has_step && minValue == o.minValue &&
           maxValue == o.maxValue && stepValue == o.stepValue &&
           maxDistance == o.maxDistance && autoAdjustMin == o.autoAdjustMin &&
           autoAdjustMax == o.autoAdjustMax &&
           descriptions == o.descriptions && validValues == o.validValues;
  }
  bool operator!=(const ControlDef& o) const { return !(*this == o); }
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
  void UpdateDef(const ControlDef& def);  // replace/insert one control's schema
  void SetValue(const std::string& id, const ControlValue& v);

  bool HasSchema() const;
  std::vector<ControlDef> Schema() const;      // in received order
  std::vector<int> SupportedRanges() const;
  ControlValue Value(const std::string& id) const;  // default if unknown
  uint64_t Generation() const;        // bumps on any value change
  uint64_t SchemaGeneration() const;  // bumps when the schema changes

 private:
  mutable std::mutex m_;
  std::vector<ControlDef> defs_;
  std::vector<int> ranges_;
  std::map<std::string, ControlValue> values_;
  uint64_t generation_ = 0;
  uint64_t schema_generation_ = 0;
  bool has_schema_ = false;
};

#endif  // MAYARA_RADAR_CONTROLS_H_
