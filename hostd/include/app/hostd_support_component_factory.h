#pragma once

#include <memory>

namespace comet::hostd {

class HostdAppControllerSupport;
class HostdAppObservationSupport;
class HostdAppAssignmentSupport;

class HostdSupportComponentFactory final {
 public:
  std::unique_ptr<HostdAppControllerSupport> CreateBackendSupport() const;
  std::unique_ptr<HostdAppObservationSupport> CreateObservationSupport() const;
  std::unique_ptr<HostdAppAssignmentSupport> CreateAssignmentSupport() const;
};

}  // namespace comet::hostd
