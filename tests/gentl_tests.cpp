#include "univision/univision.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
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

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: univision_gentl_tests <producer.cti>\n";
    return 2;
  }
  univision::GenTLAdapterOptions missing;
  missing.cti_path = "this-producer-does-not-exist.cti";
  auto missing_adapter = univision::make_gentl_adapter(missing);
  CHECK(!missing_adapter);
  CHECK(missing_adapter.status().code() == univision::ErrorCode::io_error);

  univision::GenTLAdapterOptions options;
  options.cti_path = argv[1];
  options.discovery_timeout = std::chrono::milliseconds(50);
  auto adapter_result = univision::make_gentl_adapter(options);
  CHECK(adapter_result);
  if (!adapter_result) {
    return 1;
  }
  auto adapter = std::move(adapter_result).value();
  CHECK(adapter->descriptor().name == "UniVision Fake GenTL Producer");
  CHECK(adapter->descriptor().version == "1.0.0");

  univision::System system;
  CHECK(system.register_adapter(adapter));
  auto enumerated = system.enumerate_devices();
  CHECK(enumerated);
  if (!enumerated) {
    std::cerr << enumerated.status().message() << '\n';
    return 1;
  }
  CHECK(enumerated.value().size() == 1);
  if (enumerated.value().empty()) {
    return 1;
  }
  const auto& device = enumerated.value().front();
  CHECK(device.vendor == "UniVision");
  CHECK(device.model == "Fake GigE Camera");
  CHECK(device.serial == "FAKE-0001");
  CHECK(device.stable_id == "camera:UniVision:Fake GigE Camera:FAKE-0001");
  CHECK(device.transport == univision::TransportType::gige_vision);

  univision::DeviceSelector selector;
  selector.stable_id = device.stable_id;
  auto camera_result = system.create_camera(selector);
  CHECK(camera_result);
  auto camera = camera_result.value();
  CHECK(camera->open());
  CHECK(camera->state() == univision::CameraState::open);
  const auto features = camera->features();
  CHECK(features.size() == 13);
  auto exposure_info = camera->feature_info("ExposureTime");
  CHECK(exposure_info);
  CHECK(exposure_info.value().kind == univision::FeatureKind::floating_point);
  CHECK(exposure_info.value().access == univision::AccessMode::read_write);
  CHECK(exposure_info.value().unit == "us");
  CHECK(exposure_info.value().minimum == 10.0);
  CHECK(exposure_info.value().maximum == 100000.0);
  CHECK(exposure_info.value().increment == 0.5);
  CHECK(exposure_info.value().standard_feature);

  auto exposure = camera->read_feature("ExposureTime");
  CHECK(exposure);
  CHECK(std::get<double>(exposure.value()) == 1000.0);
  CHECK(camera->write_feature("ExposureTime", 2500.5));
  CHECK(std::get<double>(camera->read_feature("ExposureTime").value()) == 2500.5);
  CHECK(!camera->write_feature("ExposureTime", 2500.25));

  CHECK(std::get<std::int64_t>(camera->read_feature("Width").value()) == 64);
  CHECK(camera->write_feature("Width", std::int64_t{32}));
  CHECK(!camera->write_feature("Width", std::int64_t{17}));
  CHECK(camera->write_feature("Width", std::int64_t{64}));

  auto trigger_info = camera->feature_info("TriggerMode");
  CHECK(trigger_info);
  CHECK(trigger_info.value().enum_entries == std::vector<std::string>({"Off", "On"}));
  CHECK(std::get<std::string>(camera->read_feature("TriggerMode").value()) == "Off");
  CHECK(camera->write_feature("TriggerMode", std::string{"On"}));
  CHECK(!camera->write_feature("TriggerMode", std::string{"Invalid"}));

  CHECK(!std::get<bool>(camera->read_feature("ReverseX").value()));
  CHECK(camera->write_feature("ReverseX", true));
  CHECK(std::get<bool>(camera->read_feature("ReverseX").value()));
  CHECK(std::get<std::string>(camera->read_feature("DeviceUserID").value()) ==
        "CI Camera");
  CHECK(camera->write_feature("DeviceUserID", std::string{"Line A"}));
  CHECK(std::get<std::string>(camera->read_feature("DeviceUserID").value()) ==
        "Line A");
  CHECK(std::get<double>(camera->read_feature("DeviceTemperature").value()) == 36.5);
  CHECK(!camera->write_feature("DeviceTemperature", 20.0));
  CHECK(camera->execute_command("TriggerSoftware"));
  CHECK(!camera->read_feature("AcquisitionStart"));

  auto vendor = camera->feature_info("VendorMagic");
  CHECK(vendor);
  CHECK(!vendor.value().standard_feature);
  CHECK(std::get<std::int64_t>(camera->read_feature("VendorMagic").value()) == 42);
  CHECK(!camera->feature_info("MissingFeature"));

  univision::StreamConfiguration stream_configuration;
  stream_configuration.buffer_count = 3;
  stream_configuration.queue_capacity = 3;
  auto stream_result = camera->create_stream(stream_configuration);
  CHECK(stream_result);
  auto stream = std::move(stream_result).value();
  CHECK(stream->start());
  CHECK(camera->state() == univision::CameraState::streaming);

  std::optional<univision::Frame> held_frame;
  {
    auto first = stream->wait_next(std::chrono::milliseconds(100));
    CHECK(first);
    CHECK(first.value().descriptor.width == 64);
    CHECK(first.value().descriptor.height == 32);
    CHECK(first.value().descriptor.stride == 64);
    CHECK(first.value().descriptor.pixel_format == 0x01080001ULL);
    CHECK(first.value().descriptor.frame_id == 1);
    CHECK(first.value().descriptor.complete);
    CHECK(first.value().buffer.size() == 64U * 32U);
    CHECK(first.value().buffer.data() != nullptr);
    CHECK(std::to_integer<unsigned char>(*first.value().buffer.data()) == 1U);
    held_frame.emplace(std::move(first).value());

    auto second = stream->wait_next(std::chrono::milliseconds(100));
    CHECK(second);
    CHECK(second.value().descriptor.frame_id == 2);
    CHECK(stream->statistics().frames_delivered == 2);
  }

  CHECK(stream->stop());
  CHECK(camera->state() == univision::CameraState::open);
  CHECK(!camera->close());
  CHECK(stream->start());
  {
    auto third = stream->wait_next(std::chrono::milliseconds(100));
    CHECK(third);
    CHECK(third.value().descriptor.frame_id == 3);
  }
  held_frame.reset();
  {
    auto fourth = stream->wait_next(std::chrono::milliseconds(100));
    CHECK(fourth);
    CHECK(fourth.value().descriptor.frame_id == 4);
  }
  CHECK(stream->stop());
  stream.reset();
  CHECK(camera->close());
  CHECK(camera->state() == univision::CameraState::closed);

  if (failures != 0) {
    std::cerr << failures << " GenTL test check(s) failed\n";
    return 1;
  }
  std::cout << "all UniVision GenTL checks passed\n";
  return 0;
}
