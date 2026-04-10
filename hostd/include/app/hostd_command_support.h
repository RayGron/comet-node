#pragma once

#include <string>

namespace comet::hostd {

class HostdCommandSupport final {
 public:
  std::string Trim(const std::string& value) const;
  std::string RunCommandCapture(const std::string& command) const;
  std::string ShellQuote(const std::string& value) const;
  bool RunCommandOk(const std::string& command) const;
  std::string ResolvedDockerCommand() const;
};

}  // namespace comet::hostd
