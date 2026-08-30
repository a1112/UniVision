#include "univision/gentl.h"

#include "gentl/gentl_abi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace univision {
namespace {

namespace ga = gentl::abi;

class DynamicLibrary {
 public:
  ~DynamicLibrary() {
#if defined(_WIN32)
    if (handle_ != nullptr) {
      FreeLibrary(handle_);
    }
#else
    if (handle_ != nullptr) {
      dlclose(handle_);
    }
#endif
  }

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;

  static Result<std::shared_ptr<DynamicLibrary>> open(
      const std::filesystem::path& path) {
    if (path.empty()) {
      return Status{ErrorCode::invalid_argument, "GenTL Producer path is empty"};
    }

#if defined(_WIN32)
    auto handle = LoadLibraryW(path.wstring().c_str());
    if (handle == nullptr) {
      return Status{ErrorCode::io_error,
                    "failed to load GenTL Producer: " + path.string() +
                        " (Windows error " + std::to_string(GetLastError()) + ")"};
    }
#else
    auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
      const char* detail = dlerror();
      return Status{ErrorCode::io_error,
                    "failed to load GenTL Producer: " + path.string() +
                        (detail == nullptr ? std::string{} : " (" + std::string(detail) + ")")};
    }
#endif
    return std::shared_ptr<DynamicLibrary>(new DynamicLibrary(handle));
  }

  [[nodiscard]] void* symbol(const char* name) const noexcept {
#if defined(_WIN32)
    auto function = GetProcAddress(handle_, name);
    void* address{};
    static_assert(sizeof(address) == sizeof(function));
    std::memcpy(&address, &function, sizeof(address));
    return address;
#else
    return dlsym(handle_, name);
#endif
  }

 private:
#if defined(_WIN32)
  using NativeHandle = HMODULE;
#else
  using NativeHandle = void*;
#endif

  explicit DynamicLibrary(NativeHandle handle) : handle_(handle) {}
  NativeHandle handle_{};
};

struct GenTLApi {
  ga::GCGetInfoFn gc_get_info{};
  ga::GCGetLastErrorFn gc_get_last_error{};
  ga::GCInitLibFn gc_init_lib{};
  ga::GCCloseLibFn gc_close_lib{};
  ga::GCReadPortFn gc_read_port{};
  ga::GCWritePortFn gc_write_port{};
  ga::GCGetPortURLFn gc_get_port_url{};
  ga::GCRegisterEventFn gc_register_event{};
  ga::GCUnregisterEventFn gc_unregister_event{};
  ga::EventGetDataFn event_get_data{};
  ga::EventKillFn event_kill{};
  ga::TLOpenFn tl_open{};
  ga::TLCloseFn tl_close{};
  ga::TLGetNumInterfacesFn tl_get_num_interfaces{};
  ga::TLGetInterfaceIDFn tl_get_interface_id{};
  ga::TLOpenInterfaceFn tl_open_interface{};
  ga::TLUpdateInterfaceListFn tl_update_interface_list{};
  ga::IFCloseFn if_close{};
  ga::IFGetNumDevicesFn if_get_num_devices{};
  ga::IFGetDeviceIDFn if_get_device_id{};
  ga::IFUpdateDeviceListFn if_update_device_list{};
  ga::IFGetDeviceInfoFn if_get_device_info{};
  ga::IFOpenDeviceFn if_open_device{};
  ga::DevGetPortFn dev_get_port{};
  ga::DevGetNumDataStreamsFn dev_get_num_data_streams{};
  ga::DevGetDataStreamIDFn dev_get_data_stream_id{};
  ga::DevOpenDataStreamFn dev_open_data_stream{};
  ga::DevCloseFn dev_close{};
  ga::DSAnnounceBufferFn ds_announce_buffer{};
  ga::DSFlushQueueFn ds_flush_queue{};
  ga::DSStartAcquisitionFn ds_start_acquisition{};
  ga::DSStopAcquisitionFn ds_stop_acquisition{};
  ga::DSGetInfoFn ds_get_info{};
  ga::DSCloseFn ds_close{};
  ga::DSRevokeBufferFn ds_revoke_buffer{};
  ga::DSQueueBufferFn ds_queue_buffer{};
  ga::DSGetBufferInfoFn ds_get_buffer_info{};

