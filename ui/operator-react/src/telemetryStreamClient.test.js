import { describe, expect, it } from "vitest";

import {
  buildTelemetryBrowserLatency,
  latestTelemetryFramesByNode,
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
        { node_name: "node-a", sequence: 1, telemetry_browser_receive_delay_ms: 12 },
        { node_name: "node-b", sequence: 2, telemetry_browser_receive_delay_ms: 18 },
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

  it("measures browser latency from the latest frame per node", () => {
    const frames = [
      { node_name: "node-a", sequence: 100, telemetry_browser_receive_delay_ms: 900 },
      { node_name: "node-a", sequence: 200, telemetry_browser_receive_delay_ms: 20 },
      { node_name: "node-b", sequence: 150, telemetry_browser_receive_delay_ms: 40 },
    ];
    const latency = buildTelemetryBrowserLatency({
      frames,
      receivedAtMs: 250,
      parseMs: 0,
      reduceMs: 0,
      applyStartedAt: performance.now(),
      acceptedFrames: frames.length,
    });

    expect(latestTelemetryFramesByNode(frames).map((frame) => frame.sequence)).toEqual([200, 150]);
    expect(latency.receiveDelayMs).toBe(40);
  });
});
