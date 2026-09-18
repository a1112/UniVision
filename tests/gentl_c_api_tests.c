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

int main(int argc, char** argv) {
  if (argc != 2) {
    fputs("usage: univision_gentl_c_api_tests <producer.cti>\n", stderr);
    return 2;
  }
  uv_system* system = NULL;
  REQUIRE(uv_system_create(&system));
  REQUIRE(uv_system_register_gentl(system, argv[1]));

  size_t count = 0;
  REQUIRE(uv_system_enumerate(system, &count));
  if (count != 1) {
    return 1;
  }

  uv_device_info info = {0};
  info.struct_size = sizeof(info);
  REQUIRE(uv_system_get_device(system, 0, &info));

  uv_camera* camera = NULL;
  REQUIRE(uv_system_create_camera(system, info.stable_id, &camera));
  REQUIRE(uv_camera_open(camera));

  size_t feature_count = 0;
  REQUIRE(uv_camera_get_feature_count(camera, &feature_count));
  if (feature_count != 13) {
    return 1;
  }
  int found_exposure = 0;
  for (size_t index = 0; index < feature_count; ++index) {
    uv_feature_info feature = {0};
    feature.struct_size = sizeof(feature);
    REQUIRE(uv_camera_get_feature_info(camera, index, &feature));
    if (strcmp(feature.name, "ExposureTime") == 0) {
      if (feature.kind != UV_FEATURE_FLOAT || feature.access != UV_ACCESS_READ_WRITE ||
          !feature.has_minimum || feature.minimum != 10.0 ||
          strcmp(feature.unit, "us") != 0 || !feature.standard_feature) {
        return 1;
      }
      found_exposure = 1;
    }
  }
  if (!found_exposure) {
    return 1;
  }

  int64_t width = 0;
  REQUIRE(uv_camera_get_integer(camera, "Width", &width));
  if (width != 64) {
    return 1;
  }
  REQUIRE(uv_camera_set_integer(camera, "Width", 32));
  REQUIRE(uv_camera_set_integer(camera, "Width", 64));

  double exposure = 0.0;
  REQUIRE(uv_camera_get_float(camera, "ExposureTime", &exposure));
  REQUIRE(uv_camera_set_float(camera, "ExposureTime", 4000.5));
  REQUIRE(uv_camera_get_float(camera, "ExposureTime", &exposure));
  if (exposure != 4000.5) {
    return 1;
  }

  int reverse_x = 0;
  REQUIRE(uv_camera_get_bool(camera, "ReverseX", &reverse_x));
  REQUIRE(uv_camera_set_bool(camera, "ReverseX", 1));
  REQUIRE(uv_camera_get_bool(camera, "ReverseX", &reverse_x));
  if (!reverse_x) {
    return 1;
  }

  size_t text_size = 0;
  REQUIRE(uv_camera_get_string(camera, "DeviceUserID", NULL, &text_size));
  char text[32] = {0};
  REQUIRE(uv_camera_get_string(camera, "DeviceUserID", text, &text_size));
  if (strcmp(text, "CI Camera") != 0) {
    return 1;
  }
  REQUIRE(uv_camera_set_string(camera, "DeviceUserID", "Line C"));
  text_size = sizeof(text);
  REQUIRE(uv_camera_get_string(camera, "DeviceUserID", text, &text_size));
  if (strcmp(text, "Line C") != 0) {
    return 1;
  }

  size_t entry_size = sizeof(text);
  REQUIRE(uv_camera_get_enum_entry(camera, "TriggerMode", 1, text, &entry_size));
  if (strcmp(text, "On") != 0) {
    return 1;
  }
  REQUIRE(uv_camera_set_string(camera, "TriggerMode", "On"));
  REQUIRE(uv_camera_execute_command(camera, "TriggerSoftware"));
  if (uv_camera_set_float(camera, "DeviceTemperature", 20.0) !=
      UV_STATUS_ACCESS_DENIED) {
    return 1;
  }

  uv_stream_config config = {0};
  config.struct_size = sizeof(config);
  config.buffer_count = 3;
  config.queue_capacity = 3;
  config.drop_policy = UV_DROP_OLDEST;
  config.frame_timeout_ms = 100;

  uv_stream* stream = NULL;
  REQUIRE(uv_camera_create_stream(camera, &config, &stream));
  REQUIRE(uv_stream_start(stream));

  uv_frame* frame = NULL;
  REQUIRE(uv_stream_wait(stream, 100, &frame));
  uv_frame_descriptor descriptor = {0};
  descriptor.struct_size = sizeof(descriptor);
  REQUIRE(uv_frame_get_descriptor(frame, &descriptor));
  if (descriptor.width != 64 || descriptor.height != 32 ||
      descriptor.frame_id != 1 || uv_frame_size(frame) != 64U * 32U) {
    return 1;
  }

  uv_frame_release(frame);
  REQUIRE(uv_stream_stop(stream));
  uv_stream_destroy(stream);
  REQUIRE(uv_camera_close(camera));
  uv_camera_destroy(camera);
  uv_system_destroy(system);
  puts("all UniVision GenTL C ABI checks passed");
  return 0;
}
