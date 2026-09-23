#include "univision/industrial/contracts.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>

namespace univision::industrial {
namespace {

bool fits_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
  out = a + b;
  return true;
}

bool fits_multiply(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
  out = a * b;
  return true;
}

std::uint32_t bytes_per_pixel(const FrameEnvelope& e, PlaneRole role) {
  if (role == PlaneRole::mask || role == PlaneRole::confidence) return 1;
  if (e.pixel_format == "Mono8" && role == PlaneRole::intensity) return 1;
  if ((e.pixel_format == "Mono16" && role == PlaneRole::intensity) ||
      (e.pixel_format == "Depth16" && role == PlaneRole::depth)) return 2;
  if (e.pixel_format == "Depth32F" && role == PlaneRole::depth) return 4;
  return 0;
}

bool valid_id(const std::string& id) { return !id.empty() && id.size() <= 2048; }

}  // namespace

Status validate_envelope(const FrameEnvelope& e, std::uint64_t payload_bytes,
                         std::uint64_t max_decoded_bytes) {
  if (e.schema_version != "0.1.0-draft")
    return {ErrorCode::unsupported, "unknown frame schema version"};
  if (!valid_id(e.session_id) || !valid_id(e.stream_id) ||
      !valid_id(e.device_id) || !valid_id(e.clock_id))
    return {ErrorCode::invalid_argument, "frame identity or clock ID is missing"};
  if (e.width == 0 || e.height == 0 || e.planes.empty() || e.planes.size() > 4096)
    return {ErrorCode::invalid_argument, "invalid frame dimensions or plane count"};
  if (e.decoded_bytes == 0 || e.decoded_bytes > max_decoded_bytes ||
      payload_bytes > max_decoded_bytes)
    return {ErrorCode::buffer_exhausted, "frame exceeds byte budget"};
  if (e.encoding == Encoding::none && payload_bytes != e.decoded_bytes)
    return {ErrorCode::invalid_argument, "raw payload length differs from layout"};
  if (e.kind == FrameKind::intensity && e.unit != Unit::unitless)
    return {ErrorCode::invalid_argument, "intensity unit must be unitless"};
  if (e.kind == FrameKind::depth_raw && e.unit != Unit::raw_device_code)
    return {ErrorCode::invalid_argument, "raw depth requires raw-device-code unit"};
  if (e.kind == FrameKind::depth_metric &&
      e.unit != Unit::millimeter && e.unit != Unit::meter)
    return {ErrorCode::invalid_argument, "metric depth requires a length unit"};
  if ((e.source == SourceKind::synthetic) != (e.validity == Validity::synthetic))
    return {ErrorCode::invalid_argument, "synthetic source and validity disagree"};
  if (e.capture_utc_ns.has_value() != e.mapping_id.has_value() ||
      (e.capture_utc_ns.has_value() &&
       e.clock_quality != ClockQuality::mapped_verified))
    return {ErrorCode::invalid_argument, "capture UTC requires a verified mapping"};
  if (e.clock_quality == ClockQuality::mapped_verified &&
      !e.capture_utc_ns.has_value())
    return {ErrorCode::invalid_argument, "verified mapping has no UTC value"};

  bool primary_plane = false;
  for (const auto& plane : e.planes) {
    const auto bpp = bytes_per_pixel(e, plane.role);
    if (bpp == 0) return {ErrorCode::unsupported, "unsupported plane pixel format"};
    if ((e.kind == FrameKind::intensity && plane.role == PlaneRole::intensity) ||
        (e.kind != FrameKind::intensity && plane.role == PlaneRole::depth))
      primary_plane = true;
    std::uint64_t row_bytes = 0, total_bytes = 0, end = 0;
    if (!fits_multiply(e.width, bpp, row_bytes) ||
        plane.row_stride < row_bytes ||
        !fits_multiply(plane.row_stride, e.height, total_bytes) ||
        total_bytes > plane.byte_length ||
        !fits_add(plane.offset, plane.byte_length, end) ||
        end > e.decoded_bytes)
      return {ErrorCode::invalid_argument, "plane stride, length or offset is invalid"};
  }
  if (!primary_plane)
    return {ErrorCode::invalid_argument, "frame has no primary image plane"};
  return Status::success();
}

