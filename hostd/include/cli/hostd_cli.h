#pragma once

#include <optional>
#include <string>

#include "cli/hostd_command_line.h"

namespace comet::hostd {

struct CometNodeConfig;
class NodeConfigLoader;

class IHostdCliActions {
 public:
  virtual ~IHostdCliActions() = default;

  virtual void ShowDemoOps(
      const std::string& node_name,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root) = 0;
  virtual void ShowStateOps(
      const std::string& db_path,
      const std::string& node_name,
      const std::string& artifacts_root,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root,
      const std::string& state_root) = 0;
  virtual void ShowLocalState(
      const std::string& node_name,
      const std::string& state_root) = 0;
  virtual void ShowRuntimeStatus(
      const std::string& node_name,
      const std::string& state_root) = 0;
  virtual void ReportLocalObservedState(
      const std::optional<std::string>& db_path,
      const std::optional<std::string>& controller_url,
      const std::optional<std::string>& host_private_key_path,
      const std::optional<std::string>& controller_fingerprint,
      const std::string& node_name,
      const std::string& state_root) = 0;
  virtual void ApplyStateOps(
      const std::string& db_path,
      const std::string& node_name,
      const std::string& artifacts_root,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root,
      const std::string& state_root,
      ComposeMode compose_mode) = 0;
  virtual void ApplyNextAssignment(
      const std::optional<std::string>& db_path,
      const std::optional<std::string>& controller_url,
      const std::optional<std::string>& host_private_key_path,
      const std::optional<std::string>& controller_fingerprint,
      const std::string& node_name,
      const std::string& storage_root,
      const std::optional<std::string>& runtime_root,
      const std::string& state_root,
      ComposeMode compose_mode) = 0;
};

class HostdCli {
 public:
  explicit HostdCli(IHostdCliActions& actions);

  int Run(const HostdCommandLine& command_line, const NodeConfigLoader& config_loader, const char* argv0) const;

 private:
  IHostdCliActions& actions_;
};

}  // namespace comet::hostd
