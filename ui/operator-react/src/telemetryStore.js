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

function runtimeInstanceMatchesPlane(status, planeName) {
  if (!planeName) {
    return true;
  }
  if (status?.plane_name === planeName) {
    return true;
  }
  const instanceName = status?.instance_name || "";
  return [
    "infer-",
    "worker-",
    "skills-",
    "app-",
    "webgateway-",
    "browsing-",
    "voice-module-",
    "interaction-",
  ].some((prefix) => {
    const stem = `${prefix}${planeName}`;
    return instanceName === stem || instanceName.startsWith(`${stem}-`);
  });
}

function gpuProcessMatchesInstances(process, instanceNames) {
  return !process?.instance_name ||
    process.instance_name === "unknown" ||
    instanceNames.has(process.instance_name);
}

export function scopeTelemetryFrameToPlane(frame, planeName = "") {
  if (!planeName || !frame?.node_name) {
    return frame;
  }
  if (frame?.plane_name && frame.plane_name !== planeName) {
    return null;
  }
  const scopedRuntime = (Array.isArray(frame?.instance_runtime) ? frame.instance_runtime : [])
    .filter((status) => runtimeInstanceMatchesPlane(status, planeName))
    .map((status) => ({ ...status, plane_name: status?.plane_name || planeName }));
  if (!frame?.plane_name && scopedRuntime.length === 0) {
    return null;
  }
  const instanceNames = new Set(scopedRuntime.map((status) => status?.instance_name).filter(Boolean));
  const scopedGpu = frame?.gpu
    ? {
        ...frame.gpu,
        devices: (Array.isArray(frame.gpu.devices) ? frame.gpu.devices : []).map((device) => ({
          ...device,
          processes: (Array.isArray(device?.processes) ? device.processes : []).filter((process) =>
            gpuProcessMatchesInstances(process, instanceNames),
          ),
        })),
      }
    : frame?.gpu;
  const scopedDisk = frame?.disk
    ? {
        ...frame.disk,
        items: (Array.isArray(frame.disk.items) ? frame.disk.items : []).filter(
          (item) => !item?.plane_name || item.plane_name === planeName,
        ),
      }
    : frame?.disk;
  const readyCount = scopedRuntime.filter((status) => status?.ready === true).length;
  return {
    ...frame,
    plane_name: planeName,
    plane_id: planeName,
    instance_runtime: scopedRuntime,
    gpu: scopedGpu,
    disk: scopedDisk,
    plane_instance_count: scopedRuntime.length,
    plane_ready_instance_count: readyCount,
    plane_not_ready_instance_count: Math.max(0, scopedRuntime.length - readyCount),
    plane_runtime_health:
      scopedRuntime.length === 0
        ? "no-runtime"
        : readyCount === scopedRuntime.length
          ? "ready"
          : "changing",
  };
}

export function reduceTelemetryStore(store, frames, planeName = "") {
  const current = store || createTelemetryStore();
  let changed = false;
  const acceptedFrames = [];
  const nextByNode = { ...(current.byNode || {}) };

  for (const rawFrame of frames || []) {
    const frame = scopeTelemetryFrameToPlane(rawFrame, planeName);
    if (!frame?.node_name) {
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
  const overloaded = Boolean(current.overloaded);
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
