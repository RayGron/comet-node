import { describe, expect, it } from "vitest";

import {
  buildDesiredStateV2FromForm,
  buildNewPlaneFormState,
  buildPlaneFormStateFromDesiredStateV2,
  validatePlaneV2Form,
} from "./planeV2Form.jsx";
import {
  buildSkillsFactoryGroupTree,
  collectGroupSkillIds,
  filterPlaneSelectableSkills,
  filterSkillsFactoryItems,
  formatSkillGroupPath,
  NO_GROUP_TREE_PATH,
  sortSkillsFactoryItems,
} from "./skillsFactory.js";
import { formatPlaneDashboardSkillsSummary } from "./planeSkills.js";

describe("skillsFactory utils", () => {
  const items = [
    {
      id: "skill-zeta",
      name: "Zeta",
      group_path: "code-agent/debugging",
      description: "Blue answer",
      content: "Always answer BLUE",
      plane_count: 1,
      plane_names: ["maglev"],
    },
    {
      id: "skill-alpha",
      name: "Alpha",
      group_path: "code-agent",
      description: "Shared answer",
      content: "Always answer ALPHA",
      plane_count: 3,
      plane_names: ["maglev", "demo", "prod"],
    },
  ];

  it("sorts by plane count before name", () => {
    expect(sortSkillsFactoryItems(items, "plane_count").map((item) => item.id)).toEqual([
      "skill-alpha",
      "skill-zeta",
    ]);
  });

  it("filters across id, content, and plane usage", () => {
    expect(filterSkillsFactoryItems(items, "blue").map((item) => item.id)).toEqual([
      "skill-zeta",
    ]);
    expect(filterPlaneSelectableSkills(items, "prod").map((item) => item.id)).toEqual([
      "skill-alpha",
    ]);
  });

  it("builds a group tree and filters by subtree", () => {
    const tree = buildSkillsFactoryGroupTree(items, [{ path: "lt-jex/localtrade/account" }]);
    expect(tree.total_skill_count).toBe(2);
    expect(tree.children.map((item) => item.path)).toEqual(["code-agent", "lt-jex"]);
    expect(tree.children[0].children.map((item) => item.path)).toEqual(["code-agent/debugging"]);
    expect(filterSkillsFactoryItems(items, "", "code-agent").map((item) => item.id)).toEqual([
      "skill-zeta",
      "skill-alpha",
    ]);
    expect(
      filterSkillsFactoryItems(items, "", "code-agent/debugging").map((item) => item.id),
    ).toEqual(["skill-zeta"]);
    expect(collectGroupSkillIds(items, "code-agent")).toEqual(["skill-zeta", "skill-alpha"]);
  });

  it("formats empty group paths as no group", () => {
    expect(formatSkillGroupPath("")).toBe("No group");
    expect(formatSkillGroupPath(" localtrade / streams ")).toBe("localtrade/streams");
  });

  it("adds No group node and filters ungrouped items", () => {
    const tree = buildSkillsFactoryGroupTree([
      ...items,
      {
        id: "skill-ungrouped",
        name: "Ungrouped",
        group_path: "",
        description: "No group skill",
        content: "No group content",
        plane_count: 0,
        plane_names: [],
      },
    ]);
    expect(tree.children.map((item) => item.path)).toEqual(["code-agent", NO_GROUP_TREE_PATH]);
    expect(filterSkillsFactoryItems(items, "", NO_GROUP_TREE_PATH)).toEqual([]);
    expect(
      filterSkillsFactoryItems(
        [
          ...items,
          {
            id: "skill-ungrouped",
            name: "Ungrouped",
            group_path: "",
            description: "No group skill",
            content: "No group content",
            plane_count: 0,
            plane_names: [],
          },
        ],
        "",
        NO_GROUP_TREE_PATH,
      ).map((item) => item.id),
    ).toEqual(["skill-ungrouped"]);
  });
});

describe("planeV2Form SkillsFactory mapping", () => {
  it("round-trips factory skill ids through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "skills-plane";
    form.modelPath = "/models/qwen";
    form.servedModelName = "skills-plane-model";
    form.skillsEnabled = true;
    form.factorySkillIds = ["skill-alpha", "skill-beta"];

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.skills).toEqual({
      enabled: true,
      factory_skill_ids: ["skill-alpha", "skill-beta"],
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.factorySkillIds).toEqual(["skill-alpha", "skill-beta"]);
  });

  it("warns when factory selections exist while Skills is disabled", () => {
    const form = buildNewPlaneFormState();
    form.modelPath = "/models/qwen";
    form.factorySkillIds = ["skill-alpha"];
    const validation = validatePlaneV2Form(form);
    expect(validation.warnings).toContain(
      "Selected Skills Factory records are ignored until Skills is enabled.",
    );
  });
});

describe("plane dashboard skills summary", () => {
  it("formats disabled skills summary", () => {
    expect(
      formatPlaneDashboardSkillsSummary({
        enabled: false,
        enabled_count: 3,
        total_count: 5,
      }),
    ).toEqual({
      value: 0,
      meta: "disabled",
    });
  });

  it("formats enabled skills summary with counts", () => {
    expect(
      formatPlaneDashboardSkillsSummary({
        enabled: true,
        enabled_count: 2,
        total_count: 5,
      }),
    ).toEqual({
      value: 2,
      meta: "enabled / 5 total",
    });
  });
});
