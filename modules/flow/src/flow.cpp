#include "univision/industrial/flow.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace univision::industrial {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;

enum class PortType { none, metric_profile, measurement, decision };
struct NodeSpec { PortType input; PortType output; };
Result<NodeSpec> node_spec(const FlowNode& node) {
  if (node.version != "0.1.0")
    return Status{ErrorCode::unsupported, "node version is not registered: " +
                                            node.id};
  if (node.type == "source.profile_file")
    return NodeSpec{PortType::none, PortType::metric_profile};
  if (node.type == "measure.circle_fit")
    return NodeSpec{PortType::metric_profile, PortType::measurement};
  if (node.type == "judge.verification")
    return NodeSpec{PortType::measurement, PortType::decision};
  if (node.type == "sink.result_archive")
    return NodeSpec{PortType::decision, PortType::none};
  return Status{ErrorCode::unsupported, "node type is not registered: " +
                                          node.type};
}
Result<std::string> parameter(const FlowNode& node, const std::string& name) {
  const auto found = node.parameters.find(name);
  if (found == node.parameters.end() || found->second.empty())
    return Status{ErrorCode::invalid_argument,
                  "node " + node.id + " lacks parameter " + name};
  return found->second;
}
Result<double> finite_double(const std::string& text) {
  double value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      !std::isfinite(value))
    return Status{ErrorCode::invalid_argument, "invalid numeric flow parameter"};
  return value;
}
bool under_root(const fs::path& root, const fs::path& path) {
  const auto canonical_root = fs::weakly_canonical(root);
  const auto canonical_path = fs::weakly_canonical(path);
  auto parent = canonical_root.begin(), child = canonical_path.begin();
  for (; parent != canonical_root.end(); ++parent, ++child)
    if (child == canonical_path.end() || *parent != *child) return false;
  return true;
}
Result<std::vector<std::byte>> read_input(const fs::path& file,
                                          std::uint64_t limit) {
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  if (ec) return Status{ErrorCode::io_error, "cannot stat profile file"};
  if (size > limit)
    return Status{ErrorCode::buffer_exhausted, "profile file exceeds budget"};
  std::ifstream input(file, std::ios::binary);
  if (!input) return Status{ErrorCode::io_error, "cannot open profile file"};
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  if (size != 0)
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(size));
  if (!input && size != 0)
    return Status{ErrorCode::io_error, "cannot read profile file"};
  return bytes;
}
Result<Profile> parse_profile(std::span<const std::byte> bytes,
                              const CalibrationProfile& calibration,
                              bool synthetic) {
  std::string input;
  if (!bytes.empty())
    input.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::istringstream lines(input);
  std::string line;
  if (!std::getline(lines, line))
    return Status{ErrorCode::invalid_argument, "empty profile CSV"};
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line != "x_mm,z_mm,valid")
    return Status{ErrorCode::invalid_argument, "unsupported profile CSV header"};
  Profile profile;
  profile.unit = Unit::millimeter;
  profile.coordinate_frame = calibration.coordinate_frame;
  profile.calibration_id = calibration.id;
  profile.synthetic = synthetic;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (profile.points.size() >= 100000)
      return Status{ErrorCode::buffer_exhausted, "too many profile points"};
    const auto first = line.find(',');
    const auto second = first == std::string::npos
                            ? std::string::npos : line.find(',', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        line.find(',', second + 1) != std::string::npos)
      return Status{ErrorCode::invalid_argument, "invalid profile CSV row"};
    auto x = finite_double(line.substr(0, first));
    auto z = finite_double(line.substr(first + 1, second - first - 1));
    const auto valid = line.substr(second + 1);
    if (!x || !z || (valid != "0" && valid != "1"))
      return Status{ErrorCode::invalid_argument, "invalid profile CSV value"};
    profile.points.push_back({x.value(), z.value(), valid == "1"});
  }
  return profile;
}
std::string decision_text(Decision decision) {
  switch (decision) {
    case Decision::pass: return "pass";
    case Decision::fail: return "fail";
    case Decision::indeterminate: return "indeterminate";
    case Decision::not_evaluated: return "not-evaluated";
  }
  return "not-evaluated";
}
std::string validity_text(MeasurementValidity validity) {
  switch (validity) {
    case MeasurementValidity::valid: return "valid";
    case MeasurementValidity::invalid: return "invalid";
    case MeasurementValidity::synthetic: return "synthetic";
  }
  return "invalid";
}
json graph_json(const FlowDefinition& definition) {
  json nodes = json::array(), edges = json::array();
  for (const auto& node : definition.nodes)
    nodes.push_back({{"id", node.id}, {"type", node.type},
                     {"version", node.version}, {"parameters", node.parameters}});
  for (const auto& edge : definition.edges)
    edges.push_back({{"from", edge.from}, {"from_port", edge.from_port},
                     {"to", edge.to}, {"to_port", edge.to_port}});
  return {{"schema_version", definition.schema_version},
          {"flow_id", definition.flow_id}, {"revision", definition.revision},
          {"execution_mode", definition.execution_mode}, {"nodes", nodes},
          {"edges", edges}, {"required_sinks", definition.required_sinks},
          {"max_owned_bytes", std::to_string(definition.max_owned_bytes)}};
}
std::string hash_text(const std::string& text) {
  return sha256_hex(std::span{
      reinterpret_cast<const std::byte*>(text.data()), text.size()});
}
}  // namespace

