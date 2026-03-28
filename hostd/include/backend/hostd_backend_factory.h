#pragma once

#include <memory>
#include <optional>
#include <string>

#include "backend/hostd_backend.h"
#include "backend/http_hostd_backend_support.h"

namespace comet::hostd {

class IHostdBackendFactory {
 public:
  virtual ~IHostdBackendFactory() = default;

  virtual std::unique_ptr<HostdBackend> CreateBackend(
      const std::optional<std::string>& db_path,
      const std::optional<std::string>& controller_url,
      const std::optional<std::string>& host_private_key_path,
      const std::optional<std::string>& controller_fingerprint) const = 0;
};

class HostdBackendFactory final : public IHostdBackendFactory {
 public:
  explicit HostdBackendFactory(const IHttpHostdBackendSupport& support);

  std::unique_ptr<HostdBackend> CreateBackend(
      const std::optional<std::string>& db_path,
      const std::optional<std::string>& controller_url,
      const std::optional<std::string>& host_private_key_path,
      const std::optional<std::string>& controller_fingerprint) const override;

 private:
  const IHttpHostdBackendSupport& support_;
};

}  // namespace comet::hostd
