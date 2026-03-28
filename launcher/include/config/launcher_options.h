#pragma once

#include <filesystem>
#include <string>

#include "config/install_layout.h"

namespace comet::launcher {

namespace fs = std::filesystem;

struct ControllerInstallOptions {
  InstallLayout layout;
  fs::path binary_path;
  std::string listen_host = "0.0.0.0";
  int listen_port = 18080;
  bool with_hostd = false;
  bool with_web_ui = false;
  std::string compose_mode = "exec";
  std::string node_name = "local-hostd";
};

struct HostdInstallOptions {
  InstallLayout layout;
  fs::path binary_path;
  std::string controller_url;
  std::string controller_fingerprint;
  std::string node_name;
  std::string transport_mode = "out";
  std::string execution_mode = "mixed";
  std::string listen_address;
  std::string compose_mode = "exec";
};

struct ControllerRunOptions {
  fs::path db_path;
  fs::path artifacts_root;
  fs::path web_ui_root;
  std::string listen_host = "0.0.0.0";
  int listen_port = 18080;
  bool with_hostd = false;
  bool with_web_ui = false;
  std::string controller_upstream;
  std::string compose_mode = "exec";
  std::string hostd_compose_mode = "exec";
  std::string node_name = "local-hostd";
  fs::path runtime_root;
  fs::path state_root;
  int hostd_poll_interval_sec = 2;
};

struct HostdRunOptions {
  fs::path db_path;
  std::string controller_url;
  std::string controller_fingerprint;
  std::string node_name;
  fs::path runtime_root;
  fs::path state_root;
  fs::path host_private_key_path;
  std::string compose_mode = "exec";
  int poll_interval_sec = 2;
};

}  // namespace comet::launcher
