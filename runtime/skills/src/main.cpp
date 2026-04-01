#include "skills/skills_server.h"

#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

std::string GetEnvOr(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

int GetEnvIntOr(const char* name, int fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return fallback;
  }
  return std::stoi(value);
}

}  // namespace

int main() {
  try {
    comet::skills::SkillsRuntimeConfig config;
    config.plane_name = GetEnvOr("COMET_PLANE_NAME", "unknown");
    config.instance_name = GetEnvOr("COMET_INSTANCE_NAME", "skills-unknown");
    config.instance_role = GetEnvOr("COMET_INSTANCE_ROLE", "skills");
    config.node_name = GetEnvOr("COMET_NODE_NAME", "unknown");
    config.control_root = GetEnvOr("COMET_CONTROL_ROOT", "");
    config.controller_url = GetEnvOr("COMET_CONTROLLER_URL", "http://controller.internal:18080");
    config.db_path = GetEnvOr("COMET_SKILLS_DB_PATH", "/comet/private/skills.sqlite");
    config.status_path =
        GetEnvOr("COMET_SKILLS_RUNTIME_STATUS_PATH", "/comet/private/skills-runtime-status.json");
    config.port = GetEnvIntOr("COMET_SKILLS_PORT", 18120);

    std::cout << "[comet-skills] booting plane=" << config.plane_name
              << " instance=" << config.instance_name << "\n";
    std::cout << "[comet-skills] db_path=" << config.db_path.string() << "\n";
    std::cout << "[comet-skills] status_path=" << config.status_path.string() << "\n";
    std::cout << "[comet-skills] port=" << config.port << "\n";
    std::cout.flush();

    comet::skills::SkillsServer server(std::move(config));
    return server.Run();
  } catch (const std::exception& error) {
    std::cerr << "comet-skillsd: " << error.what() << "\n";
    return 1;
  }
}
