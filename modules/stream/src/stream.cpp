#include "univision/industrial/stream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace univision::industrial {
namespace {
constexpr std::uint32_t max_metadata = 64 * 1024;
constexpr std::uint64_t max_payload = 1024 * 1024;
constexpr std::size_t header_size = 32;
constexpr std::size_t max_buffer = 4 * 1024 * 1024;

void put16(std::vector<std::byte>& out, std::size_t at, std::uint16_t value) {
  out[at] = std::byte((value >> 8) & 0xff);
  out[at + 1] = std::byte(value & 0xff);
}
void put32(std::vector<std::byte>& out, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i)
    out[at + i] = std::byte((value >> (8 * (3 - i))) & 0xff);
}
void put64(std::vector<std::byte>& out, std::size_t at, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i)
    out[at + i] = std::byte((value >> (8 * (7 - i))) & 0xff);
}
std::uint16_t get16(const std::byte* data) {
  return (std::uint16_t(std::to_integer<unsigned char>(data[0])) << 8) |
         std::to_integer<unsigned char>(data[1]);
}
std::uint32_t get32(const std::byte* data) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i)
    value = (value << 8) | std::to_integer<unsigned char>(data[i]);
  return value;
}
std::uint64_t get64(const std::byte* data) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i)
    value = (value << 8) | std::to_integer<unsigned char>(data[i]);
  return value;
}
bool valid_type(std::uint16_t type) {
  return type >= static_cast<std::uint16_t>(WireMessageType::hello) &&
         type <= static_cast<std::uint16_t>(WireMessageType::error);
}
}  // namespace

Result<std::vector<std::byte>> encode_wire_message(const WireMessage& message) {
  if (!valid_type(static_cast<std::uint16_t>(message.type)) ||
      message.metadata.size() > max_metadata ||
      message.payload.size() > max_payload)
    return Status{ErrorCode::invalid_argument, "UVS1 message exceeds limits"};
  std::vector<std::byte> out(
      header_size + message.metadata.size() + message.payload.size());
  out[0] = std::byte{'U'}; out[1] = std::byte{'V'};
  out[2] = std::byte{'S'}; out[3] = std::byte{'1'};
  put16(out, 4, 1);
  put16(out, 6, static_cast<std::uint16_t>(message.type));
  put32(out, 8, static_cast<std::uint32_t>(message.metadata.size()));
  put64(out, 12, message.payload.size());
  put64(out, 20, message.correlation_id);
  put32(out, 28, 0);
  std::copy(message.metadata.begin(), message.metadata.end(),
            reinterpret_cast<char*>(out.data() + header_size));
  std::copy(message.payload.begin(), message.payload.end(),
            out.begin() + header_size + message.metadata.size());
  return out;
}

Result<std::vector<WireMessage>> WireDecoder::feed(
    std::span<const std::byte> bytes) {
  if (bytes.size() > max_buffer - buffer_.size())
    return Status{ErrorCode::buffer_exhausted, "UVS1 decoder buffer full"};
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  std::vector<WireMessage> messages;
  while (buffer_.size() >= header_size) {
    const auto* h = buffer_.data();
    if (h[0] != std::byte{'U'} || h[1] != std::byte{'V'} ||
        h[2] != std::byte{'S'} || h[3] != std::byte{'1'} ||
        get16(h + 4) != 1 || !valid_type(get16(h + 6)) ||
        get32(h + 28) != 0) {
      buffer_.clear();
      return Status{ErrorCode::invalid_argument, "invalid UVS1 header"};
    }
    const auto metadata_size = get32(h + 8);
    const auto payload_size = get64(h + 12);
    if (metadata_size > max_metadata || payload_size > max_payload) {
      buffer_.clear();
      return Status{ErrorCode::buffer_exhausted, "UVS1 length exceeds limit"};
    }
    const auto total = header_size + metadata_size +
                       static_cast<std::size_t>(payload_size);
    if (buffer_.size() < total) break;
    WireMessage message;
    message.type = static_cast<WireMessageType>(get16(h + 6));
    message.correlation_id = get64(h + 20);
    message.metadata.assign(
        reinterpret_cast<const char*>(h + header_size), metadata_size);
    message.payload.assign(h + header_size + metadata_size, h + total);
    messages.push_back(std::move(message));
    buffer_.erase(buffer_.begin(), buffer_.begin() + total);
  }
  return messages;
}

LocalStream::LocalStream(DeliveryMode mode, std::size_t max_frames,
                         std::uint64_t max_bytes)
    : mode_(mode), max_frames_(max_frames), max_bytes_(max_bytes) {}

Result<Ticket> LocalStream::publish(std::string consumer_id, OwnedFrame frame) {
  if (consumer_id.empty())
    return Status{ErrorCode::invalid_argument, "consumer ID is missing"};
  const auto valid = validate_envelope(frame.envelope, frame.bytes.size(),
                                        max_bytes_);
  if (!valid) return valid;
  const Key key{consumer_id, frame.envelope.session_id,
                frame.envelope.stream_id, frame.envelope.sequence};
  const auto digest = sha256_hex(frame.bytes);
  if (const auto found = dedupe_.find(key); found != dedupe_.end()) {
    if (found->second.digest != digest)
      return Status{ErrorCode::already_exists,
                    "same frame key has conflicting payload hash"};
    return found->second.ticket;
  }
  if (max_frames_ == 0 || frame.bytes.size() > max_bytes_)
    return Status{ErrorCode::buffer_exhausted, "stream admission budget exceeded"};
  while (queue_.size() >= max_frames_ ||
         frame.bytes.size() > max_bytes_ - queued_bytes_) {
    if (mode_ == DeliveryMode::inspection_bounded || queue_.empty())
      return Status{ErrorCode::buffer_exhausted, "stream queue is full"};
    queued_bytes_ -= queue_.front().frame.bytes.size();
    queue_.pop_front();
    ++dropped_frames_;
  }
  Ticket ticket{next_ticket_++, consumer_id, frame.envelope.session_id,
                frame.envelope.stream_id, frame.envelope.sequence, digest};
  queued_bytes_ += frame.bytes.size();
  queue_.push_back({std::move(frame), Receipt{ticket, ReceiptLevel::received}});
  dedupe_.emplace(key, DedupeEntry{digest, ticket});
  dedupe_order_.push_back(key);
  constexpr std::size_t max_dedupe_entries = 1024;
  if (dedupe_order_.size() > max_dedupe_entries) {
    dedupe_.erase(dedupe_order_.front());
    dedupe_order_.pop_front();
  }
  return ticket;
}

Result<Delivery> LocalStream::next() {
  if (queue_.empty())
    return Status{ErrorCode::not_found, "stream queue is empty"};
  Delivery result = std::move(queue_.front());
  queued_bytes_ -= result.frame.bytes.size();
  queue_.pop_front();
  return result;
}

}  // namespace univision::industrial
