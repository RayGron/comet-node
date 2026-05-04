import { describe, expect, it } from "vitest";

import {
  hostObservationFromTelemetryFrame,
  hostObservationItemsFromPayload,
  mergeTelemetryIntoObservationPayload,
} from "./telemetryHostObservations.js";

describe("telemetryHostObservations", () => {
  it("maps telemetry frames into host observation payloads", () => {
    const observation = hostObservationFromTelemetryFrame({
      node_name: "node-a",
      plane_name: "plane-a",
      sequence: 42,
      sampled_at: "2026-05-04T10:00:00Z",
      telemetry_health: { status: "ok", last_frame_age_ms: 12 },
      gpu: { source: "nvidia-smi", devices: [{ total_vram_mb: 10, used_vram_mb: 4 }] },
      instance_runtime: [{ instance_name: "plane-a-worker-1" }],
    });

    expect(observation.node_name).toBe("node-a");
    expect(observation.telemetry_sequence).toBe(42);
    expect(observation.telemetry_health_status).toBe("ok");
    expect(observation.gpu_telemetry.summary.used_vram_mb).toBe(4);
    expect(observation.instance_runtimes.available).toBe(true);
  });

  it("merges only newer telemetry frames into observations", () => {
    const current = {
      observations: [
        {
          node_name: "node-a",
          telemetry_sequence: 10,
          observed_state: { instances: [{ name: "existing" }] },
        },
      ],
    };

    const stale = mergeTelemetryIntoObservationPayload(current, [
      { node_name: "node-a", sequence: 9 },
    ]);
    expect(stale).toBe(current);

    const merged = mergeTelemetryIntoObservationPayload(current, [
      { node_name: "node-a", sequence: 11, sampled_at: "now" },
    ]);
    expect(hostObservationItemsFromPayload(merged)[0].telemetry_sequence).toBe(11);
    expect(hostObservationItemsFromPayload(merged)[0].observed_state.instances[0].name).toBe(
      "existing",
    );
  });
});
