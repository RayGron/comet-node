#include "app/hostd_desired_state_apply_support.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "app/hostd_file_support.h"
#include "app/hostd_runtime_telemetry_support.h"
#include "naim/core/platform_compat.h"
#include "naim/security/crypto_utils.h"
#include "naim/state/state_json.h"
#include "naim/planning/planner.h"

#include <netdb.h>

namespace naim::hostd {

namespace {

constexpr std::uintmax_t kAppModelChunkBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kAppModelPeerChunkBytes = 64ULL * 1024ULL * 1024ULL;
constexpr int kAppModelPollAttempts = 600;
constexpr std::chrono::milliseconds kAppModelPollInterval(500);
constexpr int kPeerHttpTimeoutSeconds = 30;
constexpr int kPeerHttpAttempts = 3;

struct PeerHttpResponse {
  int status_code = 0;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct PeerEndpoint {
  std::string raw;
  std::string host;
  int port = 29999;
};

PeerEndpoint ParsePeerEndpoint(std::string endpoint) {
  PeerEndpoint parsed;
  parsed.raw = endpoint;
  if (endpoint.rfind("http://", 0) == 0) {
    endpoint = endpoint.substr(7);
  }
  const std::size_t slash = endpoint.find('/');
  if (slash != std::string::npos) {
    endpoint = endpoint.substr(0, slash);
  }
  const std::size_t colon = endpoint.rfind(':');
  if (colon == std::string::npos) {
    parsed.host = endpoint;
  } else {
    parsed.host = endpoint.substr(0, colon);
    parsed.port = std::stoi(endpoint.substr(colon + 1));
  }
  if (parsed.host.empty()) {
    throw std::runtime_error("invalid peer endpoint: " + parsed.raw);
  }
  return parsed;
}

void ApplyPeerSocketTimeouts(naim::platform::SocketHandle fd) {
#if defined(_WIN32)
  const DWORD timeout_ms = kPeerHttpTimeoutSeconds * 1000;
  setsockopt(
      fd,
      SOL_SOCKET,
      SO_RCVTIMEO,
      reinterpret_cast<const char*>(&timeout_ms),
      sizeof(timeout_ms));
  setsockopt(
      fd,
      SOL_SOCKET,
      SO_SNDTIMEO,
      reinterpret_cast<const char*>(&timeout_ms),
      sizeof(timeout_ms));
#else
  timeval timeout{};
  timeout.tv_sec = kPeerHttpTimeoutSeconds;
  timeout.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

PeerHttpResponse SendPeerHttpRawRequest(
    const std::string& endpoint,
    const std::string& path,
    const std::string& body,
    const std::string& content_type) {
  const PeerEndpoint target = ParsePeerEndpoint(endpoint);
  naim::platform::EnsureSocketsInitialized();
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const std::string port_text = std::to_string(target.port);
  const int lookup = getaddrinfo(target.host.c_str(), port_text.c_str(), &hints, &results);
  if (lookup != 0) {
    throw std::runtime_error("failed to resolve peer endpoint: " + target.raw);
  }
  naim::platform::SocketHandle fd = naim::platform::kInvalidSocket;
  for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
    fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (!naim::platform::IsSocketValid(fd)) {
      continue;
    }
    ApplyPeerSocketTimeouts(fd);
    if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) {
      break;
    }
    naim::platform::CloseSocket(fd);
    fd = naim::platform::kInvalidSocket;
  }
  freeaddrinfo(results);
  if (!naim::platform::IsSocketValid(fd)) {
    throw std::runtime_error("failed to connect to peer endpoint: " + target.raw);
  }
  std::ostringstream request;
  request << "POST " << path << " HTTP/1.1\r\n";
  request << "Host: " << target.host << ":" << target.port << "\r\n";
  request << "Connection: close\r\n";
  request << "Content-Type: " << content_type << "\r\n";
  request << "Content-Length: " << body.size() << "\r\n\r\n";
  request << body;
  const std::string request_text = request.str();
  const char* data = request_text.data();
  std::size_t remaining = request_text.size();
  while (remaining > 0) {
    const ssize_t written = send(fd, data, remaining, 0);
    if (written <= 0) {
      const std::string error = naim::platform::LastSocketErrorMessage();
      naim::platform::CloseSocket(fd);
      throw std::runtime_error("failed to write peer request: " + error);
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
  std::string response_text;
  std::array<char, 1024 * 1024> buffer{};
  bool reserved_body = false;
  while (true) {
    const ssize_t read_count = recv(fd, buffer.data(), buffer.size(), 0);
    if (read_count < 0) {
      const std::string error = naim::platform::LastSocketErrorMessage();
      naim::platform::CloseSocket(fd);
      throw std::runtime_error("failed to read peer response: " + error);
    }
    if (read_count == 0) {
      break;
    }
    response_text.append(buffer.data(), static_cast<std::size_t>(read_count));
    if (!reserved_body) {
      const std::size_t headers_end = response_text.find("\r\n\r\n");
      if (headers_end != std::string::npos) {
        const std::string headers = response_text.substr(0, headers_end);
        std::istringstream header_lines(headers);
        std::string header_line;
        while (std::getline(header_lines, header_line)) {
          if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
          }
          const std::size_t colon = header_line.find(':');
          if (colon == std::string::npos) {
            continue;
          }
          std::string key = header_line.substr(0, colon);
          std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
          });
          if (key != "content-length") {
            continue;
          }
          std::string value = header_line.substr(colon + 1);
          while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
          }
          try {
            const std::size_t content_length =
                static_cast<std::size_t>(std::stoull(value));
            response_text.reserve(headers_end + 4 + content_length);
          } catch (const std::exception&) {
          }
          break;
        }
        reserved_body = true;
      }
    }
  }
  naim::platform::CloseSocket(fd);
  const std::size_t headers_end = response_text.find("\r\n\r\n");
  const std::string headers =
      headers_end == std::string::npos ? response_text : response_text.substr(0, headers_end);
  PeerHttpResponse response;
  std::istringstream status(headers.substr(0, headers.find("\r\n")));
  std::string version;
  status >> version >> response.status_code;
  std::istringstream header_lines(headers);
  std::string header_line;
  bool first_header = true;
  while (std::getline(header_lines, header_line)) {
    if (!header_line.empty() && header_line.back() == '\r') {
      header_line.pop_back();
    }
    if (first_header) {
      first_header = false;
      continue;
    }
    const std::size_t colon = header_line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = header_line.substr(0, colon);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    std::string value = header_line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    response.headers[key] = value;
  }
  response.body =
      headers_end == std::string::npos ? std::string{} : response_text.substr(headers_end + 4);
  if (response.status_code >= 400) {
    throw std::runtime_error(
        "peer request failed with status " + std::to_string(response.status_code));
  }
  return response;
}

