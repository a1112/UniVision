#include "univision/univision.h"

#include <algorithm>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
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

std::shared_ptr<univision::Camera> make_camera(
    const univision::SimulatorConfiguration& configuration = {}) {
  auto adapter = univision::make_simulator_adapter(configuration);
  auto devices = adapter->enumerate_devices();
  if (!devices) throw std::runtime_error(devices.status().message());
  auto result = adapter->create_camera(devices.value().front());
  if (!result) throw std::runtime_error(result.status().message());
  auto camera = result.value();
  if (const auto status = camera->open(); !status) {
    throw std::runtime_error(status.message());
  }
  return camera;
}

std::int64_t read_integer(const univision::Camera& camera, const std::string& name) {
  auto result = camera.read_feature(name);
  if (!result) throw std::runtime_error(result.status().message());
  return std::get<std::int64_t>(result.value());
}

void check_configuration() {
  univision::SimulatorConfiguration configuration;
  const auto rejected = [](const univision::SimulatorConfiguration& invalid) {
    auto adapter = univision::make_simulator_adapter(invalid);
    auto devices = adapter->enumerate_devices();
    auto camera = adapter->create_camera(devices.value().front());
    CHECK(!camera);
    CHECK(camera.status().code() == univision::ErrorCode::invalid_argument);
  };
  for (const auto width : {0U, 16385U, std::numeric_limits<std::uint32_t>::max()}) {
    configuration.width = width;
    rejected(configuration);
  }
  configuration = {};
  for (const auto height : {0U, 16385U, std::numeric_limits<std::uint32_t>::max()}) {
    configuration.height = height;
    rejected(configuration);
  }
  configuration = {};
  for (const auto rate : {0.0, -1.0, 0.099, 1000.1,
                          std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity()}) {
    configuration.frame_rate = rate;
    rejected(configuration);
  }
  configuration = {};
  configuration.width = 1;
  configuration.height = 1;
  configuration.frame_rate = 0.1;
  CHECK(make_camera(configuration));
  configuration.width = 16384;
  configuration.height = 16384;
  configuration.frame_rate = 1000.0;
  CHECK(make_camera(configuration));
}

void check_small_roi_compatibility() {
  // The existing industrial CLI and recording fixtures use 8x8 Mono8 frames.
  // Simulator has no producer alignment requirement: every positive dimension
  // must remain usable, with the same minimum reported by the ROI feature.
  univision::SimulatorConfiguration configuration;
  configuration.width = 8;
  configuration.height = 8;
  configuration.frame_rate = 1000.0;
  auto camera = make_camera(configuration);
  CHECK(camera->feature_info("Width").value().minimum == 1.0);
  CHECK(camera->feature_info("Height").value().minimum == 1.0);
  CHECK(camera->feature_info("OffsetX").value().maximum == 16376.0);
  auto stream = std::move(camera->create_stream()).value();
  CHECK(stream->start());
  auto fixture = stream->wait_next(std::chrono::seconds(1));
  CHECK(fixture.value().descriptor.width == 8);
  CHECK(fixture.value().descriptor.height == 8);
  CHECK(fixture.value().descriptor.stride == 8);
  CHECK(fixture.value().buffer.size() == 64);
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 8; ++x) {
      CHECK(std::to_integer<unsigned>(fixture.value().buffer.data()[y * 8 + x]) ==
            x + y + 1);
    }
  }
  CHECK(stream->stop());
  for (const auto width : {1LL, 2LL, 8LL, 15LL}) {
    CHECK(camera->write_feature("Width", static_cast<std::int64_t>(width)));
    CHECK(read_integer(*camera, "Width") == width);
  }
  CHECK(camera->write_feature("Width", std::int64_t{1}));
  CHECK(camera->write_feature("Height", std::int64_t{1}));
  CHECK(camera->feature_info("OffsetX").value().maximum == 16383.0);
  CHECK(camera->write_feature("OffsetX", std::int64_t{16383}));
  CHECK(camera->write_feature("OffsetY", std::int64_t{16383}));
  CHECK(camera->feature_info("Width").value().maximum == 1.0);
  CHECK(camera->write_feature("Width", std::int64_t{2}).code() ==
        univision::ErrorCode::invalid_argument);
  CHECK(camera->write_feature("Width", std::int64_t{0}).code() ==
        univision::ErrorCode::invalid_argument);
  CHECK(camera->write_feature("OffsetX", std::int64_t{16384}).code() ==
        univision::ErrorCode::invalid_argument);
  CHECK(read_integer(*camera, "Width") == 1);
  CHECK(read_integer(*camera, "OffsetX") == 16383);
  CHECK(stream->start());
  auto corner = stream->wait_next(std::chrono::seconds(1));
  CHECK(corner.value().descriptor.width == 1);
  CHECK(corner.value().descriptor.height == 1);
  CHECK(corner.value().buffer.size() == 1);
  CHECK(std::to_integer<unsigned>(corner.value().buffer.data()[0]) == 0);
  CHECK(stream->stop());
}

