#pragma once

#include "univision/industrial/recording.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace univision::industrial {

struct ReplayView {
  std::shared_ptr<const OwnedFrame> frame;
  std::uint64_t generation{};
};

class ReplayEngine {
 public:
  [[nodiscard]] Status open(const std::filesystem::path& session_directory,
                            std::uint64_t max_frame_bytes =
                                256ULL * 1024 * 1024);
  [[nodiscard]] Result<ReplayView> seek_sequence(const std::string& stream_id,
                                                 std::uint64_t sequence);
  [[nodiscard]] bool is_current(std::uint64_t generation) const noexcept;
  [[nodiscard]] bool complete() const noexcept { return complete_; }
  [[nodiscard]] const std::string& session_id() const noexcept {
    return session_id_;
  }

 private:
  std::string session_id_;
  bool complete_{false};
  std::uint64_t generation_{};
  std::vector<std::shared_ptr<const OwnedFrame>> frames_;
};

struct FrameMetric {
  std::string stream_id;
  std::uint64_t sequence{};
  double value{};
};

struct RunEvidence {
  std::string input_sha256;
  std::string graph_revision;
  std::string algorithm_version;
  std::string coordinate_frame;
  std::string calibration_id;
  Unit unit{Unit::unitless};
  std::vector<FrameMetric> frames;
};

struct FrameDelta {
  std::string stream_id;
  std::uint64_t sequence{};
  double before{};
  double after{};
  double delta{};
};

struct RunComparison {
  std::string input_sha256;
  std::string before_version;
  std::string after_version;
  std::vector<FrameDelta> deltas;
};

[[nodiscard]] Result<RunComparison> compare_runs(const RunEvidence& before,
                                                 const RunEvidence& after);

}  // namespace univision::industrial
