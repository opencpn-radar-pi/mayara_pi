/******************************************************************************
 * mayara_pi - radar control schema + values.
 *****************************************************************************/
#include "RadarControls.h"

void RadarControls::SetSchema(std::vector<ControlDef> defs,
                              std::vector<int> ranges) {
  std::lock_guard<std::mutex> lock(m_);
  defs_ = std::move(defs);
  ranges_ = std::move(ranges);
  has_schema_ = true;
  ++generation_;
  ++schema_generation_;
}

void RadarControls::UpdateDef(const ControlDef& def) {
  std::lock_guard<std::mutex> lock(m_);
  for (auto& d : defs_)
    if (d.id == def.id) {
      d = def;
      ++schema_generation_;
      return;
    }
  defs_.push_back(def);
  ++schema_generation_;
}

void RadarControls::SetValue(const std::string& id, const ControlValue& v) {
  std::lock_guard<std::mutex> lock(m_);
  values_[id] = v;
  ++generation_;
}

bool RadarControls::HasSchema() const {
  std::lock_guard<std::mutex> lock(m_);
  return has_schema_;
}

std::vector<ControlDef> RadarControls::Schema() const {
  std::lock_guard<std::mutex> lock(m_);
  return defs_;
}

std::vector<int> RadarControls::SupportedRanges() const {
  std::lock_guard<std::mutex> lock(m_);
  return ranges_;
}

ControlValue RadarControls::Value(const std::string& id) const {
  std::lock_guard<std::mutex> lock(m_);
  auto it = values_.find(id);
  return it == values_.end() ? ControlValue{} : it->second;
}

uint64_t RadarControls::Generation() const {
  std::lock_guard<std::mutex> lock(m_);
  return generation_;
}

uint64_t RadarControls::SchemaGeneration() const {
  std::lock_guard<std::mutex> lock(m_);
  return schema_generation_;
}
