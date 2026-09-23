#pragma once

#include "univision/industrial/contracts.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace univision::industrial {

struct RecordingSnapshot {
  std::string session_id;
  bool complete{false};
  std::vector<OwnedFrame> frames;
};

class Recorder {
 public:
  Recorder();
  ~Recorder();
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;
  Recorder(Recorder&&) noexcept;
  Recorder& operator=(Recorder&&) noexcept;

  [[nodiscard]] Status open(const std::filesystem::path& workspace,
                            const std::string& session_id,
                            std::uint64_t segment_limit_bytes =
                                64ULL * 1024 * 1024);
  [[nodiscard]] Status append(const OwnedFrame& frame,
                              std::uint64_t record_utc_ns);
  [[nodiscard]] Status close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] Result<RecordingSnapshot> read_recording(
    const std::filesystem::path& session_directory,
    std::uint64_t max_decoded_frame_bytes = 256ULL * 1024 * 1024);

}  // namespace univision::industrial
