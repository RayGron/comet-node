import { expect, test } from "@playwright/test";

const LONG_SOURCE_PATH =
  "/mnt/array/naim/storage/gguf/Qwen/Qwen3.6-35B-A3B/Qwen3.6-35B-A3B-Q8_0.gguf";

const modelLibraryItem = {
  name: "Qwen3.6-35B-A3B Q8_0",
  model_id: "Qwen/Qwen3.6-35B-A3B",
  node_name: "storage1",
  path: LONG_SOURCE_PATH,
  paths: [LONG_SOURCE_PATH],
  format: "gguf",
  quantization: "Q8_0",
  size_bytes: 36_903_140_320,
  status: "completed",
};

const hostdHosts = [
  {
    node_name: "hpc1",
    session_state: "connected",
    derived_role: "worker",
    role_eligible: true,
    capacity_summary: {
      gpu_count: 4,
      storage_free_bytes: 250_000_000_000,
      storage_root: "/naim/storage",
      storage_total_bytes: 1_388_000_000_000,
      total_memory_bytes: 269_960_683_520,
    },
    lan_peers: [
      {
        peer_node_name: "storage1",
        peer_endpoint: "http://192.168.88.252:29999",
        tcp_reachable: true,
        seen_udp: true,
        rtt_ms: 1,
      },
    ],
  },
  {
    node_name: "storage1",
    session_state: "connected",
    derived_role: "storage",
    role_eligible: true,
    capacity_summary: {
      gpu_count: 0,
      storage_free_bytes: 17_000_000_000_000,
      storage_root: "/naim/storage",
      storage_total_bytes: 18_927_043_346_432,
      total_memory_bytes: 64_891_473_920,
    },
    lan_peers: [
      {
        peer_node_name: "hpc1",
        peer_endpoint: "http://192.168.88.13:29999",
        tcp_reachable: true,
        seen_udp: true,
        rtt_ms: 1,
      },
    ],
  },
];

const skillsFactoryPayload = {
  groups: [],
  skills: [
    { id: "lt-jex-localtrade-auth-session", name: "LocalTrade auth session", group_path: "lt-jex/localtrade/auth", enabled: true },
    { id: "lt-jex-localtrade-account-balances", name: "LocalTrade account balances", group_path: "lt-jex/localtrade/account", enabled: true },
    { id: "lt-jex-localtrade-market-data", name: "LocalTrade market data", group_path: "lt-jex/localtrade/market-data", enabled: true },
    { id: "lt-jex-localtrade-market-exchange", name: "LocalTrade market exchange", group_path: "lt-jex/localtrade/spot", enabled: true },
    { id: "lt-jex-localtrade-copy-trading-discovery", name: "LocalTrade copy trading discovery", group_path: "lt-jex/localtrade/copy-trading", enabled: true },
    { id: "lt-jex-localtrade-copy-trading-actions", name: "LocalTrade copy trading actions", group_path: "lt-jex/localtrade/copy-trading", enabled: true },
    { id: "lt-jex-localtrade-spot-order-clarification", name: "LocalTrade spot order clarification", group_path: "lt-jex/localtrade/spot", enabled: true },
    { id: "lt-jex-localtrade-user-streams", name: "LocalTrade user streams", group_path: "lt-jex/localtrade/streams", enabled: true },
    { id: "lt-jex-market-overview-report", name: "Market overview report", group_path: "lt-jex/market", enabled: true },
    { id: "lt-jex-market-asset-report", name: "Asset market report", group_path: "lt-jex/market", enabled: true },
    { id: "lt-jex-market-forecast", name: "Asset market forecast", group_path: "lt-jex/market", enabled: true },
    { id: "lt-jex-market-source-mix", name: "Asset source mix", group_path: "lt-jex/market", enabled: true },
    {
      id: "lt-jex-localtrade-account-balances-with-extra-long-regression-suffix",
      name: "LocalTrade account balances",
      group_path: "localtrade/account",
      enabled: true,
      match_terms: ["balance", "portfolio"],
    },
    {
      id: "lt-jex-market-overview-report-with-extra-long-regression-suffix",
      name: "Market overview report",
      group_path: "localtrade/market",
      enabled: true,
      match_terms: ["market", "overview"],
    },
  ],
};

