#include "app/hostd_self_update_support.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  using naim::hostd::HostdSelfUpdateRequest;
  using naim::hostd::HostdSelfUpdateSupport;

  try {
    HostdSelfUpdateSupport support;
    const auto plan = support.BuildPlan(
        HostdSelfUpdateRequest{
            "release/ABC@1234567890",
            "Storage 1",
            "chainzano.com/naim/hostd@sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "/opt/naim/hostd",
            "/opt/naim/hostd/docker-compose.yml",
            "/opt/naim/hostd/install-state/registry-docker",
            "/opt/naim/hostd/install-state/hostd-self-update.sh",
            "/opt/naim/hostd/logs/hostd-self-update-release.log",
            "112",
            true,
        });

    Expect(
        plan.helper_container_name == "naim-hostd-self-update-storage-1-release-abc-1234567890",
        "helper container name should be deterministic and docker-safe");
    Expect(
        Contains(plan.script_content, "export DOCKER_CONFIG='/opt/naim/hostd/install-state/registry-docker'"),
        "script should export registry config when available");
    Expect(
        Contains(plan.script_content, "docker compose -f '/opt/naim/hostd/docker-compose.yml' pull naim-hostd"),
        "script should pull the hostd service image");
    Expect(
        Contains(
            plan.script_content,
            "docker compose -f '/opt/naim/hostd/docker-compose.yml' up -d --remove-orphans --force-recreate naim-hostd"),
        "script should force-recreate and start naim-hostd");
    Expect(
        !Contains(plan.launch_command, "nohup"),
        "launcher must not keep the updater inside the old hostd container");
    Expect(
        Contains(
            plan.launch_command,
            " run --rm --detach --privileged --name 'naim-hostd-self-update-storage-1-release-abc-1234567890' "),
        "launcher should start a detached helper container");
    Expect(
        Contains(plan.launch_command, "--privileged"),
        "launcher should be privileged so Docker socket access works under host userns policies");
    Expect(
        Contains(plan.launch_command, "-v '/var/run/docker.sock:/var/run/docker.sock'"),
        "launcher should mount docker socket into helper");
    Expect(
        Contains(plan.launch_command, "-v '/opt/naim/hostd:/opt/naim/hostd'"),
        "launcher should mount hostd root into helper");
    Expect(
        Contains(plan.launch_command, "--entrypoint '/usr/bin/env'"),
        "launcher should bypass the hostd image default entrypoint");
    Expect(
        Contains(plan.launch_command, "--user 0:0"),
        "launcher should run as root so it can access the mounted docker socket");
    Expect(
        Contains(plan.launch_command, "--group-add '112'"),
        "launcher should include the docker socket group when it is known");
    Expect(
        Contains(plan.launch_command, "bash -lc 'bash '\"'\"'/opt/naim/hostd/install-state/hostd-self-update.sh'\"'\"' >>'\"'\"'/opt/naim/hostd/logs/hostd-self-update-release.log'\"'\"' 2>&1'"),
        "launcher should write helper script output to the hostd log path");

    const auto external_plan = support.BuildPlan(
        HostdSelfUpdateRequest{
            "abc",
            "hpc1",
            "chainzano.com/naim/hostd:abc",
            "/opt/naim/hostd",
            "/etc/naim/hostd/docker-compose.yml",
            "/run/naim/registry",
            "/opt/naim/hostd/install-state/hostd-self-update.sh",
            "/opt/naim/hostd/logs/hostd-self-update-abc.log",
            "",
            true,
        });
    Expect(
        Contains(external_plan.launch_command, "-v '/etc/naim/hostd:/etc/naim/hostd'"),
        "launcher should mount an external compose parent directory");
    Expect(
        Contains(external_plan.launch_command, "-v '/run/naim/registry:/run/naim/registry:ro'"),
        "launcher should mount an external registry config directory read-only");

    std::cout << "ok: hostd-self-update-helper-container-plan" << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
