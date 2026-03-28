#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "http/controller_http_transport.h"

class ControllerUiService {
 public:
  std::string DefaultUiRoot() const;

  std::optional<std::filesystem::path> ResolveRequestPath(
      const std::filesystem::path& ui_root,
      const std::string& request_path) const;

  HttpResponse BuildStaticFileResponse(
      const std::filesystem::path& file_path) const;

 private:
  static std::string GuessContentType(const std::filesystem::path& file_path);
};