async function mockApi(page) {
  await page.route("**/api/v1/**", async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname;
    const json = (payload) =>
      route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify(payload),
      });

    if (path === "/api/v1/events/stream" || path === "/api/v1/live/stream") {
      return route.fulfill({
        status: 200,
        contentType: "text/event-stream",
        body: "",
      });
    }
    if (path === "/api/v1/auth/state") {
      return json({
        authenticated: true,
        setup_required: false,
        user: { username: "operator", role: "admin" },
      });
    }
    if (path === "/api/v1/auth/ssh-keys" || path === "/api/v1/auth/invites") {
      return json({ items: [] });
    }
    if (path === "/api/v1/planes") {
      return json({ items: [] });
    }
    if (path === "/api/v1/dashboard") {
      return json({
        plane: { total: 0 },
        runtime: { ready_nodes: 0 },
        rollout: { total_actions: 0 },
        alerts: { total: 0 },
        peer_links: {
          items: [
            {
              observer_node_name: "hpc1",
              peer_node_name: "storage1",
              state: "direct",
              same_lan: true,
              peer_endpoint: "http://192.168.88.252:29999",
            },
            {
              observer_node_name: "storage1",
              peer_node_name: "hpc1",
              state: "direct",
              same_lan: true,
              peer_endpoint: "http://192.168.88.13:29999",
            },
          ],
          summary: { total: 2, direct: 2, partial: 0, stale: 0 },
        },
      });
    }
    if (path === "/api/v1/host-observations") {
      return json({
        observations: [
          {
            node_name: "hpc1",
            status: "idle",
            heartbeat_at: "2026-05-06 07:20:00",
            runtime_status: { available: true, runtime: { runtime_phase: "running" } },
            cpu_telemetry: {
              summary: {
                total_memory_bytes: 269_960_683_520,
                used_memory_bytes: 80_000_000_000,
              },
            },
            gpu_telemetry: {
              summary: {
                device_count: 4,
                total_vram_mb: 391_548,
                used_vram_mb: 78_218,
              },
            },
          },
          {
            node_name: "storage1",
            status: "idle",
            heartbeat_at: "2026-05-06 07:20:00",
            runtime_status: { available: false, runtime: null },
            cpu_telemetry: {
              summary: {
                total_memory_bytes: 64_891_473_920,
                used_memory_bytes: 4_900_000_000,
              },
            },
            gpu_telemetry: { summary: { device_count: 0 } },
          },
        ],
      });
    }
    if (path === "/api/v1/hostd/hosts") {
      return json({ items: hostdHosts });
    }
    if (path === "/api/v1/hostd/peer-links") {
      return json({
        items: [
          {
            observer_node_name: "hpc1",
            peer_node_name: "storage1",
            state: "direct",
            same_lan: true,
            peer_endpoint: "http://192.168.88.252:29999",
          },
          {
            observer_node_name: "storage1",
            peer_node_name: "hpc1",
            state: "direct",
            same_lan: true,
            peer_endpoint: "http://192.168.88.13:29999",
          },
        ],
      });
    }
    if (path === "/api/v1/model-library" || path === "/api/v1/model-library/jobs") {
      return json({
        items: [modelLibraryItem],
        roots: [],
        nodes: ["storage1"],
        jobs: [],
      });
    }
    if (path === "/api/v1/skills-factory") {
      return json(skillsFactoryPayload);
    }
    if (
      path === "/api/v1/telemetry/snapshot" ||
      path === "/api/v1/knowledge-vault/status" ||
      path === "/api/v1/protocols"
    ) {
      return json({ items: [], nodes: [] });
    }
    return json({});
  });
}

async function openNewPlaneDialog(page, viewport) {
  await page.setViewportSize(viewport);
  await mockApi(page);
  await page.goto("/?page=dashboard");
  const newPlaneButton = page.getByRole("button", { name: "New plane" }).first();
  await expect(newPlaneButton).toBeVisible();
  await newPlaneButton.click();
  await expect(page.getByRole("dialog", { name: "New plane" })).toBeVisible();
}

async function expectNoHorizontalOverflow(page) {
  const overflow = await page.evaluate(() => {
    const doc = document.documentElement;
    const modal = document.querySelector(".plane-editor-modal");
    return {
      documentOverflow: doc.scrollWidth - window.innerWidth,
      modalOverflow: modal ? modal.scrollWidth - modal.clientWidth : 0,
    };
  });
  expect(overflow.documentOverflow).toBeLessThanOrEqual(1);
  expect(overflow.modalOverflow).toBeLessThanOrEqual(1);
}

async function expectNoCollapsedPlaneFields(page) {
  const collapsed = await page.evaluate(() => {
    const selectors = [
      ".plane-form-grid > .field-label",
      ".plane-features-grid > .plane-feature-toggle",
      ".model-library-picker-row > strong",
      ".model-library-picker-row > span:not(.tag)",
    ];
    return selectors.flatMap((selector) =>
      [...document.querySelectorAll(selector)]
        .filter((element) => {
          const rect = element.getBoundingClientRect();
          return rect.width > 0 && rect.width < 96;
        })
        .map((element) => ({
          selector,
          text: element.textContent.trim().slice(0, 80),
          width: element.getBoundingClientRect().width,
        })),
    );
  });
  expect(collapsed).toEqual([]);
}

