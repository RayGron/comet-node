#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "http/controller_http_transport.h"
#include "http/controller_http_types.h"
#include "read_model/read_model_service.h"
#include "scheduler/scheduler_view_service.h"

class ReadModelHttpService {
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
  using DefaultStaleAfterSecondsFn = std::function<int()>;
  using LoadRolloutActionsViewDataFn =
      std::function<RolloutActionsViewData(
          const std::string&,
          const std::optional<std::string>&,
          const std::optional<std::string>&)>;
  using LoadRebalancePlanViewDataFn =
      std::function<RebalancePlanViewData(
          const std::string&,
          const std::optional<std::string>&,
          int,
          const std::optional<std::string>&)>;

  struct Deps {
    BuildJsonResponseFn build_json_response;
    FindQueryStringFn find_query_string;
    FindQueryIntFn find_query_int;
    DefaultStaleAfterSecondsFn default_stale_after_seconds;
    const comet::controller::ReadModelService* read_model_service = nullptr;
    const SchedulerViewService* scheduler_view_service = nullptr;
    LoadRolloutActionsViewDataFn load_rollout_actions_view_data;
    LoadRebalancePlanViewDataFn load_rebalance_plan_view_data;
  };

  explicit ReadModelHttpService(Deps deps);

  std::optional<HttpResponse> HandleRequest(
      const std::string& db_path,
      const HttpRequest& request) const;

 private:
  Deps deps_;
};
