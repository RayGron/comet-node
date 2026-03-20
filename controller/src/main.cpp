#include <ctime>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "comet/compose_renderer.h"
#include "comet/demo_state.h"
#include "comet/execution_plan.h"
#include "comet/import_bundle.h"
#include "comet/infer_runtime_config.h"
#include "comet/models.h"
#include "comet/planner.h"
#include "comet/reconcile.h"
#include "comet/runtime_status.h"
#include "comet/scheduling_policy.h"
#include "comet/sqlite_store.h"
#include "comet/state_json.h"

namespace {

std::string DefaultDbPath() {
  return (std::filesystem::path("var") / "controller.sqlite").string();
}

std::string DefaultArtifactsRoot() {
  return (std::filesystem::path("var") / "artifacts").string();
}

int DefaultStaleAfterSeconds() {
  return 300;
}

void PrintUsage() {
  std::cout
      << "Usage:\n"
      << "  comet-controller show-demo-plan\n"
      << "  comet-controller render-demo-compose [--node <node-name>]\n"
      << "  comet-controller init-db [--db <path>]\n"
      << "  comet-controller seed-demo [--db <path>]\n"
      << "  comet-controller validate-bundle --bundle <dir>\n"
      << "  comet-controller preview-bundle --bundle <dir> [--node <node-name>]\n"
      << "  comet-controller plan-bundle --bundle <dir> [--db <path>]\n"
      << "  comet-controller plan-host-ops --bundle <dir> [--db <path>] [--artifacts-root <path>] [--node <node-name>]\n"
      << "  comet-controller apply-bundle --bundle <dir> [--db <path>] [--artifacts-root <path>]\n"
      << "  comet-controller import-bundle --bundle <dir> [--db <path>]\n"
      << "  comet-controller show-host-assignments [--db <path>] [--node <node-name>]\n"
      << "  comet-controller show-host-observations [--db <path>] [--node <node-name>] [--stale-after <seconds>]\n"
      << "  comet-controller show-host-health [--db <path>] [--node <node-name>] [--stale-after <seconds>]\n"
      << "  comet-controller show-node-availability [--db <path>] [--node <node-name>]\n"
      << "  comet-controller set-node-availability --node <node-name> --availability <active|draining|unavailable> [--message <text>] [--db <path>]\n"
      << "  comet-controller retry-host-assignment --id <assignment-id> [--db <path>]\n"
      << "  comet-controller show-state [--db <path>]\n"
      << "  comet-controller render-infer-runtime [--db <path>]\n"
      << "  comet-controller render-compose [--db <path>] [--node <node-name>]\n";
}

std::optional<std::string> ParseNodeArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--node" && index + 1 < argc) {
      return std::string(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ParseDbArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--db" && index + 1 < argc) {
      return std::string(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ParseBundleArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--bundle" && index + 1 < argc) {
      return std::string(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ParseArtifactsRootArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--artifacts-root" && index + 1 < argc) {
      return std::string(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<int> ParseIdArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--id" && index + 1 < argc) {
      return std::stoi(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<int> ParseStaleAfterArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--stale-after" && index + 1 < argc) {
      return std::stoi(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ParseAvailabilityArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--availability" && index + 1 < argc) {
      return std::string(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::optional<std::string> ParseMessageArg(int argc, char** argv) {
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--message" && index + 1 < argc) {
      return std::string(argv[index + 1]);
    }
  }
  return std::nullopt;
}

std::string ResolveDbPath(const std::optional<std::string>& db_arg) {
  return db_arg.value_or(DefaultDbPath());
}

std::string ResolveArtifactsRoot(const std::optional<std::string>& artifacts_root_arg) {
  return artifacts_root_arg.value_or(DefaultArtifactsRoot());
}

std::map<std::string, comet::NodeAvailabilityOverride> BuildAvailabilityOverrideMap(
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides);

void PrintAssignmentDispatchSummary(
    const comet::DesiredState& desired_state,
    const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides);

void PrintStateSummary(const comet::DesiredState& state) {
  std::cout << "plane: " << state.plane_name << "\n";
  std::cout << "control_root: " << state.control_root << "\n";
  std::cout << "inference:\n";
  std::cout << "  primary_infer_node=" << state.inference.primary_infer_node
            << " net_if=" << state.inference.net_if
            << " llama_port=" << state.inference.llama_port << "\n";
  std::cout << "gateway:\n";
  std::cout << "  listen=" << state.gateway.listen_host << ":" << state.gateway.listen_port
            << " server_name=" << state.gateway.server_name << "\n";
  std::cout << "nodes:\n";
  for (const auto& node : state.nodes) {
    std::cout << "  - " << node.name << " (" << node.platform << "), gpus=";
    for (std::size_t index = 0; index < node.gpu_devices.size(); ++index) {
      if (index > 0) {
        std::cout << ",";
      }
      std::cout << node.gpu_devices[index];
    }
    std::cout << "\n";
  }

  std::cout << "instances:\n";
  for (const auto& instance : state.instances) {
    std::cout << "  - " << instance.name
              << " role=" << comet::ToString(instance.role)
              << " node=" << instance.node_name;
    if (instance.gpu_device.has_value()) {
      std::cout << " gpu=" << *instance.gpu_device
                << " fraction=" << instance.gpu_fraction;
    }
    std::cout << "\n";
  }
}

void ShowDemoPlan() {
  PrintStateSummary(comet::BuildDemoState());
}

void PrintPreviewSummary(const comet::DesiredState& state) {
  std::cout << "preview:\n";
  std::cout << "  plane=" << state.plane_name << "\n";
  std::cout << "  nodes=" << state.nodes.size() << "\n";
  std::cout << "  disks=" << state.disks.size() << "\n";
  std::cout << "  instances=" << state.instances.size() << "\n";

  const auto node_plans = comet::BuildNodeComposePlans(state);
  for (const auto& plan : node_plans) {
    std::cout << "  node " << plan.node_name
              << ": services=" << plan.services.size()
              << " disks=" << plan.disks.size() << "\n";
  }
}

int RenderComposeForState(
    const comet::DesiredState& state,
    const std::optional<std::string>& node_name) {
  if (node_name.has_value()) {
    const auto plan = comet::FindNodeComposePlan(state, *node_name);
    if (!plan.has_value()) {
      std::cerr << "error: node '" << *node_name << "' not found in state\n";
      return 1;
    }
    std::cout << comet::RenderComposeYaml(*plan);
    return 0;
  }

  const auto plans = comet::BuildNodeComposePlans(state);
  for (std::size_t index = 0; index < plans.size(); ++index) {
    if (index > 0) {
      std::cout << "\n---\n";
    }
    std::cout << comet::RenderComposeYaml(plans[index]);
  }
  return 0;
}

int RenderDemoCompose(const std::optional<std::string>& node_name) {
  return RenderComposeForState(comet::BuildDemoState(), node_name);
}

int ValidateBundle(const std::string& bundle_dir) {
  const comet::DesiredState state = comet::ImportPlaneBundle(bundle_dir);
  comet::RequireSchedulingPolicy(state);
  std::cout << "bundle validation: OK\n";
  PrintPreviewSummary(state);
  std::cout << comet::RenderSchedulingPolicyReport(
      comet::EvaluateSchedulingPolicy(state));
  return 0;
}

int PreviewBundle(const std::string& bundle_dir, const std::optional<std::string>& node_name) {
  const comet::DesiredState state = comet::ImportPlaneBundle(bundle_dir);
  PrintPreviewSummary(state);
  std::cout << comet::RenderSchedulingPolicyReport(
      comet::EvaluateSchedulingPolicy(state));
  std::cout << "\ncompose-preview:\n";
  return RenderComposeForState(state, node_name);
}

int PlanBundle(const std::string& db_path, const std::string& bundle_dir) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto current_state = store.LoadDesiredState();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const comet::ReconcilePlan plan =
      comet::BuildReconcilePlan(current_state, desired_state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(desired_state);

  std::cout << "bundle-plan:\n";
  std::cout << "  db=" << db_path << "\n";
  std::cout << "  bundle=" << bundle_dir << "\n";
  std::cout << comet::RenderReconcilePlan(plan);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  PrintAssignmentDispatchSummary(
      desired_state,
      BuildAvailabilityOverrideMap(availability_overrides));
  return 0;
}

std::vector<comet::NodeExecutionPlan> FilterNodeExecutionPlans(
    const std::vector<comet::NodeExecutionPlan>& plans,
    const std::optional<std::string>& node_name) {
  if (!node_name.has_value()) {
    return plans;
  }

  std::vector<comet::NodeExecutionPlan> filtered;
  for (const auto& plan : plans) {
    if (plan.node_name == *node_name) {
      filtered.push_back(plan);
    }
  }

  if (filtered.empty()) {
    throw std::runtime_error("node '" + *node_name + "' not found in execution plan");
  }

  return filtered;
}

std::map<std::string, comet::NodeComposePlan> BuildComposePlanMap(const comet::DesiredState& state) {
  std::map<std::string, comet::NodeComposePlan> result;
  for (const auto& plan : comet::BuildNodeComposePlans(state)) {
    result.emplace(plan.node_name, plan);
  }
  return result;
}

std::map<std::string, comet::NodeAvailabilityOverride> BuildAvailabilityOverrideMap(
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
  std::map<std::string, comet::NodeAvailabilityOverride> result;
  for (const auto& availability_override : availability_overrides) {
    result.emplace(availability_override.node_name, availability_override);
  }
  return result;
}

comet::NodeAvailability ResolveNodeAvailability(
    const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides,
    const std::string& node_name) {
  const auto it = availability_overrides.find(node_name);
  if (it == availability_overrides.end()) {
    return comet::NodeAvailability::Active;
  }
  return it->second.availability;
}

bool IsNodeSchedulable(comet::NodeAvailability availability) {
  return availability == comet::NodeAvailability::Active;
}

void PrintNodeAvailabilityOverrides(
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
  std::cout << "node-availability:\n";
  if (availability_overrides.empty()) {
    std::cout << "  (empty)\n";
    return;
  }

  for (const auto& availability_override : availability_overrides) {
    std::cout << "  - node=" << availability_override.node_name
              << " availability=" << comet::ToString(availability_override.availability)
              << " updated_at=" << availability_override.updated_at << "\n";
    if (!availability_override.status_message.empty()) {
      std::cout << "    message=" << availability_override.status_message << "\n";
    }
  }
}

void PrintAssignmentDispatchSummary(
    const comet::DesiredState& desired_state,
    const std::map<std::string, comet::NodeAvailabilityOverride>& availability_overrides) {
  std::size_t schedulable_nodes = 0;
  std::vector<std::string> skipped_nodes;
  for (const auto& node : desired_state.nodes) {
    const auto availability = ResolveNodeAvailability(availability_overrides, node.name);
    if (IsNodeSchedulable(availability)) {
      ++schedulable_nodes;
    } else {
      skipped_nodes.push_back(
          node.name + "(" + comet::ToString(availability) + ")");
    }
  }

  std::cout << "assignment-dispatch:\n";
  std::cout << "  schedulable_nodes=" << schedulable_nodes << "/" << desired_state.nodes.size()
            << "\n";
  if (!skipped_nodes.empty()) {
    std::cout << "  skipped_nodes=";
    for (std::size_t index = 0; index < skipped_nodes.size(); ++index) {
      if (index > 0) {
        std::cout << ",";
      }
      std::cout << skipped_nodes[index];
    }
    std::cout << "\n";
  }
}

void WriteTextFile(const std::string& path, const std::string& contents) {
  const std::filesystem::path file_path(path);
  if (file_path.has_parent_path()) {
    std::filesystem::create_directories(file_path.parent_path());
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open file for write: " + path);
  }
  out << contents;
  if (!out.good()) {
    throw std::runtime_error("failed to write file: " + path);
  }
}

void RemoveFileIfExists(const std::string& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
  if (error) {
    throw std::runtime_error("failed to remove file '" + path + "': " + error.message());
  }
}

void MaterializeComposeArtifacts(
    const comet::DesiredState& desired_state,
    const std::vector<comet::NodeExecutionPlan>& host_plans) {
  const auto desired_compose_plans = BuildComposePlanMap(desired_state);

  for (const auto& host_plan : host_plans) {
    for (const auto& operation : host_plan.operations) {
      if (operation.kind == comet::HostOperationKind::WriteComposeFile) {
        const auto compose_it = desired_compose_plans.find(host_plan.node_name);
        if (compose_it == desired_compose_plans.end()) {
          throw std::runtime_error(
              "missing compose plan for node '" + host_plan.node_name + "'");
        }
        WriteTextFile(operation.target, comet::RenderComposeYaml(compose_it->second));
      }

      if (operation.kind == comet::HostOperationKind::RemoveComposeFile) {
        RemoveFileIfExists(operation.target);
      }
    }
  }
}

std::string InferRuntimeArtifactPath(
    const std::string& artifacts_root,
    const std::string& plane_name) {
  return (
      std::filesystem::path(artifacts_root) / plane_name / "infer-runtime.json").string();
}

void MaterializeInferRuntimeArtifact(
    const comet::DesiredState& desired_state,
    const std::string& artifacts_root) {
  WriteTextFile(
      InferRuntimeArtifactPath(artifacts_root, desired_state.plane_name),
      comet::RenderInferRuntimeConfigJson(desired_state));
}

std::vector<comet::HostAssignment> BuildHostAssignments(
    const comet::DesiredState& desired_state,
    const std::string& artifacts_root,
    int desired_generation,
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides) {
  std::vector<comet::HostAssignment> assignments;
  assignments.reserve(desired_state.nodes.size());
  const auto availability_override_map =
      BuildAvailabilityOverrideMap(availability_overrides);

  for (const auto& node : desired_state.nodes) {
    if (!IsNodeSchedulable(
            ResolveNodeAvailability(availability_override_map, node.name))) {
      continue;
    }
    comet::HostAssignment assignment;
    assignment.node_name = node.name;
    assignment.plane_name = desired_state.plane_name;
    assignment.desired_generation = desired_generation;
    assignment.assignment_type = "apply-node-state";
    assignment.desired_state_json =
        comet::SerializeDesiredStateJson(
            comet::SliceDesiredStateForNode(desired_state, node.name));
    assignment.artifacts_root = artifacts_root;
    assignment.status = comet::HostAssignmentStatus::Pending;
    assignments.push_back(std::move(assignment));
  }

  return assignments;
}

std::optional<comet::HostAssignment> FindLatestHostAssignmentForNode(
    const std::vector<comet::HostAssignment>& assignments,
    const std::string& node_name) {
  std::optional<comet::HostAssignment> result;
  for (const auto& assignment : assignments) {
    if (assignment.node_name != node_name) {
      continue;
    }
    result = assignment;
  }
  return result;
}

std::optional<comet::HostAssignment> FindLatestHostAssignmentForPlane(
    const std::vector<comet::HostAssignment>& assignments,
    const std::string& plane_name) {
  std::optional<comet::HostAssignment> result;
  for (const auto& assignment : assignments) {
    if (assignment.plane_name != plane_name) {
      continue;
    }
    result = assignment;
  }
  return result;
}

std::optional<comet::HostAssignment> BuildResyncAssignmentForNode(
    const comet::DesiredState& desired_state,
    int desired_generation,
    const std::string& node_name,
    const std::vector<comet::HostAssignment>& existing_assignments,
    const std::optional<comet::HostObservation>& observation) {
  bool node_exists = false;
  for (const auto& node : desired_state.nodes) {
    if (node.name == node_name) {
      node_exists = true;
      break;
    }
  }
  if (!node_exists) {
    return std::nullopt;
  }

  if (observation.has_value() &&
      observation->applied_generation.has_value() &&
      *observation->applied_generation == desired_generation &&
      observation->status != comet::HostObservationStatus::Failed) {
    return std::nullopt;
  }

  const auto latest_assignment = FindLatestHostAssignmentForNode(existing_assignments, node_name);
  if (latest_assignment.has_value() &&
      latest_assignment->desired_generation == desired_generation &&
      (latest_assignment->status == comet::HostAssignmentStatus::Pending ||
       latest_assignment->status == comet::HostAssignmentStatus::Claimed ||
       latest_assignment->status == comet::HostAssignmentStatus::Applied)) {
    return std::nullopt;
  }

  comet::HostAssignment assignment;
  assignment.node_name = node_name;
  assignment.plane_name = desired_state.plane_name;
  assignment.desired_generation = desired_generation;
  assignment.assignment_type = "apply-node-state";
  assignment.desired_state_json =
      comet::SerializeDesiredStateJson(
          comet::SliceDesiredStateForNode(desired_state, node_name));
  const auto plane_assignment =
      FindLatestHostAssignmentForPlane(existing_assignments, desired_state.plane_name);
  assignment.artifacts_root = latest_assignment.has_value()
                                  ? latest_assignment->artifacts_root
                                  : (plane_assignment.has_value()
                                         ? plane_assignment->artifacts_root
                                         : DefaultArtifactsRoot());
  assignment.status = comet::HostAssignmentStatus::Pending;
  assignment.status_message = "resync after node returned to active";
  return assignment;
}

std::optional<comet::NodeInventory> FindNodeInventory(
    const comet::DesiredState& desired_state,
    const std::string& node_name) {
  for (const auto& node : desired_state.nodes) {
    if (node.name == node_name) {
      return node;
    }
  }
  return std::nullopt;
}

std::optional<comet::HostAssignment> BuildDrainAssignmentForNode(
    const comet::DesiredState& desired_state,
    int desired_generation,
    const std::string& node_name,
    const std::vector<comet::HostAssignment>& existing_assignments) {
  const auto node = FindNodeInventory(desired_state, node_name);
  if (!node.has_value()) {
    return std::nullopt;
  }

  comet::DesiredState drain_state;
  drain_state.plane_name = desired_state.plane_name;
  drain_state.plane_shared_disk_name = desired_state.plane_shared_disk_name;
  drain_state.control_root = desired_state.control_root;
  drain_state.inference = desired_state.inference;
  drain_state.gateway = desired_state.gateway;
  drain_state.runtime_gpu_nodes = desired_state.runtime_gpu_nodes;
  drain_state.nodes.push_back(*node);

  const auto latest_assignment = FindLatestHostAssignmentForNode(existing_assignments, node_name);
  const auto plane_assignment =
      FindLatestHostAssignmentForPlane(existing_assignments, desired_state.plane_name);

  comet::HostAssignment assignment;
  assignment.node_name = node_name;
  assignment.plane_name = desired_state.plane_name;
  assignment.desired_generation = desired_generation;
  assignment.assignment_type = "drain-node-state";
  assignment.desired_state_json = comet::SerializeDesiredStateJson(drain_state);
  assignment.artifacts_root = latest_assignment.has_value()
                                  ? latest_assignment->artifacts_root
                                  : (plane_assignment.has_value()
                                         ? plane_assignment->artifacts_root
                                         : DefaultArtifactsRoot());
  assignment.status = comet::HostAssignmentStatus::Pending;
  assignment.status_message = "drain after node availability changed";
  return assignment;
}

void PrintHostAssignments(const std::vector<comet::HostAssignment>& assignments) {
  std::cout << "host-assignments:\n";
  if (assignments.empty()) {
    std::cout << "  (empty)\n";
    return;
  }

  for (const auto& assignment : assignments) {
    const comet::DesiredState desired_node_state =
        comet::DeserializeDesiredStateJson(assignment.desired_state_json);
    std::cout << "  - id=" << assignment.id
              << " node=" << assignment.node_name
              << " plane=" << assignment.plane_name
              << " generation=" << assignment.desired_generation
              << " attempts=" << assignment.attempt_count << "/" << assignment.max_attempts
              << " type=" << assignment.assignment_type
              << " status=" << comet::ToString(assignment.status)
              << " instances=" << desired_node_state.instances.size()
              << " artifacts_root=" << assignment.artifacts_root << "\n";
    if (!assignment.status_message.empty()) {
      std::cout << "    message=" << assignment.status_message << "\n";
    }
  }
}

std::time_t ToUtcTime(std::tm* timestamp) {
#if defined(_WIN32)
  return _mkgmtime(timestamp);
#else
  return timegm(timestamp);
#endif
}

std::optional<long long> HeartbeatAgeSeconds(const std::string& heartbeat_at) {
  if (heartbeat_at.empty()) {
    return std::nullopt;
  }

  std::tm heartbeat_tm{};
  std::istringstream input(heartbeat_at);
  input >> std::get_time(&heartbeat_tm, "%Y-%m-%d %H:%M:%S");
  if (input.fail()) {
    return std::nullopt;
  }

  const std::time_t heartbeat_time = ToUtcTime(&heartbeat_tm);
  if (heartbeat_time < 0) {
    return std::nullopt;
  }

  const std::time_t now = std::time(nullptr);
  return static_cast<long long>(now - heartbeat_time);
}

std::string HealthFromAge(
    const std::optional<long long>& age_seconds,
    int stale_after_seconds) {
  if (!age_seconds.has_value()) {
    return "unknown";
  }
  return *age_seconds > stale_after_seconds ? "stale" : "online";
}

std::optional<comet::RuntimeStatus> ParseRuntimeStatus(
    const comet::HostObservation& observation) {
  if (observation.runtime_status_json.empty()) {
    return std::nullopt;
  }
  return comet::DeserializeRuntimeStatusJson(observation.runtime_status_json);
}

void PrintHostObservations(
    const std::vector<comet::HostObservation>& observations,
    int stale_after_seconds) {
  std::cout << "host-observations:\n";
  if (observations.empty()) {
    std::cout << "  (empty)\n";
    return;
  }

  for (const auto& observation : observations) {
    std::size_t disk_count = 0;
    std::size_t instance_count = 0;
    if (!observation.observed_state_json.empty()) {
      const comet::DesiredState observed_state =
          comet::DeserializeDesiredStateJson(observation.observed_state_json);
      disk_count = observed_state.disks.size();
      instance_count = observed_state.instances.size();
    }
    const auto runtime_status = ParseRuntimeStatus(observation);
    const auto age_seconds = HeartbeatAgeSeconds(observation.heartbeat_at);

    std::cout << "  - node=" << observation.node_name
              << " plane=" << (observation.plane_name.empty() ? "(none)" : observation.plane_name)
              << " status=" << comet::ToString(observation.status);
    if (observation.applied_generation.has_value()) {
      std::cout << " applied_generation=" << *observation.applied_generation;
    }
    if (observation.last_assignment_id.has_value()) {
      std::cout << " last_assignment_id=" << *observation.last_assignment_id;
    }
    std::cout << " disks=" << disk_count
              << " instances=" << instance_count
              << " heartbeat_at=" << observation.heartbeat_at;
    if (age_seconds.has_value()) {
      std::cout << " age_seconds=" << *age_seconds
                << " health=" << HealthFromAge(age_seconds, stale_after_seconds);
    }
    if (runtime_status.has_value()) {
      std::cout << " runtime_backend="
                << (runtime_status->runtime_backend.empty()
                        ? "(empty)"
                        : runtime_status->runtime_backend)
                << " runtime_phase="
                << (runtime_status->runtime_phase.empty()
                        ? "(empty)"
                        : runtime_status->runtime_phase)
                << " runtime_launch_ready="
                << (runtime_status->launch_ready ? "yes" : "no")
                << " runtime_model="
                << (runtime_status->active_model_id.empty()
                        ? "(empty)"
                        : runtime_status->active_model_id)
                << " gateway="
                << (runtime_status->gateway_listen.empty()
                        ? "(empty)"
                        : runtime_status->gateway_listen);
    }
    std::cout << "\n";
    if (!observation.status_message.empty()) {
      std::cout << "    message=" << observation.status_message << "\n";
    }
    if (runtime_status.has_value()) {
      std::cout << "    runtime aliases=";
      if (runtime_status->aliases.empty()) {
        std::cout << "(empty)";
      } else {
        for (std::size_t index = 0; index < runtime_status->aliases.size(); ++index) {
          if (index > 0) {
            std::cout << ",";
          }
          std::cout << runtime_status->aliases[index];
        }
      }
      std::cout << " runtime_profile="
                << (runtime_status->active_runtime_profile.empty()
                        ? "(empty)"
                        : runtime_status->active_runtime_profile)
                << " inference_ready=" << (runtime_status->inference_ready ? "yes" : "no")
                << " gateway_ready=" << (runtime_status->gateway_ready ? "yes" : "no")
                << "\n";
    }
  }
}

void PrintHostHealth(
    const std::optional<comet::DesiredState>& desired_state,
    const std::vector<comet::HostObservation>& observations,
    const std::vector<comet::NodeAvailabilityOverride>& availability_overrides,
    const std::optional<std::string>& node_name,
    int stale_after_seconds) {
  std::map<std::string, comet::HostObservation> observation_by_node;
  for (const auto& observation : observations) {
    observation_by_node.emplace(observation.node_name, observation);
  }
  const auto availability_override_map =
      BuildAvailabilityOverrideMap(availability_overrides);

  std::vector<std::string> nodes;
  std::set<std::string> seen_nodes;
  if (desired_state.has_value()) {
    for (const auto& node : desired_state->nodes) {
      if (!node_name.has_value() || node.name == *node_name) {
        nodes.push_back(node.name);
        seen_nodes.insert(node.name);
      }
    }
  }
  for (const auto& [observed_node_name, observation] : observation_by_node) {
    (void)observation;
    if ((!node_name.has_value() || observed_node_name == *node_name) &&
        seen_nodes.find(observed_node_name) == seen_nodes.end()) {
      nodes.push_back(observed_node_name);
      seen_nodes.insert(observed_node_name);
    }
  }

  std::cout << "host-health:\n";
  if (nodes.empty()) {
    std::cout << "  (empty)\n";
    return;
  }

  int online_count = 0;
  int stale_count = 0;
  int unknown_count = 0;

  for (const auto& current_node_name : nodes) {
    const auto observation_it = observation_by_node.find(current_node_name);
    if (observation_it == observation_by_node.end()) {
      std::cout << "  - node=" << current_node_name
                << " availability="
                << comet::ToString(
                       ResolveNodeAvailability(availability_override_map, current_node_name))
                << " health=unknown status=(none)\n";
      ++unknown_count;
      continue;
    }

    const auto age_seconds = HeartbeatAgeSeconds(observation_it->second.heartbeat_at);
    const std::string health = HealthFromAge(age_seconds, stale_after_seconds);
    const auto runtime_status = ParseRuntimeStatus(observation_it->second);
    if (health == "online") {
      ++online_count;
    } else if (health == "stale") {
      ++stale_count;
    } else {
      ++unknown_count;
    }

    std::cout << "  - node=" << current_node_name
              << " availability="
              << comet::ToString(
                     ResolveNodeAvailability(availability_override_map, current_node_name))
              << " health=" << health
              << " status=" << comet::ToString(observation_it->second.status);
    if (observation_it->second.applied_generation.has_value()) {
      std::cout << " applied_generation=" << *observation_it->second.applied_generation;
    }
    if (age_seconds.has_value()) {
      std::cout << " age_seconds=" << *age_seconds;
    }
    if (observation_it->second.last_assignment_id.has_value()) {
      std::cout << " last_assignment_id=" << *observation_it->second.last_assignment_id;
    }
    if (runtime_status.has_value()) {
      std::cout << " runtime_backend="
                << (runtime_status->runtime_backend.empty()
                        ? "(empty)"
                        : runtime_status->runtime_backend)
                << " runtime_phase="
                << (runtime_status->runtime_phase.empty()
                        ? "(empty)"
                        : runtime_status->runtime_phase)
                << " runtime_launch_ready="
                << (runtime_status->launch_ready ? "yes" : "no")
                << " runtime_model="
                << (runtime_status->active_model_id.empty()
                        ? "(empty)"
                        : runtime_status->active_model_id);
    }
    std::cout << "\n";
    if (!observation_it->second.status_message.empty()) {
      std::cout << "    message=" << observation_it->second.status_message << "\n";
    }
  }

  std::cout << "summary: online=" << online_count
            << " stale=" << stale_count
            << " unknown=" << unknown_count << "\n";
}

int PlanHostOps(
    const std::string& db_path,
    const std::string& bundle_dir,
    const std::string& artifacts_root,
    const std::optional<std::string>& node_name) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto current_state = store.LoadDesiredState();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  const auto host_plans =
      FilterNodeExecutionPlans(
          comet::BuildNodeExecutionPlans(current_state, desired_state, artifacts_root),
          node_name);

  std::cout << "host-op-plan:\n";
  std::cout << "  db=" << db_path << "\n";
  std::cout << "  bundle=" << bundle_dir << "\n";
  std::cout << "  artifacts_root=" << artifacts_root << "\n";
  std::cout << comet::RenderNodeExecutionPlans(host_plans);
  return 0;
}

int InitDb(const std::string& db_path) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  std::cout << "initialized db: " << db_path << "\n";
  return 0;
}

int SeedDemo(const std::string& db_path) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const comet::DesiredState desired_state = comet::BuildDemoState();
  comet::RequireSchedulingPolicy(desired_state);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const int desired_generation = store.LoadDesiredGeneration().value_or(0) + 1;
  store.ReplaceDesiredState(desired_state, desired_generation);
  store.ReplaceHostAssignments(
      BuildHostAssignments(
          desired_state,
          DefaultArtifactsRoot(),
          desired_generation,
          availability_overrides));
  std::cout << "seeded demo state into: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  PrintAssignmentDispatchSummary(
      desired_state,
      BuildAvailabilityOverrideMap(availability_overrides));
  return 0;
}

int ImportBundle(const std::string& db_path, const std::string& bundle_dir) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  comet::RequireSchedulingPolicy(desired_state);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const int desired_generation = store.LoadDesiredGeneration().value_or(0) + 1;
  store.ReplaceDesiredState(desired_state, desired_generation);
  store.ReplaceHostAssignments(
      BuildHostAssignments(
          desired_state,
          DefaultArtifactsRoot(),
          desired_generation,
          availability_overrides));
  std::cout << "imported bundle '" << bundle_dir << "' into: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  PrintAssignmentDispatchSummary(
      desired_state,
      BuildAvailabilityOverrideMap(availability_overrides));
  return 0;
}

