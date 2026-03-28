#include "cli/hostd_cli.h"

#include <iostream>
#include <stdexcept>

#include "config/node_config_loader.h"

namespace comet::hostd {

HostdCli::HostdCli(IHostdCliActions& actions) : actions_(actions) {}

int HostdCli::Run(
    const HostdCommandLine& command_line,
    const NodeConfigLoader& config_loader,
    const char* argv0) const {
  if (!command_line.HasCommand()) {
    command_line.PrintUsage(std::cout);
    return 1;
  }

  const auto node_name = command_line.node();
  if (!node_name.has_value()) {
    std::cerr << "error: --node is required\n";
    return 1;
  }

  try {
    const CometNodeConfig node_config = config_loader.Load(command_line.config_path(), argv0);
    const std::string& command = command_line.command();

    if (command == "show-demo-ops") {
      actions_.ShowDemoOps(*node_name, node_config.storage_root, command_line.runtime_root());
      return 0;
    }

    if (command == "show-state-ops") {
      actions_.ShowStateOps(
          command_line.ResolveDbPath(command_line.db()),
          *node_name,
          command_line.ResolveArtifactsRoot(command_line.artifacts_root()),
          node_config.storage_root,
          command_line.runtime_root(),
          command_line.ResolveStateRoot(command_line.state_root()));
      return 0;
    }

    if (command == "show-local-state") {
      actions_.ShowLocalState(
          *node_name,
          command_line.ResolveStateRoot(command_line.state_root()));
      return 0;
    }

    if (command == "show-runtime-status") {
      actions_.ShowRuntimeStatus(
          *node_name,
          command_line.ResolveStateRoot(command_line.state_root()));
      return 0;
    }

    if (command == "report-observed-state") {
      actions_.ReportLocalObservedState(
          command_line.db(),
          command_line.controller(),
          command_line.host_private_key(),
          command_line.controller_fingerprint(),
          *node_name,
          command_line.ResolveStateRoot(command_line.state_root()));
      return 0;
    }

    if (command == "apply-state-ops") {
      actions_.ApplyStateOps(
          command_line.ResolveDbPath(command_line.db()),
          *node_name,
          command_line.ResolveArtifactsRoot(command_line.artifacts_root()),
          node_config.storage_root,
          command_line.runtime_root(),
          command_line.ResolveStateRoot(command_line.state_root()),
          command_line.ResolveComposeMode(command_line.compose_mode_raw()));
      return 0;
    }

    if (command == "apply-next-assignment") {
      actions_.ApplyNextAssignment(
          command_line.db(),
          command_line.controller(),
          command_line.host_private_key(),
          command_line.controller_fingerprint(),
          *node_name,
          node_config.storage_root,
          command_line.runtime_root(),
          command_line.ResolveStateRoot(command_line.state_root()),
          command_line.ResolveComposeMode(command_line.compose_mode_raw()));
      return 0;
    }
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }

  command_line.PrintUsage(std::cout);
  return 1;
}

}  // namespace comet::hostd
