#include "univision/industrial/stream.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace univision::industrial;

namespace {
int failures = 0;
template <typename T>
void check(T&& ok, const char* name) {
  if (!static_cast<bool>(ok)) {
    std::cerr << "failed: " << name << '\n';
    ++failures;
  }
}
OwnedFrame fixture(std::uint64_t sequence, std::byte content = std::byte{0x2a}) {
  OwnedFrame f;
  auto& e = f.envelope;
  e.session_id = "session";
  e.stream_id = "stream";
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
  f.bytes.resize(64, content);
  return f;
}
}  // namespace

int main() {
  WireMessage message;
  message.type = WireMessageType::frame_chunk;
  message.correlation_id = 9007199254740993ULL;
  message.metadata = R"({"sequence":"1"})";
  message.payload = {std::byte{1}, std::byte{2}, std::byte{3}};
  const auto encoded = encode_wire_message(message);
  check(encoded && encoded.value().size() == 32 + message.metadata.size() + 3,
        "UVS1 frame has fixed 32-byte header");
  WireDecoder decoder;
  auto first = decoder.feed(std::span(encoded.value()).first(7));
  check(first && first.value().empty(), "partial header is buffered");
  auto second = decoder.feed(std::span(encoded.value()).subspan(7));
  check(second && second.value().size() == 1 &&
            second.value()[0].correlation_id == message.correlation_id &&
            second.value()[0].payload == message.payload,
        "split message is reassembled");
  std::vector<std::byte> joined = encoded.value();
  joined.insert(joined.end(), encoded.value().begin(), encoded.value().end());
  WireDecoder sticky;
  auto together = sticky.feed(joined);
  check(together && together.value().size() == 2,
        "concatenated messages preserve boundaries");
  joined[8] = std::byte{0xff};
  joined[9] = std::byte{0xff};
  joined[10] = std::byte{0xff};
  joined[11] = std::byte{0xff};
  WireDecoder limited;
  check(!limited.feed(joined), "oversize metadata rejected before allocation");

  LocalStream bounded(DeliveryMode::inspection_bounded, 1, 64);
  auto one = bounded.publish("consumer", fixture(1));
  check(one, "first inspection frame admitted");
  check(!bounded.publish("consumer", fixture(2)), "full queue rejects next frame");
  auto delivered = bounded.next();
  check(delivered && delivered.value().receipt.level == ReceiptLevel::received,
        "in-memory delivery is only received");
  check(!bounded.next(), "empty queue has no frame");
  auto duplicate = bounded.publish("consumer", fixture(1));
  check(duplicate && one && duplicate.value().id == one.value().id,
        "same key and hash is idempotent");
  check(!bounded.publish("consumer", fixture(1, std::byte{0x3a})),
        "same key with different hash is conflict");

  LocalStream preview(DeliveryMode::preview_latest, 2, 128);
  check(preview.publish("preview", fixture(1)), "preview first frame");
  check(preview.publish("preview", fixture(2)), "preview second frame");
  check(preview.publish("preview", fixture(3)), "preview replaces oldest");
  check(preview.dropped_frames() == 1, "preview drop is counted");
  auto latest_one = preview.next();
  check(latest_one && latest_one.value().frame.envelope.sequence == 2,
        "preview oldest whole frame evicted");

  if (failures != 0) return 1;
  std::cout << "industrial stream checks passed\n";
}
