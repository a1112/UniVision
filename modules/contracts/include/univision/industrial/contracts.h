#pragma once

#include "univision/status.h"
#include "univision/types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace univision::industrial {

enum class SourceKind { synthetic, file, camera };
enum class FrameKind { intensity, depth_raw, depth_metric };
enum class Unit { unitless, raw_device_code, millimeter, meter };
enum class Validity { complete, invalid, synthetic };
enum class PlaneRole { intensity, depth, confidence, mask };
enum class Encoding { none, zstd, jpeg };
enum class ClockQuality { unmapped, synthetic, mapped_verified };

struct PlaneLayout {
  PlaneRole role{PlaneRole::intensity};
  std::uint64_t offset{};
  std::uint64_t row_stride{};
  std::uint64_t byte_length{};
};

struct FrameEnvelope {
  std::string schema_version{"0.1.0-draft"};
  std::string session_id;
  std::string stream_id;
  std::string device_id;
  std::uint64_t sequence{};
  SourceKind source{SourceKind::file};
  FrameKind kind{FrameKind::intensity};
  Unit unit{Unit::unitless};
  Validity validity{Validity::complete};
  std::uint32_t width{};
  std::uint32_t height{};
  std::string pixel_format;
  std::vector<PlaneLayout> planes;
  std::string clock_id;
  std::uint64_t capture_ticks{};
  std::uint64_t tick_frequency_hz{};
  std::optional<std::uint64_t> capture_utc_ns;
  std::optional<std::string> mapping_id;
  ClockQuality clock_quality{ClockQuality::unmapped};
  Encoding encoding{Encoding::none};
  std::uint64_t decoded_bytes{};
  std::optional<std::string> calibration_id;
  std::vector<std::string> parent_ids;
};

struct OwnedFrame {
  FrameEnvelope envelope;
  std::vector<std::byte> bytes;
};

[[nodiscard]] Status validate_envelope(const FrameEnvelope& envelope,
                                       std::uint64_t payload_bytes,
                                       std::uint64_t max_decoded_bytes);
[[nodiscard]] Result<OwnedFrame> copy_core_frame(const Frame& frame,
                                                 FrameEnvelope envelope,
                                                 std::uint64_t max_owned_bytes);
[[nodiscard]] bool valid_artifact_path(std::string_view relative_path);
[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);

}  // namespace univision::industrial