int ApplyBundle(
    const std::string& db_path,
    const std::string& bundle_dir,
    const std::string& artifacts_root) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto current_state = store.LoadDesiredState();
  const comet::DesiredState desired_state = comet::ImportPlaneBundle(bundle_dir);
  comet::RequireSchedulingPolicy(desired_state);
  const auto availability_overrides = store.LoadNodeAvailabilityOverrides();
  const int desired_generation = store.LoadDesiredGeneration().value_or(0) + 1;
  const comet::ReconcilePlan plan =
      comet::BuildReconcilePlan(current_state, desired_state);
  const comet::SchedulingPolicyReport scheduling_report =
      comet::EvaluateSchedulingPolicy(desired_state);
  const auto host_plans =
      comet::BuildNodeExecutionPlans(current_state, desired_state, artifacts_root);

  std::cout << "apply-plan:\n";
  std::cout << comet::RenderReconcilePlan(plan);
  std::cout << comet::RenderSchedulingPolicyReport(scheduling_report);
  std::cout << comet::RenderNodeExecutionPlans(host_plans);

  MaterializeComposeArtifacts(desired_state, host_plans);
  MaterializeInferRuntimeArtifact(desired_state, artifacts_root);
  store.ReplaceDesiredState(desired_state, desired_generation);
  store.ReplaceHostAssignments(
      BuildHostAssignments(
          desired_state,
          artifacts_root,
          desired_generation,
          availability_overrides));
  std::cout << "applied bundle '" << bundle_dir << "' into: " << db_path << "\n";
  std::cout << "desired generation: " << desired_generation << "\n";
  std::cout << "artifacts written under: " << artifacts_root << "\n";
  PrintAssignmentDispatchSummary(
      desired_state,
      BuildAvailabilityOverrideMap(availability_overrides));
  return 0;
}

