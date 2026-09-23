#pragma once

#include "univision/industrial/contracts.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace univision::industrial {

enum class WireMessageType : std::uint16_t {
  hello = 1, capabilities = 2, open_stream = 3, frame_begin = 4,
  frame_chunk = 5, frame_commit = 6, receipt = 7, credit = 8,
  gap = 9, resume = 10, cancel = 11, close = 12, error = 13
};

struct WireMessage {
  WireMessageType type{WireMessageType::hello};
  std::uint64_t correlation_id{};
  std::string metadata;
  std::vector<std::byte> payload;
};

[[nodiscard]] Result<std::vector<std::byte>> encode_wire_message(
    const WireMessage& message);

class WireDecoder {
 public:
  [[nodiscard]] Result<std::vector<WireMessage>> feed(
      std::span<const std::byte> bytes);

 private:
  std::vector<std::byte> buffer_;
};

enum class DeliveryMode { preview_latest, inspection_bounded };
enum class ReceiptLevel { received, durable, processed };

struct Ticket {
  std::uint64_t id{};
  std::string consumer_id;
  std::string session_id;
  std::string stream_id;
  std::uint64_t sequence{};
  std::string payload_sha256;
};

struct Receipt {
  Ticket ticket;
  ReceiptLevel level{ReceiptLevel::received};
};

struct Delivery {
  OwnedFrame frame;
  Receipt receipt;
};

class LocalStream {
 public:
  LocalStream(DeliveryMode mode, std::size_t max_frames,
              std::uint64_t max_bytes);
  [[nodiscard]] Result<Ticket> publish(std::string consumer_id,
                                       OwnedFrame frame);
  [[nodiscard]] Result<Delivery> next();
  [[nodiscard]] std::uint64_t dropped_frames() const noexcept {
    return dropped_frames_;
  }
  [[nodiscard]] std::size_t queue_depth() const noexcept {
    return queue_.size();
  }

 private:
  using Key = std::tuple<std::string, std::string, std::string, std::uint64_t>;
  struct DedupeEntry { std::string digest; Ticket ticket; };
  DeliveryMode mode_;
  std::size_t max_frames_;
  std::uint64_t max_bytes_;
  std::uint64_t queued_bytes_{};
  std::uint64_t dropped_frames_{};
  std::uint64_t next_ticket_{1};
  std::deque<Delivery> queue_;
  std::map<Key, DedupeEntry> dedupe_;
  std::deque<Key> dedupe_order_;
};

}  // namespace univision::industrial
