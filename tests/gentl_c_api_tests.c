#include "univision/c/univision.h"

#include <stdio.h>

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
