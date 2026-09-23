#include "univision/industrial/recording.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace univision::industrial;

namespace {
int failures = 0;
void check(bool ok, const char* name) {
  if (!ok) {
    std::cerr << "failed: " << name << '\n';
    ++failures;
  }
}
OwnedFrame fixture(std::uint64_t sequence) {
  OwnedFrame f;
  auto& e = f.envelope;
  e.session_id = "session-A";
  e.stream_id = "stream-1";
  e.device_id = "simulator";
  e.sequence = sequence;
  e.source = SourceKind::synthetic;
  e.validity = Validity::synthetic;
  e.kind = FrameKind::intensity;
  e.unit = Unit::unitless;
  e.width = 8;
  e.height = 8;
  e.pixel_format = "Mono8";
  e.planes.push_back({PlaneRole::intensity, 0, 8, 64});
  e.clock_id = "sim-clock";
  e.decoded_bytes = 64;
  f.bytes.resize(64, std::byte{0x2a});
  return f;
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto workspace = fs::current_path() /
                         ("industrial-recording-test-" + std::to_string(suffix));
  Recorder recorder;
  check(static_cast<bool>(recorder.open(workspace, "session-A")),
        "open new session");
  check(static_cast<bool>(recorder.append(fixture(9007199254740993ULL), 123)),
        "append synthetic frame");
  check(static_cast<bool>(recorder.close()), "close recording");
  auto read = read_recording(workspace / "session-A", 1024);
  check(read && read.value().complete, "closed session is complete");
  check(read && read.value().frames.size() == 1, "one frame round trip");
  check(read && read.value().frames[0].envelope.sequence ==
                    9007199254740993ULL, "uint64 sequence round trip");
  check(read && read.value().frames[0].bytes == fixture(1).bytes,
        "payload round trip");
  check(!recorder.open(workspace, "session-A"), "do not overwrite session");

  {
    Recorder limited;
    check(static_cast<bool>(limited.open(workspace, "session-C", 2 * 1024 * 1024)),
          "open with small segment budget");
    auto large = fixture(1);
    large.envelope.session_id = "session-C";
    large.envelope.width = 256;
    large.envelope.height = 256;
    large.envelope.planes.front() = {PlaneRole::intensity, 0, 256, 65536};
    large.envelope.decoded_bytes = 65536;
    large.bytes.resize(65536);
    bool exhausted = false, accepted_after_exhaustion = false;
    for (std::uint64_t i = 1; i <= 24; ++i) {
      large.envelope.sequence = i;
      const auto accepted = limited.append(large, i);
      if (!accepted) exhausted = true;
      else if (exhausted) accepted_after_exhaustion = true;
    }
    check(exhausted && !accepted_after_exhaustion,
          "segment remains full after budget exhaustion");
  }

  {
    Recorder interrupted;
    check(static_cast<bool>(interrupted.open(workspace, "session-B")),
          "open interruptible session");
    auto frame = fixture(2);
    frame.envelope.session_id = "session-B";
    check(static_cast<bool>(interrupted.append(frame, 456)),
          "append frame before interrupted close");
  }
  auto partial = read_recording(workspace / "session-B", 1024);
  check(partial && !partial.value().complete,
        "uncommitted session remains partial");
  check(partial && partial.value().frames.size() == 1,
        "partial valid prefix remains readable");
  const auto partial_segment =
      workspace / "session-B" / "segments" / "000001.mcap.partial";
  const auto partial_size = fs::file_size(partial_segment);
  fs::resize_file(partial_segment, partial_size - 16);
  auto truncated = read_recording(workspace / "session-B", 1024);
  check(truncated && !truncated.value().complete &&
            truncated.value().frames.size() == 1,
        "truncated tail retains only verified prefix");

  {
    Recorder unpublished;
    check(static_cast<bool>(unpublished.open(workspace, "session-D")),
          "open recoverable session");
    auto frame = fixture(4);
    frame.envelope.session_id = "session-D";
    check(static_cast<bool>(unpublished.append(frame, 789)),
          "append recoverable frame");
    check(static_cast<bool>(unpublished.close()),
          "close recoverable session");
    fs::rename(workspace / "session-D" / "session.json",
               workspace / "session-D" / "session.json.lost");
    auto recovered = read_recording(workspace / "session-D", 1024);
    check(recovered && !recovered.value().complete &&
              recovered.value().frames.size() == 1,
          "published segment without manifest is a partial prefix");
  }

  const auto segment = workspace / "session-A" / "segments" / "000001.mcap";
  {
    std::fstream file(segment, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(100);
    const char corrupted = '\xff';
    file.write(&corrupted, 1);
  }
  check(!read_recording(workspace / "session-A", 1024),
        "corrupted segment cannot be treated as complete");

  if (failures != 0) return 1;
  std::cout << "industrial recording checks passed\n";
}
