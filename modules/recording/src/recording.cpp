#include "univision/industrial/recording.h"

#define MCAP_IMPLEMENTATION
#define MCAP_COMPRESSION_NO_LZ4
#define MCAP_COMPRESSION_NO_ZSTD
#include <mcap/mcap.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace univision::industrial {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::uint64_t max_segment_bytes = 64ULL * 1024 * 1024;
constexpr std::uint64_t segment_reserve_bytes = 1024ULL * 1024;
constexpr std::uint64_t max_metadata_bytes = 64ULL * 1024;

template <typename E, std::size_t N>
std::string text_of(E value, const std::array<std::pair<E, std::string_view>, N>& map) {
  for (const auto& [code, text] : map)
    if (code == value) return std::string{text};
  throw std::runtime_error("unknown enum value");
}
template <typename E, std::size_t N>
E parse_enum(const std::string& value,
             const std::array<std::pair<E, std::string_view>, N>& map) {
  for (const auto& [code, text] : map)
    if (text == value) return code;
  throw std::runtime_error("unknown enum text");
}

constexpr std::array source_map{
    std::pair{SourceKind::synthetic, std::string_view{"synthetic"}},
    std::pair{SourceKind::file, std::string_view{"file"}},
    std::pair{SourceKind::camera, std::string_view{"camera"}}};
constexpr std::array kind_map{
    std::pair{FrameKind::intensity, std::string_view{"intensity"}},
    std::pair{FrameKind::depth_raw, std::string_view{"depth_raw"}},
    std::pair{FrameKind::depth_metric, std::string_view{"depth_metric"}}};
constexpr std::array unit_map{
    std::pair{Unit::unitless, std::string_view{"unitless"}},
    std::pair{Unit::raw_device_code, std::string_view{"raw-device-code"}},
    std::pair{Unit::millimeter, std::string_view{"mm"}},
    std::pair{Unit::meter, std::string_view{"m"}}};
constexpr std::array validity_map{
    std::pair{Validity::complete, std::string_view{"complete"}},
    std::pair{Validity::invalid, std::string_view{"invalid"}},
    std::pair{Validity::synthetic, std::string_view{"synthetic"}}};
constexpr std::array role_map{
    std::pair{PlaneRole::intensity, std::string_view{"intensity"}},
    std::pair{PlaneRole::depth, std::string_view{"depth"}},
    std::pair{PlaneRole::confidence, std::string_view{"confidence"}},
    std::pair{PlaneRole::mask, std::string_view{"mask"}}};
constexpr std::array encoding_map{
    std::pair{Encoding::none, std::string_view{"none"}},
    std::pair{Encoding::zstd, std::string_view{"zstd"}},
    std::pair{Encoding::jpeg, std::string_view{"jpeg"}}};
constexpr std::array clock_map{
    std::pair{ClockQuality::unmapped, std::string_view{"unmapped"}},
    std::pair{ClockQuality::synthetic, std::string_view{"synthetic"}},
    std::pair{ClockQuality::mapped_verified, std::string_view{"mapped-verified"}}};

std::uint64_t decimal_u64(const json& value) {
  const auto text = value.get<std::string>();
  if (text.empty() || (text.size() > 1 && text.front() == '0'))
    throw std::runtime_error("non-canonical uint64");
  std::uint64_t output = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), output);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::runtime_error("invalid uint64");
  return output;
}

