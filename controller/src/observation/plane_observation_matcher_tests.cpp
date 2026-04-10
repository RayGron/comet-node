#include <iostream>
#include <stdexcept>
#include <string>

#include "observation/plane_observation_matcher.h"

#include "comet/runtime/runtime_status.h"
#include "comet/state/state_json.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void TestObservationMatchesPlaneViaRuntimeStatuses() {
  comet::HostObservation observation;
  observation.node_name = "remote-app-host";
  observation.instance_runtime_json = comet::SerializeRuntimeStatusListJson({
      comet::RuntimeProcessStatus{
          "app-cypher", "app", "remote-app-host", "", "", "running", "", "", 101, 0, true},
      comet::RuntimeProcessStatus{
          "skills-cypher", "skills", "remote-app-host", "", "", "running", "", "", 102, 0,
          true},
      comet::RuntimeProcessStatus{
          "webgateway-cypher", "browsing", "remote-app-host", "", "", "running", "", "", 103,
          0, true},
  });

  const comet::controller::PlaneObservationMatcher matcher;
  Expect(
      matcher.ObservationMatchesPlane(observation, "cypher"),
      "runtime-only app/skills/webgateway statuses should match the plane");

  const auto filtered = matcher.FilterInstanceRuntimeStatusesForPlane(
      comet::DeserializeRuntimeStatusListJson(observation.instance_runtime_json),
      std::string{"cypher"},
      {});
  Expect(filtered.size() == 3, "plane runtime filter should retain app/skills/webgateway items");

  std::cout << "ok: runtime-only-observation-matches-app-skills-webgateway" << '\n';
}

void TestFilterObservedStateForPlaneRemovesForeignEntities() {
  comet::DesiredState observed_state;
  observed_state.plane_name = "";

  auto push_instance =
      [&](const std::string& name,
          comet::InstanceRole role,
          const std::string& plane_name,
          const std::string& node_name) {
        comet::InstanceSpec instance;
        instance.name = name;
        instance.role = role;
        instance.plane_name = plane_name;
        instance.node_name = node_name;
        instance.image = "image";
        observed_state.instances.push_back(std::move(instance));
      };
  push_instance("app-alpha", comet::InstanceRole::App, "alpha", "node-a");
  push_instance("skills-alpha", comet::InstanceRole::Skills, "alpha", "node-a");
  push_instance("webgateway-alpha", comet::InstanceRole::Browsing, "alpha", "node-a");
  push_instance("app-beta", comet::InstanceRole::App, "beta", "node-b");

  observed_state.disks.push_back(comet::DiskSpec{
      "alpha-shared",
      comet::DiskKind::PlaneShared,
      "alpha",
      "node-a",
      "node-a",
      "/alpha",
      "/alpha",
      10});
  observed_state.disks.push_back(comet::DiskSpec{
      "beta-shared",
      comet::DiskKind::PlaneShared,
      "beta",
      "node-b",
      "node-b",
      "/beta",
      "/beta",
      10});

  comet::NodeInventory node_a;
  node_a.name = "node-a";
  observed_state.nodes.push_back(node_a);
  comet::NodeInventory node_b;
  node_b.name = "node-b";
  observed_state.nodes.push_back(node_b);

  const comet::controller::PlaneObservationMatcher matcher;
  const auto filtered = matcher.FilterObservedStateForPlane(observed_state, "alpha");

  Expect(filtered.instances.size() == 3, "filtered state should keep only alpha instances");
  for (const auto& instance : filtered.instances) {
    Expect(instance.plane_name == "alpha", "foreign plane instance leaked into filtered state");
  }
  Expect(filtered.disks.size() == 1, "filtered state should keep only alpha disks");
  Expect(filtered.nodes.size() == 1, "filtered state should keep only alpha nodes");
  Expect(filtered.nodes.front().name == "node-a", "filtered state kept the wrong node");

  std::cout << "ok: filter-observed-state-removes-foreign-entities" << '\n';
}

}  // namespace

int main() {
  try {
    TestObservationMatchesPlaneViaRuntimeStatuses();
    TestFilterObservedStateForPlaneRemovesForeignEntities();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "plane_observation_matcher_tests failed: " << error.what() << '\n';
    return 1;
  }
}
