#include "gentl_abi.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#  define UV_FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#  define UV_FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace ga = univision::gentl::abi;

namespace {

constexpr std::size_t image_width = 64;
constexpr std::size_t image_height = 32;
constexpr std::uint64_t pfnc_mono8 = 0x01080001ULL;
constexpr std::uint64_t xml_address = 0x1000;
constexpr std::size_t register_space_size = 0x100;

constexpr std::uint64_t width_address = 0x00;
constexpr std::uint64_t height_address = 0x04;
constexpr std::uint64_t exposure_address = 0x08;
constexpr std::uint64_t gain_address = 0x10;
constexpr std::uint64_t trigger_mode_address = 0x18;
constexpr std::uint64_t reverse_x_address = 0x1c;
constexpr std::uint64_t user_id_address = 0x20;
constexpr std::uint64_t acquisition_start_address = 0x40;
constexpr std::uint64_t acquisition_stop_address = 0x44;
constexpr std::uint64_t trigger_software_address = 0x48;
constexpr std::uint64_t pixel_format_address = 0x50;
constexpr std::uint64_t temperature_address = 0x58;

const std::string genapi_xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<RegisterDescription ModelName="Fake GigE Camera" VendorName="UniVision">
  <Integer Name="Width" NameSpace="Standard">
    <DisplayName>Width</DisplayName><ToolTip>Image width</ToolTip><Unit>px</Unit>
    <pValue>WidthReg</pValue><Min>16</Min><Max>64</Max><Inc>16</Inc>
  </Integer>
  <IntReg Name="WidthReg"><Address>0x00</Address><Length>4</Length>
    <AccessMode>RW</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Integer Name="Height" NameSpace="Standard">
    <DisplayName>Height</DisplayName><Unit>px</Unit>
    <pValue>HeightReg</pValue><Min>16</Min><Max>32</Max><Inc>16</Inc>
  </Integer>
  <IntReg Name="HeightReg"><Address>0x04</Address><Length>4</Length>
    <AccessMode>RW</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Float Name="ExposureTime" NameSpace="Standard">
    <DisplayName>Exposure Time</DisplayName><ToolTip>Sensor exposure</ToolTip><Unit>us</Unit>
    <pValue>ExposureTimeReg</pValue><Min>10</Min><Max>100000</Max><Inc>0.5</Inc>
  </Float>
  <FloatReg Name="ExposureTimeReg"><Address>0x08</Address><Length>8</Length>
    <AccessMode>RW</AccessMode><Endianess>BigEndian</Endianess>
  </FloatReg>
  <Float Name="Gain" NameSpace="Standard">
    <Unit>dB</Unit><pValue>GainReg</pValue><Min>0</Min><Max>24</Max><Inc>0.1</Inc>
  </Float>
  <FloatReg Name="GainReg"><Address>0x10</Address><Length>8</Length>
    <AccessMode>RW</AccessMode><Endianess>BigEndian</Endianess>
  </FloatReg>
  <Enumeration Name="TriggerMode" NameSpace="Standard">
    <pValue>TriggerModeReg</pValue>
    <pEnumEntry>TriggerMode_Off</pEnumEntry><pEnumEntry>TriggerMode_On</pEnumEntry>
  </Enumeration>
  <EnumEntry Name="TriggerMode_Off"><Value>0</Value><Symbolic>Off</Symbolic></EnumEntry>
  <EnumEntry Name="TriggerMode_On"><Value>1</Value><Symbolic>On</Symbolic></EnumEntry>
  <IntReg Name="TriggerModeReg"><Address>0x18</Address><Length>4</Length>
    <AccessMode>RW</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Boolean Name="ReverseX" NameSpace="Standard">
    <pValue>ReverseXReg</pValue><OnValue>1</OnValue><OffValue>0</OffValue>
  </Boolean>
  <IntReg Name="ReverseXReg"><Address>0x1c</Address><Length>4</Length>
    <AccessMode>RW</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <String Name="DeviceUserID" NameSpace="Standard"><pValue>DeviceUserIDReg</pValue></String>
  <StringReg Name="DeviceUserIDReg"><Address>0x20</Address><Length>32</Length>
    <AccessMode>RW</AccessMode>
  </StringReg>
  <Command Name="AcquisitionStart" NameSpace="Standard">
    <pValue>AcquisitionStartReg</pValue><CommandValue>1</CommandValue>
  </Command>
  <IntReg Name="AcquisitionStartReg"><Address>0x40</Address><Length>4</Length>
    <AccessMode>WO</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Command Name="AcquisitionStop" NameSpace="Standard">
    <pValue>AcquisitionStopReg</pValue><CommandValue>1</CommandValue>
  </Command>
  <IntReg Name="AcquisitionStopReg"><Address>0x44</Address><Length>4</Length>
    <AccessMode>WO</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Command Name="TriggerSoftware" NameSpace="Standard">
    <pValue>TriggerSoftwareReg</pValue><CommandValue>1</CommandValue>
  </Command>
  <IntReg Name="TriggerSoftwareReg"><Address>0x48</Address><Length>4</Length>
    <AccessMode>WO</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Enumeration Name="PixelFormat" NameSpace="Standard">
    <pValue>PixelFormatReg</pValue><pEnumEntry>PixelFormat_Mono8</pEnumEntry>
  </Enumeration>
  <EnumEntry Name="PixelFormat_Mono8"><Value>17301505</Value><Symbolic>Mono8</Symbolic></EnumEntry>
  <IntReg Name="PixelFormatReg"><Address>0x50</Address><Length>8</Length>
    <AccessMode>RO</AccessMode><Sign>Unsigned</Sign><Endianess>BigEndian</Endianess>
  </IntReg>
  <Float Name="DeviceTemperature" NameSpace="Standard">
    <Unit>C</Unit><pValue>DeviceTemperatureReg</pValue>
  </Float>
  <FloatReg Name="DeviceTemperatureReg"><Address>0x58</Address><Length>8</Length>
    <AccessMode>RO</AccessMode><Endianess>BigEndian</Endianess>
  </FloatReg>
  <Integer Name="VendorMagic" NameSpace="Custom">
    <DisplayName>Vendor Magic</DisplayName><AccessMode>RO</AccessMode><Value>42</Value>
  </Integer>
</RegisterDescription>)xml";

