#pragma once

#include <filesystem>
#include <optional>

#include "config/generated_config_loader.h"
#include "config/install_layout.h"
#include "config/launcher_options.h"
#include "install/launcher_install_service.h"
#include "platform/process_runner.h"
#include "platform/signal_manager.h"

namespace comet::launcher {

class LauncherRunService {
 public:
  LauncherRunService(
      const InstallLayoutResolver& install_layout_resolver,
      const GeneratedConfigLoader& config_loader,
      const ProcessRunner& process_runner,
      const LauncherInstallService& install_service);

  int RunController(
      SignalManager& signal_manager,
      const std::filesystem::path& self_path,
      const std::filesystem::path& controller_binary,
      const LauncherCommandLine& command_line) const;
  int RunHostd(
      SignalManager& signal_manager,
      const std::filesystem::path& hostd_binary,
      const std::filesystem::path& self_path,
      const LauncherCommandLine& command_line) const;

 private:
  int RunHostdLoop(
      SignalManager& signal_manager,
      const std::filesystem::path& hostd_binary,
      const HostdRunOptions& options) const;
  void PrepareControllerRuntime(const ControllerRunOptions& options) const;
  int RunControllerSupervisor(
      SignalManager& signal_manager,
      const std::filesystem::path& self_path,
      const std::filesystem::path& controller_binary,
      const ControllerRunOptions& options) const;
  std::string DefaultNodeName() const;
  std::string DefaultWebUiControllerUpstream(int listen_port) const;
  std::string Trim(const std::string& value) const;
  std::string ReadTextFile(const std::filesystem::path& path) const;
  std::string ComputePublicKeyFingerprint(const std::filesystem::path& public_key_path) const;

  const InstallLayoutResolver& install_layout_resolver_;
  const GeneratedConfigLoader& config_loader_;
  const ProcessRunner& process_runner_;
  const LauncherInstallService& install_service_;
};

}  // namespace comet::launcher