int ShowHostAssignments(
    const std::string& db_path,
    const std::optional<std::string>& node_name) {
  comet::ControllerStore store(db_path);
  store.Initialize();

  std::cout << "db: " << db_path << "\n";
  PrintHostAssignments(store.LoadHostAssignments(node_name));
  return 0;
}

int ShowHostObservations(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    int stale_after_seconds) {
  comet::ControllerStore store(db_path);
  store.Initialize();

  std::cout << "db: " << db_path << "\n";
  std::cout << "stale_after_seconds: " << stale_after_seconds << "\n";
  PrintHostObservations(store.LoadHostObservations(node_name), stale_after_seconds);
  return 0;
}

int ShowNodeAvailability(
    const std::string& db_path,
    const std::optional<std::string>& node_name) {
  comet::ControllerStore store(db_path);
  store.Initialize();

  std::cout << "db: " << db_path << "\n";
  PrintNodeAvailabilityOverrides(store.LoadNodeAvailabilityOverrides(node_name));
  return 0;
}

int ShowHostHealth(
    const std::string& db_path,
    const std::optional<std::string>& node_name,
    int stale_after_seconds) {
  comet::ControllerStore store(db_path);
  store.Initialize();

  std::cout << "db: " << db_path << "\n";
  std::cout << "stale_after_seconds: " << stale_after_seconds << "\n";
  PrintHostHealth(
      store.LoadDesiredState(),
      store.LoadHostObservations(node_name),
      store.LoadNodeAvailabilityOverrides(node_name),
      node_name,
      stale_after_seconds);
  return 0;
}

