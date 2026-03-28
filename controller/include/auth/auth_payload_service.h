#pragma once

#include <nlohmann/json.hpp>

#include "comet/state/sqlite_store.h"

#include "comet/state/models.h"

namespace comet::controller {

class AuthPayloadService {
 public:
  nlohmann::json BuildUserPayload(const comet::UserRecord& user) const;
  nlohmann::json BuildInvitePayload(
      const comet::RegistrationInviteRecord& invite) const;
  nlohmann::json BuildSshKeyPayload(
      const comet::UserSshKeyRecord& ssh_key) const;
};

}  // namespace comet::controller
