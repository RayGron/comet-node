export function createTelemetryStore() {
  return {
    byNode: {},
    latestSequence: 0,
    droppedFramesTotal: 0,
    overloaded: false,
    schemaVersion: "telemetry.store.v2",
    lastReplayRequired: false,
    lastReplayReason: "",
    browserLatency: {
      receivedAtMs: 0,
      receiveDelayMs: 0,
      parseMs: 0,
      reduceMs: 0,
      applyMs: 0,
      acceptedFrames: 0,
    },
  };
}

function shouldAcceptFrame(previous, frame) {
  const previousSequence = Number(previous?.sequence || 0);
  const nextSequence = Number(frame?.sequence || 0);
  if (!frame?.node_name || nextSequence <= 0) {
    return false;
  }
  return previousSequence <= 0 || nextSequence > previousSequence;
}

export function reduceTelemetryStore(store, frames, planeName = "") {
  const current = store || createTelemetryStore();
  let changed = false;
  const acceptedFrames = [];
  const nextByNode = { ...(current.byNode || {}) };

  for (const frame of frames || []) {
    if (!frame?.node_name) {
      continue;
    }
    if (planeName && frame?.plane_name && frame.plane_name !== planeName) {
      continue;
    }
    const previous = nextByNode[frame.node_name];
    if (!shouldAcceptFrame(previous, frame)) {
      continue;
    }
    nextByNode[frame.node_name] = frame;
    acceptedFrames.push(frame);
    changed = true;
  }

  if (!changed) {
    return { store: current, acceptedFrames: [], changed: false };
  }

  const latestSequence = Math.max(
    Number(current.latestSequence || 0),
    ...acceptedFrames.map((frame) => Number(frame?.sequence || 0)),
  );
  const droppedFramesTotal = Math.max(
    Number(current.droppedFramesTotal || 0),
    ...acceptedFrames.map((frame) =>
      Number(frame?.controller_dropped_frames_total ?? frame?.telemetry_dropped_frames ?? 0),
    ),
  );
  const overloadFrames = acceptedFrames.filter(
    (frame) => typeof frame?.telemetry_overloaded === "boolean",
  );
  const overloaded = overloadFrames.length > 0
    ? overloadFrames.some((frame) => frame.telemetry_overloaded === true)
    : Boolean(current.overloaded);
  const replayRequired =
    Boolean(current.lastReplayRequired) ||
    acceptedFrames.some((frame) => frame?.replay?.required === true);

  return {
    store: {
      byNode: nextByNode,
      latestSequence,
      droppedFramesTotal,
      overloaded,
      schemaVersion: "telemetry.store.v2",
      lastReplayRequired: replayRequired,
      lastReplayReason: current.lastReplayReason || "",
      browserLatency: current.browserLatency || createTelemetryStore().browserLatency,
    },
    acceptedFrames,
    changed: true,
  };
}

export function enrichTelemetryFrames(frames, receivedAtMs = Date.now(), parseMs = 0) {
  return (frames || [])
    .filter((frame) => frame?.node_name)
    .map((frame) => {
      const sequence = Number(frame?.sequence || 0);
      return {
        ...frame,
        telemetry_browser_received_at_ms: receivedAtMs,
        telemetry_browser_receive_delay_ms:
          sequence > 0 && receivedAtMs >= sequence ? receivedAtMs - sequence : 0,
        telemetry_browser_parse_ms: Number(parseMs || 0),
      };
    });
}

export function updateTelemetryBrowserLatency(store, latency) {
  const current = store || createTelemetryStore();
  return {
    ...current,
    browserLatency: {
      ...(current.browserLatency || createTelemetryStore().browserLatency),
      ...latency,
    },
  };
}

export function applyTelemetrySnapshotMetadata(store, payload) {
  const current = store || createTelemetryStore();
  const replay = payload?.replay || {};
  return {
    ...current,
    latestSequence: Math.max(
      Number(current.latestSequence || 0),
      Number(payload?.latest_sequence || replay?.latest_sequence || 0),
    ),
    droppedFramesTotal: Math.max(
      Number(current.droppedFramesTotal || 0),
      Number(payload?.dropped_frames_total || replay?.dropped_frames_total || 0),
    ),
    overloaded:
      typeof payload?.telemetry_overloaded === "boolean"
        ? payload.telemetry_overloaded
        : Boolean(current.overloaded),
    lastReplayRequired: replay?.required === true,
    lastReplayReason: replay?.reason || current.lastReplayReason || "",
    browserLatency: current.browserLatency || createTelemetryStore().browserLatency,
  };
}

export function selectTelemetryFrames(store) {
  return Object.values(store?.byNode || {}).sort(
    (left, right) => Number(left?.sequence || 0) - Number(right?.sequence || 0),
  );
}