void check_roi_bounds_and_pixels() {
  univision::SimulatorConfiguration configuration;
  configuration.width = 64;
  configuration.height = 32;
  configuration.frame_rate = 1000.0;
  auto camera = make_camera(configuration);
  CHECK(read_integer(*camera, "SensorWidth") == 16384);
  CHECK(read_integer(*camera, "SensorHeight") == 16384);
  CHECK(read_integer(*camera, "OffsetX") == 0);
  CHECK(read_integer(*camera, "OffsetY") == 0);
  for (const auto* name : {"OffsetX", "OffsetY", "SensorWidth", "SensorHeight"}) {
    auto info = camera->feature_info(name);
    CHECK(info);
    CHECK(info.value().standard_feature);
    CHECK(info.value().kind == univision::FeatureKind::integer);
    CHECK(info.value().unit == "px");
  }
  CHECK(camera->feature_info("OffsetX").value().maximum == 16320.0);
  CHECK(camera->feature_info("OffsetY").value().maximum == 16352.0);
  CHECK(camera->write_feature("SensorWidth", std::int64_t{1}).code() ==
        univision::ErrorCode::access_denied);
  CHECK(camera->write_feature("SensorHeight", std::int64_t{1}).code() ==
        univision::ErrorCode::access_denied);
  for (const auto* name : {"Width", "Height", "OffsetX", "OffsetY"}) {
    const auto before = read_integer(*camera, name);
    CHECK(camera->write_feature(name, std::int64_t{-1}).code() ==
          univision::ErrorCode::invalid_argument);
    CHECK(camera->write_feature(name, std::numeric_limits<std::int64_t>::max()).code() ==
          univision::ErrorCode::invalid_argument);
    CHECK(camera->write_feature(name, 1.0).code() ==
          univision::ErrorCode::invalid_argument);
    CHECK(read_integer(*camera, name) == before);
  }
  CHECK(camera->write_feature("OffsetX", std::int64_t{16320}));
  CHECK(camera->write_feature("OffsetY", std::int64_t{16352}));
  CHECK(camera->feature_info("Width").value().maximum == 64.0);
  CHECK(camera->feature_info("Height").value().maximum == 32.0);
  for (const auto& info : camera->features()) {
    if (info.name == "Width") CHECK(info.maximum == 64.0);
    if (info.name == "Height") CHECK(info.maximum == 32.0);
  }
  CHECK(!camera->write_feature("OffsetX", std::int64_t{16321}));
  CHECK(!camera->write_feature("OffsetY", std::int64_t{16353}));
  CHECK(!camera->write_feature("Width", std::int64_t{65}));
  CHECK(!camera->write_feature("Height", std::int64_t{33}));
  CHECK(read_integer(*camera, "Width") == 64);
  CHECK(read_integer(*camera, "Height") == 32);
  CHECK(read_integer(*camera, "OffsetX") == 16320);
  CHECK(read_integer(*camera, "OffsetY") == 16352);

  auto stream = std::move(camera->create_stream()).value();
  CHECK(stream->start());
  auto frame = stream->wait_next(std::chrono::seconds(1));
  CHECK(frame);
  CHECK(frame.value().descriptor.width == 64);
  CHECK(frame.value().descriptor.height == 32);
  CHECK(frame.value().descriptor.stride == 64);
  CHECK(frame.value().buffer.size() == 64U * 32U);
  CHECK(std::get<std::int64_t>(frame.value().metadata.at("OffsetX")) == 16320);
  CHECK(std::get<std::int64_t>(frame.value().metadata.at("OffsetY")) == 16352);
  for (std::uint32_t y = 0; y < 32; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      CHECK(std::to_integer<unsigned>(frame.value().buffer.data()[y * 64 + x]) ==
            ((16320U + x + 16352U + y + 1U) & 0xffU));
    }
  }
  for (const auto* name : {"Width", "Height", "OffsetX", "OffsetY"}) {
    CHECK(camera->feature_info(name).value().access == univision::AccessMode::read_only);
    CHECK(camera->write_feature(name, read_integer(*camera, name)).code() ==
          univision::ErrorCode::invalid_state);
  }
  CHECK(camera->write_feature("ExposureTime", 2500.0));
  CHECK(!camera->close());
  CHECK(stream->stop());
  CHECK(camera->feature_info("Width").value().access == univision::AccessMode::read_write);
  // Retained frames keep their pixel ownership after stopping and editing ROI.
  const auto retained = std::to_integer<unsigned>(frame.value().buffer.data()[0]);
  CHECK(camera->write_feature("OffsetX", std::int64_t{0}));
  CHECK(camera->write_feature("OffsetY", std::int64_t{0}));
  CHECK(std::to_integer<unsigned>(frame.value().buffer.data()[0]) == retained);
  CHECK(camera->close());
}

