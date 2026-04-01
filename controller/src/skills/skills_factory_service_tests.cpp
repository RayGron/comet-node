#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/state/desired_state_v2_renderer.h"
#include "comet/state/desired_state_v2_validator.h"
#include "comet/state/sqlite_store.h"
#include "plane/plane_mutation_service.h"
#include "skills/plane_skill_catalog_service.h"
#include "skills/skills_factory_service.h"

namespace fs = std::filesystem;

namespace {

using nlohmann::json;

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

comet::DesiredState BuildDesiredState(
    const std::string& plane_name,
    const std::vector<std::string>& factory_skill_ids = {}) {
  json value{
      {"version", 2},
      {"plane_name", plane_name},
      {"plane_mode", "llm"},
      {"model",
       {
           {"source", {{"type", "local"}, {"path", "/models/qwen"}}},
           {"materialization", {{"mode", "reference"}, {"local_path", "/models/qwen"}}},
           {"served_model_name", plane_name + "-model"},
       }},
      {"runtime",
       {{"engine", "llama.cpp"}, {"distributed_backend", "llama_rpc"}, {"workers", 1}}},
      {"infer", {{"replicas", 1}}},
      {"skills",
       {
           {"enabled", true},
           {"factory_skill_ids", factory_skill_ids},
       }},
      {"app", {{"enabled", false}}},
  };
  comet::DesiredStateV2Validator::ValidateOrThrow(value);
  return comet::DesiredStateV2Renderer::Render(value);
}

comet::controller::PlaneMutationService MakeMutationService() {
  comet::controller::PlaneMutationService::Deps deps;
  deps.apply_desired_state =
      [](const std::string& db_path,
         const comet::DesiredState& desired_state,
         const std::string&,
         const std::string&) {
        comet::ControllerStore store(db_path);
        store.Initialize();
        store.ReplaceDesiredState(desired_state);
        return 0;
      };
  deps.make_plane_service =
      [](const std::string&) -> comet::controller::PlaneService {
        throw std::runtime_error("plane service should not be used in skills tests");
      };
  return comet::controller::PlaneMutationService(std::move(deps));
}

void SeedDesiredState(
    comet::ControllerStore& store,
    const comet::DesiredState& desired_state,
    int generation = 1) {
  store.ReplaceDesiredState(desired_state, generation);
  const auto plane = store.LoadPlane(desired_state.plane_name);
  Expect(plane.has_value(), "plane should exist after replacing desired state");
}

}  // namespace

