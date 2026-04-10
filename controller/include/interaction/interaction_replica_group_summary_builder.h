#pragma once

#include <string>
#include <vector>

#include "comet/runtime/runtime_status.h"
#include "comet/state/models.h"

namespace comet::controller {

struct InteractionReplicaGroupSummary {
  int expected_replica_groups = 0;
  int ready_replica_groups = 0;
  int degraded_replica_groups = 0;
  int ready_worker_members = 0;
  int expected_worker_members = 0;
  int expected_api_endpoints = 0;
  int ready_api_endpoints = 0;
  int data_parallel_size = 0;
  int data_parallel_size_local_max = 0;
};

class InteractionReplicaGroupSummaryBuilder {
 public:
  std::string BuildHybridReplicaGroupKey(
      const comet::WorkerGroupMemberSpec& member) const;

  int CountExpectedHybridApiEndpoints(
      const comet::DesiredState& desired_state) const;

  InteractionReplicaGroupSummary Build(
      const comet::DesiredState& desired_state,
      const std::vector<comet::RuntimeProcessStatus>& instance_statuses) const;
};

}  // namespace comet::controller
