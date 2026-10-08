#include "univision/simulator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace univision {
namespace {

constexpr PixelFormat pfnc_mono8 = 0x01080001ULL;
constexpr std::uint32_t sensor_extent = 16384;

struct FeatureRecord {
  FeatureInfo info;
  FeatureValue value;
};

struct SimulatorState {
  std::atomic<CameraState> camera_state{CameraState::closed};
  // All mutable acquisition parameters and camera transitions use this lock.
  mutable std::mutex mutex;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t offset_x{};
  std::uint32_t offset_y{};
  double frame_rate{};
};

bool is_roi_feature(const std::string& name) {
  return name == "Width" || name == "Height" || name == "OffsetX" ||
         name == "OffsetY";
}

Status validate_configuration(const SimulatorConfiguration& configuration) {
  if (configuration.width == 0 || configuration.width > sensor_extent ||
      configuration.height == 0 || configuration.height > sensor_extent) {
    return {ErrorCode::invalid_argument, "simulator image dimensions exceed sensor bounds"};
  }
  if (!std::isfinite(configuration.frame_rate) || configuration.frame_rate < 0.1 ||
      configuration.frame_rate > 1000.0) {
    return {ErrorCode::invalid_argument, "simulator frame rate must be finite and in [0.1, 1000]"};
  }
  return Status::success();
}

class SimulatorStream final : public Stream {
 public:
  SimulatorStream(StreamConfiguration configuration,
                  std::shared_ptr<SimulatorState> state)
      : configuration_(configuration), state_(std::move(state)) {}

  ~SimulatorStream() override { static_cast<void>(stop()); }

  Status start() override {
    std::lock_guard stream_lock(mutex_);
    std::lock_guard state_lock(state_->mutex);
    if (running_.load()) {
      return {ErrorCode::invalid_state, "stream is already running"};
    }
    if (state_->camera_state.load() != CameraState::open) {
      return {ErrorCode::invalid_state, "camera must be open before streaming"};
    }
    // Snapshot under the same lock that validates ROI writes. A stream created
    // before a parameter edit therefore starts with the latest complete ROI.
    width_ = state_->width;
    height_ = state_->height;
    offset_x_ = state_->offset_x;
    offset_y_ = state_->offset_y;
    frame_rate_ = state_->frame_rate;
    last_frame_ = std::chrono::steady_clock::now();
    ++generation_;
    running_.store(true);
    state_->camera_state.store(CameraState::streaming);
    return Status::success();
  }

  Status stop() override {
    std::lock_guard stream_lock(mutex_);
    std::lock_guard state_lock(state_->mutex);
    if (!running_.exchange(false)) {
      return Status::success();
    }
    wake_.notify_all();
    if (state_->camera_state.load() == CameraState::streaming) {
      state_->camera_state.store(CameraState::open);
    }
    return Status::success();
  }

  [[nodiscard]] bool running() const noexcept override { return running_.load(); }

  [[nodiscard]] Result<Frame> wait_next(std::chrono::milliseconds timeout) override {
    std::unique_lock lock(mutex_);
    if (!running()) {
      return Status{ErrorCode::invalid_state, "stream is not running"};
    }
    if (timeout < std::chrono::milliseconds::zero()) {
      return Status{ErrorCode::invalid_argument, "frame timeout must not be negative"};
    }

    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / frame_rate_));
    const auto due = last_frame_ + period;
    const auto generation = generation_;
    const auto now = std::chrono::steady_clock::now();
    if (due > now) {
      const auto wait = due - now;
      if (wait > timeout) {
        return Status{ErrorCode::timeout, "frame wait timed out"};
      }
      if (wake_.wait_until(lock, due, [this, generation] {
            return !running() || generation_ != generation;
          })) {
        return Status{ErrorCode::invalid_state, "stream stopped during frame wait"};
      }
    }

