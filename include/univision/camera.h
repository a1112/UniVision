#pragma once

#include "univision/feature.h"
#include "univision/stream.h"

#include <memory>

namespace univision {

class Camera : public FeatureAccess {
 public:
  ~Camera() override = default;
  [[nodiscard]] virtual const DeviceInfo& device_info() const noexcept = 0;
  [[nodiscard]] virtual CameraState state() const noexcept = 0;
  virtual Status open() = 0;
  virtual Status close() = 0;
  virtual Status reconnect() = 0;
  [[nodiscard]] virtual Result<std::unique_ptr<Stream>> create_stream(
      const StreamConfiguration& configuration = {}) = 0;
};

}  // namespace univision
