#include "univision/c/univision.h"

#include "univision/gentl.h"
#include "univision/simulator.h"
#include "univision/system.h"
#include "univision/version.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct uv_system {
  univision::System implementation;
  std::vector<univision::DeviceInfo> devices;
};

struct uv_camera {
  std::shared_ptr<univision::Camera> implementation;
};

struct uv_stream {
  std::unique_ptr<univision::Stream> implementation;
};

struct uv_frame {
  univision::Frame implementation;
};

namespace {

thread_local std::string last_error;

uv_status_code code_of(const univision::Status& status) {
  last_error = status.message();
  return static_cast<uv_status_code>(status.code());
}

uv_status_code failure(uv_status_code code, std::string message) {
  last_error = std::move(message);
  return code;
}

template <typename Function>
uv_status_code guarded(Function&& function) {
  try {
    last_error.clear();
    return function();
  } catch (const std::exception& error) {
    return failure(UV_STATUS_INTERNAL_ERROR, error.what());
  } catch (...) {
    return failure(UV_STATUS_INTERNAL_ERROR, "unknown internal error");
  }
}

template <std::size_t Size>
void copy_text(char (&destination)[Size], const std::string& source) {
  const auto count = std::min(source.size(), Size - 1);
  std::memcpy(destination, source.data(), count);
  destination[count] = '\0';
}

bool valid_struct(const void* structure, std::uint32_t actual, std::size_t expected) {
  return structure != nullptr && actual >= expected;
}

uv_status_code copy_sized_text(const std::string& source, char* destination,
                               std::size_t* destination_size) {
  if (destination_size == nullptr) {
    return failure(UV_STATUS_INVALID_ARGUMENT, "text size output must not be null");
  }
  const auto required = source.size() + 1;
  if (destination == nullptr) {
    *destination_size = required;
    return UV_STATUS_OK;
  }
  if (*destination_size < required) {
    *destination_size = required;
    return failure(UV_STATUS_INVALID_ARGUMENT, "text output buffer is too small");
  }
  std::memcpy(destination, source.c_str(), required);
  *destination_size = required;
  return UV_STATUS_OK;
}

}  // namespace

extern "C" {

uv_status_code uv_get_version(uv_version* version) {
  return guarded([&] {
    if (!valid_struct(version, version == nullptr ? 0U : version->struct_size,
                      sizeof(uv_version))) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "invalid uv_version structure");
    }
    version->abi_version = UV_ABI_VERSION;
    version->major = univision::version_major;
    version->minor = univision::version_minor;
    version->patch = univision::version_patch;
    return UV_STATUS_OK;
  });
}

const char* uv_last_error_message(void) { return last_error.c_str(); }

uv_status_code uv_system_create(uv_system** out_system) {
  return guarded([&] {
    if (out_system == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "out_system must not be null");
    }
    *out_system = new uv_system{};
    return UV_STATUS_OK;
  });
}

void uv_system_destroy(uv_system* system) { delete system; }

uv_status_code uv_system_register_simulator(
    uv_system* system, const uv_simulator_config* config) {
  return guarded([&] {
    if (system == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "system must not be null");
    }
    univision::SimulatorConfiguration translated;
    if (config != nullptr) {
      if (!valid_struct(config, config->struct_size, sizeof(uv_simulator_config))) {
        return failure(UV_STATUS_INVALID_ARGUMENT,
                       "invalid uv_simulator_config structure");
      }
      if (config->serial != nullptr) {
        translated.serial = config->serial;
      }
      if (config->model != nullptr) {
        translated.model = config->model;
      }
      if (config->width != 0) {
        translated.width = config->width;
      }
      if (config->height != 0) {
        translated.height = config->height;
      }
      if (config->frame_rate > 0.0) {
        translated.frame_rate = config->frame_rate;
      }
    }
    return code_of(system->implementation.register_adapter(
        univision::make_simulator_adapter(translated)));
  });
}

