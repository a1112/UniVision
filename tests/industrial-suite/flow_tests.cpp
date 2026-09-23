#include "univision/industrial/flow.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace univision::industrial;

namespace {
int failures = 0;
template <typename T>
void check(T&& ok, const char* name) {
  if (!static_cast<bool>(ok)) {
    std::cerr << "failed: " << name << '\n';
    ++failures;
  }
}
FlowDefinition graph() {
  FlowDefinition flow;
  flow.flow_id = "circle-flow";
  flow.revision = "1";
  flow.nodes = {
      {"source", "source.profile_file", "0.1.0",
       {{"artifact_path", "data/profile-circle.csv"},
        {"source_kind", "synthetic"}}},
      {"fit", "measure.circle_fit", "0.1.0",
       {{"calibration_id", "synthetic-identity-v1"}}},
      {"judge", "judge.verification", "0.1.0",
       {{"error_limit_mm", "0.03"}}},
      {"archive", "sink.result_archive", "0.1.0",
       {{"output_namespace", "synthetic-results"}}}};
  flow.edges = {
      {"source", "out", "fit", "in"},
      {"fit", "out", "judge", "in"},
      {"judge", "out", "archive", "in"}};
  flow.required_sinks = {"archive"};
  return flow;
}
}  // namespace

int main() {
  auto loaded = load_flow_definition(UV_TEST_FLOW);
  check(loaded && loaded.value().nodes.size() == 4 &&
            loaded.value().revision == "1",
        "load saved flow definition from design example");
  if (loaded) {
    loaded.value().nodes[0].parameters["source_kind"] = "synthetic";
    check(preflight_flow(loaded.value()),
          "explicit synthetic provenance makes saved graph executable");
  }
  auto valid = graph();
  check(preflight_flow(valid), "registered offline graph passes preflight");
  auto unknown = valid;
  unknown.nodes[0].type = "source.live_camera";
  check(!preflight_flow(unknown), "live camera node rejected by engine");
  auto unknown_version = valid;
  unknown_version.nodes[0].version = "9.0.0";
  check(!preflight_flow(unknown_version), "unregistered node version rejected");
  auto wrong = valid;
  wrong.edges[0].to = "judge";
  check(!preflight_flow(wrong), "wrong port type rejected");
  auto cyclic = valid;
  cyclic.edges.push_back({"archive", "out", "source", "in"});
  check(!preflight_flow(cyclic), "cycle rejected");
  auto no_sink = valid;
  no_sink.required_sinks.clear();
  check(!preflight_flow(no_sink), "required sink must be declared");

  namespace fs = std::filesystem;
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto workspace = fs::current_path() /
                         ("industrial-flow-test-" + std::to_string(suffix));
  fs::create_directories(workspace / "data");
  fs::copy_file(UV_TEST_PROFILE,
                workspace / "data" / "profile-circle.csv");
  CalibrationProfile calibration;
  calibration.id = "synthetic-identity-v1";
  calibration.coordinate_frame = "profile-xz-mm";
  calibration.scale_to_mm = 1;
  auto run = execute_flow(valid, workspace, calibration, "run-1");
  check(run && run.value().state == FlowRunState::succeeded,
        "synthetic offline graph executes and archives");
  check(run && run.value().measurement.validity ==
                   MeasurementValidity::synthetic,
        "synthetic result remains labeled synthetic");
  check(run && run.value().decision == Decision::not_evaluated,
        "synthetic graph cannot emit production pass");
  check(run && fs::exists(workspace / run.value().artifact_path),
        "required archive exists before run succeeds");
  check(!execute_flow(valid, workspace, calibration, "run-1"),
        "run output cannot be overwritten");

  {
    std::ifstream input(UV_TEST_PROFILE);
    std::ofstream output(workspace / "data" / "too-few.csv");
    std::string line;
    for (int i = 0; i < 21 && std::getline(input, line); ++i)
      output << line << '\n';
  }
  auto invalid_input = valid;
  invalid_input.nodes[0].parameters["artifact_path"] = "data/too-few.csv";
  auto invalid_run = execute_flow(invalid_input, workspace, calibration,
                                  "run-invalid");
  check(invalid_run && invalid_run.value().state == FlowRunState::succeeded &&
            invalid_run.value().measurement.validity ==
                MeasurementValidity::invalid &&
            invalid_run.value().decision == Decision::indeterminate,
        "invalid measurement cannot become a pass decision");

  std::atomic_bool cancelled{true};
  auto stopped = execute_flow(valid, workspace, calibration, "run-2",
                              &cancelled);
  check(stopped && stopped.value().state == FlowRunState::cancelled,
        "pre-cancelled run does not execute");
  check(!fs::exists(workspace / "synthetic-results" / "run-2.json"),
        "cancelled run has no committed sink");

  const auto outside =
      workspace.parent_path() / ("external-profile-" + std::to_string(suffix) + ".csv");
  fs::copy_file(UV_TEST_PROFILE, outside);
  std::error_code symlink_error;
  fs::create_symlink(outside, workspace / "data" / "external.csv", symlink_error);
  if (!symlink_error) {
    auto escaping = valid;
    escaping.nodes[0].parameters["artifact_path"] = "data/external.csv";
    check(!execute_flow(escaping, workspace, calibration, "run-external"),
          "symlink input cannot escape workspace");
  }

  if (failures != 0) return 1;
  std::cout << "industrial flow checks passed\n";
}