  static Result<GenTLApi> load(const DynamicLibrary& library) {
    GenTLApi api;
#define UV_LOAD_GENTL(member, symbol_name)                                      \
  do {                                                                          \
    void* address = library.symbol(symbol_name);                                 \
    if (address == nullptr) {                                                    \
      return Status{ErrorCode::unsupported,                                     \
                    std::string("GenTL Producer is missing required symbol: ") + \
                        symbol_name};                                            \
    }                                                                           \
    static_assert(sizeof(api.member) == sizeof(address));                        \
    std::memcpy(&api.member, &address, sizeof(address));                         \
  } while (false)

    UV_LOAD_GENTL(gc_get_info, "GCGetInfo");
    UV_LOAD_GENTL(gc_get_last_error, "GCGetLastError");
    UV_LOAD_GENTL(gc_init_lib, "GCInitLib");
    UV_LOAD_GENTL(gc_close_lib, "GCCloseLib");
    UV_LOAD_GENTL(gc_read_port, "GCReadPort");
    UV_LOAD_GENTL(gc_write_port, "GCWritePort");
    UV_LOAD_GENTL(gc_get_port_url, "GCGetPortURL");
    UV_LOAD_GENTL(gc_register_event, "GCRegisterEvent");
    UV_LOAD_GENTL(gc_unregister_event, "GCUnregisterEvent");
    UV_LOAD_GENTL(event_get_data, "EventGetData");
    UV_LOAD_GENTL(event_kill, "EventKill");
    UV_LOAD_GENTL(tl_open, "TLOpen");
    UV_LOAD_GENTL(tl_close, "TLClose");
    UV_LOAD_GENTL(tl_get_num_interfaces, "TLGetNumInterfaces");
    UV_LOAD_GENTL(tl_get_interface_id, "TLGetInterfaceID");
    UV_LOAD_GENTL(tl_open_interface, "TLOpenInterface");
    UV_LOAD_GENTL(tl_update_interface_list, "TLUpdateInterfaceList");
    UV_LOAD_GENTL(if_close, "IFClose");
    UV_LOAD_GENTL(if_get_num_devices, "IFGetNumDevices");
    UV_LOAD_GENTL(if_get_device_id, "IFGetDeviceID");
    UV_LOAD_GENTL(if_update_device_list, "IFUpdateDeviceList");
    UV_LOAD_GENTL(if_get_device_info, "IFGetDeviceInfo");
    UV_LOAD_GENTL(if_open_device, "IFOpenDevice");
    UV_LOAD_GENTL(dev_get_port, "DevGetPort");
    UV_LOAD_GENTL(dev_get_num_data_streams, "DevGetNumDataStreams");
    UV_LOAD_GENTL(dev_get_data_stream_id, "DevGetDataStreamID");
    UV_LOAD_GENTL(dev_open_data_stream, "DevOpenDataStream");
    UV_LOAD_GENTL(dev_close, "DevClose");
    UV_LOAD_GENTL(ds_announce_buffer, "DSAnnounceBuffer");
    UV_LOAD_GENTL(ds_flush_queue, "DSFlushQueue");
    UV_LOAD_GENTL(ds_start_acquisition, "DSStartAcquisition");
    UV_LOAD_GENTL(ds_stop_acquisition, "DSStopAcquisition");
    UV_LOAD_GENTL(ds_get_info, "DSGetInfo");
    UV_LOAD_GENTL(ds_close, "DSClose");
    UV_LOAD_GENTL(ds_revoke_buffer, "DSRevokeBuffer");
    UV_LOAD_GENTL(ds_queue_buffer, "DSQueueBuffer");
    UV_LOAD_GENTL(ds_get_buffer_info, "DSGetBufferInfo");
#undef UV_LOAD_GENTL
    return api;
  }
};

ErrorCode map_error(ga::Error error) {
  switch (error) {
    case ga::success:
      return ErrorCode::ok;
    case ga::access_denied:
      return ErrorCode::access_denied;
    case ga::invalid_id:
      return ErrorCode::not_found;
    case ga::invalid_parameter:
      return ErrorCode::invalid_argument;
    case ga::io_error:
      return ErrorCode::io_error;
    case ga::timeout:
      return ErrorCode::timeout;
    case ga::invalid_buffer:
    case ga::resource_exhausted:
    case ga::out_of_memory:
      return ErrorCode::buffer_exhausted;
    case ga::not_implemented:
    case ga::not_available:
      return ErrorCode::unsupported;
    default:
      return ErrorCode::adapter_failure;
  }
}

std::uint64_t milliseconds_u64(std::chrono::milliseconds value) {
  return value.count() <= 0 ? 0U : static_cast<std::uint64_t>(value.count());
}

std::string normalized_path(const std::filesystem::path& path) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  return (error ? path : absolute).lexically_normal().string();
}

std::string fnv_hex(const std::string& value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : value) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ULL;
  }
  std::ostringstream stream;
  stream << std::hex << std::setw(16) << std::setfill('0') << hash;
  return stream.str();
}

TransportType transport_from_name(const std::string& name) {
  if (name == "GEV") {
    return TransportType::gige_vision;
  }
  if (name == "U3V") {
    return TransportType::usb3_vision;
  }
  if (name == "CXP") {
    return TransportType::coaxpress;
  }
  if (name == "CL" || name == "CLHS") {
    return TransportType::camera_link;
  }
  return TransportType::unknown;
}

