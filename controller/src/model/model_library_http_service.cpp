#include "model/model_library_http_service.h"

#include <stdexcept>
#include <utility>

using nlohmann::json;

ModelLibraryHttpService::ModelLibraryHttpService(Deps deps)
    : deps_(std::move(deps)) {}

std::optional<HttpResponse> ModelLibraryHttpService::HandleRequest(
    const std::string& db_path,
    const HttpRequest& request) const {
  if (deps_.model_library_service == nullptr) {
    throw std::runtime_error("model library service is not configured");
  }
  if (request.path == "/api/v1/model-library") {
    if (request.method == "GET") {
      try {
        return deps_.build_json_response(
            200, deps_.model_library_service->BuildPayload(db_path), {});
      } catch (const std::exception& error) {
        return deps_.build_json_response(
            500,
            json{{"status", "internal_error"},
                 {"message", error.what()},
                 {"path", request.path}},
            {});
      }
    }
    if (request.method == "DELETE") {
      try {
        return deps_.model_library_service->DeleteEntryByPath(db_path, request);
      } catch (const std::exception& error) {
        return deps_.build_json_response(
            500,
            json{{"status", "internal_error"},
                 {"message", error.what()},
                 {"path", request.path}},
            {});
      }
    }
    return deps_.build_json_response(
        405, json{{"status", "method_not_allowed"}}, {});
  }

  if (request.path == "/api/v1/model-library/download") {
    if (request.method != "POST") {
      return deps_.build_json_response(
          405, json{{"status", "method_not_allowed"}}, {});
    }
    try {
      return deps_.model_library_service->EnqueueDownload(request);
    } catch (const std::exception& error) {
      return deps_.build_json_response(
          500,
          json{{"status", "internal_error"},
               {"message", error.what()},
               {"path", request.path}},
          {});
    }
  }

  return std::nullopt;
}
