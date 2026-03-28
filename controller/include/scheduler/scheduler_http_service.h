#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "infra/controller_action.h"
#include "http/controller_http_transport.h"
#include "http/controller_http_types.h"
#include "read_model/read_model_service.h"
#include "scheduler/scheduler_service.h"

#include "comet/models.h"

class SchedulerHttpService {
 public:
  using BuildJsonResponseFn = std::function<HttpResponse(
      int,
      const nlohmann::json&,
      const std::map<std::string, std::string>&)>;
  using FindQueryStringFn = std::function<std::optional<std::string>(
      const HttpRequest&,
      const std::string&)>;
  using FindQueryIntFn =
      std::function<std::optional<int>(const HttpRequest&, const std::string&)>;
  using ResolveArtifactsRootFn = std::function<std::string(
      const std::optional<std::string>&,
      const std::string&)>;
  using BuildControllerActionPayloadFn =
      std::function<nlohmann::json(const comet::controller::ControllerActionResult&)>;
  using SetNodeAvailabilityActionFn = std::function<comet::controller::ControllerActionResult(
      const std::string&,
      const std::string&,
      comet::NodeAvailability,
      const std::optional<std::string>&)>;
  using MakeSchedulerServiceFn =
      std::function<comet::controller::SchedulerService(
          const std::string&,
          const std::string&)>;

  struct Deps {
    BuildJsonResponseFn build_json_response;
    FindQueryStringFn find_query_string;
    FindQueryIntFn find_query_int;
    ResolveArtifactsRootFn resolve_artifacts_root;
    BuildControllerActionPayloadFn build_controller_action_payload;
    const comet::controller::ReadModelService* read_model_service = nullptr;
    SetNodeAvailabilityActionFn set_node_availability_action;
    MakeSchedulerServiceFn make_scheduler_service;
  };

  explicit SchedulerHttpService(Deps deps);

  std::optional<HttpResponse> HandleRequest(
      const std::string& db_path,
      const std::string& default_artifacts_root,
      const HttpRequest& request) const;

 private:
  Deps deps_;
};