class Producer : public std::enable_shared_from_this<Producer> {
 public:
  ~Producer() {
    if (tl_ != nullptr) {
      static_cast<void>(api_.tl_close(tl_));
    }
    if (initialized_) {
      static_cast<void>(api_.gc_close_lib());
    }
  }

  static Result<std::shared_ptr<Producer>> create(const GenTLAdapterOptions& options) {
    auto library_result = DynamicLibrary::open(options.cti_path);
    if (!library_result) {
      return library_result.status();
    }
    auto api_result = GenTLApi::load(*library_result.value());
    if (!api_result) {
      return api_result.status();
    }

    auto producer = std::shared_ptr<Producer>(new Producer(
        std::move(library_result).value(), std::move(api_result).value(), options));
    auto error = producer->api_.gc_init_lib();
    if (error != ga::success) {
      return producer->status(error, "GCInitLib");
    }
    producer->initialized_ = true;
    error = producer->api_.tl_open(&producer->tl_);
    if (error != ga::success) {
      return producer->status(error, "TLOpen");
    }

    producer->vendor_ = producer->tl_string(ga::tl_info_vendor).value_or("Unknown");
    producer->model_ = producer->tl_string(ga::tl_info_model).value_or("GenTL Producer");
    producer->version_ = producer->tl_string(ga::tl_info_version).value_or("unknown");
    producer->type_ = producer->tl_string(ga::tl_info_type).value_or("Unknown");
    return producer;
  }

  [[nodiscard]] Status status(ga::Error error, const std::string& operation) const {
    std::string detail;
    std::size_t size = 0;
    ga::Error last_code = error;
    auto query = api_.gc_get_last_error(&last_code, nullptr, &size);
    if ((query == ga::success || query == ga::buffer_too_small) && size > 0) {
      std::vector<char> buffer(size, '\0');
      if (api_.gc_get_last_error(&last_code, buffer.data(), &size) == ga::success) {
        detail.assign(buffer.data());
      }
    }
    std::string message = operation + " failed with GenTL error " + std::to_string(error);
    if (!detail.empty()) {
      message += ": " + detail;
    }
    return {map_error(error), std::move(message)};
  }

  template <typename Query>
  [[nodiscard]] Result<std::string> query_string(Query&& query,
                                                 const std::string& operation) const {
    std::size_t size = 0;
    ga::InfoDataType type{};
    auto error = query(&type, nullptr, &size);
    if (error != ga::success && error != ga::buffer_too_small) {
      return status(error, operation);
    }
    if (size == 0) {
      return std::string{};
    }
    std::vector<char> buffer(size, '\0');
    error = query(&type, buffer.data(), &size);
    if (error != ga::success) {
      return status(error, operation);
    }
    if (type != ga::info_string) {
      return Status{ErrorCode::adapter_failure, operation + " did not return a string"};
    }
    return std::string(buffer.data());
  }

  [[nodiscard]] std::optional<std::string> tl_string(ga::InfoCommand command) const {
    auto value = query_string(
        [&](ga::InfoDataType* type, void* buffer, std::size_t* size) {
          return api_.gc_get_info(command, type, buffer, size);
        },
        "GCGetInfo");
    return value ? std::optional<std::string>{std::move(value).value()} : std::nullopt;
  }

  [[nodiscard]] Result<std::string> interface_id(std::uint32_t index) const {
    return query_string(
        [&](ga::InfoDataType* type, void* buffer, std::size_t* size) {
          if (type != nullptr) {
            *type = ga::info_string;
          }
          return api_.tl_get_interface_id(tl_, index, static_cast<char*>(buffer), size);
        },
        "TLGetInterfaceID");
  }

  [[nodiscard]] Result<std::string> device_id(ga::IFHandle interface,
                                              std::uint32_t index) const {
    return query_string(
        [&](ga::InfoDataType* type, void* buffer, std::size_t* size) {
          if (type != nullptr) {
            *type = ga::info_string;
          }
          return api_.if_get_device_id(interface, index, static_cast<char*>(buffer), size);
        },
        "IFGetDeviceID");
  }

  [[nodiscard]] std::string device_string(ga::IFHandle interface,
                                          const std::string& device,
                                          ga::InfoCommand command) const {
    auto value = query_string(
        [&](ga::InfoDataType* type, void* buffer, std::size_t* size) {
          return api_.if_get_device_info(interface, device.c_str(), command, type, buffer,
                                         size);
        },
        "IFGetDeviceInfo");
    return value ? std::move(value).value() : std::string{};
  }