int tl_token;
int interface_token;
int device_token;
int stream_token;
int port_token;
int event_token;

struct FakeBuffer {
  void* memory{};
  std::size_t size{};
  void* private_data{};
  std::size_t filled{};
  std::uint64_t frame_id{};
  std::uint64_t timestamp_ns{};
  bool incomplete{};
  bool queued{};
};

struct FakeState {
  std::mutex mutex;
  bool initialized{};
  bool running{};
  bool remote_acquiring{};
  bool event_registered{};
  bool event_killed{};
  std::uint64_t next_frame{};
  std::vector<std::unique_ptr<FakeBuffer>> buffers;
  std::deque<FakeBuffer*> queue;
  std::array<std::byte, register_space_size> registers{};
};

FakeState state;
thread_local ga::Error last_error_code = ga::success;
thread_local std::string last_error_text;

ga::Error fail(ga::Error code, std::string message) {
  last_error_code = code;
  last_error_text = std::move(message);
  return code;
}

ga::Error write_string(const std::string& value, ga::InfoDataType* type, void* output,
                       std::size_t* size) {
  if (size == nullptr) {
    return fail(ga::invalid_parameter, "size is null");
  }
  if (type != nullptr) {
    *type = ga::info_string;
  }
  const auto required = value.size() + 1;
  if (output == nullptr || *size < required) {
    *size = required;
    return ga::buffer_too_small;
  }
  std::memcpy(output, value.c_str(), required);
  *size = required;
  return ga::success;
}

template <typename T>
ga::Error write_value(const T& value, ga::InfoDataType type_value,
                      ga::InfoDataType* type, void* output, std::size_t* size) {
  if (size == nullptr) {
    return fail(ga::invalid_parameter, "size is null");
  }
  if (type != nullptr) {
    *type = type_value;
  }
  if (output == nullptr || *size < sizeof(T)) {
    *size = sizeof(T);
    return ga::buffer_too_small;
  }
  std::memcpy(output, &value, sizeof(T));
  *size = sizeof(T);
  return ga::success;
}

template <typename T>
bool is_handle(void* handle, T* token) {
  return handle == static_cast<void*>(token);
}

FakeBuffer* find_buffer(ga::BufferHandle handle) {
  const auto found = std::find_if(state.buffers.begin(), state.buffers.end(),
                                  [&](const auto& item) { return item.get() == handle; });
  return found == state.buffers.end() ? nullptr : found->get();
}

void set_register_uint(std::uint64_t address, std::size_t length, std::uint64_t value) {
  for (std::size_t index = 0; index < length; ++index) {
    state.registers[static_cast<std::size_t>(address) + length - index - 1] =
        static_cast<std::byte>(value & 0xffU);
    value >>= 8U;
  }
}

