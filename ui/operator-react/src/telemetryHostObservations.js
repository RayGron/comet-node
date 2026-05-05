export function hostObservationItemsFromPayload(payload) {
  if (Array.isArray(payload?.observations)) {
    return payload.observations.map((item) => ({
      ...item,
      observed_at: item?.observed_at || item?.heartbeat_at || null,
    }));
  }
  if (Array.isArray(payload?.items)) {
    return payload.items.map((item) => ({
      ...item,
      observed_at: item?.observed_at || item?.heartbeat_at || null,
    }));
  }
  return [];
}

export function summarizeGpuTelemetryFrame(gpu) {
  const devices = Array.isArray(gpu?.devices) ? gpu.devices : [];
  return {
    available: devices.length > 0,
    degraded: gpu?.degraded === true,
    source: gpu?.source || "",
    collected_at: gpu?.collected_at || "",
    summary: {
      device_count: devices.length,
      total_vram_mb: devices.reduce((sum, item) => sum + Number(item?.total_vram_mb || 0), 0),
      used_vram_mb: devices.reduce((sum, item) => sum + Number(item?.used_vram_mb || 0), 0),
      free_vram_mb: devices.reduce((sum, item) => sum + Number(item?.free_vram_mb || 0), 0),
      temperature_device_count: devices.filter((item) => item?.temperature_available).length,
      hottest_temperature_c: devices.reduce(
        (max, item) => Math.max(max, Number(item?.temperature_c || 0)),
        0,
      ),
    },
    devices,
  };
}

export function summarizeNetworkTelemetryFrame(network) {
  const interfaces = Array.isArray(network?.interfaces) ? network.interfaces : [];
  return {
    available: interfaces.length > 0,
    degraded: network?.degraded === true,
    source: network?.source || "",
    collected_at: network?.collected_at || "",
    summary: {
      interface_count: interfaces.length,
      rx_bytes: interfaces.reduce((sum, item) => sum + Number(item?.rx_bytes || 0), 0),
      tx_bytes: interfaces.reduce((sum, item) => sum + Number(item?.tx_bytes || 0), 0),
    },
    interfaces,
    peer_discovery: Array.isArray(network?.peer_discovery) ? network.peer_discovery : [],
  };
}

export function summarizeDiskTelemetryFrame(disk) {
  const items = Array.isArray(disk?.items) ? disk.items : [];
  return {
    available: items.length > 0,
    degraded: disk?.degraded === true,
    source: disk?.source || "",
    collected_at: disk?.collected_at || "",
    summary: {
      disk_count: items.length,
      total_bytes: items.reduce((sum, item) => sum + Number(item?.total_bytes || 0), 0),
      used_bytes: items.reduce((sum, item) => sum + Number(item?.used_bytes || 0), 0),
      free_bytes: items.reduce((sum, item) => sum + Number(item?.free_bytes || 0), 0),
      read_bytes: items.reduce((sum, item) => sum + Number(item?.read_bytes || 0), 0),
      write_bytes: items.reduce((sum, item) => sum + Number(item?.write_bytes || 0), 0),
    },
    items,
  };
}