  std::shared_ptr<DynamicLibrary> library_;
  GenTLApi api_;
  GenTLAdapterOptions options_;
  ga::TLHandle tl_{};
  bool initialized_{};
  std::string vendor_;
  std::string model_;
  std::string version_;
  std::string type_;
  mutable std::mutex module_mutex_;

 private:
  Producer(std::shared_ptr<DynamicLibrary> library, GenTLApi api,
           GenTLAdapterOptions options)
      : library_(std::move(library)), api_(api), options_(std::move(options)) {}
};

struct DeviceRoute {
  std::string interface_id;
  std::string device_id;
};

class DeviceSession {
 public:
  ~DeviceSession() {
    if (device != nullptr) {
      static_cast<void>(producer->api_.dev_close(device));
    }
    if (interface != nullptr) {
      static_cast<void>(producer->api_.if_close(interface));
    }
  }

  std::shared_ptr<Producer> producer;
  ga::IFHandle interface{};
  ga::DevHandle device{};
  ga::PortHandle remote_port{};
  std::string interface_id;
  std::string device_id;
  std::shared_ptr<std::atomic<CameraState>> camera_state;
};

class GenTLDataStreamState : public std::enable_shared_from_this<GenTLDataStreamState> {
 public:
  ~GenTLDataStreamState() { shutdown(); }

  static Result<std::shared_ptr<GenTLDataStreamState>> create(
      std::shared_ptr<DeviceSession> session, const StreamConfiguration& configuration) {
    if (configuration.buffer_count == 0 || configuration.queue_capacity == 0) {
      return Status{ErrorCode::invalid_argument,
                    "stream buffer and queue sizes must be greater than zero"};
    }

    auto state = std::shared_ptr<GenTLDataStreamState>(
        new GenTLDataStreamState(std::move(session), configuration));
    auto& api = state->session_->producer->api_;
    std::uint32_t count = 0;
    auto error = api.dev_get_num_data_streams(state->session_->device, &count);
    if (error != ga::success) {
      return state->session_->producer->status(error, "DevGetNumDataStreams");
    }
    if (count == 0) {
      return Status{ErrorCode::not_found, "GenTL device has no data streams"};
    }

    auto stream_id = state->session_->producer->query_string(
        [&](ga::InfoDataType* type, void* buffer, std::size_t* size) {
          if (type != nullptr) {
            *type = ga::info_string;
          }
          return api.dev_get_data_stream_id(state->session_->device, 0,
                                            static_cast<char*>(buffer), size);
        },
        "DevGetDataStreamID");
    if (!stream_id) {
      return stream_id.status();
    }
    error = api.dev_open_data_stream(state->session_->device, stream_id.value().c_str(),
                                     &state->stream_);
    if (error != ga::success) {
      return state->session_->producer->status(error, "DevOpenDataStream");
    }

    auto payload = state->stream_info<std::size_t>(ga::stream_info_payload_size);
    if (!payload || *payload == 0) {
      return Status{ErrorCode::unsupported,
                    "GenTL Producer did not report a valid stream payload size"};
    }
    auto minimum = state->stream_info<std::size_t>(ga::stream_info_min_buffers).value_or(1);
    const auto buffer_count =
        std::max({configuration.buffer_count, configuration.queue_capacity, minimum});
    state->slots_.reserve(buffer_count);
    for (std::size_t index = 0; index < buffer_count; ++index) {
      state->slots_.push_back(BufferSlot{});
      auto& slot = state->slots_.back();
      slot.storage.resize(*payload);
      error = api.ds_announce_buffer(state->stream_, slot.storage.data(), slot.storage.size(),
                                     nullptr, &slot.handle);
      if (error != ga::success) {
        return state->session_->producer->status(error, "DSAnnounceBuffer");
      }
      state->slot_index_.emplace(slot.handle, index);
    }
    return state;
  }

  Status start() {
    std::lock_guard lock(control_mutex_);
    if (running_.load()) {
      return {ErrorCode::invalid_state, "stream is already running"};
    }
    if (session_->camera_state->load() != CameraState::open) {
      return {ErrorCode::invalid_state, "camera must be open before streaming"};
    }

    auto& api = session_->producer->api_;
    auto error = api.gc_register_event(stream_, ga::event_new_buffer, &event_);
    if (error != ga::success) {
      return session_->producer->status(error, "GCRegisterEvent(EVENT_NEW_BUFFER)");
    }
    {
      std::lock_guard lease_lock(lease_mutex_);
      for (auto& slot : slots_) {
        if (slot.leased) {
          continue;
        }
        error = api.ds_queue_buffer(stream_, slot.handle);
        if (error != ga::success) {
          cleanup_event();
          static_cast<void>(
              api.ds_flush_queue(stream_, ga::acquisition_queue_all_discard));
          return session_->producer->status(error, "DSQueueBuffer");
        }
      }
      error =
          api.ds_start_acquisition(stream_, ga::acquisition_start_default, ga::infinite);
      if (error != ga::success) {
        cleanup_event();
        static_cast<void>(
            api.ds_flush_queue(stream_, ga::acquisition_queue_all_discard));
        return session_->producer->status(error, "DSStartAcquisition");
      }
      running_.store(true);
    }
    session_->camera_state->store(CameraState::streaming);
    return Status::success();
  }

