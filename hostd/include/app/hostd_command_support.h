#pragma once

#include <string>
#include <vector>

namespace naim::hostd {

class HostdCommandSupport final {
 public:
  using EnvPack = std::vector<std::pair<std::string, std::string>>;

  std::string Trim(const std::string& value) const;
  std::string RunCommandCapture(const std::string& command) const;
  std::string ShellQuote(const std::string& value) const;
  bool RunCommandOk(const std::string& command,
                    const EnvPack &additionalEnviromentVariables = {}) const;
  std::string ResolvedDockerCommand() const;
  std::string ResolvedDockerComposeCommand() const;

 private:
  bool SetEnvVar(const std::string& name, const std::string& value) const;
};

}  // namespace naim::hostd
