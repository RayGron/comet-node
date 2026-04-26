#include "app/hostd_self_update_support.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace naim::hostd {

namespace {

std::string PathString(const std::filesystem::path& path) {
  return path.lexically_normal().string();
}

}  // namespace

bool HostdSelfUpdateSupport::IsPathWithin(
    const std::filesystem::path& child,
    const std::filesystem::path& parent) {
  const auto normalized_child = child.lexically_normal();
  const auto normalized_parent = parent.lexically_normal();
  auto child_it = normalized_child.begin();
  auto parent_it = normalized_parent.begin();
  for (; parent_it != normalized_parent.end(); ++parent_it, ++child_it) {
    if (child_it == normalized_child.end() || *child_it != *parent_it) {
      return false;
    }
  }
  return true;
}

std::string HostdSelfUpdateSupport::SanitizeContainerNamePart(
    const std::string& value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const char ch : value) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0 || ch == '_' || ch == '.' || ch == '-') {
      sanitized.push_back(static_cast<char>(std::tolower(uch)));
    } else {
      sanitized.push_back('-');
    }
  }
  while (!sanitized.empty() && sanitized.front() == '-') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '-') {
    sanitized.pop_back();
  }
  if (sanitized.empty()) {
    return "unknown";
  }
  return sanitized;
}

std::string HostdSelfUpdateSupport::BuildHelperContainerName(
    const std::string& node_name,
    const std::string& release_tag) {
  std::string name = "naim-hostd-self-update-" +
                     SanitizeContainerNamePart(node_name) + "-" +
                     SanitizeContainerNamePart(release_tag);
  constexpr std::size_t kMaxNameLength = 96;
  if (name.size() > kMaxNameLength) {
    name.resize(kMaxNameLength);
    while (!name.empty() && name.back() == '-') {
      name.pop_back();
    }
  }
  return name.empty() ? "naim-hostd-self-update" : name;
}

HostdSelfUpdatePlan HostdSelfUpdateSupport::BuildPlan(
    const HostdSelfUpdateRequest& request) const {
  const std::string sed_expression =
      "s#^([[:space:]]*image:[[:space:]]*)chainzano.com/naim/hostd"
      "(:[^[:space:]]+|@sha256:[a-f0-9]+)?#\\1" +
      request.hostd_image + "#";

  std::string script = "#!/usr/bin/env bash\n"
                       "set -euo pipefail\n";
  if (request.registry_config_available) {
    script += "export DOCKER_CONFIG=" +
              command_support_.ShellQuote(PathString(request.registry_config_dir)) + "\n";
  }
  script += "sed -i -E " + command_support_.ShellQuote(sed_expression) + " " +
            command_support_.ShellQuote(PathString(request.compose_file)) + "\n"
            "grep -F " + command_support_.ShellQuote("image: " + request.hostd_image) +
            " " + command_support_.ShellQuote(PathString(request.compose_file)) +
            " >/dev/null\n"
            "sleep 2\n"
            "docker compose -f " + command_support_.ShellQuote(PathString(request.compose_file)) +
            " pull naim-hostd\n"
            "docker compose -f " + command_support_.ShellQuote(PathString(request.compose_file)) +
            " up -d --remove-orphans --force-recreate naim-hostd\n";

  const std::string helper_container_name =
      BuildHelperContainerName(request.node_name, request.release_tag);

  std::vector<std::string> mounts{
      "/var/run/docker.sock:/var/run/docker.sock",
      PathString(request.hostd_root) + ":" + PathString(request.hostd_root),
  };
  const std::filesystem::path compose_parent = request.compose_file.parent_path();
  if (!compose_parent.empty() && !IsPathWithin(compose_parent, request.hostd_root)) {
    mounts.push_back(PathString(compose_parent) + ":" + PathString(compose_parent));
  }
  if (request.registry_config_available &&
      !IsPathWithin(request.registry_config_dir, request.hostd_root)) {
    mounts.push_back(PathString(request.registry_config_dir) + ":" +
                     PathString(request.registry_config_dir) + ":ro");
  }

  std::string launch_command = command_support_.ResolvedDockerCommand();
  if (request.registry_config_available) {
    launch_command += " --config " +
                      command_support_.ShellQuote(PathString(request.registry_config_dir));
  }
  launch_command += " run --rm --detach --name " +
                    command_support_.ShellQuote(helper_container_name) +
                    " --user 0:0 --entrypoint " +
                    command_support_.ShellQuote("/usr/bin/env");
  for (const std::string& mount : mounts) {
    launch_command += " -v " + command_support_.ShellQuote(mount);
  }
  launch_command += " " + command_support_.ShellQuote(request.hostd_image) +
                    " bash -lc " +
                    command_support_.ShellQuote(
                        "bash " + command_support_.ShellQuote(PathString(request.update_script)) +
                        " >>" + command_support_.ShellQuote(PathString(request.update_log)) +
                        " 2>&1");

  return HostdSelfUpdatePlan{
      script,
      launch_command,
      helper_container_name,
  };
}

}  // namespace naim::hostd
