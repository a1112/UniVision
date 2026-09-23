#include "univision/industrial/contracts.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
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

FrameEnvelope mono_envelope() {
  FrameEnvelope e;
  e.session_id = "session-1";
  e.stream_id = "stream-1";
  e.device_id = "simulator";
  e.sequence = 9007199254740993ULL;
  e.source = SourceKind::synthetic;
  e.kind = FrameKind::intensity;
  e.unit = Unit::unitless;
  e.validity = Validity::synthetic;
  e.width = 8;
  e.height = 8;
  e.pixel_format = "Mono8";
  e.planes.push_back({PlaneRole::intensity, 0, 8, 64});
  e.clock_id = "sim-clock";
  e.capture_ticks = 42;
  e.decoded_bytes = 64;
  return e;
}
}  // namespace

int main() {
  auto e = mono_envelope();
  check(static_cast<bool>(validate_envelope(e, 64, 1024)),
        "valid large uint64 identity");
  check(e.sequence == 9007199254740993ULL, "sequence retains 64-bit value");
  check(!e.capture_utc_ns.has_value(), "unmapped capture time has no UTC");

  auto bad = e;
  bad.planes[0].row_stride = 7;
  check(!validate_envelope(bad, 64, 1024), "reject short row stride");
  bad = e;
  bad.planes[0].offset = UINT64_MAX;
  check(!validate_envelope(bad, 64, 1024), "reject offset overflow");
  bad = e;
  bad.decoded_bytes = 2048;
  check(!validate_envelope(bad, 2048, 1024), "reject byte budget");
  bad = e;
  bad.kind = FrameKind::depth_raw;
  bad.unit = Unit::millimeter;
  check(!validate_envelope(bad, 64, 1024), "reject raw depth with metric unit");
  bad = e;
  bad.capture_utc_ns = 123;
  check(!validate_envelope(bad, 64, 1024), "reject UTC without mapping");

  auto pixels = std::make_shared<std::vector<std::byte>>(64, std::byte{0x2a});
  univision::Frame frame;
  frame.descriptor.width = 8;
  frame.descriptor.height = 8;
  frame.descriptor.stride = 8;
  frame.descriptor.pixel_format = 0x01080001ULL;
  frame.descriptor.memory_type = univision::MemoryType::host;
  frame.descriptor.frame_id = 17;
  frame.buffer = univision::FrameBuffer{pixels, pixels->data(), pixels->size()};
  auto owned = copy_core_frame(frame, e, 64);
  check(owned && owned.value().bytes.size() == 64,
        "bounded owned copy succeeds");
  (*pixels)[0] = std::byte{0x00};
  check(owned && owned.value().bytes[0] == std::byte{0x2a},
        "owned bytes independent of producer lease");
  check(!copy_core_frame(frame, e, 63), "copy rejects insufficient budget");
  frame.descriptor.memory_type = univision::MemoryType::cuda_device;
  check(!copy_core_frame(frame, e, 64), "host copy rejects device pointer");

  check(valid_artifact_path("segments/000001.mcap"), "relative artifact path");
  check(!valid_artifact_path("../secret"), "reject traversal");
  check(!valid_artifact_path("C:/secret"), "reject absolute drive path");
  check(!valid_artifact_path("a\\..\\secret"), "reject Windows traversal");
  check(!valid_artifact_path("segments/"), "reject empty trailing path component");
  check(!valid_artifact_path("bad\nname"), "reject control character in path");

  const std::string abc = "abc";
  const auto abc_bytes = std::span<const std::byte>{
      reinterpret_cast<const std::byte*>(abc.data()), abc.size()};
  check(sha256_hex(abc_bytes) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 known vector");
  check(sha256_hex(std::span<const std::byte>{}) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty input");
  const std::vector<std::byte> zeros(64);
  check(sha256_hex(zeros) ==
            "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b",
        "SHA-256 full block boundary");

  if (failures != 0) return 1;
  std::cout << "industrial contracts checks passed\n";
}
