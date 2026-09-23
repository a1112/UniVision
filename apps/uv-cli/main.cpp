#include "univision/industrial/flow.h"
#include "univision/industrial/replay.h"
#include "univision/industrial/stream.h"
#include "univision/univision.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <string>

namespace {
using namespace univision;
using namespace univision::industrial;
namespace fs = std::filesystem;

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return 1;
}

int run_synthetic_flow(const fs::path& workspace,
                       const fs::path& definition_file,
                       const std::string& run_id) {
  auto graph = load_flow_definition(definition_file);
  if (!graph) return fail(graph.status().message());
  std::string calibration_id;
  bool found_source = false;
  for (auto& node : graph.value().nodes) {
    if (node.type == "source.profile_file") {
      const auto existing = node.parameters.find("source_kind");
      if (existing != node.parameters.end() &&
          existing->second != "synthetic")
        return fail("source provenance conflicts with synthetic command");
      node.parameters["source_kind"] = "synthetic";
      found_source = true;
    }
    if (node.type == "measure.circle_fit") {
      const auto found = node.parameters.find("calibration_id");
      if (found != node.parameters.end()) calibration_id = found->second;
    }
  }
  if (!found_source || calibration_id.empty())
    return fail("flow lacks profile source or calibration ID");
  CalibrationProfile calibration;
  calibration.id = calibration_id;
  calibration.coordinate_frame = "profile-xz-mm";
  auto run = execute_flow(graph.value(), workspace, calibration, run_id);
  if (!run) return fail(run.status().message());
  if (run.value().state != FlowRunState::succeeded)
    return fail("synthetic flow did not commit its required sink");
  std::cout << "synthetic flow archived: "
            << (workspace / run.value().artifact_path) << '\n';
  return 0;
}