json envelope_json(const FrameEnvelope& e, std::string_view payload_hash) {
  json planes = json::array();
  for (const auto& p : e.planes)
    planes.push_back({{"role", text_of(p.role, role_map)},
                      {"offset", std::to_string(p.offset)},
                      {"row_stride", p.row_stride},
                      {"byte_length", std::to_string(p.byte_length)}});
  return {{"schema_version", e.schema_version},
          {"session_id", e.session_id},
          {"stream_id", e.stream_id},
          {"device_id", e.device_id},
          {"sequence", std::to_string(e.sequence)},
          {"source_kind", text_of(e.source, source_map)},
          {"frame_kind", text_of(e.kind, kind_map)},
          {"unit", text_of(e.unit, unit_map)},
          {"validity", text_of(e.validity, validity_map)},
          {"layout", {{"width", e.width}, {"height", e.height},
                      {"pixel_format", e.pixel_format}, {"planes", planes}}},
          {"capture_time", {{"clock_id", e.clock_id},
                            {"capture_ticks", std::to_string(e.capture_ticks)},
                            {"tick_frequency_hz", std::to_string(e.tick_frequency_hz)},
                            {"capture_utc_ns", e.capture_utc_ns
                                ? json(std::to_string(*e.capture_utc_ns)) : json(nullptr)},
                            {"mapping_id", e.mapping_id
                                ? json(*e.mapping_id) : json(nullptr)},
                            {"quality", text_of(e.clock_quality, clock_map)}}},
          {"encoding", text_of(e.encoding, encoding_map)},
          {"decoded_bytes", std::to_string(e.decoded_bytes)},
          {"calibration_id", e.calibration_id
              ? json(*e.calibration_id) : json(nullptr)},
          {"parent_ids", e.parent_ids},
          {"payload_sha256", payload_hash}};
}

FrameEnvelope parse_envelope(const json& j) {
  FrameEnvelope e;
  e.schema_version = j.at("schema_version").get<std::string>();
  e.session_id = j.at("session_id").get<std::string>();
  e.stream_id = j.at("stream_id").get<std::string>();
  e.device_id = j.at("device_id").get<std::string>();
  e.sequence = decimal_u64(j.at("sequence"));
  e.source = parse_enum(j.at("source_kind").get<std::string>(), source_map);
  e.kind = parse_enum(j.at("frame_kind").get<std::string>(), kind_map);
  e.unit = parse_enum(j.at("unit").get<std::string>(), unit_map);
  e.validity = parse_enum(j.at("validity").get<std::string>(), validity_map);
  const auto& layout = j.at("layout");
  e.width = layout.at("width").get<std::uint32_t>();
  e.height = layout.at("height").get<std::uint32_t>();
  e.pixel_format = layout.at("pixel_format").get<std::string>();
  for (const auto& p : layout.at("planes"))
    e.planes.push_back({
        parse_enum(p.at("role").get<std::string>(), role_map),
        decimal_u64(p.at("offset")),
        p.at("row_stride").get<std::uint64_t>(),
        decimal_u64(p.at("byte_length"))});
  const auto& clock = j.at("capture_time");
  e.clock_id = clock.at("clock_id").get<std::string>();
  e.capture_ticks = decimal_u64(clock.at("capture_ticks"));
  e.tick_frequency_hz = decimal_u64(clock.at("tick_frequency_hz"));
  if (!clock.at("capture_utc_ns").is_null())
    e.capture_utc_ns = decimal_u64(clock.at("capture_utc_ns"));
  if (!clock.at("mapping_id").is_null())
    e.mapping_id = clock.at("mapping_id").get<std::string>();
  e.clock_quality = parse_enum(clock.at("quality").get<std::string>(), clock_map);
  e.encoding = parse_enum(j.at("encoding").get<std::string>(), encoding_map);
  e.decoded_bytes = decimal_u64(j.at("decoded_bytes"));
  if (!j.at("calibration_id").is_null())
    e.calibration_id = j.at("calibration_id").get<std::string>();
  e.parent_ids = j.at("parent_ids").get<std::vector<std::string>>();
  return e;
}

Result<std::vector<std::byte>> read_bounded_file(const fs::path& path,
                                                std::uint64_t limit) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec) return Status{ErrorCode::io_error, "cannot stat recording segment"};
  if (size > limit)
    return Status{ErrorCode::buffer_exhausted, "recording segment exceeds limit"};
  std::ifstream input(path, std::ios::binary);
  if (!input) return Status{ErrorCode::io_error, "cannot open recording segment"};
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  if (size != 0)
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(size));
  if (!input && size != 0)
    return Status{ErrorCode::io_error, "cannot read recording segment"};
  return bytes;
}

