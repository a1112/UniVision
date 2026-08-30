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
  REQUIRE(uv_camera_set_float(camera, "ExposureTime", 5000.0));
  double exposure = 0.0;
  REQUIRE(uv_camera_get_float(camera, "ExposureTime", &exposure));
  if (exposure != 5000.0) {
    return 1;
  }

  uv_stream* stream = NULL;
  REQUIRE(uv_camera_create_stream(camera, NULL, &stream));
  REQUIRE(uv_stream_start(stream));

  uv_frame* frame = NULL;
  REQUIRE(uv_stream_wait(stream, 100, &frame));
  uv_frame_descriptor descriptor = {0};
  descriptor.struct_size = sizeof(descriptor);
  REQUIRE(uv_frame_get_descriptor(frame, &descriptor));
  if (descriptor.width != 32 || descriptor.height != 16 ||
      uv_frame_size(frame) != 32U * 16U || uv_frame_data(frame) == NULL) {
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
