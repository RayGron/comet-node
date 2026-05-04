import { describe, expect, it } from "vitest";

import {
  buildTelemetryBrowserLatency,
  maxTelemetryReceiveDelayMs,
  parseTelemetryEventPayload,
  telemetryFramesFromSnapshot,
} from "./telemetryStreamClient.js";

describe("telemetryStreamClient", () => {
  it("parses telemetry event payloads and reports parse timing", () => {
    const result = parseTelemetryEventPayload({
      data: JSON.stringify({ node_name: "node-a", sequence: 10 }),
    });

    expect(result.payload.node_name).toBe("node-a");
    expect(result.parseMs).toBeGreaterThanOrEqual(0);
  });

  it("combines snapshot history and nodes in stream order", () => {
    expect(
      telemetryFramesFromSnapshot({
        history: [{ sequence: 1 }],
        nodes: [{ sequence: 2 }],
      }).map((frame) => frame.sequence),
    ).toEqual([1, 2]);
  });

  it("builds browser latency samples from enriched frames", () => {
    const latency = buildTelemetryBrowserLatency({
      frames: [
        { telemetry_browser_receive_delay_ms: 12 },
        { telemetry_browser_receive_delay_ms: 18 },
      ],
      receivedAtMs: 100,
      parseMs: 1,
      reduceMs: 2,
      applyStartedAt: performance.now(),
      acceptedFrames: 2,
    });

    expect(maxTelemetryReceiveDelayMs([])).toBe(0);
    expect(latency.receiveDelayMs).toBe(18);
    expect(latency.acceptedFrames).toBe(2);
  });
});