Result<OwnedFrame> decode_message(const mcap::Message& message,
                                  std::uint64_t max_frame_bytes) {
  if (message.dataSize < 4)
    return Status{ErrorCode::invalid_argument, "short frame message"};
  const auto* data = message.data;
  const auto metadata_length =
      (std::uint32_t(std::to_integer<unsigned char>(data[0])) << 24) |
      (std::uint32_t(std::to_integer<unsigned char>(data[1])) << 16) |
      (std::uint32_t(std::to_integer<unsigned char>(data[2])) << 8) |
      std::uint32_t(std::to_integer<unsigned char>(data[3]));
  if (metadata_length > max_metadata_bytes ||
      metadata_length > message.dataSize - 4 ||
      message.dataSize - 4 - metadata_length > max_frame_bytes)
    return Status{ErrorCode::buffer_exhausted, "invalid frame message length"};
  const auto metadata = std::string_view{
      reinterpret_cast<const char*>(data + 4), metadata_length};
  try {
    const auto parsed = json::parse(metadata);
    OwnedFrame frame;
    frame.envelope = parse_envelope(parsed);
    const auto* payload = data + 4 + metadata_length;
    const auto payload_size = message.dataSize - 4 - metadata_length;
    const auto status = validate_envelope(frame.envelope, payload_size,
                                          max_frame_bytes);
    if (!status) return status;
    const auto digest = sha256_hex(std::span{payload,
                                            static_cast<std::size_t>(payload_size)});
    if (digest != parsed.at("payload_sha256").get<std::string>())
      return Status{ErrorCode::io_error, "frame payload SHA-256 mismatch"};
    frame.bytes.assign(payload, payload + payload_size);
    return frame;
  } catch (const std::exception& error) {
    return Status{ErrorCode::invalid_argument,
                  std::string{"invalid frame metadata: "} + error.what()};
  }
}
}  // namespace

struct Recorder::Impl {
  fs::path directory;
  std::string session_id;
  mcap::McapWriter writer;
  mcap::Channel channel;
  std::uint64_t frame_count{};
  std::uint64_t admitted_bytes{};
  std::uint64_t segment_limit_bytes{max_segment_bytes};
  bool active{false};
};

Recorder::Recorder() : impl_(std::make_unique<Impl>()) {}
Recorder::~Recorder() {
  if (impl_ && impl_->active) impl_->writer.close();
}
Recorder::Recorder(Recorder&&) noexcept = default;
Recorder& Recorder::operator=(Recorder&&) noexcept = default;

Status Recorder::open(const fs::path& workspace, const std::string& session_id,
                      std::uint64_t segment_limit_bytes) {
  if (impl_->active)
    return {ErrorCode::invalid_state, "recorder is already open"};
  if (!valid_artifact_path(session_id) || session_id.find('/') != std::string::npos)
    return {ErrorCode::invalid_argument, "invalid session ID"};
  if (segment_limit_bytes < 2 * segment_reserve_bytes ||
      segment_limit_bytes > max_segment_bytes)
    return {ErrorCode::invalid_argument, "segment limit is outside supported range"};
  try {
    fs::create_directories(workspace);
    const auto directory = workspace / session_id;
    if (!fs::create_directory(directory))
      return {ErrorCode::already_exists, "session directory already exists"};
    fs::create_directory(directory / "segments");
    auto next = std::make_unique<Impl>();
    next->directory = directory;
    next->session_id = session_id;
    next->segment_limit_bytes = segment_limit_bytes;
    mcap::McapWriterOptions options("univision.recording.v1");
    options.compression = mcap::Compression::None;
    options.chunkSize = 8ULL * 1024 * 1024;
    options.enableDataCRC = true;
    const auto segment = (directory / "segments" / "000001.mcap.partial").string();
    const auto status = next->writer.open(segment, options);
    if (!status.ok())
      return {ErrorCode::io_error, "cannot open MCAP segment: " + status.message};
    mcap::Schema schema("univision.frame.v1", "univision.frame.v1",
                        "uint32-be metadata length + UTF-8 JSON + binary payload");
    next->writer.addSchema(schema);
    next->channel = mcap::Channel("/frames", "univision.frame.v1", schema.id);
    next->writer.addChannel(next->channel);
    next->active = true;
    impl_ = std::move(next);
    return Status::success();
  } catch (const fs::filesystem_error& error) {
    return {ErrorCode::io_error, error.what()};
  }
}

