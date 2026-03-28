#include "infra/controller_ui_service.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

std::string ControllerUiService::DefaultUiRoot() const {
  return (std::filesystem::path("var") / "ui").string();
}

std::optional<std::filesystem::path> ControllerUiService::ResolveRequestPath(
    const std::filesystem::path& ui_root,
    const std::string& request_path) const {
  if (request_path.empty() || request_path[0] != '/') {
    return std::nullopt;
  }
  if (request_path.rfind("/api/", 0) == 0 || request_path == "/health") {
    return std::nullopt;
  }

  std::filesystem::path relative_path;
  if (request_path == "/") {
    relative_path = "index.html";
  } else {
    relative_path =
        std::filesystem::path(request_path.substr(1)).lexically_normal();
  }
  if (relative_path.empty()) {
    relative_path = "index.html";
  }
  if (relative_path.is_absolute()) {
    return std::nullopt;
  }
  for (const auto& part : relative_path) {
    if (part == "..") {
      return std::nullopt;
    }
  }

  const auto candidate = ui_root / relative_path;
  if (std::filesystem::is_regular_file(candidate)) {
    return candidate;
  }

  const auto fallback = ui_root / "index.html";
  if (std::filesystem::is_regular_file(fallback)) {
    return fallback;
  }
  return std::nullopt;
}

HttpResponse ControllerUiService::BuildStaticFileResponse(
    const std::filesystem::path& file_path) const {
  std::ifstream input(file_path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error(
        "failed to open static asset: " + file_path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return HttpResponse{200, GuessContentType(file_path), buffer.str(), {}};
}

std::string ControllerUiService::GuessContentType(
    const std::filesystem::path& file_path) {
  const std::string extension = file_path.extension().string();
  if (extension == ".html") {
    return "text/html; charset=utf-8";
  }
  if (extension == ".css") {
    return "text/css; charset=utf-8";
  }
  if (extension == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (extension == ".json") {
    return "application/json; charset=utf-8";
  }
  if (extension == ".svg") {
    return "image/svg+xml";
  }
  if (extension == ".png") {
    return "image/png";
  }
  if (extension == ".jpg" || extension == ".jpeg") {
    return "image/jpeg";
  }
  if (extension == ".ico") {
    return "image/x-icon";
  }
  if (extension == ".txt") {
    return "text/plain; charset=utf-8";
  }
  return "application/octet-stream";
}
