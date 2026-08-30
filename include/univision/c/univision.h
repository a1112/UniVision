#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(UNIVISION_SHARED)
#  if defined(UNIVISION_BUILDING_LIBRARY)
#    define UV_API __declspec(dllexport)
#  else
#    define UV_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(UNIVISION_SHARED)
#  define UV_API __attribute__((visibility("default")))
#else
#  define UV_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define UV_ABI_VERSION 1U
#define UV_TEXT_CAPACITY 128U

typedef struct uv_system uv_system;
typedef struct uv_camera uv_camera;
typedef struct uv_stream uv_stream;
typedef struct uv_frame uv_frame;

typedef enum uv_status_code {
  UV_STATUS_OK = 0,
  UV_STATUS_INVALID_ARGUMENT,
  UV_STATUS_INVALID_STATE,
  UV_STATUS_NOT_FOUND,
  UV_STATUS_ALREADY_EXISTS,
  UV_STATUS_TIMEOUT,
  UV_STATUS_UNSUPPORTED,
  UV_STATUS_ACCESS_DENIED,
  UV_STATUS_IO_ERROR,
  UV_STATUS_TRANSPORT_ERROR,
  UV_STATUS_DEVICE_LOST,
  UV_STATUS_BUFFER_EXHAUSTED,
  UV_STATUS_ADAPTER_FAILURE,
  UV_STATUS_INTERNAL_ERROR
} uv_status_code;

typedef enum uv_drop_policy {
  UV_DROP_BLOCK_PRODUCER = 0,
  UV_DROP_OLDEST,
  UV_DROP_NEWEST,
  UV_DROP_LATEST_ONLY
} uv_drop_policy;

typedef struct uv_version {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
} uv_version;

typedef struct uv_simulator_config {
  uint32_t struct_size;
  const char* serial;
  const char* model;
  uint32_t width;
  uint32_t height;
  double frame_rate;
} uv_simulator_config;

typedef struct uv_device_info {
  uint32_t struct_size;
  char id[UV_TEXT_CAPACITY];
  char stable_id[UV_TEXT_CAPACITY];
  char adapter_id[UV_TEXT_CAPACITY];
  char vendor[UV_TEXT_CAPACITY];
  char model[UV_TEXT_CAPACITY];
  char serial[UV_TEXT_CAPACITY];
  char display_name[UV_TEXT_CAPACITY];
  char address[UV_TEXT_CAPACITY];
  uint32_t transport;
  uint32_t certification;
} uv_device_info;

typedef struct uv_stream_config {
  uint32_t struct_size;
  size_t buffer_count;
  size_t queue_capacity;
  uv_drop_policy drop_policy;
  uint32_t frame_timeout_ms;
} uv_stream_config;

typedef struct uv_frame_descriptor {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  size_t stride;
  uint64_t pixel_format;
  uint32_t memory_type;
  uint64_t frame_id;
  uint64_t camera_timestamp_ns;
  int complete;
} uv_frame_descriptor;

UV_API uv_status_code uv_get_version(uv_version* version);
UV_API const char* uv_last_error_message(void);

UV_API uv_status_code uv_system_create(uv_system** out_system);
UV_API void uv_system_destroy(uv_system* system);
UV_API uv_status_code uv_system_register_simulator(
    uv_system* system, const uv_simulator_config* config);
UV_API uv_status_code uv_system_enumerate(uv_system* system, size_t* device_count);
UV_API uv_status_code uv_system_get_device(
    const uv_system* system, size_t index, uv_device_info* info);
UV_API uv_status_code uv_system_create_camera(
    uv_system* system, const char* stable_id, uv_camera** out_camera);

UV_API void uv_camera_destroy(uv_camera* camera);
UV_API uv_status_code uv_camera_open(uv_camera* camera);
UV_API uv_status_code uv_camera_close(uv_camera* camera);
UV_API uint32_t uv_camera_state(const uv_camera* camera);
UV_API uv_status_code uv_camera_get_float(
    const uv_camera* camera, const char* feature_name, double* value);
UV_API uv_status_code uv_camera_set_float(
    uv_camera* camera, const char* feature_name, double value);

UV_API uv_status_code uv_camera_create_stream(
    uv_camera* camera, const uv_stream_config* config, uv_stream** out_stream);
UV_API void uv_stream_destroy(uv_stream* stream);
UV_API uv_status_code uv_stream_start(uv_stream* stream);
UV_API uv_status_code uv_stream_stop(uv_stream* stream);
UV_API uv_status_code uv_stream_wait(
    uv_stream* stream, uint32_t timeout_ms, uv_frame** out_frame);

UV_API void uv_frame_release(uv_frame* frame);
UV_API uv_status_code uv_frame_get_descriptor(
    const uv_frame* frame, uv_frame_descriptor* descriptor);
UV_API const void* uv_frame_data(const uv_frame* frame);
UV_API size_t uv_frame_size(const uv_frame* frame);

#ifdef __cplusplus
}
#endif