Result<FlowDefinition> load_flow_definition(const fs::path& file,
                                             std::uint64_t max_definition_bytes) {
  try {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return Status{ErrorCode::io_error, "cannot stat flow definition"};
    if (size == 0 || size > max_definition_bytes)
      return Status{ErrorCode::buffer_exhausted, "flow definition exceeds limit"};
    std::ifstream input(file, std::ios::binary);
    if (!input) return Status{ErrorCode::io_error, "cannot open flow definition"};
    const auto parsed = json::parse(input);
    FlowDefinition flow;
    flow.schema_version = parsed.at("schema_version").get<std::string>();
    flow.flow_id = parsed.at("flow_id").get<std::string>();
    flow.revision = parsed.at("revision").get<std::string>();
    flow.execution_mode = parsed.at("execution_mode").get<std::string>();
    for (const auto& item : parsed.at("nodes")) {
      FlowNode node;
      node.id = item.at("id").get<std::string>();
      node.type = item.at("type").get<std::string>();
      node.version = item.at("version").get<std::string>();
      const auto& parameters = item.at("parameters");
      if (!parameters.is_object())
        return Status{ErrorCode::invalid_argument, "node parameters must be an object"};
      for (auto entry = parameters.begin(); entry != parameters.end(); ++entry) {
        if (entry.value().is_string())
          node.parameters.emplace(entry.key(), entry.value().get<std::string>());
        else if (entry.value().is_number() || entry.value().is_boolean())
          node.parameters.emplace(entry.key(), entry.value().dump());
        else
          return Status{ErrorCode::invalid_argument,
                        "unsupported node parameter type"};
      }
      flow.nodes.push_back(std::move(node));
    }
    for (const auto& item : parsed.at("edges"))
      flow.edges.push_back({item.at("from").get<std::string>(),
                            item.at("from_port").get<std::string>(),
                            item.at("to").get<std::string>(),
                            item.at("to_port").get<std::string>()});
    flow.required_sinks =
        parsed.at("required_sinks").get<std::vector<std::string>>();
    const auto limit =
        parsed.at("limits").at("max_owned_bytes").get<std::string>();
    const auto [end, error] = std::from_chars(
        limit.data(), limit.data() + limit.size(), flow.max_owned_bytes);
    if (error != std::errc{} || end != limit.data() + limit.size())
      return Status{ErrorCode::invalid_argument, "invalid owned-byte limit"};
    return flow;
  } catch (const std::exception& error) {
    return Status{ErrorCode::invalid_argument,
                  std::string{"invalid flow definition: "} + error.what()};
  }
}