int SetNodeAvailability(
    const std::string& db_path,
    const std::string& node_name,
    comet::NodeAvailability availability,
    const std::optional<std::string>& status_message) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto previous_override = store.LoadNodeAvailabilityOverride(node_name);
  const auto previous_availability =
      previous_override.has_value() ? previous_override->availability
                                    : comet::NodeAvailability::Active;

  comet::NodeAvailabilityOverride availability_override;
  availability_override.node_name = node_name;
  availability_override.availability = availability;
  availability_override.status_message = status_message.value_or("");
  store.UpsertNodeAvailabilityOverride(availability_override);

  std::cout << "updated node availability for " << node_name << "\n";
  PrintNodeAvailabilityOverrides(store.LoadNodeAvailabilityOverrides(node_name));

  const auto desired_state = store.LoadDesiredState();
  const auto desired_generation = store.LoadDesiredGeneration();
  if (desired_state.has_value() && desired_generation.has_value()) {
    if (previous_availability == comet::NodeAvailability::Active &&
        availability != comet::NodeAvailability::Active) {
      const auto drain_assignment = BuildDrainAssignmentForNode(
          *desired_state,
          *desired_generation,
          node_name,
          store.LoadHostAssignments());
      if (drain_assignment.has_value()) {
        store.EnqueueHostAssignments(
            {*drain_assignment},
            "superseded by node drain for desired generation " +
                std::to_string(*desired_generation));
        std::cout << "queued drain assignment for " << node_name
                  << " at desired generation " << *desired_generation << "\n";
        PrintHostAssignments(store.LoadHostAssignments(node_name));
      }
    }

    if (previous_availability != comet::NodeAvailability::Active &&
        availability == comet::NodeAvailability::Active) {
      const auto resync_assignment = BuildResyncAssignmentForNode(
          *desired_state,
          *desired_generation,
          node_name,
          store.LoadHostAssignments(),
          store.LoadHostObservation(node_name));
      if (resync_assignment.has_value()) {
        store.EnqueueHostAssignments(
            {*resync_assignment},
            "superseded by node reactivation for desired generation " +
                std::to_string(*desired_generation));
        std::cout << "queued resync assignment for " << node_name
                  << " at desired generation " << *desired_generation << "\n";
        PrintHostAssignments(store.LoadHostAssignments(node_name));
      } else {
        std::cout << "no resync assignment needed for " << node_name << "\n";
      }
    }
  }
  return 0;
}

