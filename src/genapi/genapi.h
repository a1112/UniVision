#pragma once

#include "univision/feature.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace univision::genapi {

using PortRead =
    std::function<Status(std::uint64_t address, void* destination, std::size_t size)>;
using PortWrite =
    std::function<Status(std::uint64_t address, const void* source, std::size_t size)>;

class NodeMap {
 public:
  [[nodiscard]] static Result<std::shared_ptr<NodeMap>> parse(
      std::string xml, PortRead read, PortWrite write);

  [[nodiscard]] std::vector<FeatureInfo> features() const;
  [[nodiscard]] Result<FeatureInfo> feature_info(const std::string& name) const;
  [[nodiscard]] Result<FeatureValue> read(const std::string& name) const;
  Status write(const std::string& name, const FeatureValue& value);
  Status execute(const std::string& name);
  [[nodiscard]] bool contains(const std::string& name) const;

 private:
  class Impl;
  explicit NodeMap(std::shared_ptr<Impl> implementation);

  std::shared_ptr<Impl> implementation_;
};

}  // namespace univision::genapi
