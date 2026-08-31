#include "univision/simulator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace univision {
namespace {

constexpr PixelFormat pfnc_mono8 = 0x01080001ULL;

struct FeatureRecord {
  FeatureInfo info;
  FeatureValue value;
};

class SimulatorStream final : public Stream {
 public:
  SimulatorStream(StreamConfiguration configuration,
                  std::shared_ptr<std::atomic<CameraState>> camera_state,
                  std::uint32_t width,
                  std::uint32_t height,
                  double frame_rate)
      : configuration_(configuration),
        camera_state_(std::move(camera_state)),
        width_(width),
        height_(height),
        frame_rate_(frame_rate) {}

  ~SimulatorStream() override { static_cast<void>(stop()); }

  Status start() override {
    if (running_.exchange(true)) {
      return {ErrorCode::invalid_state, "stream is already running"};
    }
    if (camera_state_->load() != CameraState::open) {
      running_.store(false);
      return {ErrorCode::invalid_state, "camera must be open before streaming"};
    }
    camera_state_->store(CameraState::streaming);
    last_frame_ = std::chrono::steady_clock::now();
    return Status::success();
  }

  Status stop() override {
    if (!running_.exchange(false)) {
      return Status::success();
    }
    if (camera_state_->load() == CameraState::streaming) {
      camera_state_->store(CameraState::open);
    }
    return Status::success();
  }

  [[nodiscard]] bool running() const noexcept override { return running_.load(); }

  [[nodiscard]] Result<Frame> wait_next(std::chrono::milliseconds timeout) override {
    if (!running()) {
      return Status{ErrorCode::invalid_state, "stream is not running"};
    }

    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / frame_rate_));
    const auto due = last_frame_ + period;
    const auto now = std::chrono::steady_clock::now();
    if (due > now) {
      const auto wait = due - now;
      if (wait > timeout) {
        return Status{ErrorCode::timeout, "frame wait timed out"};
      }
      std::this_thread::sleep_for(wait);
    }

    const auto captured = std::chrono::steady_clock::now();
    last_frame_ = captured;
    auto pixels = std::make_shared<std::vector<std::byte>>(
        static_cast<std::size_t>(width_) * height_);
    const auto next_id = ++frame_id_;
    for (std::uint32_t y = 0; y < height_; ++y) {
      for (std::uint32_t x = 0; x < width_; ++x) {
        const auto value = static_cast<unsigned char>((x + y + next_id) & 0xffU);
        (*pixels)[static_cast<std::size_t>(y) * width_ + x] =
            static_cast<std::byte>(value);
      }
    }

    Frame frame;
    frame.descriptor.width = width_;
    frame.descriptor.height = height_;
    frame.descriptor.stride = width_;
    frame.descriptor.pixel_format = pfnc_mono8;
    frame.descriptor.memory_type = MemoryType::host;
    frame.descriptor.frame_id = next_id;
    frame.descriptor.host_timestamp = captured;
    frame.descriptor.camera_timestamp_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       captured.time_since_epoch())
                                       .count());
    frame.buffer = FrameBuffer{pixels, pixels->data(), pixels->size()};
    frame.metadata.emplace("ChunkFrameID", static_cast<std::int64_t>(next_id));
    frame.metadata.emplace("ChunkTimestamp",
                           static_cast<std::int64_t>(frame.descriptor.camera_timestamp_ns));

    ++statistics_.frames_received;
    ++statistics_.frames_delivered;
    return frame;
  }

  [[nodiscard]] StreamStatistics statistics() const noexcept override {
    return statistics_;
  }

 private:
  StreamConfiguration configuration_;
  std::shared_ptr<std::atomic<CameraState>> camera_state_;
  std::uint32_t width_;
  std::uint32_t height_;
  double frame_rate_;
  std::atomic<bool> running_{false};
  std::chrono::steady_clock::time_point last_frame_{};
  std::uint64_t frame_id_{};
  StreamStatistics statistics_{};
};

