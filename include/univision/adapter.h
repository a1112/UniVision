#pragma once

#include "univision/camera.h"
#include "univision/export.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace univision {

struct AdapterDescriptor {
  std::string id;
  std::string name;
  std::string version;
  std::string vendor;
  std::int32_t priority{};
};

class Adapter {
 public:
  virtual ~Adapter() = default;
  [[nodiscard]] virtual const AdapterDescriptor& descriptor() const noexcept = 0;
  [[nodiscard]] virtual Result<std::vector<DeviceInfo>> enumerate_devices() = 0;
  [[nodiscard]] virtual Result<std::shared_ptr<Camera>> create_camera(
      const DeviceInfo& device) = 0;
};

using AdapterPtr = std::shared_ptr<Adapter>;

}  // namespace univision
