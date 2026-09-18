#include "genapi/genapi.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

const std::string xml = R"xml(
<RegisterDescription>
  <Integer Name="SignedValue"><pValue>SignedReg</pValue><Min>-10</Min><Max>10</Max><Inc>2</Inc></Integer>
  <IntReg Name="SignedReg"><Address>0</Address><Length>2</Length><AccessMode>RW</AccessMode><Sign>Signed</Sign><Endianess>LittleEndian</Endianess></IntReg>
  <Float Name="FloatValue"><pValue>FloatRegValue</pValue></Float>
  <FloatReg Name="FloatRegValue"><Address>4</Address><Length>4</Length><AccessMode>RW</AccessMode><Endianess>LittleEndian</Endianess></FloatReg>
  <Integer Name="ReadOnly"><pValue>ReadOnlyReg</pValue></Integer>
  <IntReg Name="ReadOnlyReg"><Address>8</Address><Length>1</Length><AccessMode>RO</AccessMode><Sign>Unsigned</Sign></IntReg>
</RegisterDescription>)xml";

}  // namespace

int main() {
  std::array<std::byte, 16> memory{};
  memory[0] = std::byte{0xfe};
  memory[1] = std::byte{0xff};
  const auto float_bits = std::bit_cast<std::uint32_t>(1.5F);
  std::memcpy(memory.data() + 4, &float_bits, sizeof(float_bits));
  memory[8] = std::byte{7};

  auto read = [&](std::uint64_t address, void* output, std::size_t size) {
    if (address > memory.size() || size > memory.size() - address) {
      return univision::Status{univision::ErrorCode::io_error, "out of range read"};
    }
    std::memcpy(output, memory.data() + address, size);
    return univision::Status::success();
  };
  auto write = [&](std::uint64_t address, const void* input, std::size_t size) {
    if (address > memory.size() || size > memory.size() - address) {
      return univision::Status{univision::ErrorCode::io_error, "out of range write"};
    }
    std::memcpy(memory.data() + address, input, size);
    return univision::Status::success();
  };

  auto parsed = univision::genapi::NodeMap::parse(xml, read, write);
  CHECK(parsed);
  if (!parsed) {
    return 1;
  }
  const auto node_map = parsed.value();
  CHECK(node_map->features().size() == 3);
  CHECK(std::get<std::int64_t>(node_map->read("SignedValue").value()) == -2);
  CHECK(node_map->write("SignedValue", std::int64_t{4}));
  CHECK(memory[0] == std::byte{4});
  CHECK(memory[1] == std::byte{0});
  CHECK(!node_map->write("SignedValue", std::int64_t{3}));
  CHECK(std::get<double>(node_map->read("FloatValue").value()) == 1.5);
  CHECK(node_map->write("FloatValue", 2.25));
  CHECK(std::get<double>(node_map->read("FloatValue").value()) == 2.25);
  CHECK(std::get<std::int64_t>(node_map->read("ReadOnly").value()) == 7);
  CHECK(!node_map->write("ReadOnly", std::int64_t{5}));
  CHECK(!node_map->read("Missing"));

  auto malformed = univision::genapi::NodeMap::parse(
      "<RegisterDescription><Integer></RegisterDescription>", read, write);
  CHECK(!malformed);

  if (failures != 0) {
    std::cerr << failures << " GenApi test check(s) failed\n";
    return 1;
  }
  std::cout << "all UniVision GenApi checks passed\n";
  return 0;
}
