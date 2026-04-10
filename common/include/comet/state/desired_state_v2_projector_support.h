#pragma once

#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "comet/state/models.h"

namespace comet {

class DesiredStateV2ProjectorSupport {
 public:
  static constexpr int kDefaultSharedDiskSizeGb = 40;
  static constexpr int kDefaultInferPrivateDiskSizeGb = 12;
  static constexpr int kDefaultWorkerPrivateDiskSizeGb = 2;
  static constexpr int kDefaultAppPrivateDiskSizeGb = 8;
  static constexpr int kDefaultSkillsPrivateDiskSizeGb = 1;
  static constexpr int kDefaultWebGatewayPrivateDiskSizeGb = 1;

  static constexpr std::string_view kDefaultInferImage = "comet/infer-runtime:dev";
  static constexpr std::string_view kDefaultWorkerImage = "comet/worker-runtime:dev";
  static constexpr std::string_view kDefaultSkillsImage = "comet/skills-runtime:dev";
  static constexpr std::string_view kDefaultWebGatewayImage =
      "comet/webgateway-runtime:dev";

  static constexpr std::string_view kDefaultInferCommand =
      "/runtime/bin/comet-inferctl container-boot";
  static constexpr std::string_view kDefaultWorkerCommand =
      "/runtime/bin/comet-workerd";
  static constexpr std::string_view kDefaultSkillsCommand =
      "/runtime/bin/comet-skillsd";
  static constexpr std::string_view kDefaultWebGatewayCommand =
      "/runtime/bin/comet-webgatewayd";

  static nlohmann::json ProjectServiceStart(
      const InstanceSpec& instance,
      const std::string& default_command);
  static nlohmann::json ProjectPublishedPorts(const InstanceSpec& instance);
  static nlohmann::json ProjectServiceStorage(const DiskSpec* disk);
  static nlohmann::json ProjectAppVolumes(const DiskSpec* disk);
  static std::map<std::string, std::string> ProjectCustomEnv(
      const InstanceSpec& instance,
      bool strip_comet_env);

  static bool IsDefaultWorkerImage(const std::string& image);
};

}  // namespace comet
