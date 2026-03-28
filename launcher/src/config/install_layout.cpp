#include "config/install_layout.h"

#include <cstdlib>

#include "comet/core/platform_compat.h"

namespace comet::launcher {

std::optional<fs::path> InstallLayoutResolver::InstallRootOverride() const {
  const char* value = std::getenv("COMET_INSTALL_ROOT");
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return fs::path(value);
}

InstallLayout InstallLayoutResolver::DefaultInstallLayout() const {
  if (const auto root = InstallRootOverride(); root.has_value()) {
    return InstallLayout{
        *root / "etc/comet-node/config.toml",
        *root / "var/lib/comet-node",
        *root / "var/log/comet-node",
        *root / "etc/systemd/system",
    };
  }
  if (!comet::platform::HasElevatedPrivileges()) {
    const fs::path home = std::getenv("HOME") != nullptr ? fs::path(std::getenv("HOME"))
                                                         : fs::current_path();
    return InstallLayout{
        home / ".config/comet-node/config.toml",
        home / ".local/share/comet-node",
        home / ".local/state/comet-node",
        home / ".config/systemd/user",
    };
  }
  return InstallLayout{};
}

fs::path InstallLayoutResolver::DefaultControllerDbPath() const {
  return DefaultInstallLayout().state_root / "controller.sqlite";
}

fs::path InstallLayoutResolver::DefaultArtifactsRoot() const {
  return DefaultInstallLayout().state_root / "artifacts";
}

fs::path InstallLayoutResolver::DefaultWebUiRoot() const {
  return DefaultInstallLayout().state_root / "web-ui";
}

fs::path InstallLayoutResolver::DefaultRuntimeRoot() const {
  return DefaultInstallLayout().state_root / "runtime";
}

fs::path InstallLayoutResolver::DefaultHostdStateRoot() const {
  return DefaultInstallLayout().state_root / "hostd-state";
}

bool InstallLayoutResolver::IsUserServiceLayout(const InstallLayout& layout) const {
  const std::string rendered = layout.systemd_dir.string();
  return rendered.find(".config/systemd/user") != std::string::npos;
}

}  // namespace comet::launcher
