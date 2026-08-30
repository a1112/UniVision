#include "univision/univision.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

}  // namespace

int main() {
  univision::System system;
  CHECK(system.adapters().empty());
  CHECK(!system.diagnostics().empty());

  univision::SimulatorConfiguration simulator;
  simulator.width = 64;
  simulator.height = 32;
  simulator.frame_rate = 100.0;
  CHECK(system.register_adapter(univision::make_simulator_adapter(simulator)));
  CHECK(system.adapters().size() == 1);
  CHECK(!system.register_adapter(univision::make_simulator_adapter(simulator)));

  auto devices = system.enumerate_devices();
  CHECK(devices);
  CHECK(devices.value().size() == 1);
  CHECK(devices.value().front().serial == "SIM-0001");
  CHECK(devices.value().front().transport == univision::TransportType::virtual_device);

  univision::DeviceSelector selector;
  selector.stable_id = devices.value().front().stable_id;
  auto created = system.create_camera(selector);
  CHECK(created);
  auto camera = created.value();
  CHECK(camera->state() == univision::CameraState::closed);
  CHECK(camera->open());
  CHECK(camera->state() == univision::CameraState::open);

  auto exposure = camera->read_feature("ExposureTime");
  CHECK(exposure);
  CHECK(std::get<double>(exposure.value()) == 10000.0);
  CHECK(camera->write_feature("ExposureTime", 2500.0));
  CHECK(!camera->write_feature("ExposureTime", -1.0));
  CHECK(!camera->write_feature("DeviceTemperature", 20.0));
  CHECK(camera->execute_command("TriggerSoftware"));

  auto stream_result = camera->create_stream();
  CHECK(stream_result);
  auto stream = std::move(stream_result).value();
  CHECK(stream->start());
  CHECK(camera->state() == univision::CameraState::streaming);

  auto frame = stream->wait_next(std::chrono::milliseconds(100));
  CHECK(frame);
  CHECK(frame.value().descriptor.width == 64);
  CHECK(frame.value().descriptor.height == 32);
  CHECK(frame.value().descriptor.frame_id == 1);
  CHECK(frame.value().buffer.size() == 64U * 32U);
  CHECK(frame.value().buffer.data() != nullptr);
  CHECK(stream->statistics().frames_delivered == 1);

  CHECK(stream->stop());
  CHECK(camera->state() == univision::CameraState::open);
  CHECK(camera->close());
  CHECK(camera->state() == univision::CameraState::closed);

  CHECK(system.unregister_adapter("simulator"));
  CHECK(system.adapters().empty());

  if (failures != 0) {
    std::cerr << failures << " test check(s) failed\n";
    return 1;
  }
  std::cout << "all UniVision core checks passed\n";
  return 0;
}