PeerHttpResponse SendPeerHttpRequest(
    const std::string& endpoint,
    const std::string& path,
    const nlohmann::json& payload) {
  return SendPeerHttpRawRequest(endpoint, path, payload.dump(), "application/json");
}

PeerHttpResponse SendPeerHttpRequestWithRetries(
    const std::string& endpoint,
    const std::string& path,
    const nlohmann::json& payload,
    const std::string& operation) {
  std::string last_error;
  for (int attempt = 1; attempt <= kPeerHttpAttempts; ++attempt) {
    try {
      return SendPeerHttpRequest(endpoint, path, payload);
    } catch (const std::exception& error) {
      last_error = error.what();
      if (attempt == kPeerHttpAttempts) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
    }
  }
  throw std::runtime_error(
      operation + " failed after " + std::to_string(kPeerHttpAttempts) +
      " attempts: " + last_error);
}

std::optional<std::uintmax_t> JsonUintmax(
    const nlohmann::json& value,
    const std::string& key) {
  if (!value.contains(key) || !value.at(key).is_number_unsigned()) {
    return std::nullopt;
  }
  return value.at(key).get<std::uintmax_t>();
}

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool FileMatchesManifest(
    const std::string& path,
    std::uintmax_t expected_size,
    const std::string& expected_sha256) {
  std::error_code error;
  if (!std::filesystem::exists(path, error) || error ||
      !std::filesystem::is_regular_file(path, error) || error) {
    return false;
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size != expected_size) {
    return false;
  }
  return expected_sha256.empty() ||
         Lowercase(naim::ComputeFileSha256Hex(path)) == Lowercase(expected_sha256);
}

