#pragma once

#include <string>

namespace comet::hostd {

class HostdFileSupport final {
 public:
  void WriteTextFile(const std::string& path, const std::string& contents) const;
  void RemoveFileIfExists(const std::string& path) const;
  void EnsureParentDirectory(const std::string& path) const;
};

}  // namespace comet::hostd