export function hostObservationFromTelemetryFrame(frame) {
  if (!frame?.node_name) {
    return null;
  }
  const cpuSnapshot = frame?.cpu || {};
  const instanceRuntime = Array.isArray(frame?.instance_runtime) ? frame.instance_runtime : [];
  const stale = frame?.stale === true;
  return {
    node_name: frame.node_name,
    plane_name: frame.plane_name || "",
    status: stale ? "stale" : "healthy",
    observed_at: frame.sampled_at || null,
    heartbeat_at: frame.sampled_at || null,
    telemetry_sequence: frame.sequence || 0,
    telemetry_schema_version: frame.schema_version || "host.telemetry.v1",
    telemetry_source: frame.source || "hostd",
    telemetry_node_id: frame.node_id || frame.node_name,
    telemetry_plane_id: frame.plane_id || frame.plane_name || "",
    telemetry_channel: frame.channel || "host.telemetry.v1",
    telemetry_lane: frame.lane || "fast",
    telemetry_monotonic_ms: frame.monotonic_ms || 0,
    telemetry_monotonic_timestamp_ms: frame.monotonic_timestamp_ms || frame.monotonic_ms || 0,
    telemetry_expires_at: frame.expires_at || "",
    telemetry_expires_in_ms: frame.expires_in_ms ?? null,
    telemetry_stale: stale,
    telemetry_health_status:
      frame?.telemetry_health?.status || frame?.telemetry_health_status || (stale ? "stale" : "ok"),
    telemetry_last_frame_age_ms: Number(
      frame?.telemetry_health?.last_frame_age_ms ?? frame?.last_frame_age_ms ?? 0,
    ),
    telemetry_degraded: Boolean(frame?.degraded_reason),
    telemetry_degraded_reason: frame?.degraded_reason || "",
    telemetry_collector_duration_ms: Number(frame?.collector_duration_ms || 0),
    telemetry_publish_duration_ms: Number(frame?.publish_duration_ms || 0),
    telemetry_queue_delay_ms: Number(frame?.publisher_queue_delay_ms || 0),
    telemetry_bus_depth: Number(frame?.telemetry_bus_depth || 0),
    telemetry_dropped_frames: Number(
      frame?.controller_dropped_frames_total ?? frame?.telemetry_dropped_frames ?? 0,
    ),
    telemetry_publish_errors: Number(frame?.publish_error_count || 0),
    telemetry_adaptive_interval_ms: Number(frame?.adaptive_interval_ms || frame?.interval_ms || 0),
    telemetry_adaptive_reason: frame?.adaptive_reason || "",
    telemetry_controller_ingest_delay_ms: Number(frame?.controller_ingest_delay_ms || 0),
    telemetry_latency_total_ms: Number(frame?.latency_breakdown?.total_observed_ms || 0),
    telemetry_browser_receive_delay_ms: Number(frame?.telemetry_browser_receive_delay_ms || 0),
    telemetry_browser_parse_ms: Number(frame?.telemetry_browser_parse_ms || 0),
    telemetry_last_publish_error: frame?.last_publish_error || "",
    telemetry_plane_instance_count: Number(frame?.plane_instance_count || 0),
    telemetry_plane_ready_instance_count: Number(frame?.plane_ready_instance_count || 0),
    telemetry_plane_not_ready_instance_count: Number(frame?.plane_not_ready_instance_count || 0),
    telemetry_plane_runtime_health: frame?.plane_runtime_health || "unknown",
    runtime_status: { available: instanceRuntime.length > 0 },
    instance_runtimes: {
      available: instanceRuntime.length > 0,
      items: instanceRuntime,
    },
    cpu_telemetry: {
      available: Boolean(cpuSnapshot?.source),
      degraded: cpuSnapshot?.degraded === true,
      source: cpuSnapshot?.source || "",
      collected_at: cpuSnapshot?.collected_at || "",
      summary: cpuSnapshot,
    },
    gpu_telemetry: summarizeGpuTelemetryFrame(frame?.gpu),
    network_telemetry: summarizeNetworkTelemetryFrame(frame?.network),
    disk_telemetry: summarizeDiskTelemetryFrame(frame?.disk),
  };
}

export function mergeTelemetryIntoObservationPayload(currentPayload, frames, planeName = "") {
  const currentItems = hostObservationItemsFromPayload(currentPayload);
  const byNode = new Map(
    currentItems.filter((item) => item?.node_name).map((item) => [item.node_name, item]),
  );
  let changed = false;
  for (const frame of frames || []) {
    if (planeName && frame?.plane_name && frame.plane_name !== planeName) {
      continue;
    }
    const telemetryItem = hostObservationFromTelemetryFrame(frame);
    if (!telemetryItem?.node_name) {
      continue;
    }
    const previous = byNode.get(telemetryItem.node_name) || {};
    const previousSequence = Number(previous.telemetry_sequence || 0);
    const nextSequence = Number(telemetryItem.telemetry_sequence || 0);
    if (previousSequence > 0 && nextSequence > 0 && nextSequence <= previousSequence) {
      continue;
    }
    const nextItem = {
      ...previous,
      ...telemetryItem,
      observed_state: previous.observed_state,
      applied_generation: previous.applied_generation,
      disk_telemetry: telemetryItem.disk_telemetry?.available
        ? telemetryItem.disk_telemetry
        : previous.disk_telemetry,
    };
    byNode.set(telemetryItem.node_name, nextItem);
    changed = true;
  }
  if (!changed && currentPayload) {
    return currentPayload;
  }
  return {
    ...(currentPayload || {}),
    observations: [...byNode.values()],
  };
}