class SimulatorCamera final : public Camera {
 public:
  explicit SimulatorCamera(DeviceInfo info, const SimulatorConfiguration& configuration)
      : info_(std::move(info)), state_(std::make_shared<std::atomic<CameraState>>()) {
    state_->store(CameraState::closed);
    add_feature({"Width", "Width", "Image width", "px", FeatureKind::integer,
                 AccessMode::read_write, 16.0, 16384.0, 1.0, {}, true},
                static_cast<std::int64_t>(configuration.width));
    add_feature({"Height", "Height", "Image height", "px", FeatureKind::integer,
                 AccessMode::read_write, 1.0, 16384.0, 1.0, {}, true},
                static_cast<std::int64_t>(configuration.height));
    add_feature({"AcquisitionFrameRate", "Frame Rate", "Generated frames per second",
                 "Hz", FeatureKind::floating_point, AccessMode::read_write, 0.1,
                 1000.0, std::nullopt, {}, true},
                configuration.frame_rate);
    add_feature({"ExposureTime", "Exposure Time", "Simulated exposure time", "us",
                 FeatureKind::floating_point, AccessMode::read_write, 1.0, 1000000.0,
                 1.0, {}, true},
                10000.0);
    add_feature({"Gain", "Gain", "Simulated analog gain", "dB",
                 FeatureKind::floating_point, AccessMode::read_write, 0.0, 24.0, 0.1,
                 {}, true},
                0.0);
    add_feature({"PixelFormat", "Pixel Format", "PFNC pixel format", "",
                 FeatureKind::enumeration, AccessMode::read_write, std::nullopt,
                 std::nullopt, std::nullopt, {"Mono8"}, true},
                std::string{"Mono8"});
    add_feature({"TriggerMode", "Trigger Mode", "Acquisition trigger mode", "",
                 FeatureKind::enumeration, AccessMode::read_write, std::nullopt,
                 std::nullopt, std::nullopt, {"Off", "On"}, true},
                std::string{"Off"});
    add_feature({"TriggerSoftware", "Software Trigger", "Generate one trigger", "",
                 FeatureKind::command, AccessMode::write_only, std::nullopt,
                 std::nullopt, std::nullopt, {}, true},
                false);
    add_feature({"DeviceTemperature", "Device Temperature", "Simulated sensor value",
                 "C", FeatureKind::floating_point, AccessMode::read_only, -40.0, 125.0,
                 0.1, {}, false},
                35.0);
  }

  [[nodiscard]] const DeviceInfo& device_info() const noexcept override { return info_; }
  [[nodiscard]] CameraState state() const noexcept override { return state_->load(); }

  Status open() override {
    CameraState expected = CameraState::closed;
    if (!state_->compare_exchange_strong(expected, CameraState::opening)) {
      return {ErrorCode::invalid_state, "camera is not closed"};
    }
    state_->store(CameraState::open);
    return Status::success();
  }

  Status close() override {
    const auto current = state_->load();
    if (current == CameraState::streaming) {
      return {ErrorCode::invalid_state, "stop the stream before closing the camera"};
    }
    state_->store(CameraState::closed);
    return Status::success();
  }

  Status reconnect() override {
    const auto current = state_->load();
    if (current != CameraState::lost && current != CameraState::failed &&
        current != CameraState::open) {
      return {ErrorCode::invalid_state,
              "camera can only reconnect from open, lost, or failed state"};
    }
    state_->store(CameraState::recovering);
    state_->store(CameraState::open);
    return Status::success();
  }

  [[nodiscard]] std::vector<FeatureInfo> features() const override {
    std::lock_guard lock(feature_mutex_);
    std::vector<FeatureInfo> result;
    result.reserve(features_.size());
    for (const auto& [name, record] : features_) {
      static_cast<void>(name);
      result.push_back(record.info);
    }
    return result;
  }

  [[nodiscard]] Result<FeatureInfo> feature_info(
      const std::string& name) const override {
    std::lock_guard lock(feature_mutex_);
    const auto found = features_.find(name);
    if (found == features_.end()) {
      return Status{ErrorCode::not_found, "feature not found: " + name};
    }
    return found->second.info;
  }

  [[nodiscard]] Result<FeatureValue> read_feature(
      const std::string& name) const override {
    std::lock_guard lock(feature_mutex_);
    const auto found = features_.find(name);
    if (found == features_.end()) {
      return Status{ErrorCode::not_found, "feature not found: " + name};
    }
    if (found->second.info.access == AccessMode::write_only) {
      return Status{ErrorCode::access_denied, "feature is write-only: " + name};
    }
    return found->second.value;
  }