int RetryHostAssignment(const std::string& db_path, int assignment_id) {
  comet::ControllerStore store(db_path);
  store.Initialize();

  const auto assignment = store.LoadHostAssignment(assignment_id);
  if (!assignment.has_value()) {
    throw std::runtime_error(
        "host assignment id=" + std::to_string(assignment_id) + " not found");
  }
  if (assignment->status != comet::HostAssignmentStatus::Failed) {
    throw std::runtime_error(
        "host assignment id=" + std::to_string(assignment_id) +
        " is not failed; current status=" + comet::ToString(assignment->status));
  }

  const auto latest_generation = store.LoadDesiredGeneration();
  if (latest_generation.has_value() &&
      assignment->desired_generation != *latest_generation) {
    throw std::runtime_error(
        "host assignment id=" + std::to_string(assignment_id) +
        " belongs to stale desired generation " +
        std::to_string(assignment->desired_generation) +
        "; latest generation is " + std::to_string(*latest_generation));
  }

  if (!store.RetryFailedHostAssignment(
          assignment_id,
          "requeued by operator for desired generation " +
              std::to_string(assignment->desired_generation))) {
    throw std::runtime_error(
        "failed to requeue host assignment id=" + std::to_string(assignment_id));
  }

  const auto updated_assignment = store.LoadHostAssignment(assignment_id);
  std::cout << "requeued host assignment id=" << assignment_id << "\n";
  if (updated_assignment.has_value()) {
    PrintHostAssignments({*updated_assignment});
  }
  return 0;
}

