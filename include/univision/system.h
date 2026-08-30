#pragma once

#include "univision/adapter.h"
#include "univision/export.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace univision {

struct DiagnosticEntry {
  enum class Level { info, warning, error };
  Level level{Level::info};
  std::string component;
  std::string message;
};

class UNIVISION_API System {
 public:
  System() = default;
  ~System() = default;
  System(const System&) = delete;
  System& operator=(const System&) = delete;

  Status register_adapter(AdapterPtr adapter);
  Status unregister_adapter(const std::string& adapter_id);
  [[nodiscard]] std::vector<AdapterDescriptor> adapters() const;
  [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices(
      const DeviceSelector& selector = {}) const;
  [[nodiscard]] Result<std::shared_ptr<Camera>> create_camera(
      const DeviceSelector& selector) const;
  [[nodiscard]] std::vector<DiagnosticEntry> diagnostics() const;

 private:
  [[nodiscard]] std::vector<AdapterPtr> adapter_snapshot() const;

  mutable std::mutex mutex_;
  std::vector<AdapterPtr> adapters_;
};

}  // namespace univision