uv_status_code uv_system_register_gentl(uv_system* system, const char* cti_path) {
  return guarded([&] {
    if (system == nullptr || cti_path == nullptr || cti_path[0] == '\0') {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "system and a non-empty cti_path are required");
    }
    univision::GenTLAdapterOptions options;
    options.cti_path = cti_path;
    auto adapter = univision::make_gentl_adapter(options);
    if (!adapter) {
      return code_of(adapter.status());
    }
    return code_of(system->implementation.register_adapter(std::move(adapter).value()));
  });
}

uv_status_code uv_system_enumerate(uv_system* system, size_t* device_count) {
  return guarded([&] {
    if (system == nullptr || device_count == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "system and device_count must not be null");
    }
    auto result = system->implementation.enumerate_devices();
    if (!result) {
      return code_of(result.status());
    }
    system->devices = std::move(result).value();
    *device_count = system->devices.size();
    return UV_STATUS_OK;
  });
}

uv_status_code uv_system_get_device(
    const uv_system* system, size_t index, uv_device_info* info) {
  return guarded([&] {
    if (system == nullptr ||
        !valid_struct(info, info == nullptr ? 0U : info->struct_size,
                      sizeof(uv_device_info))) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "invalid device output structure");
    }
    if (index >= system->devices.size()) {
      return failure(UV_STATUS_NOT_FOUND, "device index is out of range");
    }
    const auto& source = system->devices[index];
    copy_text(info->id, source.id);
    copy_text(info->stable_id, source.stable_id);
    copy_text(info->adapter_id, source.adapter_id);
    copy_text(info->vendor, source.vendor);
    copy_text(info->model, source.model);
    copy_text(info->serial, source.serial);
    copy_text(info->display_name, source.display_name);
    copy_text(info->address, source.address);
    info->transport = static_cast<uint32_t>(source.transport);
    info->certification = static_cast<uint32_t>(source.certification);
    return UV_STATUS_OK;
  });
}

uv_status_code uv_system_create_camera(
    uv_system* system, const char* stable_id, uv_camera** out_camera) {
  return guarded([&] {
    if (system == nullptr || stable_id == nullptr || out_camera == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "system, stable_id, and out_camera must not be null");
    }
    univision::DeviceSelector selector;
    selector.stable_id = stable_id;
    auto result = system->implementation.create_camera(selector);
    if (!result) {
      return code_of(result.status());
    }
    *out_camera = new uv_camera{std::move(result).value()};
    return UV_STATUS_OK;
  });
}

void uv_camera_destroy(uv_camera* camera) { delete camera; }

uv_status_code uv_camera_open(uv_camera* camera) {
  return guarded([&] {
    if (camera == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "camera must not be null");
    }
    return code_of(camera->implementation->open());
  });
}

uv_status_code uv_camera_close(uv_camera* camera) {
  return guarded([&] {
    if (camera == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "camera must not be null");
    }
    return code_of(camera->implementation->close());
  });
}

uint32_t uv_camera_state(const uv_camera* camera) {
  if (camera == nullptr) {
    last_error = "camera must not be null";
    return static_cast<uint32_t>(univision::CameraState::failed);
  }
  return static_cast<uint32_t>(camera->implementation->state());
}

uv_status_code uv_camera_get_feature_count(
    const uv_camera* camera, size_t* feature_count) {
  return guarded([&] {
    if (camera == nullptr || feature_count == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_count must not be null");
    }
    *feature_count = camera->implementation->features().size();
    return UV_STATUS_OK;
  });
}