void check_start_snapshot_and_restart() {
  univision::SimulatorConfiguration configuration;
  configuration.width = 64;
  configuration.height = 32;
  configuration.frame_rate = 1000.0;
  auto camera = make_camera(configuration);
  auto stream = std::move(camera->create_stream()).value();
  CHECK(camera->write_feature("Width", std::int64_t{96}));
  CHECK(camera->write_feature("Height", std::int64_t{48}));
  CHECK(camera->write_feature("OffsetX", std::int64_t{3}));
  CHECK(camera->write_feature("OffsetY", std::int64_t{5}));
  CHECK(stream->start());
  CHECK(!stream->start());
  CHECK(stream->wait_next(std::chrono::milliseconds(-1)).status().code() ==
        univision::ErrorCode::invalid_argument);
  auto first = stream->wait_next(std::chrono::seconds(1));
  CHECK(first.value().descriptor.width == 96);
  CHECK(first.value().descriptor.height == 48);
  CHECK(std::to_integer<unsigned>(first.value().buffer.data()[0]) == 9);
  CHECK(stream->stop());
  CHECK(camera->write_feature("Width", std::int64_t{16}));
  CHECK(camera->write_feature("Height", std::int64_t{1}));
  CHECK(camera->write_feature("OffsetX", std::int64_t{7}));
  CHECK(camera->write_feature("OffsetY", std::int64_t{11}));
  CHECK(stream->start());
  auto second = stream->wait_next(std::chrono::seconds(1));
  CHECK(second.value().descriptor.width == 16);
  CHECK(second.value().descriptor.height == 1);
  CHECK(second.value().descriptor.frame_id == 2);
  CHECK(std::to_integer<unsigned>(second.value().buffer.data()[0]) == 20);
  CHECK(stream->statistics().frames_delivered == 2);
  CHECK(stream->stop());
  CHECK(stream->stop());
  CHECK(stream->wait_next(std::chrono::seconds(1)).status().code() ==
        univision::ErrorCode::invalid_state);
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
    CHECK(camera->write_feature("AcquisitionFrameRate", value).code() ==
          univision::ErrorCode::invalid_argument);
  }
}

void check_concurrent_start_and_write() {
  univision::SimulatorConfiguration configuration;
  configuration.width = 64;
  configuration.height = 1;
  configuration.frame_rate = 1000.0;
  auto camera = make_camera(configuration);
  auto stream = std::move(camera->create_stream()).value();
  for (int iteration = 0; iteration < 32; ++iteration) {
    CHECK(camera->write_feature("Width", std::int64_t{64}));
    std::barrier gate(2);
    auto started = std::async(std::launch::async, [&] {
      gate.arrive_and_wait();
      return stream->start();
    });
    gate.arrive_and_wait();
    const auto written = camera->write_feature("Width", std::int64_t{80});
    CHECK(started.get());
    CHECK(written || written.code() == univision::ErrorCode::invalid_state);
    const auto expected = written ? 80U : 64U;
    auto frame = stream->wait_next(std::chrono::seconds(1));
    CHECK(frame.value().descriptor.width == expected);
    CHECK(read_integer(*camera, "Width") == expected);
    CHECK(stream->stop());
  }
  auto other = std::move(camera->create_stream()).value();
  std::barrier gate(2);
  auto started = std::async(std::launch::async, [&] {
    gate.arrive_and_wait();
    return stream->start();
  });
  gate.arrive_and_wait();
  const auto other_status = other->start();
  const auto first_status = started.get();
  CHECK(static_cast<bool>(first_status) != static_cast<bool>(other_status));
  CHECK((first_status ? other_status : first_status).code() ==
        univision::ErrorCode::invalid_state);
  auto* winner = first_status ? stream.get() : other.get();
  auto* loser = first_status ? other.get() : stream.get();
  CHECK(loser->stop());
  CHECK(camera->state() == univision::CameraState::streaming);
  CHECK(winner->running());
  CHECK(winner->stop());
  CHECK(camera->state() == univision::CameraState::open);

  CHECK(camera->write_feature("AcquisitionFrameRate", 0.1));
  CHECK(stream->start());
  std::promise<void> waiting;
  auto ready = waiting.get_future();
  auto pending = std::async(std::launch::async, [&] {
    waiting.set_value();
    return stream->wait_next(std::chrono::seconds(20));
  });
  ready.wait();
  CHECK(stream->stop());
  CHECK(pending.get().status().code() == univision::ErrorCode::invalid_state);
  CHECK(camera->state() == univision::CameraState::open);
}

}  // namespace

int main() {
  try {
    check_configuration();
    check_small_roi_compatibility();
    check_roi_bounds_and_pixels();
    check_start_snapshot_and_restart();
    check_concurrent_start_and_write();
  } catch (const std::exception& error) {
    std::cerr << "simulator ROI test failed: " << error.what() << '\n';
    return 1;
  }
  if (failures != 0) return 1;
  std::cout << "all UniVision simulator ROI checks passed\n";
  return 0;
}
