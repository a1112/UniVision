#include "univision/industrial/replay.h"

#include <cmath>
#include <map>
#include <utility>

namespace univision::industrial {

Status ReplayEngine::open(const std::filesystem::path& session_directory,
                          std::uint64_t max_frame_bytes) {
  ++generation_;
  frames_.clear();
  session_id_.clear();
  complete_ = false;
  auto loaded = read_recording(session_directory, max_frame_bytes);
  if (!loaded) return loaded.status();
  frames_.reserve(loaded.value().frames.size());
  for (auto& frame : loaded.value().frames)
    frames_.push_back(std::make_shared<const OwnedFrame>(std::move(frame)));
  session_id_ = std::move(loaded.value().session_id);
  complete_ = loaded.value().complete;
  return Status::success();
}

Result<ReplayView> ReplayEngine::seek_sequence(const std::string& stream_id,
                                               std::uint64_t sequence) {
  ++generation_;
  for (const auto& frame : frames_) {
    if (frame->envelope.stream_id == stream_id &&
        frame->envelope.sequence == sequence)
      return ReplayView{frame, generation_};
  }
  return Status{ErrorCode::not_found, "requested stream sequence is a gap"};
}

bool ReplayEngine::is_current(std::uint64_t generation) const noexcept {
  return generation == generation_;
}

Result<RunComparison> compare_runs(const RunEvidence& before,
                                   const RunEvidence& after) {
  if (before.input_sha256.empty() ||
      before.input_sha256 != after.input_sha256)
    return Status{ErrorCode::invalid_argument, "run input digests differ"};
  if (before.coordinate_frame.empty() ||
      before.coordinate_frame != after.coordinate_frame ||
      before.calibration_id != after.calibration_id ||
      before.unit != after.unit)
    return Status{ErrorCode::invalid_argument,
                  "run geometry, calibration or unit differs"};
  using Key = std::pair<std::string, std::uint64_t>;
  std::map<Key, double> indexed;
  for (const auto& frame : before.frames) {
    if (frame.stream_id.empty() || !std::isfinite(frame.value) ||
        !indexed.emplace(Key{frame.stream_id, frame.sequence}, frame.value).second)
      return Status{ErrorCode::invalid_argument, "invalid before frame metrics"};
  }
  if (indexed.size() != after.frames.size())
    return Status{ErrorCode::invalid_argument, "run frame sets differ"};
  RunComparison result;
  result.input_sha256 = before.input_sha256;
  result.before_version = before.algorithm_version;
  result.after_version = after.algorithm_version;
  std::map<Key, bool> seen;
  for (const auto& frame : after.frames) {
    const Key key{frame.stream_id, frame.sequence};
    const auto found = indexed.find(key);
    if (found == indexed.end() || !std::isfinite(frame.value) ||
        !seen.emplace(key, true).second)
      return Status{ErrorCode::invalid_argument, "run frame sets differ"};
    result.deltas.push_back({frame.stream_id, frame.sequence, found->second,
                             frame.value, frame.value - found->second});
  }
  return result;
}

}  // namespace univision::industrial