Status Recorder::append(const OwnedFrame& frame, std::uint64_t record_utc_ns) {
  if (!impl_->active)
    return {ErrorCode::invalid_state, "recorder is not open"};
  if (frame.envelope.session_id != impl_->session_id)
    return {ErrorCode::invalid_argument, "frame session differs from recorder"};
  if (frame.envelope.encoding != Encoding::none)
    return {ErrorCode::unsupported, "compressed frame encoding is not implemented"};
  const auto valid = validate_envelope(frame.envelope, frame.bytes.size(),
                                        256ULL * 1024 * 1024);
  if (!valid) return valid;
  try {
    const auto digest = sha256_hex(frame.bytes);
    const auto metadata = envelope_json(frame.envelope, digest).dump();
    if (metadata.size() > max_metadata_bytes)
      return {ErrorCode::buffer_exhausted, "frame metadata exceeds limit"};
    const auto message_bytes = 4ULL + metadata.size() + frame.bytes.size();
    const auto usable_bytes = impl_->segment_limit_bytes - segment_reserve_bytes;
    if (impl_->admitted_bytes > usable_bytes ||
        message_bytes > usable_bytes - impl_->admitted_bytes)
      return {ErrorCode::buffer_exhausted, "segment byte budget exhausted"};
    std::vector<std::byte> data(4 + metadata.size() + frame.bytes.size());
    const auto size = static_cast<std::uint32_t>(metadata.size());
    data[0] = std::byte((size >> 24) & 0xff);
    data[1] = std::byte((size >> 16) & 0xff);
    data[2] = std::byte((size >> 8) & 0xff);
    data[3] = std::byte(size & 0xff);
    std::copy(metadata.begin(), metadata.end(),
              reinterpret_cast<char*>(data.data() + 4));
    std::copy(frame.bytes.begin(), frame.bytes.end(),
              data.begin() + 4 + metadata.size());
    mcap::Message message{};
    message.channelId = impl_->channel.id;
    message.sequence = 0;  // Full uint64 identity is in FrameEnvelope JSON.
    message.logTime = record_utc_ns;
    message.publishTime = frame.envelope.capture_utc_ns.value_or(record_utc_ns);
    message.data = data.data();
    message.dataSize = data.size();
    const auto status = impl_->writer.write(message);
    if (!status.ok())
      return {ErrorCode::io_error, "MCAP write failed: " + status.message};
    ++impl_->frame_count;
    impl_->admitted_bytes += data.size();
    return Status::success();
  } catch (const std::exception& error) {
    return {ErrorCode::internal_error, error.what()};
  }
}

Status Recorder::close() {
  if (!impl_->active)
    return {ErrorCode::invalid_state, "recorder is not open"};
  try {
    impl_->writer.close();
    impl_->active = false;
    const auto partial = impl_->directory / "segments" / "000001.mcap.partial";
    const auto published = impl_->directory / "segments" / "000001.mcap";
    const auto content = read_bounded_file(partial, impl_->segment_limit_bytes);
    if (!content) return content.status();
    const auto hash = sha256_hex(content.value());
    fs::rename(partial, published);
    const auto manifest = json{
        {"schema_version", "0.1.0-draft"},
        {"session_id", impl_->session_id},
        {"complete", true},
        {"frame_count", std::to_string(impl_->frame_count)},
        {"segment", {{"path", "segments/000001.mcap"},
                     {"bytes", std::to_string(content.value().size())},
                     {"sha256", hash}}}};
    const auto temporary = impl_->directory / "session.json.tmp";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output) return {ErrorCode::io_error, "cannot write session manifest"};
      output << manifest.dump(2) << '\n';
      output.flush();
      if (!output) return {ErrorCode::io_error, "cannot flush session manifest"};
    }
    fs::rename(temporary, impl_->directory / "session.json");
    return Status::success();
  } catch (const std::exception& error) {
    return {ErrorCode::io_error, error.what()};
  }
}