int ShowState(const std::string& db_path) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto state = store.LoadDesiredState();
  if (!state.has_value()) {
    std::cout << "state: empty\n";
    return 0;
  }

  std::cout << "db: " << db_path << "\n";
  const auto generation = store.LoadDesiredGeneration();
  if (generation.has_value()) {
    std::cout << "desired generation: " << *generation << "\n";
  }
  PrintStateSummary(*state);
  std::cout << comet::RenderSchedulingPolicyReport(
      comet::EvaluateSchedulingPolicy(*state));
  std::cout << "\n";
  PrintNodeAvailabilityOverrides(store.LoadNodeAvailabilityOverrides());
  std::cout << "\n";
  PrintHostObservations(store.LoadHostObservations(), DefaultStaleAfterSeconds());
  std::cout << "\n";
  PrintHostHealth(
      state,
      store.LoadHostObservations(),
      store.LoadNodeAvailabilityOverrides(),
      std::nullopt,
      DefaultStaleAfterSeconds());
  return 0;
}

int RenderCompose(const std::string& db_path, const std::optional<std::string>& node_name) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto state = store.LoadDesiredState();
  if (!state.has_value()) {
    std::cerr << "error: no desired state found in db '" << db_path << "'\n";
    return 1;
  }
  return RenderComposeForState(*state, node_name);
}

