import { describe, expect, it } from "vitest";

import {
  applyTelemetrySnapshotMetadata,
  createTelemetryStore,
  enrichTelemetryFrames,
  reduceTelemetryStore,
  scopeTelemetryFrameToPlane,
  updateTelemetryBrowserLatency,
} from "./telemetryStore.js";

describe("telemetryStore", () => {
  it("enriches frames with browser-side latency fields", () => {
    const frames = enrichTelemetryFrames(
      [{ node_name: "node-a", plane_name: "plane-a", sequence: 1000 }],
      1250,
      1.5,
    );

    expect(frames).toHaveLength(1);
    expect(frames[0].telemetry_browser_received_at_ms).toBe(1250);
    expect(frames[0].telemetry_browser_receive_delay_ms).toBe(250);
    expect(frames[0].telemetry_browser_parse_ms).toBe(1.5);
  });

  it("keeps browser latency state through telemetry reductions", () => {
    const current = updateTelemetryBrowserLatency(createTelemetryStore(), {
      receivedAtMs: 1250,
      receiveDelayMs: 250,
      parseMs: 1,
      reduceMs: 2,
      applyMs: 3,
      acceptedFrames: 1,
    });
    const result = reduceTelemetryStore(current, [
      { node_name: "node-a", plane_name: "plane-a", sequence: 1000 },
    ]);

    expect(result.changed).toBe(true);
    expect(result.store.browserLatency.receiveDelayMs).toBe(250);
    expect(result.store.browserLatency.applyMs).toBe(3);
  });

  it("does not treat historical dropped frame counters as active overload", () => {
    const result = reduceTelemetryStore(createTelemetryStore(), [
      {
        node_name: "node-a",
        plane_name: "plane-a",
        sequence: 1000,
        controller_dropped_frames_total: 25,
        telemetry_dropped_frames: 4,
      },
    ]);

    expect(result.changed).toBe(true);
    expect(result.store.droppedFramesTotal).toBe(25);
    expect(result.store.overloaded).toBe(false);
  });

  it("does not treat legacy frame overload flags as authoritative", () => {
    const result = reduceTelemetryStore(createTelemetryStore(), [
      {
        node_name: "node-a",
        plane_name: "plane-a",
        sequence: 1000,
        telemetry_overloaded: true,
      },
    ]);

    expect(result.changed).toBe(true);
    expect(result.store.overloaded).toBe(false);
  });

  it("lets snapshot metadata clear recovered overload state", () => {
    const overloaded = {
      ...createTelemetryStore(),
      overloaded: true,
      droppedFramesTotal: 10,
    };
    const recovered = applyTelemetrySnapshotMetadata(overloaded, {
      telemetry_overloaded: false,
      dropped_frames_total: 12,
    });

    expect(recovered.droppedFramesTotal).toBe(12);
    expect(recovered.overloaded).toBe(false);
  });

  it("scopes aggregate node frames to a selected plane", () => {
    const frame = {
      node_name: "hpc1",
      sequence: 1000,
      instance_runtime: [
        { instance_name: "worker-maglev-service", plane_name: "maglev-service", ready: true },
        { instance_name: "worker-lt-cypher-ai", plane_name: "lt-cypher-ai", ready: false },
      ],
      gpu: {
        devices: [
          {
            gpu_device: "0",
            processes: [
              { instance_name: "worker-maglev-service", used_vram_mb: 100 },
              { instance_name: "worker-lt-cypher-ai", used_vram_mb: 200 },
            ],
          },
        ],
      },
    };

    const scoped = scopeTelemetryFrameToPlane(frame, "maglev-service");
    expect(scoped.plane_name).toBe("maglev-service");
    expect(scoped.instance_runtime).toHaveLength(1);
    expect(scoped.plane_ready_instance_count).toBe(1);
    expect(scoped.gpu.devices[0].processes).toHaveLength(1);

    const result = reduceTelemetryStore(createTelemetryStore(), [frame], "maglev-service");
    expect(result.changed).toBe(true);
    expect(result.store.byNode.hpc1.plane_name).toBe("maglev-service");
    expect(result.store.byNode.hpc1.instance_runtime[0].instance_name).toBe(
      "worker-maglev-service",
    );
  });
});
