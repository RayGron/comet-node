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

export function buildTelemetryBrowserLatency({
  frames,
  receivedAtMs,
  parseMs,
  reduceMs,
  applyStartedAt,
  acceptedFrames,
}) {
  return {
    receivedAtMs,
    receiveDelayMs: maxTelemetryReceiveDelayMs(frames),
    parseMs,
    reduceMs,
    applyMs: performance.now() - applyStartedAt,
    acceptedFrames,
  };
}
