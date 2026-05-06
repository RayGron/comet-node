import { describe, expect, it } from "vitest";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";

import {
  buildDesiredStateV2FromForm,
  buildHostNodeOptions,
  buildNewPlaneFormState,
  buildNewPlaneFormStateWithNodes,
  buildPlaneFormStateFromDesiredStateV2,
  chooseDefaultPlaneNode,
  updatePlaneDialogForm,
  validatePlaneV2Form,
} from "./planeV2Form.jsx";
import { PlaneEditorDialog } from "./App.jsx";
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
  it("derives served model and server names from plane name by default", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "Maglev Web";

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.model.served_model_name).toBe("maglev-web");
    expect(desiredState.network.server_name).toBe("maglev-web.local");
  });

  it("does not emit a post-deploy hook unless one is configured", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "no-hook-plane";

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.hooks).toBeUndefined();
  });

  it("round-trips factory skill ids through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "skills-plane";
    form.modelPath = "/models/qwen";
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

  it("clears stale plane dialog errors when the form changes", () => {
    let dialog = {
      form: buildNewPlaneFormState(),
      text: "",
      error: "Model Library selection is required when model source type is library.",
    };
    const setDialog = (updater) => {
      dialog = typeof updater === "function" ? updater(dialog) : updater;
    };

    updatePlaneDialogForm(setDialog, (current) => ({
      ...current,
      planeName: "updated-plane",
    }));

    expect(dialog.error).toBe("");
    expect(dialog.text).toContain('"plane_name": "updated-plane"');
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

  it("round-trips browsing capability through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "browsing-plane";
    form.modelPath = "/models/qwen";
    form.browsingEnabled = true;
    form.browserSessionEnabled = true;
    form.renderedBrowserEnabled = true;
    form.browsingLoginEnabled = false;

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.webgateway).toEqual({
      enabled: true,
      policy: {
        browser_session_enabled: true,
        rendered_browser_enabled: true,
        login_enabled: false,
        max_search_results: 8,
        max_fetch_bytes: 262144,
      },
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.browsingEnabled).toBe(true);
    expect(reparsed.browserSessionEnabled).toBe(true);
    expect(reparsed.renderedBrowserEnabled).toBe(true);
  });

  it("loads legacy browsing desired state into WebGateway form fields", () => {
    const reparsed = buildPlaneFormStateFromDesiredStateV2({
      version: 2,
      plane_name: "legacy-browsing-plane",
      plane_mode: "llm",
      model: { source: { type: "library", path: "/models/qwen" } },
      browsing: {
        enabled: true,
        policy: {
          browser_session_enabled: true,
          rendered_browser_enabled: false,
          login_enabled: true,
          max_search_results: 4,
          max_fetch_bytes: 65536,
        },
      },
    });
    expect(reparsed.browsingEnabled).toBe(true);
    expect(reparsed.browserSessionEnabled).toBe(true);
    expect(reparsed.renderedBrowserEnabled).toBe(false);
    expect(reparsed.browsingLoginEnabled).toBe(true);
    expect(reparsed.browsingMaxSearchResults).toBe(4);
    expect(reparsed.browsingMaxFetchBytes).toBe(65536);
  });

  it("round-trips knowledge base selection through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "knowledge-plane";
    form.modelPath = "/models/qwen";
    form.knowledgeEnabled = true;
    form.selectedKnowledgeIds = ["knowledge.alpha", "knowledge.beta"];

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.knowledge).toEqual({
      enabled: true,
      service_id: "kv_default",
      selection_mode: "latest",
      selected_knowledge_ids: ["knowledge.alpha", "knowledge.beta"],
      context_policy: {
        include_graph: true,
        max_graph_depth: 1,
        token_budget: 12000,
      },
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.knowledgeEnabled).toBe(true);
    expect(reparsed.selectedKnowledgeIds).toEqual(["knowledge.alpha", "knowledge.beta"]);
  });

  it("round-trips turboquant capability through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "turboquant-plane";
    form.modelPath = "/models/qwen";
    form.turboquantEnabled = true;
    form.turboquantCacheTypeK = "turbo4";
    form.turboquantCacheTypeV = "turbo4";

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.features).toEqual({
      turboquant: {
        enabled: true,
        cache_type_k: "turbo4",
        cache_type_v: "turbo4",
      },
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.turboquantEnabled).toBe(true);
    expect(reparsed.turboquantCacheTypeK).toBe("turbo4");
    expect(reparsed.turboquantCacheTypeV).toBe("turbo4");
  });

  it("round-trips context compression capability through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "compression-plane";
    form.modelPath = "/models/qwen";
    form.contextCompressionEnabled = true;

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.features).toEqual({
      context_compression: {
        enabled: true,
        mode: "auto",
        target: "dialog_and_knowledge",
        memory_priority: "balanced",
      },
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.contextCompressionEnabled).toBe(true);
    expect(reparsed.contextCompressionMode).toBe("auto");
    expect(reparsed.contextCompressionTarget).toBe("dialog_and_knowledge");
    expect(reparsed.contextCompressionMemoryPriority).toBe("balanced");
  });

  it("round-trips voice listener capability through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "voice-plane";
    form.modelPath = "/models/qwen";
    form.voiceListenerEnabled = true;
    form.voiceListenerWakePhrase = "Hey Jex";
    form.voiceListenerLanguage = "auto";
    form.voiceListenerModelPath = "/models/whisper/ggml-large-v3-turbo-q5_0.bin";
    form.voiceListenerModelRef = "ggml-large-v3-turbo-q5_0.bin";
    form.voiceListenerModelNodeName = "storage1";
    form.voiceListenerModelPaths = ["/models/whisper/ggml-large-v3-turbo-q5_0.bin"];
    form.voiceListenerModelMountPath = "/models/whisper/ggml-large-v3-turbo-q5_0.bin";

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.features.voice_listener).toEqual({
      enabled: true,
      wake_phrase: "Hey Jex",
      language: "auto",
      model: {
        name: "ggml-large-v3-turbo-q5_0.bin",
        source: {
          type: "library",
          path: "/models/whisper/ggml-large-v3-turbo-q5_0.bin",
          node: "storage1",
          paths: ["/models/whisper/ggml-large-v3-turbo-q5_0.bin"],
        },
        mount_path: "/models/whisper/ggml-large-v3-turbo-q5_0.bin",
        env: "WHISPER_MODEL_PATH",
        required: true,
      },
      env: {
        HOST: "0.0.0.0",
        PORT: "18140",
        VOICE_ASR_LANGUAGE: "auto",
        VOICE_ASR_THREADS: "8",
        VOICE_LISTENER_WAKE_PHRASE: "Hey Jex",
      },
      storage: {
        mount_path: "/naim/private",
        size_gb: 1,
      },
    });
    expect(desiredState.resources.voice_module).toEqual({
      gpu_enabled: true,
      gpu_fraction: 0.2,
      memory_cap_mb: 4096,
      share_mode: "shared",
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.voiceListenerEnabled).toBe(true);
    expect(reparsed.voiceListenerWakePhrase).toBe("Hey Jex");
    expect(reparsed.voiceListenerModelPath).toBe("/models/whisper/ggml-large-v3-turbo-q5_0.bin");
  });

  it("preserves interaction runtime image through desired state v2 round-trip", () => {
    const desiredState = {
      ...buildDesiredStateV2FromForm(buildNewPlaneFormState()),
      plane_name: "interaction-image-plane",
      model: {
        source: { type: "library", ref: "Qwen/Qwen3.6-35B-A3B" },
        materialization: { mode: "prepare_on_worker" },
        served_model_name: "interaction-image-plane",
      },
      interaction: {
        image: "chainzano.com/naim/interaction-runtime@sha256:feedface",
        system_prompt: "Preserve interaction image",
        thinking_enabled: false,
        default_response_language: "ru",
        supported_response_languages: ["en", "de", "uk", "ru"],
        follow_user_language: true,
      },
    };

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    const rebuilt = buildDesiredStateV2FromForm(reparsed);
    expect(rebuilt.interaction.image).toBe(
      "chainzano.com/naim/interaction-runtime@sha256:feedface",
    );
  });

  it("round-trips multiple feature toggles through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "combined-feature-plane";
    form.modelPath = "/models/qwen";
    form.contextCompressionEnabled = true;
    form.turboquantEnabled = true;

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.features).toEqual({
      context_compression: {
        enabled: true,
        mode: "auto",
        target: "dialog_and_knowledge",
        memory_priority: "balanced",
      },
      turboquant: {
        enabled: true,
        cache_type_k: "turbo4",
        cache_type_v: "turbo4",
      },
    });
  });

  it("round-trips multiple app containers through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "multi-app-plane";
    form.modelPath = "/models/qwen";
    form.appEnabled = true;
    form.appImage = "example/app:dev";
    form.appStartType = "script";
    form.appStartValue = "node server.js";
    form.extraApps = [
      {
        name: "market-ingest",
        enabled: true,
        image: "example/app:dev",
        startType: "script",
        startValue: "node market-collector.js",
        hostPort: "",
        containerPort: "",
        node: "hpc1",
        envText: "CYPHER_MARKET_COLLECTOR_ENABLED=true",
        volumeEnabled: true,
        volumeName: "market-ingest-data",
        volumeSizeGb: "5",
        volumeMountPath: "/naim/private",
      },
    ];

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.app).toBeUndefined();
    expect(desiredState.apps).toHaveLength(2);
    expect(desiredState.apps[0]).toMatchObject({
      primary: true,
      enabled: true,
      image: "example/app:dev",
    });
    expect(desiredState.apps[1]).toMatchObject({
      name: "market-ingest",
      primary: false,
      enabled: true,
      image: "example/app:dev",
      node: "hpc1",
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.appEnabled).toBe(true);
    expect(reparsed.extraApps).toHaveLength(1);
    expect(reparsed.extraApps[0].name).toBe("market-ingest");
    expect(reparsed.extraApps[0].startValue).toBe("node market-collector.js");
    expect(reparsed.extraApps[0].node).toBe("hpc1");
  });

  it("does not serialize turboquant for compute planes", () => {
    const form = buildNewPlaneFormState();
    form.planeMode = "compute";
    form.turboquantEnabled = true;

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.features).toBeUndefined();
  });

  it("does not serialize context compression for compute planes", () => {
    const form = buildNewPlaneFormState();
    form.planeMode = "compute";
    form.contextCompressionEnabled = true;

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.features).toBeUndefined();
  });

  it("round-trips placement execution node and external app host through desired state v2", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "placement-ui-plane";
    form.modelPath = "/models/qwen";
    form.executionNode = "worker-node-a";
    form.appEnabled = true;
    form.appImage = "example/app:dev";
    form.appHostEnabled = true;
    form.appHostAddress = "10.0.0.15";
    form.appHostAuthMode = "ssh-key";
    form.appHostSshKeyPath = "/home/test/.ssh/id_ed25519";

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.placement).toEqual({
      execution_node: "worker-node-a",
      app_host: {
        address: "10.0.0.15",
        ssh_key_path: "/home/test/.ssh/id_ed25519",
      },
    });

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.executionNode).toBe("worker-node-a");
    expect(reparsed.appHostEnabled).toBe(true);
    expect(reparsed.appHostAddress).toBe("10.0.0.15");
    expect(reparsed.appHostAuthMode).toBe("ssh-key");
    expect(reparsed.appHostSshKeyPath).toBe("/home/test/.ssh/id_ed25519");
  });

  it("loads legacy placement primary_node as execution node", () => {
    const form = buildPlaneFormStateFromDesiredStateV2({
      version: 2,
      plane_name: "legacy-placement-plane",
      plane_mode: "compute",
      placement: {
        primary_node: "worker-node-a",
      },
      runtime: {
        engine: "custom",
        workers: 1,
      },
    });

    expect(form.executionNode).toBe("worker-node-a");
  });

  it("prefers the connected local node for single-machine plane defaults", () => {
    const hosts = [
      {
        node_name: "storage1",
        state: "connected",
        roles: ["storage"],
      },
      {
        node_name: "hostd-node",
        state: "connected",
        roles: ["storage", "worker"],
        gpu_count: 1,
      },
    ];

    expect(buildHostNodeOptions(hosts).map((host) => host.node_name)).toEqual([
      "hostd-node",
      "storage1",
    ]);
    expect(chooseDefaultPlaneNode(hosts, "execution")).toBe("hostd-node");
    expect(chooseDefaultPlaneNode(hosts, "storage")).toBe("hostd-node");
  });

  it("keeps single-machine defaults on the connected local node even with stale remote storage", () => {
    const hosts = [
      {
        node_name: "storage1",
        state: "connected",
        roles: ["storage"],
      },
      {
        node_name: "local-hostd",
        state: "connected",
        roles: ["worker"],
        gpu_count: 1,
      },
    ];

    expect(chooseDefaultPlaneNode(hosts, "execution")).toBe("local-hostd");
    expect(chooseDefaultPlaneNode(hosts, "storage")).toBe("local-hostd");
  });

  it("uses role-specific defaults for distributed production nodes", () => {
    const hosts = [
      {
        node_name: "hpc1",
        session_state: "connected",
        derived_role: "worker",
        capacity_summary: {
          gpu_count: 4,
          storage_root: "/var/lib/naim/storage",
        },
      },
      {
        node_name: "storage1",
        session_state: "connected",
        derived_role: "storage",
        capacity_summary: {
          gpu_count: 0,
          storage_root: "/mnt/array/naim/storage",
        },
      },
    ];

    expect(chooseDefaultPlaneNode(hosts, "execution")).toBe("hpc1");
    expect(chooseDefaultPlaneNode(hosts, "storage")).toBe("storage1");

    const form = buildNewPlaneFormStateWithNodes(hosts);
    expect(form.executionNode).toBe("hpc1");
    expect(form.materializationSourceNodeName).toBe("storage1");
  });

  it("does not emit legacy node-placement fields when topology is disabled", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "placement-clean-plane";
    form.modelPath = "/models/qwen";
    form.executionNode = "worker-node-a";
    form.inferOverridesEnabled = true;
    form.inferNode = "legacy-infer-node";
    form.workerNode = "legacy-worker-node";
    form.workerAssignmentsEnabled = true;
    form.workerAssignments = [{ node: "legacy-worker-node", gpuDevice: "0" }];
    form.appEnabled = true;
    form.appImage = "example/app:dev";
    form.appNode = "legacy-app-node";
    form.topologyEnabled = false;

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.placement).toEqual({
      execution_node: "worker-node-a",
    });
    expect(desiredState.infer.node).toBeUndefined();
    expect(desiredState.worker.node).toBeUndefined();
    expect(desiredState.worker.assignments).toBeUndefined();
    expect(desiredState.app.node).toBeUndefined();
  });

  it("serializes and reparses worker-prepared library model selection", () => {
    const form = buildNewPlaneFormState();
    form.planeName = "ux-plane-smoke";
    form.modelRef = "Qwen/Qwen3.5-9B-Q8_0";
    form.materializationMode = "prepare_on_worker";
    form.materializationLocalPath =
      "/mnt/array/naim/storage/gguf/Qwen/Qwen3.5-9B-Q8_0/Qwen3.5-9B-Q8_0.gguf";
    form.materializationSourceNodeName = "storage1";
    form.materializationSourcePaths = [
      "/mnt/array/naim/storage/gguf/Qwen/Qwen3.5-9B-Q8_0/Qwen3.5-9B-Q8_0.gguf",
    ];
    form.materializationSourceFormat = "gguf";
    form.materializationSourceQuantization = "Q8_0";
    form.modelQuantization = "Q8_0";
    form.modelTargetFilename = "Qwen3.5-9B-Q8_0.gguf";
    form.servedModelName = "ux-plane-smoke-qwen";
    form.servedModelNameManual = true;
    form.executionNode = "hpc1";
    form.workerGpuDevice = "1";
    form.gatewayPort = 18284;
    form.inferencePort = 18294;

    const validation = validatePlaneV2Form(form);
    expect(validation.errors).toEqual([]);

    const desiredState = buildDesiredStateV2FromForm(form);
    expect(desiredState.model).toMatchObject({
      source: {
        type: "library",
        ref: "Qwen/Qwen3.5-9B-Q8_0",
      },
      served_model_name: "ux-plane-smoke-qwen",
      materialization: {
        mode: "prepare_on_worker",
        source_node_name: "storage1",
        source_format: "gguf",
        source_quantization: "Q8_0",
        quantization: "Q8_0",
      },
      target_filename: "Qwen3.5-9B-Q8_0.gguf",
    });
    expect(desiredState.network.gateway_port).toBe(18284);
    expect(desiredState.network.inference_port).toBe(18294);

    const reparsed = buildPlaneFormStateFromDesiredStateV2(desiredState);
    expect(reparsed.modelRef).toBe("Qwen/Qwen3.5-9B-Q8_0");
    expect(reparsed.materializationSourceNodeName).toBe("storage1");
    expect(reparsed.materializationSourcePaths).toEqual(form.materializationSourcePaths);
    expect(reparsed.modelQuantization).toBe("Q8_0");
    expect(reparsed.workerGpuDevice).toBe("1");
  });

  it("validates required execution-node fields for external app host", () => {
    const form = buildNewPlaneFormState();
    form.modelPath = "/models/qwen";
    form.executionNode = "";
    form.appHostEnabled = true;

    const validation = validatePlaneV2Form(form);
    expect(validation.errors).toContain("Execution node is required.");
    expect(validation.errors).toContain("External app host requires the app container to be enabled.");
    expect(validation.errors).toContain("External app host address is required.");
    expect(validation.errors).toContain("External app host SSH key path is required.");
  });

  it("warns when browser sessions are enabled without browsing", () => {
    const form = buildNewPlaneFormState();
    form.modelPath = "/models/qwen";
    form.browserSessionEnabled = true;
    const validation = validatePlaneV2Form(form);
    expect(validation.warnings).toContain(
      "Browser sessions are ignored until Isolated Browsing is enabled.",
    );
  });

  it("requires worker image and start for compute planes", () => {
    const form = buildNewPlaneFormState();
    form.planeMode = "compute";
    const validation = validatePlaneV2Form(form);
    expect(validation.errors).toContain("Worker image is required for compute planes.");
    expect(validation.errors).toContain("Worker start is required for compute planes.");
  });
});

describe("PlaneEditorDialog", () => {
  it("renders with explicit skillsFactoryGroups prop", () => {
    const html = renderToStaticMarkup(
      React.createElement(PlaneEditorDialog, {
        dialog: {
          open: true,
          mode: "new",
          planeName: "",
          planeState: "",
          text: "{}",
          form: buildNewPlaneFormState(),
          originalSkillsEnabled: false,
          busy: false,
          error: "",
        },
        setDialog: () => {},
        onClose: () => {},
        onSave: () => {},
        modelLibraryItems: [],
        skillsFactoryItems: [],
        skillsFactoryGroups: [],
      }),
    );

    expect(html).toContain("New plane");
    expect(html).toContain("Isolated Browsing");
    expect(html).toContain("Context Compression");
    expect(html).toContain("TurboQuant");
    expect(html).toContain("Generated JSON");
    expect(html).not.toContain("Runtime engine");
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
