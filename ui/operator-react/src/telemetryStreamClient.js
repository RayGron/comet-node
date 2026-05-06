export function parseTelemetryEventPayload(event, fallbackPayload = {}) {
  const parseStartedAt = performance.now();
  const payload = JSON.parse(event?.data || JSON.stringify(fallbackPayload));
  return {
    payload,
    parseMs: performance.now() - parseStartedAt,
  };
}

export function telemetryFramesFromSnapshot(payload) {
  return [...(payload?.history || []), ...(payload?.nodes || [])];
}

export function maxTelemetryReceiveDelayMs(frames) {
  return Math.max(
    0,
    ...(frames || []).map((frame) => Number(frame?.telemetry_browser_receive_delay_ms || 0)),
  );
}

export function latestTelemetryFramesByNode(frames) {
  const byNode = new Map();
  for (const frame of frames || []) {
    if (!frame?.node_name) {
      continue;
    }
    const previous = byNode.get(frame.node_name);
    if (Number(frame?.sequence || 0) >= Number(previous?.sequence || 0)) {
      byNode.set(frame.node_name, frame);
    }
  }
  return [...byNode.values()];
}

export function buildTelemetryBrowserLatency({
  frames,
  receivedAtMs,
  parseMs,
  reduceMs,
  applyStartedAt,
  acceptedFrames,
}) {
  const latestFrames = latestTelemetryFramesByNode(frames);
  return {
    receivedAtMs,
    receiveDelayMs: maxTelemetryReceiveDelayMs(latestFrames),
    parseMs,
    reduceMs,
    applyMs: performance.now() - applyStartedAt,
    acceptedFrames,
  };
}
