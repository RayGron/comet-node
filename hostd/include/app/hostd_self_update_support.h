#pragma once

#include <filesystem>
#include <string>

#include "app/hostd_command_support.h"

namespace naim::hostd {

struct HostdSelfUpdateRequest {
  std::string release_tag;
  std::string node_name;
  std::string hostd_image;
  std::filesystem::path hostd_root;
  std::filesystem::path compose_file;
  std::filesystem::path registry_config_dir;
  std::filesystem::path update_script;
  std::filesystem::path update_log;
  std::string docker_socket_group_id;
  bool registry_config_available = false;
};

struct HostdSelfUpdatePlan {
  std::string script_content;
  std::string launch_command;
  std::string helper_container_name;
};

class HostdSelfUpdateSupport final {
 public:
  HostdSelfUpdatePlan BuildPlan(const HostdSelfUpdateRequest& request) const;

 private:
  static bool IsPathWithin(
      const std::filesystem::path& child,
      const std::filesystem::path& parent);
  static std::string SanitizeContainerNamePart(const std::string& value);
  static std::string BuildHelperContainerName(
      const std::string& node_name,
      const std::string& release_tag);

  HostdCommandSupport command_support_;
};

}  // namespace naim::hostd