int demo(const fs::path& workspace) {
  if (fs::exists(workspace))
    return fail("workspace already exists; choose a new path");
  fs::create_directories(workspace / "data");
  {
    std::ofstream output(workspace / "data" / "profile-circle.csv",
                         std::ios::binary);
    if (!output) return fail("cannot create synthetic profile");
    output << "x_mm,z_mm,valid\n";
    output.precision(15);
    for (int i = 0; i < 72; ++i) {
      const auto angle = 2 * std::numbers::pi * i / 72;
      output << 2.0 + 10 * std::cos(angle) << ','
             << -3.0 + 10 * std::sin(angle) << ",1\n";
    }
    output.flush();
    if (!output) return fail("cannot flush synthetic profile");
  }

  System system;
  SimulatorConfiguration simulator;
  simulator.width = 8;
  simulator.height = 8;
  simulator.frame_rate = 100;
  if (const auto status = system.register_adapter(make_simulator_adapter(simulator));
      !status)
    return fail(status.message());
  auto devices = system.enumerate_devices();
  if (!devices || devices.value().empty())
    return fail("simulator enumeration failed");
  DeviceSelector selector;
  selector.stable_id = devices.value().front().stable_id;
  auto camera = system.create_camera(selector);
  if (!camera) return fail(camera.status().message());
  if (const auto status = camera.value()->open(); !status)
    return fail(status.message());
  auto stream = camera.value()->create_stream();
  if (!stream) return fail(stream.status().message());
  if (const auto status = stream.value()->start(); !status)
    return fail(status.message());
  auto frame = stream.value()->wait_next(std::chrono::seconds(1));
  if (!frame) return fail(frame.status().message());
  FrameEnvelope envelope;
  envelope.session_id = "demo-session";
  envelope.stream_id = "sim-stream";
  envelope.device_id = devices.value().front().stable_id;
  envelope.sequence = frame.value().descriptor.frame_id;
  envelope.source = SourceKind::synthetic;
  envelope.validity = Validity::synthetic;
  envelope.width = frame.value().descriptor.width;
  envelope.height = frame.value().descriptor.height;
  envelope.pixel_format = "Mono8";
  envelope.planes.push_back({PlaneRole::intensity, 0,
                              frame.value().descriptor.stride,
                              frame.value().buffer.size()});
  envelope.clock_id = "sim-clock";
  envelope.capture_ticks = frame.value().descriptor.camera_timestamp_ns;
  envelope.tick_frequency_hz = 1000000000;
  envelope.decoded_bytes = frame.value().buffer.size();
  auto owned = copy_core_frame(frame.value(), envelope, 64);
  if (!owned) return fail(owned.status().message());
  frame = Status{ErrorCode::invalid_state, "producer lease released"};
  if (const auto status = stream.value()->stop(); !status)
    return fail(status.message());
  stream.value().reset();
  if (const auto status = camera.value()->close(); !status)
    return fail(status.message());

  LocalStream local(DeliveryMode::inspection_bounded, 1, 64);
  auto accepted = local.publish("recording", std::move(owned).value());
  if (!accepted) return fail(accepted.status().message());
  auto delivery = local.next();
  if (!delivery || delivery.value().receipt.level != ReceiptLevel::received)
    return fail("local transfer did not produce received receipt");
  Recorder recorder;
  if (const auto status = recorder.open(workspace, "demo-session"); !status)
    return fail(status.message());
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (const auto status = recorder.append(delivery.value().frame,
                                          static_cast<std::uint64_t>(now)); !status)
    return fail(status.message());
  if (const auto status = recorder.close(); !status)
    return fail(status.message());

  ReplayEngine replay;
  if (const auto status = replay.open(workspace / "demo-session"); !status)
    return fail(status.message());
  auto replayed = replay.seek_sequence("sim-stream", accepted.value().sequence);
  if (!replayed || !replay.complete() ||
      replayed.value().frame->bytes != delivery.value().frame.bytes)
    return fail("replay did not match the recorded frame");

  FlowDefinition graph;
  graph.flow_id = "demo-circle-flow";
  graph.revision = "1";
  graph.nodes = {
      {"source", "source.profile_file", "0.1.0",
       {{"artifact_path", "data/profile-circle.csv"},
        {"source_kind", "synthetic"}}},
      {"fit", "measure.circle_fit", "0.1.0",
       {{"calibration_id", "synthetic-identity-v1"}}},
      {"judge", "judge.verification", "0.1.0",
       {{"error_limit_mm", "0.03"}}},
      {"archive", "sink.result_archive", "0.1.0",
       {{"output_namespace", "synthetic-results"}}}};
  graph.edges = {{"source", "out", "fit", "in"},
                 {"fit", "out", "judge", "in"},
                 {"judge", "out", "archive", "in"}};
  graph.required_sinks = {"archive"};
  CalibrationProfile calibration;
  calibration.id = "synthetic-identity-v1";
  calibration.coordinate_frame = "profile-xz-mm";
  auto run = execute_flow(graph, workspace, calibration, "demo-run");
  if (!run || run.value().state != FlowRunState::succeeded ||
      run.value().decision != Decision::not_evaluated ||
      !run.value().measurement.diameter_mm ||
      std::abs(*run.value().measurement.diameter_mm - 20.0) > 1e-6)
    return fail(run ? "synthetic flow result is wrong" : run.status().message());
  std::cout << "demo succeeded\n"
            << "recording: " << (workspace / "demo-session" / "session.json") << '\n'
            << "run: " << (workspace / run.value().artifact_path) << '\n'
            << "diameter_mm: " << *run.value().measurement.diameter_mm << '\n'
            << "decision: not-evaluated (synthetic)\n";
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string{argv[1]} == "demo")
      return demo(argv[2]);
    if (argc == 5 && std::string{argv[1]} == "run-synthetic-flow")
      return run_synthetic_flow(argv[2], argv[3], argv[4]);
    return fail("usage: univision_uv_cli demo <new-workspace-path> | "
                "run-synthetic-flow <workspace> <definition.json> <run-id>");
  } catch (const std::exception& error) {
    return fail(error.what());
  }
}