Result<RecordingSnapshot> read_recording(const fs::path& session_directory,
                                         std::uint64_t max_frame_bytes) {
  try {
    RecordingSnapshot snapshot;
    const auto manifest_path = session_directory / "session.json";
    json manifest;
    fs::path segment;
    if (fs::exists(manifest_path)) {
      std::ifstream input(manifest_path, std::ios::binary);
      if (!input) return Status{ErrorCode::io_error, "cannot open session manifest"};
      input >> manifest;
      if (manifest.at("schema_version") != "0.1.0-draft")
        return Status{ErrorCode::unsupported, "unsupported session manifest"};
      snapshot.session_id = manifest.at("session_id").get<std::string>();
      snapshot.complete = manifest.at("complete").get<bool>();
      const auto relative = manifest.at("segment").at("path").get<std::string>();
      if (relative != "segments/000001.mcap")
        return Status{ErrorCode::invalid_argument, "unexpected segment path"};
      segment = session_directory / relative;
      const auto content = read_bounded_file(segment, max_segment_bytes);
      if (!content) return content.status();
      if (decimal_u64(manifest.at("segment").at("bytes")) !=
              content.value().size() ||
          manifest.at("segment").at("sha256").get<std::string>() !=
              sha256_hex(content.value()))
        return Status{ErrorCode::io_error, "segment length or SHA-256 mismatch"};
    } else {
      snapshot.session_id = session_directory.filename().string();
      const auto partial = session_directory / "segments" / "000001.mcap.partial";
      const auto published = session_directory / "segments" / "000001.mcap";
      if (fs::exists(partial) && fs::exists(published))
        return Status{ErrorCode::invalid_state,
                      "ambiguous published and partial segments"};
      segment = fs::exists(partial) ? partial : published;
      if (!fs::exists(segment))
        return Status{ErrorCode::not_found, "no session manifest or segment"};
      const auto content = read_bounded_file(segment, max_segment_bytes);
      if (!content) return content.status();
    }
    mcap::McapReader reader;
    const auto opened = reader.open(segment.string());
    if (!opened.ok())
      return Status{ErrorCode::io_error, "cannot open MCAP segment: " + opened.message};
    bool parser_failed = false;
    auto messages = reader.readMessages(
        [&](const mcap::Status&) { parser_failed = true; });
    for (const auto& view : messages) {
      if (!view.channel ||
          view.channel->messageEncoding != "univision.frame.v1") {
        parser_failed = true;
        break;
      }
      auto decoded = decode_message(view.message, max_frame_bytes);
      if (!decoded) return decoded.status();
      if (decoded.value().envelope.session_id != snapshot.session_id)
        return Status{ErrorCode::invalid_argument, "frame session ID mismatch"};
      snapshot.frames.push_back(std::move(decoded).value());
    }
    reader.close();
    if (parser_failed)
      return Status{ErrorCode::io_error, "MCAP segment has malformed records"};
    if (snapshot.complete &&
        snapshot.frames.size() != decimal_u64(manifest.at("frame_count")))
      return Status{ErrorCode::io_error, "manifest frame count mismatch"};
    return snapshot;
  } catch (const std::exception& error) {
    return Status{ErrorCode::io_error,
                  std::string{"recording read failed: "} + error.what()};
  }
}

}  // namespace univision::industrial
