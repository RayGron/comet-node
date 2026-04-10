#include "interaction/interaction_http_service.h"

#include "auth/auth_support_service.h"
#include "browsing/interaction_browsing_service.h"
#include "interaction/interaction_conversation_service.h"
#include "interaction/interaction_http_executor_factory.h"
#include "interaction/interaction_request_contract_support.h"
#include "interaction/interaction_request_identity_support.h"
#include "interaction/interaction_sse_frame_builder.h"
#include "skills/plane_skills_service.h"

using nlohmann::json;
using comet::controller::InteractionConversationService;
using comet::controller::InteractionRequestContext;
using comet::controller::PlaneInteractionResolution;
using comet::controller::ResolvedInteractionPolicy;

InteractionHttpService::InteractionHttpService(InteractionHttpSupport support)
    : support_(std::move(support)) {}

comet::controller::PlaneInteractionResolution InteractionHttpService::ResolvePlane(
    const std::string& db_path,
    const std::string& plane_name) const {
  return comet::controller::InteractionHttpExecutorFactory(support_)
      .MakePlaneResolver()
      .Resolve(db_path, plane_name);
}

comet::controller::InteractionSessionResult InteractionHttpService::ExecuteSession(
    const comet::controller::PlaneInteractionResolution& resolution,
    const comet::controller::InteractionRequestContext& request_context) const {
  return comet::controller::InteractionHttpExecutorFactory(support_)
      .MakeSessionExecutor()
      .Execute(resolution, request_context);
}

std::optional<comet::controller::InteractionValidationError>
InteractionHttpService::ResolveRequestSkills(
    const comet::controller::PlaneInteractionResolution& resolution,
    comet::controller::InteractionRequestContext* request_context) const {
  return comet::controller::PlaneSkillsService().ResolveInteractionSkills(
      resolution, request_context);
}

std::optional<comet::controller::InteractionValidationError>
InteractionHttpService::ResolveRequestBrowsing(
    const comet::controller::PlaneInteractionResolution& resolution,
    comet::controller::InteractionRequestContext* request_context) const {
  return comet::controller::InteractionBrowsingService().ResolveInteractionBrowsing(
      resolution, request_context);
}

std::optional<comet::controller::InteractionValidationError>
InteractionHttpService::ResolveRequestContext(
    const comet::controller::PlaneInteractionResolution& resolution,
    comet::controller::InteractionRequestContext* request_context) const {
  if (const auto error = ResolveRequestSkills(resolution, request_context)) {
    return error;
  }
  return ResolveRequestBrowsing(resolution, request_context);
}

HttpResponse InteractionHttpService::BuildSessionResponse(
    const comet::controller::PlaneInteractionResolution& resolution,
    const comet::controller::InteractionRequestContext& request_context,
    const comet::controller::InteractionSessionResult& result) const {
  const comet::controller::InteractionSessionPresenter presenter;
  comet::controller::InteractionSessionResult reviewed_result = result;
  comet::controller::InteractionBrowsingService().ReviewInteractionResponse(
      resolution,
      request_context,
      &reviewed_result);
  const auto response_spec =
      presenter.BuildResponseSpec(resolution, request_context, reviewed_result);
  return support_.BuildJsonResponse(
      response_spec.status_code,
      response_spec.payload,
      comet::controller::InteractionRequestContractSupport{}
          .BuildInteractionResponseHeaders(request_context.request_id));
}

HttpResponse InteractionHttpService::ProxyJson(
    const comet::controller::PlaneInteractionResolution& resolution,
    const std::string& request_id,
    const std::string& method,
    const std::string& path,
    const std::string& body) const {
  const auto proxy_executor =
      comet::controller::InteractionHttpExecutorFactory(support_)
          .MakeProxyExecutor(
              [&](const comet::controller::PlaneInteractionResolution& candidate,
                  comet::controller::InteractionRequestContext* request_context) {
                return ResolveRequestContext(candidate, request_context);
              });
  const auto result =
      proxy_executor.Execute(resolution, request_id, method, path, body);
  if (result.json_response.has_value()) {
    return support_.BuildJsonResponse(
        result.json_response->status_code,
        result.json_response->payload,
        comet::controller::InteractionRequestContractSupport{}
            .BuildInteractionResponseHeaders(request_id));
  }
  HttpResponse upstream;
  upstream.status_code = result.upstream.status_code;
  upstream.body = result.upstream.body;
  upstream.headers = result.upstream.headers;
  return upstream;
}

void InteractionHttpService::StreamPlaneInteractionSse(
    comet::platform::SocketHandle client_fd,
    const std::string& db_path,
    const HttpRequest& request,
    AuthSupportService& auth_support) const {
  const std::string request_id =
      comet::controller::InteractionRequestIdentitySupport{}.GenerateRequestId();
  const auto executor_factory =
      comet::controller::InteractionHttpExecutorFactory(support_);
  const auto setup_result = executor_factory
      .MakeStreamRequestPreparationService(
          [&](const comet::controller::PlaneInteractionResolution& resolution,
              comet::controller::InteractionRequestContext* request_context) {
            return ResolveRequestContext(resolution, request_context);
          })
      .Prepare(
      db_path, request, request_id, auth_support);
  if (setup_result.error_response.has_value()) {
    support_.SendHttpResponse(client_fd, *setup_result.error_response);
    support_.ShutdownAndCloseSocket(client_fd);
    return;
  }

  const std::string plane_name = setup_result.setup->plane_name;
  PlaneInteractionResolution resolution = std::move(setup_result.setup->resolution);
  InteractionRequestContext request_context =
      std::move(setup_result.setup->request_context);
  ResolvedInteractionPolicy resolved_policy =
      std::move(setup_result.setup->resolved_policy);

  const std::string stream_session_id =
      request_context.conversation_session_id.empty()
          ? comet::controller::InteractionRequestIdentitySupport{}
                .GenerateSessionId()
          : request_context.conversation_session_id;

  if (!support_.SendSseHeaders(
          client_fd,
          comet::controller::InteractionRequestContractSupport{}
              .BuildInteractionResponseHeaders(request_id))) {
    support_.ShutdownAndCloseSocket(client_fd);
    return;
  }

  const auto stream_session_executor =
      executor_factory.MakeStreamSessionExecutor();
  const auto stream_segment_executor =
      executor_factory.MakeStreamSegmentExecutor();
  const comet::controller::InteractionSseFrameBuilder sse_frame_builder;
  const auto result = stream_session_executor.Execute(
      request_id,
      stream_session_id,
      plane_name,
      resolution,
      request_context,
      resolved_policy,
      [&](const json& payload, int segment_index) {
        return stream_segment_executor.Execute(
            resolution,
            request_context,
            resolved_policy,
            request_id,
            payload,
            segment_index,
            [&](const std::string& model, const std::string& delta) {
              return support_.SendAll(
                  client_fd,
                  sse_frame_builder.BuildEventFrame(
                      "delta",
                      json{
                          {"request_id", request_id},
                          {"session_id", stream_session_id},
                          {"segment_index", segment_index},
                          {"continuation_index", segment_index},
                          {"model", model},
                          {"delta", delta},
                      }));
            });
      },
      [&](const std::string& event_name, const json& payload) {
        return support_.SendAll(
            client_fd,
            sse_frame_builder.BuildEventFrame(event_name, payload));
      },
      [&]() { return support_.SendAll(client_fd, sse_frame_builder.BuildDoneFrame()); });
  (void)InteractionConversationService().PersistResponse(
      db_path, resolution, &request_context, result);

  support_.ShutdownAndCloseSocket(client_fd);
}
