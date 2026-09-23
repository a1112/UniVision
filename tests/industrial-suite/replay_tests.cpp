#include "univision/industrial/replay.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

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
  e.session_id = "replay-session";
  e.stream_id = "camera-1";
  e.device_id = "simulator";
  e.sequence = sequence;
  e.source = SourceKind::synthetic;
  e.validity = Validity::synthetic;
  e.width = 8;
  e.height = 8;
  e.pixel_format = "Mono8";
  e.planes.push_back({PlaneRole::intensity, 0, 8, 64});
  e.clock_id = "sim-clock";
  e.decoded_bytes = 64;
  f.bytes.resize(64, std::byte{static_cast<unsigned char>(sequence)});
  return f;
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto workspace = fs::current_path() /
                         ("industrial-replay-test-" + std::to_string(suffix));
  Recorder recorder;
  check(static_cast<bool>(recorder.open(workspace, "replay-session")),
        "create replay fixture");
  check(static_cast<bool>(recorder.append(fixture(1), 100)), "append frame 1");
  check(static_cast<bool>(recorder.append(fixture(3), 300)), "append frame 3");
  check(static_cast<bool>(recorder.close()), "close replay fixture");

  ReplayEngine engine;
  check(static_cast<bool>(engine.open(workspace / "replay-session")),
        "open recorded session");
  check(engine.complete(), "closed recording is complete");
  auto first = engine.seek_sequence("camera-1", 1);
  check(first && first.value().frame->envelope.sequence == 1, "seek frame 1");
  const auto old_generation = first ? first.value().generation : 0;
  auto third = engine.seek_sequence("camera-1", 3);
  check(third && third.value().frame->envelope.sequence == 3, "seek frame 3");
  check(!engine.is_current(old_generation), "old seek generation invalidated");
  check(third && engine.is_current(third.value().generation),
        "new seek generation is current");
  check(!engine.seek_sequence("camera-1", 2), "missing sequence remains a gap");
  const auto previous_generation = third ? third.value().generation : 0;
  check(!engine.open(workspace / "missing-session"),
        "opening absent session reports failure");
  check(engine.session_id().empty() && !engine.complete(),
        "failed open clears previous session state");
  check(!engine.is_current(previous_generation),
        "failed open invalidates previous generation");
  check(!engine.seek_sequence("camera-1", 1),
        "failed open cannot seek an old session");

  RunEvidence a;
  a.input_sha256 = "same-input";
  a.coordinate_frame = "profile-xz-mm";
  a.calibration_id = "calibration-v1";
  a.unit = Unit::millimeter;
  a.frames.push_back({"camera-1", 1, 20.0});
  RunEvidence b = a;
  b.frames[0].value = 20.1;
  auto compared = compare_runs(a, b);
  check(compared && compared.value().deltas.size() == 1 &&
            std::abs(compared.value().deltas[0].delta - 0.1) < 1e-10,
        "comparable runs report frame delta");
  b.input_sha256 = "different-input";
  check(!compare_runs(a, b), "different input cannot be compared");
  b = a;
  b.unit = Unit::raw_device_code;
  check(!compare_runs(a, b), "different unit cannot be compared");

  if (failures != 0) return 1;
  std::cout << "industrial replay checks passed\n";
}
