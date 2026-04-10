#pragma once

#include <string>

#include "app/controller_time_support.h"
#include "infra/controller_print_service.h"

namespace comet::controller {

class PlaneStatePresentationSupport {
 public:
  virtual ~PlaneStatePresentationSupport() = default;

  virtual std::string FormatTimestamp(const std::string& value) const = 0;
  virtual void PrintStateSummary(const comet::DesiredState& state) const = 0;
};

class ControllerPlaneStatePresentationSupport final
    : public PlaneStatePresentationSupport {
 public:
  explicit ControllerPlaneStatePresentationSupport(
      const ControllerPrintService& controller_print_service);

  std::string FormatTimestamp(const std::string& value) const override;
  void PrintStateSummary(const comet::DesiredState& state) const override;

 private:
  const ControllerPrintService& controller_print_service_;
};

}  // namespace comet::controller