    const auto captured = std::chrono::steady_clock::now();
    last_frame_ = captured;
    auto pixels = std::make_shared<std::vector<std::byte>>(
        static_cast<std::size_t>(width_) * height_);
    const auto next_id = ++frame_id_;
    for (std::uint32_t y = 0; y < height_; ++y) {
      for (std::uint32_t x = 0; x < width_; ++x) {
        const auto value = static_cast<unsigned char>(
            (x + offset_x_ + y + offset_y_ + next_id) & 0xffU);
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
    frame.metadata.emplace("OffsetX", static_cast<std::int64_t>(offset_x_));
    frame.metadata.emplace("OffsetY", static_cast<std::int64_t>(offset_y_));

    ++statistics_.frames_received;
    ++statistics_.frames_delivered;
    return frame;
  }

  [[nodiscard]] StreamStatistics statistics() const noexcept override {
    std::lock_guard lock(mutex_);
    return statistics_;
  }

 private:
  StreamConfiguration configuration_;
  std::shared_ptr<SimulatorState> state_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::uint32_t width_{};
  std::uint32_t height_{};
  std::uint32_t offset_x_{};
  std::uint32_t offset_y_{};
  double frame_rate_{};
  std::atomic<bool> running_{false};
  std::chrono::steady_clock::time_point last_frame_{};
  std::uint64_t frame_id_{};
  std::uint64_t generation_{};
  StreamStatistics statistics_{};
};

class SimulatorCamera final : public Camera {
 public:
  explicit SimulatorCamera(DeviceInfo info, const SimulatorConfiguration& configuration)
      : info_(std::move(info)), state_(std::make_shared<SimulatorState>()) {
    state_->width = configuration.width;
    state_->height = configuration.height;
    state_->frame_rate = configuration.frame_rate;
    add_feature({"Width", "Width", "Image width", "px", FeatureKind::integer,
                 AccessMode::read_write, 1.0, 16384.0, 1.0, {}, true},
                static_cast<std::int64_t>(configuration.width));
    add_feature({"Height", "Height", "Image height", "px", FeatureKind::integer,
                 AccessMode::read_write, 1.0, 16384.0, 1.0, {}, true},
                static_cast<std::int64_t>(configuration.height));
    add_feature({"OffsetX", "Offset X", "Horizontal sensor ROI offset", "px",
                 FeatureKind::integer, AccessMode::read_write, 0.0, 16383.0, 1.0,
                 {}, true}, std::int64_t{0});
    add_feature({"OffsetY", "Offset Y", "Vertical sensor ROI offset", "px",
                 FeatureKind::integer, AccessMode::read_write, 0.0, 16383.0, 1.0,
                 {}, true}, std::int64_t{0});
    add_feature({"SensorWidth", "Sensor Width", "Virtual sensor width", "px",
                 FeatureKind::integer, AccessMode::read_only, std::nullopt,
                 std::nullopt, std::nullopt, {}, true},
                static_cast<std::int64_t>(sensor_extent));
    add_feature({"SensorHeight", "Sensor Height", "Virtual sensor height", "px",
                 FeatureKind::integer, AccessMode::read_only, std::nullopt,
                 std::nullopt, std::nullopt, {}, true},
                static_cast<std::int64_t>(sensor_extent));
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
  [[nodiscard]] CameraState state() const noexcept override {
    return state_->camera_state.load();
  }

  Status open() override {
    std::lock_guard lock(state_->mutex);
    CameraState expected = CameraState::closed;
    if (!state_->camera_state.compare_exchange_strong(expected, CameraState::opening)) {
      return {ErrorCode::invalid_state, "camera is not closed"};
    }
    state_->camera_state.store(CameraState::open);
    return Status::success();
  }

  Status close() override {
    std::lock_guard lock(state_->mutex);
    const auto current = state_->camera_state.load();
    if (current == CameraState::streaming) {
      return {ErrorCode::invalid_state, "stop the stream before closing the camera"};
    }
    state_->camera_state.store(CameraState::closed);
    return Status::success();
  }

  Status reconnect() override {
    std::lock_guard lock(state_->mutex);
    const auto current = state_->camera_state.load();
    if (current != CameraState::lost && current != CameraState::failed &&
        current != CameraState::open) {
      return {ErrorCode::invalid_state,
              "camera can only reconnect from open, lost, or failed state"};
    }
    state_->camera_state.store(CameraState::recovering);
    state_->camera_state.store(CameraState::open);
    return Status::success();
  }

  [[nodiscard]] std::vector<FeatureInfo> features() const override {
    std::lock_guard lock(state_->mutex);
    std::vector<FeatureInfo> result;
    result.reserve(features_.size());
    for (const auto& [name, record] : features_) {
      static_cast<void>(name);
      result.push_back(current_feature_info(record));
    }
    return result;
  }

  [[nodiscard]] Result<FeatureInfo> feature_info(
      const std::string& name) const override {
    std::lock_guard lock(state_->mutex);
    const auto found = features_.find(name);
    if (found == features_.end()) {
      return Status{ErrorCode::not_found, "feature not found: " + name};
    }
    return current_feature_info(found->second);
  }

  [[nodiscard]] Result<FeatureValue> read_feature(
      const std::string& name) const override {
    std::lock_guard lock(state_->mutex);
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
    std::lock_guard lock(state_->mutex);
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
    if (is_roi_feature(name) && state() == CameraState::streaming) {
      return {ErrorCode::invalid_state, "stop the stream before changing ROI: " + name};
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
    if (numeric_value && !std::isfinite(*numeric_value)) {
      return {ErrorCode::invalid_argument, "feature value must be finite: " + name};
    }
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

    if (is_roi_feature(name)) {
      const auto integer = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
      const auto width = name == "Width" ? integer : state_->width;
      const auto height = name == "Height" ? integer : state_->height;
      const auto offset_x = name == "OffsetX" ? integer : state_->offset_x;
      const auto offset_y = name == "OffsetY" ? integer : state_->offset_y;
      if (width > sensor_extent - offset_x || height > sensor_extent - offset_y) {
        return {ErrorCode::invalid_argument, "ROI exceeds virtual sensor bounds: " + name};
      }
      state_->width = width;
      state_->height = height;
      state_->offset_x = offset_x;
      state_->offset_y = offset_y;
    } else if (name == "AcquisitionFrameRate") {
      state_->frame_rate = std::get<double>(value);
    }

    record.value = value;
    return Status::success();
  }

  Status execute_command(const std::string& name) override {
    std::lock_guard lock(state_->mutex);
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
    std::lock_guard lock(state_->mutex);
    if (state() != CameraState::open) {
      return Status{ErrorCode::invalid_state, "camera must be open before creating a stream"};
    }
    if (configuration.buffer_count == 0 || configuration.queue_capacity == 0) {
      return Status{ErrorCode::invalid_argument,
                    "stream buffer and queue sizes must be greater than zero"};
    }

    std::unique_ptr<Stream> stream = std::make_unique<SimulatorStream>(
        configuration, state_);
    return stream;
  }

 private:
  [[nodiscard]] FeatureInfo current_feature_info(const FeatureRecord& record) const {
    auto info = record.info;
    if (info.name == "Width") info.maximum = sensor_extent - state_->offset_x;
    if (info.name == "Height") info.maximum = sensor_extent - state_->offset_y;
    if (info.name == "OffsetX") info.maximum = sensor_extent - state_->width;
    if (info.name == "OffsetY") info.maximum = sensor_extent - state_->height;
    if (is_roi_feature(info.name) && state() == CameraState::streaming) {
      info.access = AccessMode::read_only;
    }
    return info;
  }

  void add_feature(FeatureInfo info, FeatureValue value) {
    const auto name = info.name;
    features_.emplace(name, FeatureRecord{std::move(info), std::move(value)});
  }

  DeviceInfo info_;
  std::shared_ptr<SimulatorState> state_;
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
    if (const auto status = validate_configuration(configuration_); !status) {
      return status;
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
