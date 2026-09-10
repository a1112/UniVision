#pragma once

#include "univision/adapter.h"
#include "univision/export.h"

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace univision {

struct GenTLAdapterOptions {
  std::filesystem::path cti_path;
  std::chrono::milliseconds discovery_timeout{500};
  CertificationTier certification{CertificationTier::compatible};
  std::int32_t priority{50};
};

// Loads exactly one GenTL Producer from an explicit path. The process-wide
// GENICAM_GENTL*_PATH variables are deliberately not consulted.
[[nodiscard]] UNIVISION_API Result<AdapterPtr> make_gentl_adapter(
    const GenTLAdapterOptions& options);

}  // namespace univision