bool TryMaterializeAppModelMountDirectly(
    const std::string& node_name,
    const naim::AppModelMountSpec& mount,
    const std::vector<std::string>& source_paths,
    HostdBackend* backend,
    const std::function<void(const std::string&, const std::string&, int)>& publish) {
  if (backend == nullptr || mount.source_node_name.empty()) {
    return false;
  }

  const nlohmann::json ticket =
      backend->RequestFileTransferTicket(node_name, mount.source_node_name, source_paths);
  if (ticket.value("status", std::string{}) != "issued") {
    return false;
  }
  const std::string ticket_id = ticket.value("ticket_id", std::string{});
  const std::string peer_endpoint = ticket.value("source_endpoint", std::string{});
  if (ticket_id.empty() || peer_endpoint.empty()) {
    return false;
  }

  publish(
      "app-model-direct",
      "Checking direct app model transfer from storage node " + mount.source_node_name + ".",
      20);
  const PeerHttpResponse manifest_response = SendPeerHttpRequestWithRetries(
      peer_endpoint,
      "/peer/v1/files/manifest",
      nlohmann::json{
          {"ticket_id", ticket_id},
          {"source_paths", source_paths},
          {"defer_sha256", true},
      },
      "direct app model manifest request");
  const nlohmann::json manifest = nlohmann::json::parse(manifest_response.body, nullptr, false);
  if (manifest.is_discarded() || manifest.value("phase", std::string{}) != "manifest-ready" ||
      !manifest.contains("files") || !manifest.at("files").is_array()) {
    return false;
  }
  const auto files = manifest.at("files").get<std::vector<nlohmann::json>>();
  if (files.size() != 1) {
    return false;
  }
  const auto expected_size = JsonUintmax(files.front(), "size_bytes");
  if (!expected_size.has_value()) {
    return false;
  }
  if (FileMatchesManifest(mount.host_path, *expected_size, files.front().value("sha256", ""))) {
    publish("app-model-ready", "Using cached app model artifact.", 72);
    return true;
  }

  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(mount.host_path).parent_path(), error);
  if (error) {
    throw std::runtime_error("failed to create app model target directory: " + mount.host_path);
  }
  const std::string temp_path = mount.host_path + ".part";
  std::filesystem::remove(temp_path, error);
  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open app model direct target: " + temp_path);
  }

  std::uintmax_t offset = 0;
  while (offset < *expected_size) {
    const PeerHttpResponse chunk = SendPeerHttpRequestWithRetries(
        peer_endpoint,
        "/peer/v1/files/chunk",
        nlohmann::json{
            {"ticket_id", ticket_id},
            {"source_path", files.front().value("source_path", source_paths.front())},
            {"offset", offset},
            {"max_bytes", kAppModelPeerChunkBytes},
        },
        "direct app model chunk request at offset " + std::to_string(offset));
    if (chunk.body.empty() && offset < *expected_size) {
      throw std::runtime_error("direct app model transfer returned an empty non-final chunk");
    }
    const auto chunk_sha256_it = chunk.headers.find("x-naim-chunk-sha256");
    if (chunk_sha256_it != chunk.headers.end() &&
        Lowercase(naim::ComputeSha256Hex(chunk.body)) != Lowercase(chunk_sha256_it->second)) {
      throw std::runtime_error("direct app model transfer chunk checksum mismatch");
    }
    output.write(chunk.body.data(), static_cast<std::streamsize>(chunk.body.size()));
    if (!output.good()) {
      throw std::runtime_error("failed to write app model direct target: " + temp_path);
    }
    offset += chunk.body.size();
    const int percent =
        *expected_size > 0
            ? 20 + static_cast<int>(
                       (static_cast<double>(offset) / static_cast<double>(*expected_size)) * 50.0)
            : 60;
    publish(
        "app-model-direct",
        "Copying app model artifact directly from storage node " + mount.source_node_name + ".",
        percent);
  }
  output.close();
  if (!output.good()) {
    throw std::runtime_error("failed to close app model direct target: " + temp_path);
  }
  std::filesystem::rename(temp_path, mount.host_path, error);
  if (error) {
    throw std::runtime_error("failed to finalize app model direct target: " + error.message());
  }
  const auto final_size = std::filesystem::file_size(mount.host_path, error);
  if (error || final_size != *expected_size) {
    throw std::runtime_error("direct app model transfer size mismatch: " + mount.host_path);
  }
  publish("app-model-ready", "App model artifact is ready.", 72);
  return true;
}

void WriteDesiredStateSnapshot(
    const HostdDesiredStatePathSupport& path_support,
    const naim::DesiredState& desired_node_state,
    const std::string& node_name) {
  const auto snapshot_path =
      path_support.DesiredStateSnapshotPathForNode(desired_node_state, node_name);
  if (!snapshot_path.has_value()) {
    return;
  }
  HostdFileSupport().WriteTextFile(
      *snapshot_path,
      naim::SerializeDesiredStateJson(desired_node_state));
}