int RenderInferRuntime(const std::string& db_path) {
  comet::ControllerStore store(db_path);
  store.Initialize();
  const auto state = store.LoadDesiredState();
  if (!state.has_value()) {
    std::cerr << "error: no desired state found in db '" << db_path << "'\n";
    return 1;
  }
  std::cout << comet::RenderInferRuntimeConfigJson(*state) << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  const std::string command = argv[1];
  if (command == "show-demo-plan") {
    ShowDemoPlan();
    return 0;
  }

  if (command == "render-demo-compose") {
    return RenderDemoCompose(ParseNodeArg(argc, argv));
  }

  try {
    const std::string db_path = ResolveDbPath(ParseDbArg(argc, argv));

    if (command == "init-db") {
      return InitDb(db_path);
    }

    if (command == "seed-demo") {
      return SeedDemo(db_path);
    }

    if (command == "validate-bundle") {
      const auto bundle_dir = ParseBundleArg(argc, argv);
      if (!bundle_dir.has_value()) {
        std::cerr << "error: --bundle is required\n";
        return 1;
      }
      return ValidateBundle(*bundle_dir);
    }

    if (command == "preview-bundle") {
      const auto bundle_dir = ParseBundleArg(argc, argv);
      if (!bundle_dir.has_value()) {
        std::cerr << "error: --bundle is required\n";
        return 1;
      }
      return PreviewBundle(*bundle_dir, ParseNodeArg(argc, argv));
    }

    if (command == "plan-bundle") {
      const auto bundle_dir = ParseBundleArg(argc, argv);
      if (!bundle_dir.has_value()) {
        std::cerr << "error: --bundle is required\n";
        return 1;
      }
      return PlanBundle(db_path, *bundle_dir);
    }

    if (command == "apply-bundle") {
      const auto bundle_dir = ParseBundleArg(argc, argv);
      if (!bundle_dir.has_value()) {
        std::cerr << "error: --bundle is required\n";
        return 1;
      }
      return ApplyBundle(
          db_path,
          *bundle_dir,
          ResolveArtifactsRoot(ParseArtifactsRootArg(argc, argv)));
    }

    if (command == "plan-host-ops") {
      const auto bundle_dir = ParseBundleArg(argc, argv);
      if (!bundle_dir.has_value()) {
        std::cerr << "error: --bundle is required\n";
        return 1;
      }
      return PlanHostOps(
          db_path,
          *bundle_dir,
          ResolveArtifactsRoot(ParseArtifactsRootArg(argc, argv)),
          ParseNodeArg(argc, argv));
    }

    if (command == "show-state") {
      return ShowState(db_path);
    }

    if (command == "show-host-assignments") {
      return ShowHostAssignments(db_path, ParseNodeArg(argc, argv));
    }

    if (command == "show-host-observations") {
      return ShowHostObservations(
          db_path,
          ParseNodeArg(argc, argv),
          ParseStaleAfterArg(argc, argv).value_or(DefaultStaleAfterSeconds()));
    }

    if (command == "show-host-health") {
      return ShowHostHealth(
          db_path,
          ParseNodeArg(argc, argv),
          ParseStaleAfterArg(argc, argv).value_or(DefaultStaleAfterSeconds()));
    }

    if (command == "show-node-availability") {
      return ShowNodeAvailability(db_path, ParseNodeArg(argc, argv));
    }

    if (command == "set-node-availability") {
      const auto requested_node_name = ParseNodeArg(argc, argv);
      if (!requested_node_name.has_value()) {
        std::cerr << "error: --node is required\n";
        return 1;
      }
      const auto requested_availability = ParseAvailabilityArg(argc, argv);
      if (!requested_availability.has_value()) {
        std::cerr << "error: --availability is required\n";
        return 1;
      }
      return SetNodeAvailability(
          db_path,
          *requested_node_name,
          comet::ParseNodeAvailability(*requested_availability),
          ParseMessageArg(argc, argv));
    }

    if (command == "retry-host-assignment") {
      const auto assignment_id = ParseIdArg(argc, argv);
      if (!assignment_id.has_value()) {
        std::cerr << "error: --id is required\n";
        return 1;
      }
      return RetryHostAssignment(db_path, *assignment_id);
    }

    if (command == "import-bundle") {
      const auto bundle_dir = ParseBundleArg(argc, argv);
      if (!bundle_dir.has_value()) {
        std::cerr << "error: --bundle is required\n";
        return 1;
      }
      return ImportBundle(db_path, *bundle_dir);
    }

    if (command == "render-compose") {
      return RenderCompose(db_path, ParseNodeArg(argc, argv));
    }

    if (command == "render-infer-runtime") {
      return RenderInferRuntime(db_path);
    }
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }

  PrintUsage();
  return 1;
}
