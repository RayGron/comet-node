#pragma once

#include <optional>
#include <string>

namespace comet::hostd {

struct CometNodeConfig {
  std::string storage_root = "/var/lib/comet";
};

class NodeConfigLoader {
 public:
  CometNodeConfig Load(
      const std::optional<std::string>& config_arg,
      const char* argv0) const;

 private:
  std::optional<std::string> FindNodeConfigPath(const char* argv0) const;
};

}  // namespace comet::hostd
