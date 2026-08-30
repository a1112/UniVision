#include "univision/univision.h"

#include <chrono>
#include <iostream>

int main() {
  univision::System system;
  if (auto status = system.register_adapter(univision::make_simulator_adapter());
      !status) {
    std::cerr << status.message() << '\n';
    return 1;
  }

  auto devices = system.enumerate_devices();
  if (!devices) {
    std::cerr << devices.status().message() << '\n';
    return 1;
  }

  for (const auto& device : devices.value()) {
    std::cout << device.display_name << "\n  stable id: " << device.stable_id
              << "\n  adapter: " << device.adapter_id << '\n';
  }
  if (devices.value().empty()) {
    return 0;
  }

  univision::DeviceSelector selector;
  selector.stable_id = devices.value().front().stable_id;
  auto created = system.create_camera(selector);
  if (!created) {
    std::cerr << created.status().message() << '\n';
    return 1;
  }

  auto camera = created.value();
  if (auto status = camera->open(); !status) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  auto stream_result = camera->create_stream();
  if (!stream_result) {
    std::cerr << stream_result.status().message() << '\n';
    return 1;
  }
  auto stream = std::move(stream_result).value();
  if (auto status = stream->start(); !status) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  auto frame = stream->wait_next(std::chrono::seconds(1));
  if (!frame) {
    std::cerr << frame.status().message() << '\n';
    return 1;
  }

  std::cout << "captured frame " << frame.value().descriptor.frame_id << ": "
            << frame.value().descriptor.width << 'x'
            << frame.value().descriptor.height << ", "
            << frame.value().buffer.size() << " bytes\n";
  static_cast<void>(stream->stop());
  static_cast<void>(camera->close());
  return 0;
}
