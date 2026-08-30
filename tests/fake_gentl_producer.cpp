#include "gentl_abi.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
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
constexpr std::size_t payload_size = image_width * image_height;
constexpr std::uint64_t pfnc_mono8 = 0x01080001ULL;

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
  bool event_registered{};
  bool event_killed{};
  std::uint64_t next_frame{};
  std::vector<std::unique_ptr<FakeBuffer>> buffers;
  std::deque<FakeBuffer*> queue;
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
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCCloseLib() {
  std::lock_guard lock(state.mutex);
  state.initialized = false;
  state.running = false;
  state.event_registered = false;
  state.event_killed = false;
  state.next_frame = 0;
  state.queue.clear();
  state.buffers.clear();
  return ga::success;
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCReadPort(
    ga::PortHandle, std::uint64_t, void*, std::size_t*) {
  return fail(ga::not_implemented, "fake register port is not implemented");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCWritePort(
    ga::PortHandle, std::uint64_t, const void*, std::size_t*) {
  return fail(ga::not_implemented, "fake register port is not implemented");
}

UV_FAKE_EXPORT ga::Error UV_GENTL_CALL GCGetPortURL(
    ga::PortHandle, char*, std::size_t*) {
  return fail(ga::not_available, "fake device has no GenApi XML");
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
  if (!state.running || !state.event_registered) {
    return fail(ga::resource_in_use, "acquisition is not running");
  }
  if (state.queue.empty()) {
    return fail(ga::timeout, "no queued buffers");
  }

  auto* buffer = state.queue.front();
  state.queue.pop_front();
  buffer->queued = false;
  buffer->frame_id = ++state.next_frame;
  buffer->filled = std::min(buffer->size, payload_size);
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
    case ga::stream_info_payload_size:
      return write_value(payload_size, ga::info_size, type, output, size);
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
    case ga::buffer_info_width:
      return write_value(image_width, ga::info_size, type, output, size);
    case ga::buffer_info_height:
    case ga::buffer_info_delivered_height:
      return write_value(image_height, ga::info_size, type, output, size);
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