Result<OwnedFrame> copy_core_frame(const Frame& frame, FrameEnvelope envelope,
                                   std::uint64_t max_owned_bytes) {
  if (frame.descriptor.memory_type != MemoryType::host &&
      frame.descriptor.memory_type != MemoryType::host_pinned)
    return Status{ErrorCode::unsupported, "Core frame is not host-addressable"};
  if (!frame.descriptor.complete || frame.buffer.empty())
    return Status{ErrorCode::invalid_argument, "Core frame is incomplete or empty"};
  if (frame.descriptor.width != envelope.width ||
      frame.descriptor.height != envelope.height ||
      frame.descriptor.pixel_format != 0x01080001ULL ||
      envelope.pixel_format != "Mono8" || envelope.planes.size() != 1 ||
      envelope.planes.front().row_stride != frame.descriptor.stride)
    return Status{ErrorCode::unsupported, "Core descriptor and Mono8 envelope disagree"};
  if (frame.buffer.size() > max_owned_bytes)
    return Status{ErrorCode::buffer_exhausted, "owned frame budget exceeded"};
  const auto status = validate_envelope(envelope, frame.buffer.size(), max_owned_bytes);
  if (!status) return status;
  OwnedFrame owned;
  owned.envelope = std::move(envelope);
  owned.bytes.assign(frame.buffer.data(), frame.buffer.data() + frame.buffer.size());
  return owned;
}

bool valid_artifact_path(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != path.npos ||
      path.find(':') != path.npos || path.find('\0') != path.npos)
    return false;
  for (const unsigned char ch : path)
    if (ch < 0x20 || ch == 0x7f) return false;
  while (!path.empty()) {
    const auto slash = path.find('/');
    const auto part = path.substr(0, slash);
    if (part.empty() || part == "." || part == "..") return false;
    if (slash == path.npos) break;
    path.remove_prefix(slash + 1);
  }
  return true;
}

std::string sha256_hex(std::span<const std::byte> bytes) {
  constexpr std::array<std::uint32_t, 64> k{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
      0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
      0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
      0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
      0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
      0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
      0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
      0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<std::uint32_t, 8> state{
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto transform = [&](const std::byte* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      const auto p = block + i * 4;
      w[i] = (std::uint32_t(std::to_integer<unsigned char>(p[0])) << 24) |
             (std::uint32_t(std::to_integer<unsigned char>(p[1])) << 16) |
             (std::uint32_t(std::to_integer<unsigned char>(p[2])) << 8) |
             std::uint32_t(std::to_integer<unsigned char>(p[3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const auto s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
      const auto s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto a = state[0], b = state[1], c = state[2], d = state[3];
    auto e = state[4], f = state[5], g = state[6], h = state[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choose = (e & f) ^ (~e & g);
      const auto t1 = h + s1 + choose + k[i] + w[i];
      const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto t2 = s0 + majority;
      h = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
  };
  std::size_t offset = 0;
  while (bytes.size() - offset >= 64) {
    transform(bytes.data() + offset);
    offset += 64;
  }
  std::array<std::byte, 128> tail{};
  const auto remainder = bytes.size() - offset;
  if (remainder != 0)
    std::copy_n(bytes.data() + offset, remainder, tail.data());
  tail[remainder] = std::byte{0x80};
  const auto padded = remainder < 56 ? 64U : 128U;
  const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
  for (std::size_t i = 0; i < 8; ++i)
    tail[padded - 1 - i] = std::byte((bit_length >> (8 * i)) & 0xff);
  transform(tail.data());
  if (padded == 128) transform(tail.data() + 64);
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const auto word : state) {
    for (int shift = 28; shift >= 0; shift -= 4)
      result.push_back(hex[(word >> shift) & 0xf]);
  }
  return result;
}

}  // namespace univision::industrial