  Status write_feature(const std::string& name, const FeatureValue& value) override {
    std::lock_guard lock(feature_mutex_);
    const auto found = features_.find(name);
    if (found == features_.end()) {
      return {ErrorCode::not_found, "feature not found: " + name};
    }
    auto& record = found->second;
    if (record.info.access != AccessMode::write_only &&
        record.info.access != AccessMode::read_write) {
      return {ErrorCode::access_denied, "feature is not writable: " + name};
    }
    if (record.info.kind == FeatureKind::command) {
      return {ErrorCode::invalid_argument, "use execute_command for command features"};
    }
    if (record.value.index() != value.index()) {
      return {ErrorCode::invalid_argument, "feature value type does not match: " + name};
    }

    const auto numeric_value = [&]() -> std::optional<double> {
      if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
      }
      if (const auto* floating = std::get_if<double>(&value)) {
        return *floating;
      }
      return std::nullopt;
    }();
    if (numeric_value && record.info.minimum && *numeric_value < *record.info.minimum) {
      return {ErrorCode::invalid_argument, "feature value is below minimum: " + name};
    }
    if (numeric_value && record.info.maximum && *numeric_value > *record.info.maximum) {
      return {ErrorCode::invalid_argument, "feature value is above maximum: " + name};
    }
    if (const auto* enumeration = std::get_if<std::string>(&value);
        enumeration && record.info.kind == FeatureKind::enumeration &&
        std::find(record.info.enum_entries.begin(), record.info.enum_entries.end(),
                  *enumeration) == record.info.enum_entries.end()) {
      return {ErrorCode::invalid_argument, "invalid enumeration entry: " + *enumeration};
    }

    record.value = value;
    return Status::success();
  }

  Status execute_command(const std::string& name) override {
    std::lock_guard lock(feature_mutex_);
    const auto found = features_.find(name);
    if (found == features_.end()) {
      return {ErrorCode::not_found, "feature not found: " + name};
    }
    if (found->second.info.kind != FeatureKind::command) {
      return {ErrorCode::invalid_argument, "feature is not a command: " + name};
    }
    return Status::success();
  }

  [[nodiscard]] Result<std::unique_ptr<Stream>> create_stream(
      const StreamConfiguration& configuration) override {
    if (state() != CameraState::open) {
      return Status{ErrorCode::invalid_state, "camera must be open before creating a stream"};
    }
    if (configuration.buffer_count == 0 || configuration.queue_capacity == 0) {
      return Status{ErrorCode::invalid_argument,
                    "stream buffer and queue sizes must be greater than zero"};
    }

    std::lock_guard lock(feature_mutex_);
    const auto width = static_cast<std::uint32_t>(
        std::get<std::int64_t>(features_.at("Width").value));
    const auto height = static_cast<std::uint32_t>(
        std::get<std::int64_t>(features_.at("Height").value));
    const auto frame_rate = std::get<double>(features_.at("AcquisitionFrameRate").value);
    std::unique_ptr<Stream> stream = std::make_unique<SimulatorStream>(
        configuration, state_, width, height, frame_rate);
    return stream;
  }

 private:
  void add_feature(FeatureInfo info, FeatureValue value) {
    const auto name = info.name;
    features_.emplace(name, FeatureRecord{std::move(info), std::move(value)});
  }

  DeviceInfo info_;
  std::shared_ptr<std::atomic<CameraState>> state_;
  mutable std::mutex feature_mutex_;
  std::map<std::string, FeatureRecord> features_;
};

class SimulatorAdapter final : public Adapter {
 public:
  explicit SimulatorAdapter(SimulatorConfiguration configuration)
      : configuration_(std::move(configuration)) {}

  [[nodiscard]] const AdapterDescriptor& descriptor() const noexcept override {
    return descriptor_;
  }

  [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices() override {
    return std::vector<DeviceInfo>{device_info()};
  }

  [[nodiscard]] Result<std::shared_ptr<Camera>> create_camera(
      const DeviceInfo& device) override {
    if (device.stable_id != device_info().stable_id) {
      return Status{ErrorCode::not_found, "simulator device is not available"};
    }
    std::shared_ptr<Camera> camera =
        std::make_shared<SimulatorCamera>(device_info(), configuration_);
    return camera;
  }

 private:
  [[nodiscard]] DeviceInfo device_info() const {
    return DeviceInfo{
        "sim://" + configuration_.serial,
        "univision:simulator:" + configuration_.serial,
        descriptor_.id,
        "UniVision",
        configuration_.model,
        configuration_.serial,
        configuration_.model + " (" + configuration_.serial + ")",
        "virtual",
        TransportType::virtual_device,
        CertificationTier::certified,
    };
  }

  SimulatorConfiguration configuration_;
  AdapterDescriptor descriptor_{"simulator", "UniVision Simulator", "0.3.0",
                                "UniVision", 100};
};

}  // namespace

AdapterPtr make_simulator_adapter(const SimulatorConfiguration& configuration) {
  return std::make_shared<SimulatorAdapter>(configuration);
}

}  // namespace univision
