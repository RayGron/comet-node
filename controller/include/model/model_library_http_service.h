#pragma once

#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "http/controller_http_transport.h"
#include "http/controller_http_types.h"
#include "model/model_library_service.h"

class ModelLibraryHttpService {
 public:
  using BuildJsonResponseFn = std::function<HttpResponse(
      int,
      const nlohmann::json&,
      const std::map<std::string, std::string>&)>;
  struct Deps {
    BuildJsonResponseFn build_json_response;
    const ModelLibraryService* model_library_service = nullptr;
  };

  explicit ModelLibraryHttpService(Deps deps);

  std::optional<HttpResponse> HandleRequest(
      const std::string& db_path,
      const HttpRequest& request) const;

 private:
  Deps deps_;
};