  Status stop() {
    std::lock_guard lock(control_mutex_);
    std::lock_guard lease_lock(lease_mutex_);
    if (!running_.exchange(false)) {
      return Status::success();
    }

    auto& api = session_->producer->api_;
    Status result = Status::success();
    const auto stop_error = api.ds_stop_acquisition(stream_, ga::acquisition_stop_default);
    if (stop_error != ga::success && stop_error != ga::resource_in_use) {
      result = session_->producer->status(stop_error, "DSStopAcquisition");
    }
    if (event_ != nullptr) {
      static_cast<void>(api.event_kill(event_));
      cleanup_event();
    }
    static_cast<void>(api.ds_flush_queue(stream_, ga::acquisition_queue_all_discard));
    session_->camera_state->store(CameraState::open);
    return result;
  }

  [[nodiscard]] Result<Frame> next(std::chrono::milliseconds timeout_value) {
    std::lock_guard wait_lock(wait_mutex_);
    if (!running_.load()) {
      return Status{ErrorCode::invalid_state, "stream is not running"};
    }

    ga::NewBufferEventData event_data{};
    std::size_t event_size = sizeof(event_data);
    auto error = session_->producer->api_.event_get_data(
        event_, &event_data, &event_size, milliseconds_u64(timeout_value));
    if (error != ga::success) {
      if (error == ga::abort && !running_.load()) {
        return Status{ErrorCode::invalid_state, "stream was stopped"};
      }
      return session_->producer->status(error, "EventGetData(EVENT_NEW_BUFFER)");
    }
    const auto found = slot_index_.find(event_data.buffer_handle);
    if (found == slot_index_.end()) {
      return Status{ErrorCode::adapter_failure,
                    "GenTL Producer returned an unknown buffer handle"};
    }
    auto& slot = slots_[found->second];
    {
      std::lock_guard lease_lock(lease_mutex_);
      if (slot.leased) {
        return Status{ErrorCode::adapter_failure,
                      "GenTL Producer delivered a buffer that is still leased"};
      }
      slot.leased = true;
    }

    const auto filled = buffer_info<std::size_t>(slot.handle, ga::buffer_info_size_filled)
                            .value_or(slot.storage.size());
    const auto offset =
        buffer_info<std::size_t>(slot.handle, ga::buffer_info_image_offset).value_or(0);
    if (filled > slot.storage.size() || offset > filled) {
      release(slot.handle);
      return Status{ErrorCode::adapter_failure,
                    "GenTL Producer reported buffer bounds outside announced memory"};
    }

    Frame frame;
    frame.descriptor.width = static_cast<std::uint32_t>(
        buffer_info<std::size_t>(slot.handle, ga::buffer_info_width).value_or(0));
    frame.descriptor.height = static_cast<std::uint32_t>(
        buffer_info<std::size_t>(slot.handle, ga::buffer_info_delivered_height)
            .value_or(buffer_info<std::size_t>(slot.handle, ga::buffer_info_height)
                          .value_or(0)));
    const auto data_size = filled - offset;
    frame.descriptor.stride = frame.descriptor.height == 0
                                  ? data_size
                                  : (data_size + frame.descriptor.height - 1) /
                                        frame.descriptor.height;
    frame.descriptor.pixel_format =
        buffer_info<std::uint64_t>(slot.handle, ga::buffer_info_pixel_format).value_or(0);
    frame.descriptor.memory_type = MemoryType::host;
    frame.descriptor.frame_id =
        buffer_info<std::uint64_t>(slot.handle, ga::buffer_info_frame_id).value_or(0);
    frame.descriptor.camera_timestamp_ns =
        buffer_info<std::uint64_t>(slot.handle, ga::buffer_info_timestamp_ns)
            .value_or(buffer_info<std::uint64_t>(slot.handle, ga::buffer_info_timestamp)
                          .value_or(0));
    frame.descriptor.host_timestamp = std::chrono::steady_clock::now();
    frame.descriptor.complete =
        buffer_info<ga::Bool8>(slot.handle, ga::buffer_info_incomplete).value_or(0) == 0;

    auto self = shared_from_this();
    std::shared_ptr<void> lease(nullptr, [self, handle = slot.handle](void*) {
      self->release(handle);
    });
    frame.buffer =
        FrameBuffer{std::move(lease), slot.storage.data() + offset, data_size};
    frame.metadata.emplace("GenTLInterfaceID", session_->interface_id);
    frame.metadata.emplace("GenTLDeviceID", session_->device_id);

    {
      std::lock_guard stats_lock(statistics_mutex_);
      ++statistics_.frames_received;
      ++statistics_.frames_delivered;
      if (!frame.descriptor.complete) {
        ++statistics_.incomplete_frames;
      }
    }
    return frame;
  }

