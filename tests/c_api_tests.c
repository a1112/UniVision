#include "univision/c/univision.h"

#include <stdio.h>
#include <string.h>

#define REQUIRE(call)                                                          \
  do {                                                                         \
    uv_status_code code_ = (call);                                              \
    if (code_ != UV_STATUS_OK) {                                                \
      fprintf(stderr, "%s failed: %s\n", #call, uv_last_error_message());      \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(void) {
  uv_version version = {0};
  version.struct_size = sizeof(version);
  REQUIRE(uv_get_version(&version));
  if (version.abi_version != UV_ABI_VERSION) {
    return 1;
  }

  uv_system* system = NULL;
  REQUIRE(uv_system_create(&system));

  uv_simulator_config simulator = {0};
  simulator.struct_size = sizeof(simulator);
  simulator.serial = "C-API-001";
  simulator.width = 32;
  simulator.height = 16;
  simulator.frame_rate = 100.0;
  REQUIRE(uv_system_register_simulator(system, &simulator));

  size_t count = 0;
  REQUIRE(uv_system_enumerate(system, &count));
  if (count != 1) {
    return 1;
  }

  uv_device_info info = {0};
  info.struct_size = sizeof(info);
  REQUIRE(uv_system_get_device(system, 0, &info));
  if (strcmp(info.serial, "C-API-001") != 0) {
    return 1;
  }

  uv_camera* camera = NULL;
  REQUIRE(uv_system_create_camera(system, info.stable_id, &camera));
  REQUIRE(uv_camera_open(camera));
  size_t feature_count = 0;
  REQUIRE(uv_camera_get_feature_count(camera, &feature_count));
  if (feature_count < 8) {
    return 1;
  }
  int64_t width = 0;
  REQUIRE(uv_camera_get_integer(camera, "Width", &width));
  if (width != 32) {
    return 1;
  }
  int64_t sensor_width = 0;
  REQUIRE(uv_camera_get_integer(camera, "SensorWidth", &sensor_width));
  if (sensor_width != 16384 ||
      uv_camera_set_integer(camera, "SensorWidth", 16) != UV_STATUS_ACCESS_DENIED ||
      uv_camera_set_integer(camera, "OffsetX", -1) != UV_STATUS_INVALID_ARGUMENT) {
    return 1;
  }
  REQUIRE(uv_camera_set_integer(camera, "OffsetX", 11));
  REQUIRE(uv_camera_set_integer(camera, "OffsetY", 7));
  int64_t offset = 0;
  REQUIRE(uv_camera_get_integer(camera, "OffsetX", &offset));
  if (offset != 11) {
    return 1;
  }
  int roi_info_found = 0;
  for (size_t index = 0; index < feature_count; ++index) {
    uv_feature_info feature = {0};
    feature.struct_size = sizeof(feature);
    REQUIRE(uv_camera_get_feature_info(camera, index, &feature));
    if (strcmp(feature.name, "OffsetX") == 0) {
      if (!feature.standard_feature || !feature.has_maximum ||
          feature.maximum != 16352.0) {
        return 1;
      }
      roi_info_found = 1;
    }
  }
  if (!roi_info_found) {
    return 1;
  }
  REQUIRE(uv_camera_set_float(camera, "ExposureTime", 5000.0));
  double exposure = 0.0;
  REQUIRE(uv_camera_get_float(camera, "ExposureTime", &exposure));
  if (exposure != 5000.0) {
    return 1;
  }
  size_t pixel_format_size = 0;
  REQUIRE(uv_camera_get_string(camera, "PixelFormat", NULL, &pixel_format_size));
  char pixel_format[16] = {0};
  REQUIRE(uv_camera_get_string(camera, "PixelFormat", pixel_format,
                               &pixel_format_size));
  if (strcmp(pixel_format, "Mono8") != 0) {
    return 1;
  }
  REQUIRE(uv_camera_execute_command(camera, "TriggerSoftware"));

  uv_stream* stream = NULL;
  REQUIRE(uv_camera_create_stream(camera, NULL, &stream));
  REQUIRE(uv_stream_start(stream));
  if (uv_camera_set_integer(camera, "OffsetX", 12) != UV_STATUS_INVALID_STATE) {
    return 1;
  }

  uv_frame* frame = NULL;
  REQUIRE(uv_stream_wait(stream, 100, &frame));
  uv_frame_descriptor descriptor = {0};
  descriptor.struct_size = sizeof(descriptor);
  REQUIRE(uv_frame_get_descriptor(frame, &descriptor));
  if (descriptor.width != 32 || descriptor.height != 16 ||
      uv_frame_size(frame) != 32U * 16U || uv_frame_data(frame) == NULL ||
      ((const unsigned char*)uv_frame_data(frame))[0] != 19) {
    return 1;
  }

  uv_frame_release(frame);
  REQUIRE(uv_stream_stop(stream));
  uv_stream_destroy(stream);
  REQUIRE(uv_camera_close(camera));
  uv_camera_destroy(camera);
  uv_system_destroy(system);
  puts("all UniVision C ABI checks passed");
  return 0;
}