uv_status_code uv_camera_get_feature_info(
    const uv_camera* camera, size_t index, uv_feature_info* info) {
  return guarded([&] {
    if (camera == nullptr ||
        !valid_struct(info, info == nullptr ? 0U : info->struct_size,
                      sizeof(uv_feature_info))) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "invalid feature output structure");
    }
    const auto features = camera->implementation->features();
    if (index >= features.size()) {
      return failure(UV_STATUS_NOT_FOUND, "feature index is out of range");
    }
    const auto& source = features[index];
    copy_text(info->name, source.name);
    copy_text(info->display_name, source.display_name);
    copy_text(info->description, source.description);
    copy_text(info->unit, source.unit);
    info->kind = static_cast<uint32_t>(source.kind);
    info->access = static_cast<uint32_t>(source.access);
    info->has_minimum = source.minimum.has_value();
    info->has_maximum = source.maximum.has_value();
    info->has_increment = source.increment.has_value();
    info->minimum = source.minimum.value_or(0.0);
    info->maximum = source.maximum.value_or(0.0);
    info->increment = source.increment.value_or(0.0);
    info->enum_entry_count = source.enum_entries.size();
    info->standard_feature = source.standard_feature;
    return UV_STATUS_OK;
  });
}

uv_status_code uv_camera_get_enum_entry(
    const uv_camera* camera, const char* feature_name, size_t index,
    char* value, size_t* value_size) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_name must not be null");
    }
    auto result = camera->implementation->feature_info(feature_name);
    if (!result) {
      return code_of(result.status());
    }
    if (result.value().kind != univision::FeatureKind::enumeration) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "feature is not an enumeration");
    }
    if (index >= result.value().enum_entries.size()) {
      return failure(UV_STATUS_NOT_FOUND, "enumeration entry index is out of range");
    }
    return copy_sized_text(result.value().enum_entries[index], value, value_size);
  });
}

uv_status_code uv_camera_get_integer(
    const uv_camera* camera, const char* feature_name, int64_t* value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr || value == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera, feature_name, and value must not be null");
    }
    auto result = camera->implementation->read_feature(feature_name);
    if (!result) {
      return code_of(result.status());
    }
    const auto* integer = std::get_if<std::int64_t>(&result.value());
    if (integer == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "feature is not integer");
    }
    *value = *integer;
    return UV_STATUS_OK;
  });
}

uv_status_code uv_camera_set_integer(
    uv_camera* camera, const char* feature_name, int64_t value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_name must not be null");
    }
    return code_of(camera->implementation->write_feature(
        feature_name, static_cast<std::int64_t>(value)));
  });
}

uv_status_code uv_camera_get_float(
    const uv_camera* camera, const char* feature_name, double* value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr || value == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera, feature_name, and value must not be null");
    }
    auto result = camera->implementation->read_feature(feature_name);
    if (!result) {
      return code_of(result.status());
    }
    const auto* floating = std::get_if<double>(&result.value());
    if (floating == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "feature is not floating-point");
    }
    *value = *floating;
    return UV_STATUS_OK;
  });
}

uv_status_code uv_camera_set_float(
    uv_camera* camera, const char* feature_name, double value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_name must not be null");
    }
    return code_of(camera->implementation->write_feature(feature_name, value));
  });
}

uv_status_code uv_camera_get_bool(
    const uv_camera* camera, const char* feature_name, int* value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr || value == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera, feature_name, and value must not be null");
    }
    auto result = camera->implementation->read_feature(feature_name);
    if (!result) {
      return code_of(result.status());
    }
    const auto* boolean = std::get_if<bool>(&result.value());
    if (boolean == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "feature is not boolean");
    }
    *value = *boolean;
    return UV_STATUS_OK;
  });
}

uv_status_code uv_camera_set_bool(
    uv_camera* camera, const char* feature_name, int value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_name must not be null");
    }
    return code_of(camera->implementation->write_feature(feature_name, value != 0));
  });
}

uv_status_code uv_camera_get_string(
    const uv_camera* camera, const char* feature_name,
    char* value, size_t* value_size) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_name must not be null");
    }
    auto result = camera->implementation->read_feature(feature_name);
    if (!result) {
      return code_of(result.status());
    }
    const auto* string = std::get_if<std::string>(&result.value());
    if (string == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "feature is not string or enumeration");
    }
    return copy_sized_text(*string, value, value_size);
  });
}