Result<FlowPreflight> preflight_flow(const FlowDefinition& definition) {
  if (definition.schema_version != "0.1.0-draft" ||
      definition.execution_mode != "offline")
    return Status{ErrorCode::unsupported, "flow version or mode is unsupported"};
  if (definition.flow_id.empty() || definition.revision.empty() ||
      definition.nodes.empty() || definition.nodes.size() > 1024 ||
      definition.max_owned_bytes == 0 ||
      definition.max_owned_bytes > 256ULL * 1024 * 1024)
    return Status{ErrorCode::invalid_argument, "flow identity or budget is invalid"};
  std::map<std::string, const FlowNode*> nodes;
  std::map<std::string, NodeSpec> specs;
  std::map<std::string, std::size_t> indegree;
  std::map<std::string, std::vector<std::string>> successors;
  std::map<std::string, std::size_t> incoming;
  std::map<std::string, std::size_t> kind_count;
  for (const auto& node : definition.nodes) {
    if (node.id.empty() || !nodes.emplace(node.id, &node).second)
      return Status{ErrorCode::invalid_argument, "duplicate or empty node ID"};
    auto spec = node_spec(node);
    if (!spec) return spec.status();
    specs.emplace(node.id, spec.value());
    indegree.emplace(node.id, 0);
    ++kind_count[node.type];
  }
  if (kind_count["source.profile_file"] != 1 ||
      kind_count["measure.circle_fit"] != 1 ||
      kind_count["judge.verification"] != 1 ||
      kind_count["sink.result_archive"] != 1 ||
      definition.nodes.size() != 4)
    return Status{ErrorCode::unsupported,
                  "first flow runtime requires one node of each registered type"};
  for (const auto& edge : definition.edges) {
    const auto from = specs.find(edge.from), to = specs.find(edge.to);
    if (from == specs.end() || to == specs.end() ||
        edge.from_port != "out" || edge.to_port != "in" ||
        from->second.output == PortType::none ||
        from->second.output != to->second.input)
      return Status{ErrorCode::invalid_argument,
                    "flow edge has an unknown or incompatible port"};
    if (++incoming[edge.to] > 1)
      return Status{ErrorCode::invalid_argument, "node has multiple inputs"};
    ++indegree[edge.to];
    successors[edge.from].push_back(edge.to);
  }
  for (const auto& [id, spec] : specs)
    if ((spec.input == PortType::none && incoming[id] != 0) ||
        (spec.input != PortType::none && incoming[id] != 1))
      return Status{ErrorCode::invalid_argument, "required node input is unconnected"};
  if (definition.required_sinks.size() != 1 ||
      definition.required_sinks.front() !=
          [&]() {
            for (const auto& node : definition.nodes)
              if (node.type == "sink.result_archive") return node.id;
            return std::string{};
          }())
    return Status{ErrorCode::invalid_argument, "required archive sink is missing"};
  std::queue<std::string> ready;
  for (const auto& [id, degree] : indegree)
    if (degree == 0) ready.push(id);
  FlowPreflight result;
  while (!ready.empty()) {
    auto id = ready.front(); ready.pop();
    result.topological_order.push_back(id);
    for (const auto& next : successors[id])
      if (--indegree[next] == 0) ready.push(next);
  }
  if (result.topological_order.size() != nodes.size())
    return Status{ErrorCode::invalid_argument, "flow contains a cycle"};
  const auto source = *std::find_if(definition.nodes.begin(), definition.nodes.end(),
      [](const FlowNode& n) { return n.type == "source.profile_file"; });
  auto path = parameter(source, "artifact_path");
  auto kind = parameter(source, "source_kind");
  if (!path || !valid_artifact_path(path.value()) || !kind ||
      (kind.value() != "synthetic" && kind.value() != "file"))
    return Status{ErrorCode::invalid_argument, "profile source is not authorized"};
  for (const auto& node : definition.nodes) {
    if (node.type == "measure.circle_fit" &&
        !parameter(node, "calibration_id"))
      return Status{ErrorCode::invalid_argument, "measurement lacks calibration"};
    if (node.type == "judge.verification") {
      auto limit = parameter(node, "error_limit_mm");
      if (!limit) return limit.status();
      auto number = finite_double(limit.value());
      if (!number || number.value() <= 0)
        return Status{ErrorCode::invalid_argument, "invalid error limit"};
    }
    if (node.type == "sink.result_archive") {
      auto space = parameter(node, "output_namespace");
      if (!space || !valid_artifact_path(space.value()))
        return Status{ErrorCode::invalid_argument, "invalid output namespace"};
    }
  }
  return result;
}

