#pragma once

#include "univision/adapter.h"
#include "univision/export.h"

#include <cstdint>
#include <memory>
#include <string>

namespace univision {

struct SimulatorConfiguration {
  std::string serial{"SIM-0001"};
  std::string model{"UniVision Virtual Camera"};
  std::uint32_t width{640};
  std::uint32_t height{480};
  double frame_rate{30.0};
};

[[nodiscard]] UNIVISION_API AdapterPtr make_simulator_adapter(
    const SimulatorConfiguration& configuration = {});

}  // namespace univision