uv_status_code uv_camera_set_string(
    uv_camera* camera, const char* feature_name, const char* value) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr || value == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera, feature_name, and value must not be null");
    }
    return code_of(camera->implementation->write_feature(
        feature_name, std::string{value}));
  });
}

uv_status_code uv_camera_execute_command(
    uv_camera* camera, const char* feature_name) {
  return guarded([&] {
    if (camera == nullptr || feature_name == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and feature_name must not be null");
    }
    return code_of(camera->implementation->execute_command(feature_name));
  });
}

uv_status_code uv_camera_create_stream(
    uv_camera* camera, const uv_stream_config* config, uv_stream** out_stream) {
  return guarded([&] {
    if (camera == nullptr || out_stream == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "camera and out_stream must not be null");
    }
    univision::StreamConfiguration translated;
    if (config != nullptr) {
      if (!valid_struct(config, config->struct_size, sizeof(uv_stream_config))) {
        return failure(UV_STATUS_INVALID_ARGUMENT,
                       "invalid uv_stream_config structure");
      }
      translated.buffer_count = config->buffer_count;
      translated.queue_capacity = config->queue_capacity;
      translated.drop_policy =
          static_cast<univision::DropPolicy>(config->drop_policy);
      translated.frame_timeout = std::chrono::milliseconds(config->frame_timeout_ms);
    }
    auto result = camera->implementation->create_stream(translated);
    if (!result) {
      return code_of(result.status());
    }
    *out_stream = new uv_stream{std::move(result).value()};
    return UV_STATUS_OK;
  });
}

void uv_stream_destroy(uv_stream* stream) { delete stream; }

uv_status_code uv_stream_start(uv_stream* stream) {
  return guarded([&] {
    if (stream == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "stream must not be null");
    }
    return code_of(stream->implementation->start());
  });
}

uv_status_code uv_stream_stop(uv_stream* stream) {
  return guarded([&] {
    if (stream == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "stream must not be null");
    }
    return code_of(stream->implementation->stop());
  });
}

uv_status_code uv_stream_wait(
    uv_stream* stream, uint32_t timeout_ms, uv_frame** out_frame) {
  return guarded([&] {
    if (stream == nullptr || out_frame == nullptr) {
      return failure(UV_STATUS_INVALID_ARGUMENT,
                     "stream and out_frame must not be null");
    }
    auto result = stream->implementation->wait_next(
        std::chrono::milliseconds(timeout_ms));
    if (!result) {
      return code_of(result.status());
    }
    *out_frame = new uv_frame{std::move(result).value()};
    return UV_STATUS_OK;
  });
}

void uv_frame_release(uv_frame* frame) { delete frame; }

uv_status_code uv_frame_get_descriptor(
    const uv_frame* frame, uv_frame_descriptor* descriptor) {
  return guarded([&] {
    if (frame == nullptr ||
        !valid_struct(descriptor,
                      descriptor == nullptr ? 0U : descriptor->struct_size,
                      sizeof(uv_frame_descriptor))) {
      return failure(UV_STATUS_INVALID_ARGUMENT, "invalid frame descriptor");
    }
    const auto& source = frame->implementation.descriptor;
    descriptor->width = source.width;
    descriptor->height = source.height;
    descriptor->stride = source.stride;
    descriptor->pixel_format = source.pixel_format;
    descriptor->memory_type = static_cast<uint32_t>(source.memory_type);
    descriptor->frame_id = source.frame_id;
    descriptor->camera_timestamp_ns = source.camera_timestamp_ns;
    descriptor->complete = source.complete ? 1 : 0;
    return UV_STATUS_OK;
  });
}

const void* uv_frame_data(const uv_frame* frame) {
  return frame == nullptr ? nullptr : frame->implementation.buffer.data();
}

size_t uv_frame_size(const uv_frame* frame) {
  return frame == nullptr ? 0 : frame->implementation.buffer.size();
}

}  // extern "C"
