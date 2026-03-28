#include "app/controller_serve_service.h"

#include <filesystem>

#include "auth/auth_http_service.h"
#include "bundle/bundle_http_service.h"
#include "infra/controller_health_service.h"
#include "http/controller_http_router.h"
#include "http/controller_http_server.h"
#include "infra/controller_ui_service.h"
#include "host/hostd_http_service.h"
#include "interaction/interaction_http_service.h"
#include "model/model_library_http_service.h"
#include "model/model_library_service.h"
#include "plane/plane_http_service.h"
#include "read_model/read_model_http_service.h"
#include "read_model/read_model_service.h"
#include "scheduler/scheduler_http_service.h"

namespace comet::controller {

int ControllerServeService::Serve(
    const std::string& db_path,
    const std::string& artifacts_root,
    const std::string& listen_host,
    int listen_port,
    const std::optional<std::string>& requested_ui_root,
    AuthSupportService& auth_support,
    const AssignmentOrchestrationService& assignment_orchestration_service,
    const BundleCliService& bundle_cli_service) const {
  ControllerUiService controller_ui_service;
  std::optional<std::filesystem::path> ui_root;
  if (requested_ui_root.has_value()) {
    ui_root = std::filesystem::path(*requested_ui_root);
  } else {
    const std::filesystem::path default_ui_root = controller_ui_service.DefaultUiRoot();
    if (std::filesystem::exists(default_ui_root)) {
      ui_root = default_ui_root;
    }
  }

  auto interaction_http_service = MakeInteractionHttpService();
  auto auth_http_service = MakeAuthHttpService(auth_support);
  auto hostd_http_service = MakeHostdHttpService();
  auto bundle_http_service = MakeBundleHttpService(bundle_cli_service);
  auto model_library_service = MakeModelLibraryService();
  auto model_library_http_service = MakeModelLibraryHttpService(model_library_service);
  ControllerHealthService controller_health_service;
  auto plane_http_service = MakePlaneHttpService();
  auto read_model_service = MakeReadModelService();
  auto read_model_http_service = MakeReadModelHttpService(read_model_service);
  auto scheduler_http_service = MakeSchedulerHttpService(read_model_service);

  ControllerHttpRouter router(
      db_path,
      artifacts_root,
      ui_root,
      auth_support,
      {
          &auth_http_service,
          &hostd_http_service,
          &bundle_http_service,
          &model_library_http_service,
          &plane_http_service,
          &read_model_http_service,
          &scheduler_http_service,
          &interaction_http_service,
          &controller_health_service,
      },
      {
          [&](int status_code,
              const nlohmann::json& payload,
              const std::map<std::string, std::string>& headers) {
            return BuildJsonResponse(status_code, payload, headers);
          },
          [&](const std::filesystem::path& root, const std::string& request_path) {
            return controller_ui_service.ResolveRequestPath(root, request_path);
          },
          [&](const std::filesystem::path& file_path) {
            return controller_ui_service.BuildStaticFileResponse(file_path);
          },
          [&](const std::string& action_db_path, int assignment_id) {
            return assignment_orchestration_service.ExecuteRetryHostAssignmentAction(
                action_db_path,
                assignment_id);
          },
      });

  ControllerHttpServer server({
      [&](const HttpRequest& request) {
        const ScopedCurrentHttpRequest scoped_request(request);
        return router.HandleRequest(request);
      },
      [&](SocketHandle client_fd,
         const std::string& interaction_db_path,
         const HttpRequest& request) {
        interaction_http_service.StreamPlaneInteractionSse(
            client_fd,
            interaction_db_path,
            request);
      },
      [](const std::string& method, const std::string& path) {
        return ParseInteractionStreamPlaneName(method, path);
      },
      [&](const comet::EventRecord& event) {
        return read_model_service.BuildEventPayloadItem(event);
      },
  });

  return server.Serve({
      db_path,
      artifacts_root,
      listen_host,
      listen_port,
      ui_root,
      "/health,/api/v1/health,/api/v1/bundles/validate,/api/v1/bundles/preview,/api/v1/bundles/import,/api/v1/bundles/apply,/api/v1/model-library,/api/v1/model-library/download,/api/v1/planes,/api/v1/planes/<plane>,/api/v1/planes/<plane>/dashboard,/api/v1/planes/<plane>/start,/api/v1/planes/<plane>/stop,/api/v1/planes/<plane>[DELETE],/api/v1/planes/<plane>/interaction/status,/api/v1/planes/<plane>/interaction/models,/api/v1/planes/<plane>/interaction/chat/completions,/api/v1/planes/<plane>/interaction/chat/completions/stream,/api/v1/state,/api/v1/dashboard,/api/v1/host-assignments,/api/v1/host-observations,/api/v1/host-health,/api/v1/disk-state,/api/v1/rollout-actions,/api/v1/rebalance-plan,/api/v1/events,/api/v1/events/stream,/api/v1/scheduler-tick,/api/v1/reconcile-rebalance-proposals,/api/v1/reconcile-rollout-actions,/api/v1/apply-rebalance-proposal,/api/v1/set-rollout-action-status,/api/v1/enqueue-rollout-eviction,/api/v1/apply-ready-rollout-action,/api/v1/node-availability,/api/v1/retry-host-assignment,/api/v1/hostd/hosts,/api/v1/hostd/hosts/<node>/revoke,/api/v1/hostd/hosts/<node>/rotate-key",
  });
}

}  // namespace comet::controller