std::uint64_t register_uint(std::uint64_t address, std::size_t length) {
  std::uint64_t value{};
  for (std::size_t index = 0; index < length; ++index) {
    value = (value << 8U) |
            std::to_integer<unsigned char>(
                state.registers[static_cast<std::size_t>(address) + index]);
  }
  return value;
}

void set_register_double(std::uint64_t address, double value) {
  set_register_uint(address, sizeof(value), std::bit_cast<std::uint64_t>(value));
}

void initialize_registers() {
  state.registers.fill(std::byte{});
  set_register_uint(width_address, 4, image_width);
  set_register_uint(height_address, 4, image_height);
  set_register_double(exposure_address, 1000.0);
  set_register_double(gain_address, 0.0);
  set_register_uint(trigger_mode_address, 4, 0);
  set_register_uint(reverse_x_address, 4, 0);
  constexpr std::string_view user_id = "CI Camera";
  std::copy(user_id.begin(), user_id.end(),
            reinterpret_cast<char*>(state.registers.data() + user_id_address));
  set_register_uint(pixel_format_address, 8, pfnc_mono8);
  set_register_double(temperature_address, 36.5);
}

std::size_t current_width() {
  return static_cast<std::size_t>(register_uint(width_address, 4));
}

std::size_t current_height() {
  return static_cast<std::size_t>(register_uint(height_address, 4));
}

}  // namespace

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCGetInfo(
    ga::InfoCommand command, ga::InfoDataType* type, void* output, std::size_t* size) {
  switch (command) {
    case ga::tl_info_id:
      return write_string("UniVision.FakeGenTL", type, output, size);
    case ga::tl_info_vendor:
      return write_string("UniVision", type, output, size);
    case ga::tl_info_model:
      return write_string("Fake GenTL Producer", type, output, size);
    case ga::tl_info_version:
      return write_string("1.0.0", type, output, size);
    case ga::tl_info_type:
      return write_string("GEV", type, output, size);
    case ga::tl_info_name:
      return write_string("univision_fake.cti", type, output, size);
    case ga::tl_info_pathname:
      return write_string("univision_fake.cti", type, output, size);
    case ga::tl_info_display_name:
      return write_string("UniVision Fake GenTL Producer", type, output, size);
    default:
      return fail(ga::invalid_id, "unsupported TL info command");
  }
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCGetLastError(
    ga::Error* code, char* text, std::size_t* size) {
  if (code == nullptr) {
    return ga::invalid_parameter;
  }
  *code = last_error_code;
  return write_string(last_error_text, nullptr, text, size);
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCInitLib() {
  std::lock_guard lock(state.mutex);
  state.initialized = true;
  state.remote_acquiring = false;
  initialize_registers();
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCCloseLib() {
  std::lock_guard lock(state.mutex);
  state.initialized = false;
  state.running = false;
  state.remote_acquiring = false;
  state.event_registered = false;
  state.event_killed = false;
  state.next_frame = 0;
  state.queue.clear();
  state.buffers.clear();
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCReadPort(
    ga::PortHandle handle, std::uint64_t address, void* output, std::size_t* size) {
  if (!is_handle(handle, &port_token) || output == nullptr || size == nullptr) {
    return fail(ga::invalid_parameter, "invalid fake port read");
  }
  std::lock_guard lock(state.mutex);
  if (address >= xml_address && address - xml_address <= genapi_xml.size() &&
      *size <= genapi_xml.size() - static_cast<std::size_t>(address - xml_address)) {
    std::memcpy(output, genapi_xml.data() + (address - xml_address), *size);
    return ga::success;
  }
  if (address <= state.registers.size() &&
      *size <= state.registers.size() - static_cast<std::size_t>(address)) {
    std::memcpy(output, state.registers.data() + address, *size);
    return ga::success;
  }
  return fail(ga::invalid_parameter, "fake port read is outside mapped memory");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCWritePort(
    ga::PortHandle handle, std::uint64_t address, const void* input,
    std::size_t* size) {
  if (!is_handle(handle, &port_token) || input == nullptr || size == nullptr) {
    return fail(ga::invalid_parameter, "invalid fake port write");
  }
  std::lock_guard lock(state.mutex);
  if (address > state.registers.size() ||
      *size > state.registers.size() - static_cast<std::size_t>(address)) {
    return fail(ga::invalid_parameter, "fake port write is outside mapped memory");
  }
  std::memcpy(state.registers.data() + address, input, *size);
  if (address == acquisition_start_address && *size == 4 &&
      register_uint(address, 4) == 1) {
    state.remote_acquiring = true;
  } else if (address == acquisition_stop_address && *size == 4 &&
             register_uint(address, 4) == 1) {
    state.remote_acquiring = false;
  }
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCGetPortURL(
    ga::PortHandle handle, char* output, std::size_t* size) {
  if (!is_handle(handle, &port_token)) {
    return fail(ga::invalid_handle, "invalid fake port handle");
  }
  return write_string("Local:univision_fake.xml;0x1000;" +
                          std::to_string(genapi_xml.size()),
                      nullptr, output, size);
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCRegisterEvent(
    ga::Handle source, std::int32_t event_type, ga::EventHandle* event) {
  if (!is_handle(source, &stream_token) || event_type != ga::event_new_buffer ||
      event == nullptr) {
    return fail(ga::invalid_parameter, "invalid event registration");
  }
  std::lock_guard lock(state.mutex);
  state.event_registered = true;
  state.event_killed = false;
  *event = &event_token;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCUnregisterEvent(
    ga::Handle source, std::int32_t event_type) {
  if (!is_handle(source, &stream_token) || event_type != ga::event_new_buffer) {
    return fail(ga::invalid_parameter, "invalid event unregistration");
  }
  std::lock_guard lock(state.mutex);
  state.event_registered = false;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL EventGetData(
    ga::EventHandle event, void* output, std::size_t* size, std::uint64_t) {
  if (!is_handle(event, &event_token) || output == nullptr || size == nullptr ||
      *size < sizeof(ga::NewBufferEventData)) {
    return fail(ga::invalid_parameter, "invalid event data output");
  }
  std::lock_guard lock(state.mutex);
  if (state.event_killed) {
    return fail(ga::abort, "event was killed");
  }
  if (!state.running || !state.remote_acquiring || !state.event_registered) {
    return fail(ga::resource_in_use, "acquisition is not running");
  }
  if (state.queue.empty()) {
    return fail(ga::timeout, "no queued buffers");
  }

  auto* buffer = state.queue.front();
  state.queue.pop_front();
  buffer->queued = false;
  buffer->frame_id = ++state.next_frame;
  buffer->filled = std::min(buffer->size, current_width() * current_height());
  auto* bytes = static_cast<std::byte*>(buffer->memory);
  for (std::size_t index = 0; index < buffer->filled; ++index) {
    bytes[index] = static_cast<std::byte>((index + buffer->frame_id) & 0xffU);
  }
  buffer->timestamp_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  const ga::NewBufferEventData data{buffer, buffer->private_data};
  std::memcpy(output, &data, sizeof(data));
  *size = sizeof(data);
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL EventKill(ga::EventHandle event) {
  if (!is_handle(event, &event_token)) {
    return fail(ga::invalid_handle, "invalid event handle");
  }
  std::lock_guard lock(state.mutex);
  state.event_killed = true;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL TLOpen(ga::TLHandle* handle) {
  if (handle == nullptr || !state.initialized) {
    return fail(ga::not_initialized, "producer is not initialized");
  }
  *handle = &tl_token;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL TLClose(ga::TLHandle handle) {
  return is_handle(handle, &tl_token) ? ga::success
                                     : fail(ga::invalid_handle, "invalid TL handle");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL TLGetNumInterfaces(
    ga::TLHandle handle, std::uint32_t* count) {
  if (!is_handle(handle, &tl_token) || count == nullptr) {
    return fail(ga::invalid_parameter, "invalid TL interface count query");
  }
  *count = 1;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL TLGetInterfaceID(
    ga::TLHandle handle, std::uint32_t index, char* output, std::size_t* size) {
  if (!is_handle(handle, &tl_token) || index != 0) {
    return fail(ga::invalid_id, "invalid interface index");
  }
  return write_string("fake-interface-0", nullptr, output, size);
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL TLOpenInterface(
    ga::TLHandle handle, const char* id, ga::IFHandle* output) {
  if (!is_handle(handle, &tl_token) || id == nullptr ||
      std::string{id} != "fake-interface-0" || output == nullptr) {
    return fail(ga::invalid_id, "invalid interface id");
  }
  *output = &interface_token;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL TLUpdateInterfaceList(
    ga::TLHandle handle, ga::Bool8* changed, std::uint64_t) {
  if (!is_handle(handle, &tl_token) || changed == nullptr) {
    return fail(ga::invalid_parameter, "invalid interface list query");
  }
  *changed = 0;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL IFClose(ga::IFHandle handle) {
  return is_handle(handle, &interface_token)
             ? ga::success
             : fail(ga::invalid_handle, "invalid interface handle");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL IFGetNumDevices(
    ga::IFHandle handle, std::uint32_t* count) {
  if (!is_handle(handle, &interface_token) || count == nullptr) {
    return fail(ga::invalid_parameter, "invalid device count query");
  }
  *count = 1;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL IFGetDeviceID(
    ga::IFHandle handle, std::uint32_t index, char* output, std::size_t* size) {
  if (!is_handle(handle, &interface_token) || index != 0) {
    return fail(ga::invalid_id, "invalid device index");
  }
  return write_string("fake-device-0", nullptr, output, size);
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL IFUpdateDeviceList(
    ga::IFHandle handle, ga::Bool8* changed, std::uint64_t) {
  if (!is_handle(handle, &interface_token) || changed == nullptr) {
    return fail(ga::invalid_parameter, "invalid device list query");
  }
  *changed = 0;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL IFGetDeviceInfo(
    ga::IFHandle handle, const char* id, ga::InfoCommand command,
    ga::InfoDataType* type, void* output, std::size_t* size) {
  if (!is_handle(handle, &interface_token) || id == nullptr ||
      std::string{id} != "fake-device-0") {
    return fail(ga::invalid_id, "invalid device id");
  }
  switch (command) {
    case ga::device_info_id:
      return write_string("fake-device-0", type, output, size);
    case ga::device_info_vendor:
      return write_string("UniVision", type, output, size);
    case ga::device_info_model:
      return write_string("Fake GigE Camera", type, output, size);
    case ga::device_info_type:
      return write_string("GEV", type, output, size);
    case ga::device_info_display_name:
      return write_string("UniVision Fake GigE Camera", type, output, size);
    case ga::device_info_user_name:
      return write_string("CI Camera", type, output, size);
    case ga::device_info_serial:
      return write_string("FAKE-0001", type, output, size);
    default:
      return fail(ga::invalid_id, "unsupported device info command");
  }
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL IFOpenDevice(
    ga::IFHandle handle, const char* id, std::int32_t access, ga::DevHandle* output) {
  if (!is_handle(handle, &interface_token) || id == nullptr ||
      std::string{id} != "fake-device-0" || access != ga::device_access_control ||
      output == nullptr) {
    return fail(ga::invalid_parameter, "invalid device open request");
  }
  *output = &device_token;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DevGetPort(
    ga::DevHandle handle, ga::PortHandle* output) {
  if (!is_handle(handle, &device_token) || output == nullptr) {
    return fail(ga::invalid_parameter, "invalid device port query");
  }
  *output = &port_token;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DevGetNumDataStreams(
    ga::DevHandle handle, std::uint32_t* count) {
  if (!is_handle(handle, &device_token) || count == nullptr) {
    return fail(ga::invalid_parameter, "invalid stream count query");
  }
  *count = 1;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DevGetDataStreamID(
    ga::DevHandle handle, std::uint32_t index, char* output, std::size_t* size) {
  if (!is_handle(handle, &device_token) || index != 0) {
    return fail(ga::invalid_id, "invalid stream index");
  }
  return write_string("fake-stream-0", nullptr, output, size);
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DevOpenDataStream(
    ga::DevHandle handle, const char* id, ga::DSHandle* output) {
  if (!is_handle(handle, &device_token) || id == nullptr ||
      std::string{id} != "fake-stream-0" || output == nullptr) {
    return fail(ga::invalid_parameter, "invalid stream open request");
  }
  *output = &stream_token;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DevClose(ga::DevHandle handle) {
  return is_handle(handle, &device_token)
             ? ga::success
             : fail(ga::invalid_handle, "invalid device handle");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSAnnounceBuffer(
    ga::DSHandle handle, void* memory, std::size_t size, void* private_data,
    ga::BufferHandle* output) {
  if (!is_handle(handle, &stream_token) || memory == nullptr || size == 0 ||
      output == nullptr) {
    return fail(ga::invalid_parameter, "invalid buffer announcement");
  }
  std::lock_guard lock(state.mutex);
  auto buffer = std::make_unique<FakeBuffer>();
  buffer->memory = memory;
  buffer->size = size;
  buffer->private_data = private_data;
  *output = buffer.get();
  state.buffers.push_back(std::move(buffer));
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSFlushQueue(
    ga::DSHandle handle, std::int32_t) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  for (auto& buffer : state.buffers) {
    buffer->queued = false;
  }
  state.queue.clear();
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSStartAcquisition(
    ga::DSHandle handle, std::int32_t, std::uint64_t) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  state.running = true;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSStopAcquisition(
    ga::DSHandle handle, std::int32_t) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  state.running = false;
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSGetInfo(
    ga::DSHandle handle, ga::InfoCommand command, ga::InfoDataType* type,
    void* output, std::size_t* size) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  switch (command) {
    case ga::stream_info_payload_size: {
      const auto payload = current_width() * current_height();
      return write_value(payload, ga::info_size, type, output, size);
    }
    case ga::stream_info_min_buffers: {
      const std::size_t minimum = 3;
      return write_value(minimum, ga::info_size, type, output, size);
    }
    case ga::stream_info_num_underrun: {
      const std::uint64_t underruns = 0;
      return write_value(underruns, ga::info_uint64, type, output, size);
    }
    default:
      return fail(ga::invalid_id, "unsupported stream info command");
  }
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSClose(ga::DSHandle handle) {
  return is_handle(handle, &stream_token)
             ? ga::success
             : fail(ga::invalid_handle, "invalid stream handle");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSRevokeBuffer(
    ga::DSHandle handle, ga::BufferHandle buffer_handle, void** memory,
    void** private_data) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  auto* buffer = find_buffer(buffer_handle);
  if (buffer == nullptr) {
    return fail(ga::invalid_buffer, "unknown buffer handle");
  }
  if (memory != nullptr) {
    *memory = buffer->memory;
  }
  if (private_data != nullptr) {
    *private_data = buffer->private_data;
  }
  state.queue.erase(std::remove(state.queue.begin(), state.queue.end(), buffer),
                    state.queue.end());
  state.buffers.erase(
      std::remove_if(state.buffers.begin(), state.buffers.end(),
                     [&](const auto& item) { return item.get() == buffer; }),
      state.buffers.end());
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSQueueBuffer(
    ga::DSHandle handle, ga::BufferHandle buffer_handle) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  auto* buffer = find_buffer(buffer_handle);
  if (buffer == nullptr || buffer->queued) {
    return fail(ga::invalid_buffer, "buffer is unknown or already queued");
  }
  buffer->queued = true;
  buffer->filled = 0;
  state.queue.push_back(buffer);
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL DSGetBufferInfo(
    ga::DSHandle handle, ga::BufferHandle buffer_handle, ga::InfoCommand command,
    ga::InfoDataType* type, void* output, std::size_t* size) {
  if (!is_handle(handle, &stream_token)) {
    return fail(ga::invalid_handle, "invalid stream handle");
  }
  std::lock_guard lock(state.mutex);
  auto* buffer = find_buffer(buffer_handle);
  if (buffer == nullptr) {
    return fail(ga::invalid_buffer, "unknown buffer handle");
  }
  switch (command) {
    case ga::buffer_info_timestamp:
    case ga::buffer_info_timestamp_ns:
      return write_value(buffer->timestamp_ns, ga::info_uint64, type, output, size);
    case ga::buffer_info_incomplete: {
      const ga::Bool8 incomplete = buffer->incomplete ? 1 : 0;
      return write_value(incomplete, ga::info_bool8, type, output, size);
    }
    case ga::buffer_info_size_filled:
      return write_value(buffer->filled, ga::info_size, type, output, size);
    case ga::buffer_info_width: {
      const auto width = current_width();
      return write_value(width, ga::info_size, type, output, size);
    }
    case ga::buffer_info_height:
    case ga::buffer_info_delivered_height: {
      const auto height = current_height();
      return write_value(height, ga::info_size, type, output, size);
    }
    case ga::buffer_info_xpadding: {
      const std::size_t padding = 0;
      return write_value(padding, ga::info_size, type, output, size);
    }
    case ga::buffer_info_frame_id:
      return write_value(buffer->frame_id, ga::info_uint64, type, output, size);
    case ga::buffer_info_image_offset: {
      const std::size_t offset = 0;
      return write_value(offset, ga::info_size, type, output, size);
    }
    case ga::buffer_info_pixel_format:
      return write_value(pfnc_mono8, ga::info_uint64, type, output, size);
    default:
      return fail(ga::invalid_id, "unsupported buffer info command");
  }
}