void MaterializeAppModelMount(
    const naim::DesiredState& desired_node_state,
    const std::string& node_name,
    const naim::InstanceSpec& instance,
    const naim::AppModelMountSpec& mount,
    HostdBackend* backend,
    const HostdDesiredStateApplySupport::ProgressPublisher& publish_progress) {
  if (mount.host_path.empty() || mount.mount_path.empty() || mount.source_path.empty()) {
    if (mount.required) {
      throw std::runtime_error(
          "app model mount '" + mount.name + "' for instance '" + instance.name +
          "' is missing source, host, or mount path");
    }
    return;
  }

  const std::vector<std::string> source_paths =
      mount.source_paths.empty() ? std::vector<std::string>{mount.source_path}
                                 : mount.source_paths;
  if (source_paths.size() != 1) {
    if (mount.required) {
      throw std::runtime_error(
          "app model mount '" + mount.name +
          "' currently supports one retained file path; got " +
          std::to_string(source_paths.size()));
    }
    return;
  }
  const std::string& source_path = source_paths.front();

  auto publish = [&](const std::string& phase, const std::string& detail, int percent) {
    if (publish_progress) {
      publish_progress(
          phase,
          "Materializing app model",
          detail,
          percent,
          desired_node_state.plane_name,
          node_name);
    }
  };

  std::error_code error;
  if (std::filesystem::exists(source_path, error) && !error &&
      std::filesystem::is_regular_file(source_path, error) && !error) {
    std::filesystem::create_directories(std::filesystem::path(mount.host_path).parent_path(), error);
    if (error) {
      throw std::runtime_error("failed to create app model target directory: " + mount.host_path);
    }
    if (std::filesystem::equivalent(source_path, mount.host_path, error) && !error) {
      return;
    }
    error.clear();
    std::filesystem::copy_file(
        source_path,
        mount.host_path,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
      throw std::runtime_error(
          "failed to copy local app model artifact from '" + source_path +
          "' to '" + mount.host_path + "': " + error.message());
    }
    publish("app-model-ready", "Using app model artifact from local storage.", 72);
    return;
  }

  if (backend == nullptr) {
    if (mount.required) {
      throw std::runtime_error(
          "app model mount '" + mount.name +
          "' requires controller relay because the source path is not local");
    }
    return;
  }

  try {
    if (TryMaterializeAppModelMountDirectly(
            node_name,
            mount,
            source_paths,
            backend,
            publish)) {
      return;
    }
  } catch (const std::exception& direct_error) {
    publish(
        "app-model-relay",
        "Direct app model transfer is unavailable; falling back to controller relay: " +
            std::string(direct_error.what()),
        20);
  }

  publish("app-model-manifest", "Requesting app model manifest from storage node.", 20);
  const nlohmann::json manifest_request = backend->RequestModelArtifactManifest(
      node_name,
      mount.source_node_name,
      source_paths);
  if (manifest_request.value("status", std::string{}) != "queued") {
    throw std::runtime_error(
        "controller did not queue app model manifest relay: " +
        manifest_request.value("message", std::string("unknown error")));
  }
  const int manifest_assignment_id = manifest_request.value("assignment_id", 0);
  if (manifest_assignment_id <= 0) {
    throw std::runtime_error("controller returned invalid app model manifest assignment id");
  }

  nlohmann::json manifest = nlohmann::json::object();
  bool manifest_ready = false;
  for (int attempt = 0; attempt < kAppModelPollAttempts; ++attempt) {
    const nlohmann::json poll = backend->LoadModelArtifactManifest(node_name, manifest_assignment_id);
    const std::string status = poll.value("status", std::string{});
    if (status == "failed" || status == "superseded") {
      throw std::runtime_error(
          "app model manifest relay failed: " +
          poll.value("status_message", std::string("unknown error")));
    }
    if (status == "applied") {
      manifest = poll.contains("progress") && poll.at("progress").is_object()
                     ? poll.at("progress")
                     : nlohmann::json::object();
      if (manifest.value("phase", std::string{}) != "manifest-ready" ||
          !manifest.contains("files") ||
          !manifest.at("files").is_array()) {
        throw std::runtime_error("app model manifest relay applied without manifest payload");
      }
      manifest_ready = true;
      break;
    }
    std::this_thread::sleep_for(kAppModelPollInterval);
  }
  if (!manifest_ready) {
    throw std::runtime_error("timed out waiting for app model manifest relay");
  }

  const auto files = manifest.at("files").get<std::vector<nlohmann::json>>();
  if (files.size() != 1) {
    throw std::runtime_error("app model mount currently supports one manifest file");
  }
  const auto expected_size = JsonUintmax(files.front(), "size_bytes");
  if (!expected_size.has_value()) {
    throw std::runtime_error("app model manifest is missing file size metadata");
  }
  const std::string expected_sha256 = files.front().value("sha256", std::string{});
  if (expected_sha256.empty()) {
    throw std::runtime_error("app model manifest is missing file checksum metadata");
  }
  if (FileMatchesManifest(mount.host_path, *expected_size, expected_sha256)) {
    publish("app-model-ready", "Using cached app model artifact.", 72);
    return;
  }

  std::filesystem::create_directories(std::filesystem::path(mount.host_path).parent_path(), error);
  if (error) {
    throw std::runtime_error("failed to create app model target directory: " + mount.host_path);
  }
  const std::string temp_path = mount.host_path + ".part";
  std::filesystem::remove(temp_path, error);
  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open app model relay target: " + temp_path);
  }

  std::uintmax_t offset = 0;
  bool eof = false;
  while (!eof) {
    const nlohmann::json request = backend->RequestModelArtifactChunk(
        node_name,
        mount.source_node_name,
        files.front().value("source_path", source_path),
        offset,
        kAppModelChunkBytes);
    if (request.value("status", std::string{}) != "queued") {
      throw std::runtime_error(
          "controller did not queue app model chunk relay: " +
          request.value("message", std::string("unknown error")));
    }
    const int chunk_assignment_id = request.value("assignment_id", 0);
    if (chunk_assignment_id <= 0) {
      throw std::runtime_error("controller returned invalid app model chunk assignment id");
    }

    bool chunk_ready = false;
    for (int attempt = 0; attempt < kAppModelPollAttempts; ++attempt) {
      const nlohmann::json poll = backend->LoadModelArtifactChunk(node_name, chunk_assignment_id);
      const std::string status = poll.value("status", std::string{});
      if (status == "failed" || status == "superseded") {
        throw std::runtime_error(
            "app model chunk relay failed: " +
            poll.value("status_message", std::string("unknown error")));
      }
      if (status == "applied") {
        const nlohmann::json progress =
            poll.contains("progress") && poll.at("progress").is_object()
                ? poll.at("progress")
                : nlohmann::json::object();
        if (progress.value("phase", std::string{}) != "chunk-ready") {
          throw std::runtime_error("app model chunk relay applied without chunk payload");
        }
        const auto progress_offset = JsonUintmax(progress, "offset").value_or(offset);
        if (progress_offset != offset) {
          throw std::runtime_error("app model chunk relay returned an unexpected offset");
        }
        const std::vector<unsigned char> bytes =
            naim::DecodeBytesBase64(progress.value("bytes_base64", std::string{}));
        if (bytes.empty() && !progress.value("eof", false)) {
          throw std::runtime_error("app model chunk relay returned an empty non-final chunk");
        }
        if (!bytes.empty()) {
          output.write(
              reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
          if (!output.good()) {
            throw std::runtime_error("failed to write app model relay target: " + temp_path);
          }
        }
        const auto next_offset = JsonUintmax(progress, "next_offset").value_or(offset + bytes.size());
        offset = next_offset;
        eof = progress.value("eof", false);
        const int percent = *expected_size > 0
                                ? 20 + static_cast<int>((static_cast<double>(offset) /
                                                         static_cast<double>(*expected_size)) *
                                                        50.0)
                                : 60;
        publish("app-model-copy", "Copying app model artifact through controller relay.", percent);
        chunk_ready = true;
        break;
      }
      std::this_thread::sleep_for(kAppModelPollInterval);
    }
    if (!chunk_ready) {
      throw std::runtime_error("timed out waiting for app model chunk relay");
    }
  }

  output.close();
  if (!output.good()) {
    throw std::runtime_error("failed to close app model relay target: " + temp_path);
  }
  std::filesystem::rename(temp_path, mount.host_path, error);
  if (error) {
    throw std::runtime_error("failed to finalize app model relay target: " + error.message());
  }
  if (!FileMatchesManifest(mount.host_path, *expected_size, expected_sha256)) {
    throw std::runtime_error("app model artifact checksum mismatch: " + mount.host_path);
  }
  publish("app-model-ready", "App model artifact is ready.", 72);
}

