#pragma once

#include "univision/industrial/measure.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace univision::industrial {

struct FlowNode {
  std::string id;
  std::string type;
  std::string version;
  std::map<std::string, std::string> parameters;
};

struct FlowEdge {
  std::string from;
  std::string from_port;
  std::string to;
  std::string to_port;
};

struct FlowDefinition {
  std::string schema_version{"0.1.0-draft"};
  std::string flow_id;
  std::string revision;
  std::string execution_mode{"offline"};
  std::vector<FlowNode> nodes;
  std::vector<FlowEdge> edges;
  std::vector<std::string> required_sinks;
  std::uint64_t max_owned_bytes{64ULL * 1024 * 1024};
};

struct FlowPreflight {
  std::vector<std::string> topological_order;
};

[[nodiscard]] Result<FlowDefinition> load_flow_definition(
    const std::filesystem::path& file,
    std::uint64_t max_definition_bytes = 1024 * 1024);
[[nodiscard]] Result<FlowPreflight> preflight_flow(
    const FlowDefinition& definition);

enum class FlowRunState {
  queued, preparing, running, draining, finalizing,
  succeeded, failed, partial, cancelled
};

struct FlowRun {
  std::string run_id;
  std::string flow_id;
  std::string graph_revision;
  std::string graph_sha256;
  std::string input_sha256;
  FlowRunState state{FlowRunState::queued};
  CircleFitResult measurement;
  Decision decision{Decision::not_evaluated};
  std::string artifact_path;
};

[[nodiscard]] Result<FlowRun> execute_flow(
    const FlowDefinition& definition,
    const std::filesystem::path& workspace,
    const CalibrationProfile& calibration,
    const std::string& run_id,
    const std::atomic_bool* cancelled = nullptr);

}  // namespace univision::industrial