const viewports = [
  { width: 1440, height: 900 },
  { width: 1280, height: 800 },
  { width: 1024, height: 768 },
  { width: 768, height: 900 },
  { width: 390, height: 844 },
];

test("dashboard renders host roles, capacity, LAN peers, and node overview", async ({ page }) => {
  const pageErrors = [];
  page.on("pageerror", (error) => pageErrors.push(error.message));
  await page.setViewportSize({ width: 1440, height: 1000 });
  await mockApi(page);
  await page.goto("/?page=dashboard");

  await expect(page.getByRole("heading", { name: "LAN peer links" })).toBeVisible();
  await expect(page.getByText("2 direct / 0 partial / 0 stale")).toBeVisible();
  await expect(page.getByRole("heading", { name: "Naim nodes" })).toBeVisible();

  const hpcCard = page.locator(".dashboard-hosts-panel .node-card").filter({ hasText: "hpc1" }).first();
  await expect(hpcCard.locator(".role-badges .tag", { hasText: "worker" })).toBeVisible();
  await expect(hpcCard.getByText("GPU count")).toBeVisible();
  await expect(hpcCard.getByText("LAN peers")).toBeVisible();
  await expect(hpcCard.getByText("1/1 direct")).toBeVisible();

  const storageCard = page.locator(".dashboard-hosts-panel .node-card").filter({ hasText: "storage1" }).first();
  await expect(storageCard.locator(".role-badges .tag", { hasText: "storage" })).toBeVisible();
  await expect(storageCard).toContainText(/1[5-7] TB free/);

  await hpcCard.getByRole("button", { name: /open node overview/i }).click();
  const overviewDialog = page.getByRole("dialog", { name: /node overview hpc1/i });
  await expect(overviewDialog).toBeVisible();
  await expect(overviewDialog.getByText("Runtime", { exact: true })).toBeVisible();
  await expect(overviewDialog.getByText("running", { exact: true })).toBeVisible();
  expect(pageErrors).toEqual([]);
});

for (const viewport of viewports) {
  test(`new plane editor remains readable at ${viewport.width}x${viewport.height}`, async ({
    page,
  }) => {
    await openNewPlaneDialog(page, viewport);
    await expectNoHorizontalOverflow(page);
    await expectNoCollapsedPlaneFields(page);

    await page.getByRole("button", { name: "Apply lt-cypher-ai preset" }).click();
    await expect
      .poll(() =>
        page.evaluate(() => [...document.querySelectorAll("input")].map((input) => input.value)),
      )
      .toContain("qwen3.6-35b-a3b-jex");

    await page.getByRole("button", { name: "Run preset preflight" }).click();
    await expect(page.getByText("Qwen3.6-35B-A3B Q8_0 / gguf / Q8_0")).toBeVisible();
    await expectNoHorizontalOverflow(page);
    await expectNoCollapsedPlaneFields(page);

    await page.getByText("Generated JSON", { exact: true }).click();
    await expect(page.locator("#plane-editor-json")).toBeVisible();
    await expect(page.locator("#plane-editor-json")).toContainText(
      '"ref": "Qwen/Qwen3.6-35B-A3B"',
    );
    await expect(page.locator("#plane-editor-json")).toContainText(LONG_SOURCE_PATH);
    await expect(page.locator("#plane-editor-json")).toContainText('"CYPHER_PUBLIC_BASE_PATH": "/"');
    for (const skillId of [
      "lt-jex-localtrade-auth-session",
      "lt-jex-localtrade-account-balances",
      "lt-jex-localtrade-market-data",
      "lt-jex-localtrade-market-exchange",
      "lt-jex-localtrade-copy-trading-discovery",
      "lt-jex-localtrade-copy-trading-actions",
      "lt-jex-localtrade-spot-order-clarification",
      "lt-jex-localtrade-user-streams",
      "lt-jex-market-overview-report",
      "lt-jex-market-asset-report",
      "lt-jex-market-forecast",
      "lt-jex-market-source-mix",
    ]) {
      await expect(page.locator("#plane-editor-json")).toContainText(skillId);
    }
    await expect(page.getByRole("button", { name: "Create plane" })).toBeVisible();
    await expect(page.getByRole("button", { name: "Cancel" })).toBeVisible();
  });
}