void MaterializeAppModelMounts(
    const naim::DesiredState& desired_node_state,
    const std::string& node_name,
    HostdBackend* backend,
    const HostdDesiredStateApplySupport::ProgressPublisher& publish_progress) {
  for (const auto& instance : desired_node_state.instances) {
    if (instance.node_name != node_name || instance.role != naim::InstanceRole::App) {
      continue;
    }
    for (const auto& mount : instance.app_model_mounts) {
      MaterializeAppModelMount(
          desired_node_state,
          node_name,
          instance,
          mount,
          backend,
          publish_progress);
    }
  }
}

}  // namespace

HostdDesiredStateApplySupport::HostdDesiredStateApplySupport(
    const HostdDesiredStatePathSupport& path_support,
    const HostdDesiredStateDisplaySupport& display_support,
    const HostdDesiredStateApplyPlanSupport& apply_plan_support,
    const HostdDiskRuntimeSupport& disk_runtime_support,
    const HostdPostDeploySupport& post_deploy_support,
    const HostdLocalStateRepository& local_state_repository,
    const HostdLocalRuntimeStateSupport& local_runtime_state_support,
    const HostdBootstrapModelSupport& bootstrap_model_support)
    : path_support_(path_support),
      display_support_(display_support),
      apply_plan_support_(apply_plan_support),
      disk_runtime_support_(disk_runtime_support),
      post_deploy_support_(post_deploy_support),
      local_state_repository_(local_state_repository),
      local_runtime_state_support_(local_runtime_state_support),
      bootstrap_model_support_(bootstrap_model_support) {}