  [[nodiscard]] bool running() const noexcept { return running_.load(); }

  [[nodiscard]] StreamStatistics statistics() const noexcept {
    StreamStatistics result;
    {
      std::lock_guard lock(statistics_mutex_);
      result = statistics_;
    }
    if (auto underrun = stream_info<std::uint64_t>(ga::stream_info_num_underrun)) {
      result.frames_dropped = *underrun;
    }
    {
      std::lock_guard lease_lock(lease_mutex_);
      result.queue_depth = static_cast<std::size_t>(std::count_if(
          slots_.begin(), slots_.end(), [](const BufferSlot& slot) { return !slot.leased; }));
    }
    return result;
  }

 private:
  struct BufferSlot {
    std::vector<std::byte> storage;
    ga::BufferHandle handle{};
    bool leased{};
  };

  GenTLDataStreamState(std::shared_ptr<DeviceSession> session,
                       StreamConfiguration configuration)
      : session_(std::move(session)), configuration_(configuration) {}

  template <typename T>
  [[nodiscard]] std::optional<T> stream_info(ga::InfoCommand command) const {
    if (stream_ == nullptr) {
      return std::nullopt;
    }
    T value{};
    std::size_t size = sizeof(value);
    ga::InfoDataType type{};
    if (session_->producer->api_.ds_get_info(stream_, command, &type, &value, &size) !=
            ga::success ||
        size != sizeof(value)) {
      return std::nullopt;
    }
    return value;
  }

  template <typename T>
  [[nodiscard]] std::optional<T> buffer_info(ga::BufferHandle buffer,
                                              ga::InfoCommand command) const {
    T value{};
    std::size_t size = sizeof(value);
    ga::InfoDataType type{};
    if (session_->producer->api_.ds_get_buffer_info(stream_, buffer, command, &type,
                                                    &value, &size) != ga::success ||
        size != sizeof(value)) {
      return std::nullopt;
    }
    return value;
  }

  void release(ga::BufferHandle handle) noexcept {
    const auto found = slot_index_.find(handle);
    if (found == slot_index_.end()) {
      return;
    }
    std::lock_guard lock(lease_mutex_);
    slots_[found->second].leased = false;
    if (running_.load()) {
      if (session_->producer->api_.ds_queue_buffer(stream_, handle) != ga::success) {
        std::lock_guard stats_lock(statistics_mutex_);
        ++statistics_.frames_dropped;
      }
    }
  }

  void cleanup_event() noexcept {
    if (event_ != nullptr) {
      static_cast<void>(session_->producer->api_.gc_unregister_event(
          stream_, ga::event_new_buffer));
      event_ = nullptr;
    }
  }

  void shutdown() noexcept {
    static_cast<void>(stop());
    if (stream_ == nullptr) {
      return;
    }
    auto& api = session_->producer->api_;
    static_cast<void>(api.ds_flush_queue(stream_, ga::acquisition_queue_all_discard));
    for (auto& slot : slots_) {
      if (slot.handle != nullptr) {
        static_cast<void>(api.ds_revoke_buffer(stream_, slot.handle, nullptr, nullptr));
        slot.handle = nullptr;
      }
    }
    static_cast<void>(api.ds_close(stream_));
    stream_ = nullptr;
  }

  std::shared_ptr<DeviceSession> session_;
  StreamConfiguration configuration_;
  ga::DSHandle stream_{};
  ga::EventHandle event_{};
  std::vector<BufferSlot> slots_;
  std::unordered_map<ga::BufferHandle, std::size_t> slot_index_;
  std::atomic<bool> running_{false};
  mutable std::mutex control_mutex_;
  mutable std::mutex wait_mutex_;
  mutable std::mutex lease_mutex_;
  mutable std::mutex statistics_mutex_;
  StreamStatistics statistics_{};
};

class GenTLStream final : public Stream {
 public:
  explicit GenTLStream(std::shared_ptr<GenTLDataStreamState> state)
      : state_(std::move(state)) {}
  ~GenTLStream() override { static_cast<void>(state_->stop()); }

  Status start() override { return state_->start(); }
  Status stop() override { return state_->stop(); }
  [[nodiscard]] bool running() const noexcept override { return state_->running(); }
  [[nodiscard]] Result<Frame> wait_next(std::chrono::milliseconds timeout) override {
    return state_->next(timeout);
  }
  [[nodiscard]] StreamStatistics statistics() const noexcept override {
    return state_->statistics();
  }

