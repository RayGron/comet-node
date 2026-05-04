export function createTelemetryStore() {
  return {
    byNode: {},
    latestSequence: 0,
    droppedFramesTotal: 0,
    overloaded: false,
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
  const overloaded =
    Boolean(current.overloaded) ||
    acceptedFrames.some((frame) => frame?.telemetry_overloaded === true) ||
    droppedFramesTotal > 0;

  return {
    store: {
      byNode: nextByNode,
      latestSequence,
      droppedFramesTotal,
      overloaded,
    },
    acceptedFrames,
    changed: true,
  };
}

export function selectTelemetryFrames(store) {
  return Object.values(store?.byNode || {}).sort(
    (left, right) => Number(left?.sequence || 0) - Number(right?.sequence || 0),
  );
}
