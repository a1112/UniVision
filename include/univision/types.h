#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace univision {

enum class TransportType : std::uint8_t {
  unknown,
  gige_vision,
  usb3_vision,
  coaxpress,
  camera_link,
  virtual_device,
};

enum class CertificationTier : std::uint8_t {
  best_effort,
  compatible,
  certified,
};

enum class CameraState : std::uint8_t {
  closed,
  opening,
  open,
  streaming,
  lost,
  recovering,
  failed,
};

enum class MemoryType : std::uint8_t {
  host,
  host_pinned,
  dma,
  cuda_device,
  d3d_resource,
  rdma_registered,
};

enum class DropPolicy : std::uint8_t {
  block_producer,
  drop_oldest,
  drop_newest,
  latest_only,
};

using PixelFormat = std::uint64_t;
using FeatureValue = std::variant<std::int64_t, double, bool, std::string>;
using Metadata = std::unordered_map<std::string, FeatureValue>;

struct DeviceInfo {
  std::string id;
  std::string stable_id;
  std::string adapter_id;
  std::string vendor;
  std::string model;
  std::string serial;
  std::string display_name;
  std::string address;
  TransportType transport{TransportType::unknown};
  CertificationTier certification{CertificationTier::best_effort};
};

struct DeviceSelector {
  std::optional<std::string> id;
  std::optional<std::string> stable_id;
  std::optional<std::string> adapter_id;
  std::optional<std::string> vendor;
  std::optional<std::string> model;
  std::optional<std::string> serial;

  [[nodiscard]] bool matches(const DeviceInfo& info) const;
};

struct StreamConfiguration {
  std::size_t buffer_count{16};
  std::size_t queue_capacity{8};
  DropPolicy drop_policy{DropPolicy::drop_oldest};
  std::chrono::milliseconds frame_timeout{1000};
};

struct StreamStatistics {
  std::uint64_t frames_received{};
  std::uint64_t frames_delivered{};
  std::uint64_t frames_dropped{};
  std::uint64_t incomplete_frames{};
  std::uint64_t packet_resends{};
  std::size_t queue_depth{};
};

struct FrameDescriptor {
  std::uint32_t width{};
  std::uint32_t height{};
  std::size_t stride{};
  PixelFormat pixel_format{};
  MemoryType memory_type{MemoryType::host};
  std::uint64_t frame_id{};
  std::uint64_t camera_timestamp_ns{};
  std::chrono::steady_clock::time_point host_timestamp{};
  bool complete{true};
};

class FrameBuffer {
 public:
  FrameBuffer() = default;

  template <typename Owner>
  FrameBuffer(std::shared_ptr<Owner> owner, const std::byte* data, std::size_t size)
      : owner_(std::move(owner)), data_(data), size_(size) {}

  [[nodiscard]] const std::byte* data() const noexcept { return data_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return data_ == nullptr || size_ == 0; }

 private:
  std::shared_ptr<const void> owner_;
  const std::byte* data_{};
  std::size_t size_{};
};

struct Frame {
  FrameDescriptor descriptor;
  FrameBuffer buffer;
  Metadata metadata;
};

}  // namespace univision