 private:
  std::shared_ptr<GenTLDataStreamState> state_;
};

class GenTLCamera final : public Camera {
 public:
  GenTLCamera(DeviceInfo info, DeviceRoute route, std::shared_ptr<Producer> producer)
      : info_(std::move(info)),
        route_(std::move(route)),
        producer_(std::move(producer)),
        state_(std::make_shared<std::atomic<CameraState>>(CameraState::closed)) {}

  ~GenTLCamera() override { static_cast<void>(close()); }

  [[nodiscard]] const DeviceInfo& device_info() const noexcept override { return info_; }
  [[nodiscard]] CameraState state() const noexcept override { return state_->load(); }

  Status open() override {
    std::lock_guard lock(mutex_);
    if (state_->load() != CameraState::closed) {
      return {ErrorCode::invalid_state, "camera is not closed"};
    }
    state_->store(CameraState::opening);
    auto session = std::make_shared<DeviceSession>();
    session->producer = producer_;
    session->interface_id = route_.interface_id;
    session->device_id = route_.device_id;
    session->camera_state = state_;

    auto error = producer_->api_.tl_open_interface(producer_->tl_, route_.interface_id.c_str(),
                                                   &session->interface);
    if (error != ga::success) {
      state_->store(CameraState::failed);
      return producer_->status(error, "TLOpenInterface");
    }
    error = producer_->api_.if_open_device(session->interface, route_.device_id.c_str(),
                                           ga::device_access_control, &session->device);
    if (error != ga::success) {
      state_->store(CameraState::failed);
      return producer_->status(error, "IFOpenDevice");
    }
    error = producer_->api_.dev_get_port(session->device, &session->remote_port);
    if (error != ga::success && error != ga::not_implemented && error != ga::not_available) {
      state_->store(CameraState::failed);
      return producer_->status(error, "DevGetPort");
    }
    session_ = std::move(session);
    state_->store(CameraState::open);
    return Status::success();
  }

  Status close() override {
    std::lock_guard lock(mutex_);
    if (state_->load() == CameraState::closed) {
      return Status::success();
    }
    if (state_->load() == CameraState::streaming ||
        (session_ != nullptr && session_.use_count() > 1)) {
      return {ErrorCode::invalid_state,
              "destroy all GenTL streams before closing the camera"};
    }
    session_.reset();
    state_->store(CameraState::closed);
    return Status::success();
  }

  Status reconnect() override {
    {
      std::lock_guard lock(mutex_);
      if (state_->load() == CameraState::streaming ||
          (session_ != nullptr && session_.use_count() > 1)) {
        return {ErrorCode::invalid_state,
                "destroy all GenTL streams before reconnecting the camera"};
      }
      session_.reset();
      state_->store(CameraState::closed);
    }
    return open();
  }

  [[nodiscard]] std::vector<FeatureInfo> features() const override { return {}; }

  [[nodiscard]] Result<FeatureInfo> feature_info(const std::string&) const override {
    return feature_unavailable();
  }
  [[nodiscard]] Result<FeatureValue> read_feature(const std::string&) const override {
    return feature_unavailable();
  }
  Status write_feature(const std::string&, const FeatureValue&) override {
    return feature_unavailable();
  }
  Status execute_command(const std::string&) override { return feature_unavailable(); }

  [[nodiscard]] Result<std::unique_ptr<Stream>> create_stream(
      const StreamConfiguration& configuration) override {
    std::lock_guard lock(mutex_);
    if (state_->load() != CameraState::open || session_ == nullptr) {
      return Status{ErrorCode::invalid_state, "camera must be open before creating a stream"};
    }
    auto state = GenTLDataStreamState::create(session_, configuration);
    if (!state) {
      return state.status();
    }
    std::unique_ptr<Stream> stream =
        std::make_unique<GenTLStream>(std::move(state).value());
    return stream;
  }

 private:
  [[nodiscard]] static Status feature_unavailable() {
    return {ErrorCode::unsupported,
            "GenApi NodeMap support is not enabled in this GenTL milestone"};
  }

  DeviceInfo info_;
  DeviceRoute route_;
  std::shared_ptr<Producer> producer_;
  std::shared_ptr<std::atomic<CameraState>> state_;
  std::shared_ptr<DeviceSession> session_;
  mutable std::mutex mutex_;
};

class GenTLAdapter final : public Adapter {
 public:
  explicit GenTLAdapter(std::shared_ptr<Producer> producer)
      : producer_(std::move(producer)) {
    const auto path = normalized_path(producer_->options_.cti_path);
    descriptor_.id = "gentl:" + fnv_hex(path);
    descriptor_.name = producer_->vendor_ + " " + producer_->model_;
    descriptor_.version = producer_->version_;
    descriptor_.vendor = producer_->vendor_;
    descriptor_.priority = producer_->options_.priority;
  }