void HostdDesiredStateApplySupport::ApplyDesiredNodeState(
    const naim::DesiredState& desired_node_state,
    const std::string& artifacts_root,
    const std::string& storage_root,
    const std::optional<std::string>& runtime_root,
    const std::string& state_root,
    ComposeMode compose_mode,
    const std::string& source_label,
    const std::optional<int>& desired_generation,
    const std::optional<int>& assignment_id,
    HostdBackend* backend,
    const ProgressPublisher& publish_progress) const {
  const std::string node_name =
      local_state_repository_.RequireSingleNodeName(desired_node_state);
  const std::string& plane_name = desired_node_state.plane_name;
  const auto current_local_state =
      local_state_repository_.LoadLocalAppliedState(
          state_root,
          node_name,
          plane_name);
  const auto applied_generation =
      local_state_repository_.LoadLocalAppliedGeneration(
          state_root,
          node_name,
          plane_name);
  const auto execution_plan = HostdDesiredStateDisplaySupport::ResolveNodeExecutionPlan(
      naim::BuildNodeExecutionPlans(current_local_state, desired_node_state, artifacts_root),
      current_local_state,
      desired_node_state,
      node_name,
      artifacts_root);
  const auto compose_plan = RequireNodeComposePlan(desired_node_state, node_name);

  std::cout << source_label << "\n";
  std::cout << "artifacts_root=" << artifacts_root << "\n";
  std::cout << "state_path="
            << HostdLocalStatePathSupport().LocalPlaneStatePath(
                   state_root,
                   node_name,
                   plane_name)
            << "\n";
  if (desired_generation.has_value()) {
    std::cout << "desired_generation=" << *desired_generation << "\n";
  }
  if (applied_generation.has_value()) {
    std::cout << "applied_generation=" << *applied_generation << "\n";
  }
  if (runtime_root.has_value()) {
    std::cout << "runtime_root=" << *runtime_root << "\n";
  }
  if (const auto runtime_config_path =
          path_support_.InferRuntimeConfigPathForNode(desired_node_state, node_name)) {
    std::cout << "infer_runtime_config=" << *runtime_config_path << "\n";
    std::cout << "infer_runtime_summary="
              << HostdDesiredStateDisplaySupport::RuntimeConfigSummary(desired_node_state)
              << "\n";
  }
  std::cout << "compose_mode="
            << (compose_mode == ComposeMode::Exec ? "exec" : "skip") << "\n";

  ValidateDesiredNodeStateForCurrentHost(desired_node_state, compose_mode);
  WriteDesiredStateSnapshot(path_support_, desired_node_state, node_name);

  auto maybe_publish_progress =
      [&](const std::string& phase,
          const std::string& title,
          const std::string& detail,
          int percent,
          const std::string& current_plane_name,
          const std::string& current_node_name) {
        if (publish_progress) {
          publish_progress(
              phase,
              title,
              detail,
              percent,
              current_plane_name,
              current_node_name);
        }
      };

  if (execution_plan.operations.empty()) {
    std::cout << "no local changes for node=" << node_name << "\n";
    disk_runtime_support_.PersistDiskRuntimeStateForDesiredDisks(
        backend,
        desired_node_state,
        storage_root,
        runtime_root,
        "disk runtime verified by hostd");
    MaterializeAppModelMounts(
        desired_node_state,
        node_name,
        backend,
        maybe_publish_progress);
    if (desired_node_state.instances.empty()) {
      maybe_publish_progress(
          "stopping-runtime",
          "Stopping runtime",
          "Ensuring stale compose runtime is stopped on the node.",
          92,
          plane_name,
          node_name);
      apply_plan_support_.StopAndRemoveComposeArtifactIfPresent(
          artifacts_root,
          plane_name,
          node_name,
          compose_mode);
    }
    if (HostdDesiredStateApplyPlanSupport::IsDesiredNodeStateEmpty(desired_node_state)) {
      local_state_repository_.RemoveLocalAppliedPlaneState(
          state_root,
          node_name,
          plane_name);
    } else {
      local_state_repository_.SaveLocalAppliedState(
          state_root,
          node_name,
          desired_node_state,
          plane_name);
      if (desired_generation.has_value()) {
        local_state_repository_.SaveLocalAppliedGeneration(
            state_root,
            node_name,
            *desired_generation,
            plane_name);
      }
    }
    local_state_repository_.RewriteAggregateLocalState(state_root, node_name);
    local_state_repository_.RewriteAggregateLocalGeneration(state_root, node_name);
    maybe_publish_progress(
        "completed",
        "Assignment complete",
        "No local changes were required for the node.",
        100,
        plane_name,
        node_name);
    return;
  }

  disk_runtime_support_.EnsureDesiredDisksReady(
      backend,
      desired_node_state,
      storage_root,
      runtime_root);
  bootstrap_model_support_.BootstrapPlaneModelIfNeeded(
      desired_node_state,
      node_name,
      backend,
      assignment_id);
  MaterializeAppModelMounts(
      desired_node_state,
      node_name,
      backend,
      maybe_publish_progress);

  apply_plan_support_.ApplyNodePlan(
      execution_plan,
      desired_node_state,
      compose_plan,
      storage_root,
      runtime_root,
      compose_mode,
      backend,
      maybe_publish_progress);
  if (desired_node_state.instances.empty()) {
    maybe_publish_progress(
        "stopping-runtime",
        "Stopping runtime",
        "Ensuring stale compose runtime is stopped on the node.",
        92,
        plane_name,
        node_name);
    apply_plan_support_.StopAndRemoveComposeArtifactIfPresent(
        artifacts_root,
        plane_name,
        node_name,
        compose_mode);
  }
  disk_runtime_support_.PersistDiskRuntimeStateForRemovedDisks(
      backend,
      current_local_state,
      execution_plan);
  disk_runtime_support_.PersistDiskRuntimeStateForDesiredDisks(
      backend,
      desired_node_state,
      storage_root,
      runtime_root,
      "disk runtime applied by hostd");
  if (HostdDesiredStateApplyPlanSupport::IsDesiredNodeStateEmpty(desired_node_state)) {
    local_state_repository_.RemoveLocalAppliedPlaneState(
        state_root,
        node_name,
        plane_name);
    local_state_repository_.RewriteAggregateLocalState(state_root, node_name);
    local_state_repository_.RewriteAggregateLocalGeneration(state_root, node_name);
    return;
  }
  local_state_repository_.SaveLocalAppliedState(
      state_root,
      node_name,
      desired_node_state,
      plane_name);
  if (desired_generation.has_value()) {
    local_state_repository_.SaveLocalAppliedGeneration(
        state_root,
        node_name,
        *desired_generation,
        plane_name);
  }
  local_state_repository_.RewriteAggregateLocalState(state_root, node_name);
  local_state_repository_.RewriteAggregateLocalGeneration(state_root, node_name);
  if (compose_mode == ComposeMode::Exec) {
    maybe_publish_progress(
        "waiting-runtime-ready",
        "Waiting for runtime readiness",
        "Runtime was started; waiting for infer and worker observation to converge.",
        97,
        plane_name,
        node_name);
    if (NodeHasInferInstance(desired_node_state)) {
      local_runtime_state_support_.WaitForLocalRuntimeStatus(
          state_root,
          node_name,
          plane_name,
          std::chrono::seconds(300));
    }
    local_runtime_state_support_.WaitForLocalInstanceRuntimeStatuses(
        state_root,
        node_name,
        plane_name,
        apply_plan_support_.ExpectedRuntimeStatusCountForComposePlan(compose_plan),
        std::chrono::seconds(300));
    post_deploy_support_.RunIfNeeded(
        desired_node_state,
        node_name,
        artifacts_root,
        storage_root,
        runtime_root,
        state_root,
        desired_generation,
        assignment_id,
        backend);
  }
  maybe_publish_progress(
      "completed",
      "Assignment complete",
      "Desired runtime state was applied on the node.",
      100,
      plane_name,
      node_name);
}

