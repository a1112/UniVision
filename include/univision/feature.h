#pragma once

#include "univision/status.h"
#include "univision/types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace univision {

enum class FeatureKind : std::uint8_t {
  integer,
  floating_point,
  boolean,
  enumeration,
  string,
  command,
};

enum class AccessMode : std::uint8_t {
  unavailable,
  read_only,
  write_only,
  read_write,
};

struct FeatureInfo {
  std::string name;
  std::string display_name;
  std::string description;
  std::string unit;
  FeatureKind kind{FeatureKind::string};
  AccessMode access{AccessMode::unavailable};
  std::optional<double> minimum;
  std::optional<double> maximum;
  std::optional<double> increment;
  std::vector<std::string> enum_entries;
  bool standard_feature{false};
};

class FeatureAccess {
 public:
  virtual ~FeatureAccess() = default;
  [[nodiscard]] virtual std::vector<FeatureInfo> features() const = 0;
  [[nodiscard]] virtual Result<FeatureInfo> feature_info(const std::string& name) const = 0;
  [[nodiscard]] virtual Result<FeatureValue> read_feature(const std::string& name) const = 0;
  virtual Status write_feature(const std::string& name, const FeatureValue& value) = 0;
  virtual Status execute_command(const std::string& name) = 0;
};

}  // namespace univision