  [[nodiscard]] const AdapterDescriptor& descriptor() const noexcept override {
    return descriptor_;
  }

  [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate_devices() override {
    std::lock_guard module_lock(producer_->module_mutex_);
    auto& api = producer_->api_;
    ga::Bool8 changed{};
    auto error = api.tl_update_interface_list(
        producer_->tl_, &changed, milliseconds_u64(producer_->options_.discovery_timeout));
    if (error != ga::success) {
      return producer_->status(error, "TLUpdateInterfaceList");
    }

    std::uint32_t interface_count = 0;
    error = api.tl_get_num_interfaces(producer_->tl_, &interface_count);
    if (error != ga::success) {
      return producer_->status(error, "TLGetNumInterfaces");
    }

    std::vector<DeviceInfo> devices;
    std::unordered_map<std::string, DeviceRoute> routes;
    for (std::uint32_t interface_index = 0; interface_index < interface_count;
         ++interface_index) {
      auto interface_id = producer_->interface_id(interface_index);
      if (!interface_id) {
        return interface_id.status();
      }
      ga::IFHandle interface{};
      error = api.tl_open_interface(producer_->tl_, interface_id.value().c_str(), &interface);
      if (error != ga::success) {
        return producer_->status(error, "TLOpenInterface");
      }

      error = api.if_update_device_list(
          interface, &changed, milliseconds_u64(producer_->options_.discovery_timeout));
      if (error != ga::success) {
        static_cast<void>(api.if_close(interface));
        return producer_->status(error, "IFUpdateDeviceList");
      }
      std::uint32_t device_count = 0;
      error = api.if_get_num_devices(interface, &device_count);
      if (error != ga::success) {
        static_cast<void>(api.if_close(interface));
        return producer_->status(error, "IFGetNumDevices");
      }

      for (std::uint32_t device_index = 0; device_index < device_count; ++device_index) {
        auto device_id = producer_->device_id(interface, device_index);
        if (!device_id) {
          static_cast<void>(api.if_close(interface));
          return device_id.status();
        }
        const auto vendor = producer_->device_string(
            interface, device_id.value(), ga::device_info_vendor);
        const auto model = producer_->device_string(interface, device_id.value(),
                                                    ga::device_info_model);
        const auto serial = producer_->device_string(interface, device_id.value(),
                                                     ga::device_info_serial);
        const auto transport_name = producer_->device_string(
            interface, device_id.value(), ga::device_info_type);
        auto display_name = producer_->device_string(
            interface, device_id.value(), ga::device_info_display_name);
        if (display_name.empty()) {
          display_name = vendor + " " + model + " (" + device_id.value() + ")";
        }

        const auto route_id = "gentl://" + interface_id.value() + "/" + device_id.value();
        const auto stable_id = serial.empty()
                                   ? descriptor_.id + ":" + interface_id.value() + ":" +
                                         device_id.value()
                                   : "camera:" + vendor + ":" + model + ":" + serial;
        devices.push_back(DeviceInfo{
            route_id,
            stable_id,
            descriptor_.id,
            vendor,
            model,
            serial,
            display_name,
            device_id.value(),
            transport_from_name(transport_name),
            producer_->options_.certification,
        });
        routes.emplace(route_id,
                       DeviceRoute{interface_id.value(), device_id.value()});
      }
      static_cast<void>(api.if_close(interface));
    }

    {
      std::lock_guard route_lock(route_mutex_);
      routes_ = std::move(routes);
    }
    return devices;
  }

  [[nodiscard]] Result<std::shared_ptr<Camera>> create_camera(
      const DeviceInfo& device) override {
    std::lock_guard lock(route_mutex_);
    const auto route = routes_.find(device.id);
    if (route == routes_.end()) {
      return Status{ErrorCode::not_found,
                    "GenTL device route is stale; enumerate devices again"};
    }
    std::shared_ptr<Camera> camera =
        std::make_shared<GenTLCamera>(device, route->second, producer_);
    return camera;
  }

 private:
  std::shared_ptr<Producer> producer_;
  AdapterDescriptor descriptor_;
  std::mutex route_mutex_;
  std::unordered_map<std::string, DeviceRoute> routes_;
};

}  // namespace

Result<AdapterPtr> make_gentl_adapter(const GenTLAdapterOptions& options) {
  auto producer = Producer::create(options);
  if (!producer) {
    return producer.status();
  }
  AdapterPtr adapter = std::make_shared<GenTLAdapter>(std::move(producer).value());
  return adapter;
}

}  // namespace univision
