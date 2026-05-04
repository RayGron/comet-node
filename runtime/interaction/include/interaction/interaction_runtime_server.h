#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "http/controller_http_types.h"
#include "http/controller_http_transport.h"
#include "interaction/interaction_types.h"
#include "naim/core/platform_compat.h"

namespace naim::interaction_runtime {

struct InteractionRuntimeConfig {
  std::string plane_name;
  std::string instance_name;
  std::string instance_role = "interaction";
  std::string node_name;
  std::string control_root;
  std::string controller_url;
  std::filesystem::path status_path;
  std::string listen_host = "0.0.0.0";
  int port = 18110;
  std::string upstream_base = "http://127.0.0.1:8000/v1";
  std::string webgateway_base_url;
};

class InteractionRuntimeServer final {
 public:
  explicit InteractionRuntimeServer(InteractionRuntimeConfig config);
  ~InteractionRuntimeServer();

  int Run();
  void RequestStop();

 private:
  struct RuntimeExecution {
    naim::controller::PlaneInteractionResolution resolution;
    naim::controller::InteractionRequestContext request_context;
    naim::controller::ResolvedInteractionPolicy resolved_policy;
    bool force_stream = false;
    bool structured_output_json = false;
    bool local_raw_execution = false;
    int skills_resolve_ms = 0;
  };

  void AcceptLoop();
  void HandleClient(naim::platform::SocketHandle client_fd);
  HttpResponse HandleRequest(const HttpRequest& request);
  HttpResponse HandleGet(const HttpRequest& request);
  HttpResponse HandlePost(const HttpRequest& request);
  HttpResponse ExecuteNonStream(const HttpRequest& request);
  void ExecuteStream(naim::platform::SocketHandle client_fd, const HttpRequest& request);
  bool ShouldProxyRawRequestThroughController(const HttpRequest& request) const;
  bool HasLocalPlaneStateSnapshot() const;
  HttpResponse ProxyRawRequestThroughController(
      const HttpRequest& request,
      const std::string& controller_path) const;
  void ProxyRawStreamThroughController(
      naim::platform::SocketHandle client_fd,
      const HttpRequest& request,
      const std::string& controller_path) const;
  RuntimeExecution BuildRuntimeExecution(const HttpRequest& request) const;
  std::optional<RuntimeExecution> TryBuildWrappedRuntimeExecution(
      const std::string& body) const;
  RuntimeExecution BuildDirectRuntimeExecution(const HttpRequest& request) const;
  int ResolveExplicitSkillsForDirectExecution(
      const naim::controller::PlaneInteractionResolution& resolution,
      naim::controller::InteractionRequestContext* request_context) const;
  std::optional<naim::controller::ControllerEndpointTarget> ResolvePlaneNetworkSkillsTarget(
      const naim::DesiredState& desired_state) const;
  int ResolvePlaneOwnedBrowsing(
      const naim::controller::PlaneInteractionResolution& resolution,
      naim::controller::InteractionRequestContext* request_context) const;
  void ReviewPlaneOwnedBrowsingResponse(
      const naim::controller::PlaneInteractionResolution& resolution,
      const naim::controller::InteractionRequestContext& request_context,
      HttpResponse* response) const;
  std::optional<naim::controller::ControllerEndpointTarget> ResolvePlaneNetworkWebGatewayTarget(
      const naim::DesiredState& desired_state) const;
  nlohmann::json LoadPlaneStatePayload() const;
  nlohmann::json LoadPlaneStatePayloadFromSnapshot() const;
  HttpResponse BuildJsonResponse(int status_code, const nlohmann::json& payload) const;
  naim::controller::ControllerEndpointTarget UpstreamTarget() const;
  std::vector<std::string> SplitPath(const std::string& path) const;
  void WriteRuntimeStatus(const std::string& phase, bool ready) const;
  void SetReadyFile(bool ready) const;

  InteractionRuntimeConfig config_;
  std::atomic<bool> stop_requested_{false};
  naim::platform::SocketHandle listen_fd_ = naim::platform::kInvalidSocket;
};

}  // namespace naim::interaction_runtime
