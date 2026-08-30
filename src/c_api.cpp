#include "univision/c/univision.h"

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