int main() {
  try {
    const fs::path temp_root =
        fs::temp_directory_path() / "comet-skills-factory-service-tests";
    std::error_code error;
    fs::remove_all(temp_root, error);
    fs::create_directories(temp_root);
    const fs::path db_path = temp_root / "controller.sqlite";

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      SeedDesiredState(store, BuildDesiredState("factory-plane", {"skill-alpha"}), 3);
      store.UpsertSkillsFactorySkill(comet::SkillsFactorySkillRecord{
          "skill-alpha",
          "Alpha skill",
          "alpha/core",
          "Canonical alpha description",
          "canonical alpha content",
          "",
          "",
      });
      store.UpsertPlaneSkillBinding(comet::PlaneSkillBindingRecord{
          "factory-plane",
          "skill-alpha",
          false,
          {"session-a"},
          {"comet://alpha"},
          "",
          "",
      });

      comet::controller::SkillsFactoryService factory_service(
          MakeMutationService(),
          comet::controller::PlaneSkillRuntimeSyncService(),
          [](const std::string&, const std::string&, const std::string& fallback) {
            return fallback;
          });
      const auto payload = factory_service.BuildListPayload(db_path.string());
      Expect(payload.at("skills").size() == 1, "factory list should contain one skill");
      const auto& item = payload.at("skills").front();
      Expect(item.at("id").get<std::string>() == "skill-alpha", "factory skill id mismatch");
      Expect(item.at("plane_count").get<int>() == 1, "factory skill plane_count mismatch");
      Expect(
          item.at("plane_names").get<std::vector<std::string>>() ==
              std::vector<std::string>({"factory-plane"}),
          "factory skill plane_names mismatch");
      Expect(
          item.at("group_path").get<std::string>() == "alpha/core",
          "factory skill group_path mismatch");

      const auto deleted =
          factory_service.DeleteSkill(db_path.string(), "skill-alpha", temp_root.string());
      Expect(
          deleted.at("status").get<std::string>() == "deleted",
          "factory delete should return deleted status");
      Expect(
          !store.LoadSkillsFactorySkill("skill-alpha").has_value(),
          "factory delete should remove canonical record");
      Expect(
          !store.LoadPlaneSkillBinding("factory-plane", "skill-alpha").has_value(),
          "factory delete should remove plane binding");
      const auto next_state = store.LoadDesiredState("factory-plane");
      Expect(next_state.has_value(), "factory delete should keep plane desired state");
      Expect(
          next_state->skills.has_value() && next_state->skills->factory_skill_ids.empty(),
          "factory delete should detach skill id from plane desired state");
      std::cout << "ok: factory-delete-detaches-all-planes" << '\n';
    }

    {
      comet::ControllerStore store(db_path.string());
      store.Initialize();
      SeedDesiredState(store, BuildDesiredState("catalog-plane"), 5);

      comet::controller::PlaneSkillCatalogService catalog_service(
          MakeMutationService(),
          comet::controller::PlaneSkillRuntimeSyncService(),
          [](const std::string&, const std::string&, const std::string& fallback) {
            return fallback;
          });
      const auto created = catalog_service.CreateSkill(
          db_path.string(),
          "catalog-plane",
          json{
              {"name", "Catalog skill"},
              {"description", "Plane-owned binding using canonical content"},
              {"content", "always answer CATALOG-SKILL"},
              {"enabled", false},
              {"session_ids", json::array({"session-1", "session-2"})},
              {"comet_links", json::array({"comet://one"})},
          },
          temp_root.string());
      const auto skill_id = created.at("id").get<std::string>();
      Expect(!skill_id.empty(), "catalog create should return a skill id");
      Expect(
          created.at("enabled").get<bool>() == false,
          "catalog create should preserve enabled flag");
      Expect(
          created.at("session_ids").get<std::vector<std::string>>() ==
              std::vector<std::string>({"session-1", "session-2"}),
          "catalog create should persist session_ids");

      const auto canonical = store.LoadSkillsFactorySkill(skill_id);
      Expect(canonical.has_value(), "catalog create should upsert canonical record");
      Expect(
          canonical->content == "always answer CATALOG-SKILL",
          "catalog create should persist canonical content");
      const auto binding = store.LoadPlaneSkillBinding("catalog-plane", skill_id);
      Expect(binding.has_value(), "catalog create should persist plane binding");
      Expect(!binding->enabled, "catalog create should persist plane-local enabled=false");
      const auto state_after_create = store.LoadDesiredState("catalog-plane");
      Expect(state_after_create.has_value(), "catalog create should keep plane desired state");
      Expect(
          state_after_create->skills.has_value() &&
              state_after_create->skills->factory_skill_ids ==
                  std::vector<std::string>({skill_id}),
          "catalog create should attach skill id to plane desired state");

      const auto listed = catalog_service.BuildListPayload(db_path.string(), "catalog-plane");
      Expect(listed.at("skills").size() == 1, "catalog list should contain one attached skill");
      Expect(
          listed.at("skills").front().at("content").get<std::string>() ==
              "always answer CATALOG-SKILL",
          "catalog list should merge canonical content");

      const auto deleted = catalog_service.DeleteSkill(
          db_path.string(), "catalog-plane", skill_id, temp_root.string());
      Expect(
          deleted.at("status").get<std::string>() == "deleted",
          "catalog delete should return deleted status");
      Expect(
          store.LoadSkillsFactorySkill(skill_id).has_value(),
          "catalog delete should keep canonical record");
      Expect(
          !store.LoadPlaneSkillBinding("catalog-plane", skill_id).has_value(),
          "catalog delete should remove plane binding");
      const auto state_after_delete = store.LoadDesiredState("catalog-plane");
      Expect(state_after_delete.has_value(), "catalog delete should keep plane desired state");
      Expect(
          state_after_delete->skills.has_value() &&
              state_after_delete->skills->factory_skill_ids.empty(),
          "catalog delete should detach skill id from plane desired state");
      std::cout << "ok: plane-catalog-create-and-detach" << '\n';
    }

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "skills_factory_service_tests failed: " << error.what() << '\n';
    return 1;
  }
}