Result<FlowRun> execute_flow(const FlowDefinition& definition,
                             const fs::path& workspace,
                             const CalibrationProfile& calibration,
                             const std::string& run_id,
                             const std::atomic_bool* cancelled) {
  const FlowDefinition frozen = definition;
  auto checked = preflight_flow(frozen);
  if (!checked) return checked.status();
  if (!valid_artifact_path(run_id) || run_id.find('/') != std::string::npos)
    return Status{ErrorCode::invalid_argument, "invalid run ID"};
  FlowRun run;
  run.run_id = run_id;
  run.flow_id = frozen.flow_id;
  run.graph_revision = frozen.revision;
  run.graph_sha256 = hash_text(graph_json(frozen).dump());
  if (cancelled && cancelled->load()) {
    run.state = FlowRunState::cancelled;
    return run;
  }
  run.state = FlowRunState::preparing;
  const FlowNode *source = nullptr, *fit = nullptr, *judge = nullptr, *archive = nullptr;
  for (const auto& node : frozen.nodes) {
    if (node.type == "source.profile_file") source = &node;
    if (node.type == "measure.circle_fit") fit = &node;
    if (node.type == "judge.verification") judge = &node;
    if (node.type == "sink.result_archive") archive = &node;
  }
  if (fit->parameters.at("calibration_id") != calibration.id)
    return Status{ErrorCode::invalid_argument, "frozen calibration version differs"};
  try {
    const auto source_path = workspace / source->parameters.at("artifact_path");
    const auto output_namespace = archive->parameters.at("output_namespace");
    const auto output_path = workspace / output_namespace / (run_id + ".json");
    if (!under_root(workspace, source_path) ||
        !under_root(workspace, output_path))
      return Status{ErrorCode::access_denied, "flow artifact escapes workspace"};
    if (fs::exists(output_path) || fs::exists(output_path.string() + ".tmp"))
      return Status{ErrorCode::already_exists, "run artifact already exists"};
    auto bytes = read_input(source_path, frozen.max_owned_bytes);
    if (!bytes) return bytes.status();
    run.input_sha256 = sha256_hex(bytes.value());
    const bool synthetic = source->parameters.at("source_kind") == "synthetic";
    auto profile = parse_profile(bytes.value(), calibration, synthetic);
    if (!profile) return profile.status();
    run.state = FlowRunState::running;
    if (cancelled && cancelled->load()) {
      run.state = FlowRunState::cancelled;
      return run;
    }
    run.measurement = fit_circle_diameter(profile.value(), &calibration);
    run.decision = Decision::indeterminate;
    if (run.measurement.validity == MeasurementValidity::valid &&
        run.measurement.diameter_mm) {
      const auto reference = judge->parameters.find("reference_diameter_mm");
      const auto uncertainty = judge->parameters.find("expanded_uncertainty_mm");
      if (reference != judge->parameters.end() &&
          uncertainty != judge->parameters.end()) {
        auto ref = finite_double(reference->second);
        auto u = finite_double(uncertainty->second);
        auto limit = finite_double(judge->parameters.at("error_limit_mm"));
        if (!ref || !u || !limit)
          return Status{ErrorCode::invalid_argument, "invalid judge parameters"};
        run.decision = verification_decision(
            *run.measurement.diameter_mm - ref.value(),
            u.value(), limit.value());
      } else {
        run.decision = Decision::not_evaluated;
      }
    } else if (run.measurement.validity == MeasurementValidity::synthetic) {
      run.decision = Decision::not_evaluated;
    }
    if (cancelled && cancelled->load()) {
      run.state = FlowRunState::cancelled;
      return run;
    }
    run.state = FlowRunState::finalizing;
    const auto manifest = json{
        {"schema_version", "0.1.0-draft"}, {"run_id", run.run_id},
        {"flow_id", run.flow_id}, {"graph_revision", run.graph_revision},
        {"graph_sha256", run.graph_sha256}, {"input_sha256", run.input_sha256},
        {"state", "succeeded"}, {"decision", decision_text(run.decision)},
        {"measurement", {{"validity", validity_text(run.measurement.validity)},
                         {"diameter_mm", run.measurement.diameter_mm
                             ? json(*run.measurement.diameter_mm) : json(nullptr)},
                         {"residual_rms_mm", run.measurement.residual_rms_mm},
                         {"point_count", run.measurement.point_count},
                         {"method_version", run.measurement.method_version},
                         {"calibration_id", run.measurement.calibration_id},
                         {"reason", run.measurement.reason}}}};
    fs::create_directories(output_path.parent_path());
    const auto temporary = fs::path(output_path.string() + ".tmp");
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output) return Status{ErrorCode::io_error, "cannot write run archive"};
      output << manifest.dump(2) << '\n';
      output.flush();
      if (!output) return Status{ErrorCode::io_error, "cannot flush run archive"};
    }
    fs::rename(temporary, output_path);
    run.artifact_path = (fs::path(output_namespace) / (run_id + ".json")).generic_string();
    run.state = FlowRunState::succeeded;
    return run;
  } catch (const std::exception& error) {
    return Status{ErrorCode::io_error, error.what()};
  }
}

}  // namespace univision::industrial