std::string HostdDesiredStateApplySupport::CurrentHostPlatform() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

const naim::NodeInventory* HostdDesiredStateApplySupport::FindNodeInventory(
    const naim::DesiredState& desired_node_state,
    const std::string& node_name) {
  for (const auto& node : desired_node_state.nodes) {
    if (node.name == node_name) {
      return &node;
    }
  }
  return nullptr;
}

bool HostdDesiredStateApplySupport::NodeHasInferInstance(const naim::DesiredState& state) {
  for (const auto& instance : state.instances) {
    if (instance.role == naim::InstanceRole::Infer) {
      return true;
    }
  }
  return false;
}

naim::NodeComposePlan HostdDesiredStateApplySupport::RequireNodeComposePlan(
    const naim::DesiredState& state,
    const std::string& node_name) {
  const auto plan = naim::FindNodeComposePlan(state, node_name);
  if (!plan.has_value()) {
    throw std::runtime_error("node '" + node_name + "' not found in compose plan");
  }
  return *plan;
}

bool HostdDesiredStateApplySupport::NodeUsesManagedRuntimeServices(
    const naim::DesiredState& desired_node_state,
    const std::string& node_name) {
  for (const auto& instance : desired_node_state.instances) {
    if (instance.node_name == node_name) {
      return true;
    }
  }
  return false;
}

