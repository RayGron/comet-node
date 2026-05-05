import { describe, expect, it } from "vitest";

import {
  applyTelemetrySnapshotMetadata,
  createTelemetryStore,
  enrichTelemetryFrames,
  reduceTelemetryStore,
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
});
