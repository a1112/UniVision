#include "univision/system.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace univision {
namespace {

std::string identity_key(const DeviceInfo& device) {
  if (!device.stable_id.empty()) {
    return device.stable_id;
  }

  std::ostringstream key;
  key << device.vendor << '|' << device.model << '|' << device.serial << '|'
      << static_cast<unsigned>(device.transport) << '|' << device.address;
  return key.str();
}

}  // namespace

bool DeviceSelector::matches(const DeviceInfo& info) const {
  const auto same = [](const std::optional<std::string>& expected,
                       const std::string& actual) {
    return !expected || *expected == actual;
  };
  return same(id, info.id) && same(stable_id, info.stable_id) &&
         same(adapter_id, info.adapter_id) && same(vendor, info.vendor) &&
         same(model, info.model) && same(serial, info.serial);
}

Status System::register_adapter(AdapterPtr adapter) {
  if (!adapter) {
    return {ErrorCode::invalid_argument, "adapter must not be null"};
  }
  if (adapter->descriptor().id.empty()) {
    return {ErrorCode::invalid_argument, "adapter id must not be empty"};
  }

  std::lock_guard lock(mutex_);
  const auto duplicate = std::find_if(
      adapters_.begin(), adapters_.end(), [&](const AdapterPtr& candidate) {
        return candidate->descriptor().id == adapter->descriptor().id;
      });
  if (duplicate != adapters_.end()) {
    return {ErrorCode::already_exists,
            "adapter already registered: " + adapter->descriptor().id};
  }
  adapters_.push_back(std::move(adapter));
  return Status::success();
}

Status System::unregister_adapter(const std::string& adapter_id) {
  std::lock_guard lock(mutex_);
  const auto found = std::find_if(
      adapters_.begin(), adapters_.end(), [&](const AdapterPtr& adapter) {
        return adapter->descriptor().id == adapter_id;
      });
  if (found == adapters_.end()) {
    return {ErrorCode::not_found, "adapter not registered: " + adapter_id};
  }
  adapters_.erase(found);
  return Status::success();
}

std::vector<AdapterPtr> System::adapter_snapshot() const {
  std::lock_guard lock(mutex_);
  return adapters_;
}

std::vector<AdapterDescriptor> System::adapters() const {
  auto snapshot = adapter_snapshot();
  std::vector<AdapterDescriptor> result;
  result.reserve(snapshot.size());
  for (const auto& adapter : snapshot) {
    result.push_back(adapter->descriptor());
  }
  return result;
}

Result<std::vector<DeviceInfo>> System::enumerate_devices(
    const DeviceSelector& selector) const {
  auto snapshot = adapter_snapshot();
  std::unordered_map<std::string, std::pair<DeviceInfo, std::int32_t>> unique;

  for (const auto& adapter : snapshot) {
    auto enumerated = adapter->enumerate_devices();
    if (!enumerated) {
      return Status{ErrorCode::adapter_failure,
                    "device enumeration failed in adapter " +
                        adapter->descriptor().id + ": " +
                        enumerated.status().message()};
    }

    for (auto& device : enumerated.value()) {
      device.adapter_id = adapter->descriptor().id;
      if (!selector.matches(device)) {
        continue;
      }

      const auto key = identity_key(device);
      const auto priority = adapter->descriptor().priority;
      const auto existing = unique.find(key);
      if (existing == unique.end() || priority > existing->second.second) {
        unique.insert_or_assign(key, std::make_pair(std::move(device), priority));
      }
    }
  }

  std::vector<DeviceInfo> devices;
  devices.reserve(unique.size());
  for (auto& [key, entry] : unique) {
    static_cast<void>(key);
    devices.push_back(std::move(entry.first));
  }
  std::sort(devices.begin(), devices.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.stable_id < rhs.stable_id;
  });
  return devices;
}

Result<std::shared_ptr<Camera>> System::create_camera(
    const DeviceSelector& selector) const {
  auto devices = enumerate_devices(selector);
  if (!devices) {
    return devices.status();
  }
  if (devices.value().empty()) {
    return Status{ErrorCode::not_found, "no camera matched the selector"};
  }
  if (devices.value().size() > 1) {
    return Status{ErrorCode::invalid_argument,
                  "camera selector matched more than one device"};
  }

  const auto& device = devices.value().front();
  for (const auto& adapter : adapter_snapshot()) {
    if (adapter->descriptor().id == device.adapter_id) {
      return adapter->create_camera(device);
    }
  }
  return Status{ErrorCode::not_found,
                "selected adapter was unregistered during camera creation"};
}

std::vector<DiagnosticEntry> System::diagnostics() const {
  std::vector<DiagnosticEntry> entries;
  const auto snapshot = adapter_snapshot();
  if (snapshot.empty()) {
    entries.push_back({DiagnosticEntry::Level::warning, "system",
                       "no camera adapters are registered"});
    return entries;
  }

  for (const auto& adapter : snapshot) {
    entries.push_back({DiagnosticEntry::Level::info,
                       adapter->descriptor().id,
                       adapter->descriptor().name + " " +
                           adapter->descriptor().version});
  }
  return entries;
}

}  // namespace univision