bool HostdDesiredStateApplySupport::NodeUsesGpuRuntime(
    const naim::DesiredState& desired_node_state,
    const std::string& node_name) {
  for (const auto& runtime_gpu_node : desired_node_state.runtime_gpu_nodes) {
    if (runtime_gpu_node.enabled && runtime_gpu_node.node_name == node_name) {
      return true;
    }
  }
  for (const auto& instance : desired_node_state.instances) {
    if (instance.node_name == node_name &&
        (instance.role == naim::InstanceRole::Worker ||
         (instance.gpu_device.has_value() && !instance.gpu_device->empty()))) {
      return true;
    }
  }
  if (const auto* node = FindNodeInventory(desired_node_state, node_name);
      node != nullptr && NodeHasConfiguredGpuDevices(*node)) {
    return true;
  }
  return false;
}

void HostdDesiredStateApplySupport::ValidateDesiredNodeStateForCurrentHost(
    const naim::DesiredState& desired_node_state,
    ComposeMode compose_mode) const {
  if (compose_mode != ComposeMode::Exec) {
    return;
  }

  if (desired_node_state.nodes.empty()) {
    throw std::runtime_error("desired node state is empty");
  }
  const std::string node_name = desired_node_state.nodes.front().name;
  const std::string host_platform = CurrentHostPlatform();
  if (const auto* node = FindNodeInventory(desired_node_state, node_name);
      node != nullptr && !node->platform.empty() && node->platform != host_platform) {
    throw std::runtime_error(
        "node '" + node_name + "' targets platform '" + node->platform +
        "', but hostd is running on '" + host_platform + "'");
  }

  if (host_platform == "macos" &&
      NodeUsesManagedRuntimeServices(desired_node_state, node_name) &&
      NodeUsesGpuRuntime(desired_node_state, node_name)) {
    throw std::runtime_error(
        "node '" + node_name +
        "' requests Linux/NVIDIA GPU runtime, but hostd compose exec is unsupported on macOS");
  }
}

}  // namespace naim::hostd
