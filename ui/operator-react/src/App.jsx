import { useEffect, useRef, useState, startTransition } from "react";

const REFRESH_DEBOUNCE_MS = 350;
const EVENT_LIMIT = 24;

function fetchJson(path, init = {}) {
  return fetch(path, {
    ...init,
    headers: {
      Accept: "application/json",
      ...(init.headers || {}),
    },
  }).then(async (response) => {
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      const message =
        payload?.error?.message ||
        payload?.message ||
        payload?.status ||
        response.statusText;
      throw new Error(message);
    }
    return payload;
  });
}

function planePath(planeName, suffix = "") {
  const encoded = encodeURIComponent(planeName);
  return suffix
    ? `/api/v1/planes/${encoded}/${suffix}`
    : `/api/v1/planes/${encoded}`;
}

function queryPath(path, query) {
  const params = new URLSearchParams();
  for (const [key, value] of Object.entries(query)) {
    if (value !== undefined && value !== null && value !== "") {
      params.set(key, String(value));
    }
  }
  const rendered = params.toString();
  return rendered ? `${path}?${rendered}` : path;
}

function formatTimestamp(value) {
  if (!value) {
    return "n/a";
  }
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) {
    return value;
  }
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "short",
    timeStyle: "medium",
  }).format(date);
}

function yesNo(value) {
  if (value === null || value === undefined) {
    return "n/a";
  }
  return value ? "yes" : "no";
}

function compactBytes(value) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "n/a";
  }
  const units = ["B", "KB", "MB", "GB", "TB"];
  let amount = value;
  let unitIndex = 0;
  while (amount >= 1024 && unitIndex < units.length - 1) {
    amount /= 1024;
    unitIndex += 1;
  }
  return `${amount.toFixed(amount >= 10 || unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

function eventSeverityClass(severity) {
  if (severity === "error") {
    return "is-critical";
  }
  if (severity === "warning") {
    return "is-warning";
  }
  return "is-healthy";
}

function alertSeverityClass(severity) {
  if (severity === "critical") {
    return "is-critical";
  }
  if (severity === "warning") {
    return "is-warning";
  }
  if (severity === "booting") {
    return "is-booting";
  }
  return "is-healthy";
}

function runtimeIndicatorClass(value, fallbackHealth) {
  if (value === true) {
    return "is-healthy";
  }
  if (value === false) {
    return "is-warning";
  }
  if (fallbackHealth === "stale" || fallbackHealth === "failed") {
    return "is-critical";
  }
  return "is-booting";
}

function planeStateClass(state) {
  if (state === "failed" || state === "degraded") {
    return "is-critical";
  }
  if (state === "stopping" || state === "starting" || state === "stopped") {
    return "is-warning";
  }
  return "is-healthy";
}

function statusDot(className) {
  return <span className={`status-dot ${className}`} aria-hidden="true" />;
}

function EmptyState({ title, detail }) {
  return (
    <div className="empty-state">
      <div className="empty-state-title">{title}</div>
      {detail ? <div className="empty-state-detail">{detail}</div> : null}
    </div>
  );
}

function SummaryCard({ label, value, meta }) {
  return (
    <article className="summary-card">
      <div className="summary-label">{label}</div>
      <div className="summary-value">{value}</div>
      <div className="summary-meta">{meta}</div>
    </article>
  );
}

function App() {
  const initialPlane = new URLSearchParams(window.location.search).get("plane") || "";
  const [planes, setPlanes] = useState([]);
  const [selectedPlane, setSelectedPlane] = useState(initialPlane);
  const [planeDetail, setPlaneDetail] = useState(null);
  const [dashboard, setDashboard] = useState(null);
  const [hostObservations, setHostObservations] = useState(null);
  const [diskState, setDiskState] = useState(null);
  const [rolloutState, setRolloutState] = useState(null);
  const [rebalancePlan, setRebalancePlan] = useState(null);
  const [events, setEvents] = useState([]);
  const [loading, setLoading] = useState(true);
  const [actionBusy, setActionBusy] = useState("");
  const [bundlePath, setBundlePath] = useState("");
  const [bundleBusy, setBundleBusy] = useState("");
  const [bundleOutput, setBundleOutput] = useState("");
  const [apiError, setApiError] = useState("");
  const [apiHealthy, setApiHealthy] = useState(false);
  const [streamHealthy, setStreamHealthy] = useState(false);
  const [lastRefreshAt, setLastRefreshAt] = useState("");
  const [lastEventName, setLastEventName] = useState("none");

  const refreshTimerRef = useRef(null);
  const eventSourceRef = useRef(null);

  async function loadPlanes(preferredPlane = selectedPlane) {
    const payload = await fetchJson("/api/v1/planes");
    const items = Array.isArray(payload.items) ? payload.items : [];
    setPlanes(items);
    const planeExists =
      preferredPlane && items.some((item) => item.name === preferredPlane);
    const nextPlane = planeExists
      ? preferredPlane
      : items.length > 0
        ? items[0].name
        : "";
    if (nextPlane !== selectedPlane) {
      startTransition(() => {
        setSelectedPlane(nextPlane);
      });
    }
    return nextPlane;
  }

  async function refreshAll(planeOverride) {
    setLoading(true);
    setApiError("");
    try {
      const planeName = await loadPlanes(planeOverride ?? selectedPlane);
      if (!planeName) {
        setPlaneDetail(null);
        setDashboard(null);
        setHostObservations(null);
        setDiskState(null);
        setRolloutState(null);
        setRebalancePlan(null);
        setEvents([]);
        setApiHealthy(true);
        setLastRefreshAt(new Date().toISOString());
        return;
      }

      const [
        planePayload,
        dashboardPayload,
        hostObservationsPayload,
        diskPayload,
        rolloutPayload,
        rebalancePayload,
        eventsPayload,
      ] = await Promise.all([
        fetchJson(planePath(planeName)),
        fetchJson(planePath(planeName, "dashboard")),
        fetchJson(
          queryPath("/api/v1/host-observations", {
            plane: planeName,
            stale_after: 30,
          }),
        ),
        fetchJson(queryPath("/api/v1/disk-state", { plane: planeName })),
        fetchJson(queryPath("/api/v1/rollout-actions", { plane: planeName })),
        fetchJson(
          queryPath("/api/v1/rebalance-plan", {
            plane: planeName,
            stale_after: 30,
          }),
        ),
        fetchJson(
          queryPath("/api/v1/events", {
            plane: planeName,
            limit: EVENT_LIMIT,
          }),
        ),
      ]);

      setPlaneDetail(planePayload);
      setDashboard(dashboardPayload);
      setHostObservations(hostObservationsPayload);
      setDiskState(diskPayload);
      setRolloutState(rolloutPayload);
      setRebalancePlan(rebalancePayload);
      setEvents(Array.isArray(eventsPayload.events) ? eventsPayload.events : []);
      setApiHealthy(true);
      setLastRefreshAt(new Date().toISOString());
    } catch (error) {
      setApiHealthy(false);
      setApiError(error.message || String(error));
    } finally {
      setLoading(false);
    }
  }

  function scheduleRefresh(planeName) {
    if (refreshTimerRef.current) {
      clearTimeout(refreshTimerRef.current);
    }
    refreshTimerRef.current = setTimeout(() => {
      refreshTimerRef.current = null;
      refreshAll(planeName);
    }, REFRESH_DEBOUNCE_MS);
  }

  async function executePlaneAction(action) {
    if (!selectedPlane) {
      return;
    }
    setActionBusy(action);
    setApiError("");
    try {
      await fetchJson(planePath(selectedPlane, action), { method: "POST" });
      await refreshAll(selectedPlane);
    } catch (error) {
      setApiError(error.message || String(error));
    } finally {
      setActionBusy("");
    }
  }

  async function executeBundleAction(action) {
    if (!bundlePath.trim()) {
      setApiError("Bundle path is required.");
      return;
    }
    setBundleBusy(action);
    setApiError("");
    try {
      const payload = await fetchJson(
        queryPath(`/api/v1/bundles/${action}`, { bundle: bundlePath.trim() }),
        { method: "POST" },
      );
      setBundleOutput(payload.output || JSON.stringify(payload, null, 2));
      if (action === "apply") {
        await refreshAll(selectedPlane);
      }
    } catch (error) {
      setApiError(error.message || String(error));
    } finally {
      setBundleBusy("");
    }
  }

  useEffect(() => {
    refreshAll(initialPlane);
    return () => {
      if (refreshTimerRef.current) {
        clearTimeout(refreshTimerRef.current);
      }
    };
  }, []);

  useEffect(() => {
    if (!selectedPlane) {
      if (eventSourceRef.current) {
        eventSourceRef.current.close();
        eventSourceRef.current = null;
      }
      setStreamHealthy(false);
      return;
    }

    const source = new EventSource(
      queryPath("/api/v1/events/stream", {
        plane: selectedPlane,
        limit: EVENT_LIMIT,
      }),
    );
    eventSourceRef.current = source;
    setStreamHealthy(false);

    source.onopen = () => {
      setStreamHealthy(true);
    };
    source.onerror = () => {
      setStreamHealthy(false);
    };
    source.onmessage = (event) => {
      setLastEventName(event.type || "message");
      scheduleRefresh(selectedPlane);
    };

    return () => {
      source.close();
      if (eventSourceRef.current === source) {
        eventSourceRef.current = null;
      }
    };
  }, [selectedPlane]);

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    if (selectedPlane) {
      params.set("plane", selectedPlane);
    } else {
      params.delete("plane");
    }
    const next = params.toString()
      ? `${window.location.pathname}?${params.toString()}`
      : window.location.pathname;
    window.history.replaceState({}, "", next);
  }, [selectedPlane]);

  const desiredState = planeDetail?.desired_state || null;
  const planeRecord =
    planeDetail?.planes?.find((item) => item.name === selectedPlane) || null;
  const nodeItems = dashboard?.nodes || [];
  const observationItems = hostObservations?.observations || [];
  const assignmentItems = dashboard?.assignments?.by_node || [];
  const rolloutItems = rolloutState?.actions || [];
  const rebalanceItems = rebalancePlan?.rebalance_plan || [];
  const diskItems = diskState?.items || [];
  const instances = desiredState?.instances || [];
  const inferItems = instances.filter((item) => item.kind === "infer");
  const workerItems = instances.filter((item) => item.kind === "worker");
  const alertSummary = dashboard?.alerts || {
    critical: 0,
    warning: 0,
    booting: 0,
    total: 0,
    items: [],
  };
  const alertItems = Array.isArray(alertSummary.items) ? alertSummary.items : [];

  return (
    <div className="app-shell">
      <div className="starfield" aria-hidden="true" />
      <header className="hero">
        <div className="hero-copy">
          <div className="eyebrow">Comet Operator Interface</div>
          <h1>Constellation Control</h1>
          <p className="hero-text">
            Multi-plane control surface for lifecycle, rollout pressure, runtime
            readiness, and live controller telemetry.
          </p>
        </div>
        <div className="hero-meta">
          <div className="status-chip">
            {statusDot(apiHealthy ? "is-healthy" : apiError ? "is-critical" : "is-booting")}
            <span>{apiHealthy ? "API ready" : apiError ? "API error" : "API pending"}</span>
          </div>
          <div className="status-chip">
            {statusDot(streamHealthy ? "is-healthy" : selectedPlane ? "is-warning" : "is-booting")}
            <span>{streamHealthy ? "Stream live" : selectedPlane ? "Stream reconnecting" : "Stream idle"}</span>
          </div>
          <div className="meta-card">
            <span className="meta-label">Last refresh</span>
            <span className="meta-value">{formatTimestamp(lastRefreshAt)}</span>
          </div>
          <div className="meta-card">
            <span className="meta-label">Last event</span>
            <span className="meta-value">{lastEventName}</span>
          </div>
        </div>
      </header>

      <main className="main-grid">
        <section className="panel plane-sidebar">
          <div className="panel-header">
            <div>
              <div className="section-label">Planes</div>
              <h2>Plane registry</h2>
            </div>
            <button
              className="ghost-button"
              type="button"
              onClick={() => refreshAll(selectedPlane)}
              disabled={loading}
            >
              Refresh
            </button>
          </div>
          <div className="plane-list">
            {planes.length === 0 ? (
              <EmptyState title="No planes" detail="Import and apply a plane bundle first." />
            ) : (
              planes.map((plane) => {
                const selected = plane.name === selectedPlane;
                return (
                  <button
                    key={plane.name}
                    className={`plane-card ${selected ? "is-selected" : ""}`}
                    type="button"
                    onClick={() => {
                      setSelectedPlane(plane.name);
                      refreshAll(plane.name);
                    }}
                  >
                    <div className="plane-card-top">
                      <div className="plane-name">{plane.name}</div>
                      <div className={`pill ${planeStateClass(plane.state)}`}>
                        {statusDot(planeStateClass(plane.state))}
                        <span>{plane.state}</span>
                      </div>
                    </div>
                    <div className="plane-card-meta">
                      <span>gen {plane.generation ?? "n/a"}</span>
                      <span>{plane.instance_count ?? 0} instances</span>
                      <span>{plane.node_count ?? 0} nodes</span>
                    </div>
                  </button>
                );
              })
            )}
          </div>
          <div className="bundle-workflow">
            <div className="section-label">Bundle workflow</div>
            <label className="field-label" htmlFor="bundle-path-input">
              Bundle path
            </label>
            <input
              id="bundle-path-input"
              className="text-input"
              type="text"
              value={bundlePath}
              onChange={(event) => setBundlePath(event.target.value)}
              placeholder="/abs/path/to/plane-bundle"
              spellCheck="false"
            />
            <div className="toolbar">
              <button
                className="ghost-button"
                type="button"
                onClick={() => executeBundleAction("preview")}
                disabled={bundleBusy !== ""}
              >
                Preview bundle
              </button>
              <button
                className="ghost-button"
                type="button"
                onClick={() => executeBundleAction("apply")}
                disabled={bundleBusy !== ""}
              >
                Apply bundle
              </button>
            </div>
            {bundleOutput ? <pre className="bundle-output">{bundleOutput}</pre> : null}
          </div>
        </section>

        <section className="panel plane-overview">
          <div className="panel-header">
            <div>
              <div className="section-label">Plane detail</div>
              <h2>{selectedPlane || "No plane selected"}</h2>
            </div>
            <div className="toolbar">
              <button
                className="ghost-button"
                type="button"
                onClick={() => executePlaneAction("start")}
                disabled={!selectedPlane || actionBusy !== "" || planeRecord?.state === "running"}
              >
                Start plane
              </button>
              <button
                className="ghost-button"
                type="button"
                onClick={() => executePlaneAction("stop")}
                disabled={!selectedPlane || actionBusy !== "" || planeRecord?.state === "stopped"}
              >
                Stop plane
              </button>
            </div>
          </div>

          {apiError ? <div className="error-banner">{apiError}</div> : null}

          {!selectedPlane || !planeRecord || !dashboard ? (
            <EmptyState
              title={loading ? "Loading plane state" : "No plane selected"}
              detail="Select a plane to inspect nodes, instances, disks, rollout state, and events."
            />
          ) : (
            <>
              <div className="summary-grid">
                <SummaryCard
                  label="Plane state"
                  value={planeRecord.state}
                  meta={`generation ${dashboard.desired_generation ?? "n/a"}`}
                />
                <SummaryCard
                  label="Nodes"
                  value={dashboard.plane?.node_count ?? 0}
                  meta={`${dashboard.runtime?.ready_nodes ?? 0} ready / ${dashboard.runtime?.not_ready_nodes ?? 0} not ready`}
                />
                <SummaryCard
                  label="Instances"
                  value={dashboard.plane?.instance_count ?? 0}
                  meta={`${inferItems.length} infer / ${workerItems.length} worker`}
                />
                <SummaryCard
                  label="Rollout"
                  value={dashboard.rollout?.total_actions ?? 0}
                  meta={`${dashboard.rollout?.loop_status ?? "n/a"} / ${dashboard.rollout?.loop_reason ?? "n/a"}`}
                />
                <SummaryCard
                  label="Alerts"
                  value={alertSummary.total ?? 0}
                  meta={`${alertSummary.critical ?? 0} critical / ${alertSummary.warning ?? 0} warning / ${alertSummary.booting ?? 0} booting`}
                />
              </div>

              <div className="panel-grid">
                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Operational watch</h3>
                    <span className="subpanel-meta">Live rollout, failure, and readiness signals</span>
                  </div>
                  <div className="list-column">
                    {alertItems.length === 0 ? (
                      <EmptyState
                        title="No active alerts"
                        detail="Plane is currently stable from the controller point of view."
                      />
                    ) : (
                      alertItems.map((alert, index) => {
                        const severityClass = alertSeverityClass(alert.severity);
                        return (
                          <article
                            className={`list-card alert-card ${severityClass}`}
                            key={`${alert.kind}-${alert.node_name || "global"}-${alert.assignment_id || alert.event_id || index}`}
                          >
                            <div className="card-row">
                              <strong>{alert.title}</strong>
                              <span className={`tag ${severityClass}`}>
                                {statusDot(severityClass)}
                                <span>{alert.severity}</span>
                              </span>
                            </div>
                            <div className="list-detail">
                              <div>{alert.detail || "n/a"}</div>
                              {alert.node_name ? <div>node {alert.node_name}</div> : null}
                              {alert.worker_name ? <div>worker {alert.worker_name}</div> : null}
                              <div>{alert.kind}</div>
                            </div>
                          </article>
                        );
                      })
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Instances</h3>
                    <span className="subpanel-meta">Desired configuration</span>
                  </div>
                  <div className="instance-grid">
                    {instances.length === 0 ? (
                      <EmptyState title="No instances" />
                    ) : (
                      instances.map((instance) => (
                        <article className="instance-card" key={instance.name}>
                          <div className="card-row">
                            <strong>{instance.name}</strong>
                            <span className="tag">{instance.kind}</span>
                          </div>
                          <div className="metric-grid">
                            <div className="metric-row"><span>Node</span><strong>{instance.node_name || "auto"}</strong></div>
                            <div className="metric-row"><span>GPU</span><strong>{instance.gpu_device || "auto"}</strong></div>
                            <div className="metric-row"><span>Share</span><strong>{instance.share_mode || "n/a"}</strong></div>
                            <div className="metric-row"><span>Fraction</span><strong>{instance.gpu_fraction ?? "n/a"}</strong></div>
                            <div className="metric-row"><span>Placement</span><strong>{instance.placement_mode || "n/a"}</strong></div>
                            <div className="metric-row"><span>Memory cap</span><strong>{instance.memory_cap_mb ? `${instance.memory_cap_mb} MB` : "n/a"}</strong></div>
                          </div>
                        </article>
                      ))
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Nodes</h3>
                    <span className="subpanel-meta">Availability and runtime posture</span>
                  </div>
                  <div className="node-grid">
                    {nodeItems.length === 0 ? (
                      <EmptyState title="No nodes in current plane" />
                    ) : (
                      nodeItems.map((node) => (
                        <article className="node-card" key={node.node_name}>
                          <div className="card-row">
                            <strong>{node.node_name}</strong>
                            <div className={`pill ${runtimeIndicatorClass(node.runtime_launch_ready, node.health)}`}>
                              {statusDot(runtimeIndicatorClass(node.runtime_launch_ready, node.health))}
                              <span>{node.health || "unknown"}</span>
                            </div>
                          </div>
                          <div className="metric-grid">
                            <div className="metric-row"><span>Availability</span><strong>{node.availability || "active"}</strong></div>
                            <div className="metric-row"><span>Status</span><strong>{node.status || "n/a"}</strong></div>
                            <div className="metric-row"><span>Runtime</span><strong>{node.runtime_phase || "n/a"}</strong></div>
                            <div className="metric-row"><span>Launch ready</span><strong>{yesNo(node.runtime_launch_ready)}</strong></div>
                            <div className="metric-row"><span>GPU count</span><strong>{node.gpu_count ?? "n/a"}</strong></div>
                            <div className="metric-row"><span>Telemetry degraded</span><strong>{yesNo(node.telemetry_degraded)}</strong></div>
                          </div>
                        </article>
                      ))
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Assignments</h3>
                    <span className="subpanel-meta">Latest per node</span>
                  </div>
                  <div className="assignment-grid">
                    {assignmentItems.length === 0 ? (
                      <EmptyState title="No assignment activity" />
                    ) : (
                      assignmentItems.map((item) => (
                        <article className="assignment-card" key={item.node_name}>
                          <div className="card-row">
                            <strong>{item.node_name}</strong>
                            <span className="tag">{item.latest_status}</span>
                          </div>
                          <div className="metric-grid">
                            <div className="metric-row"><span>Assignment</span><strong>#{item.latest_assignment_id}</strong></div>
                            <div className="metric-row"><span>Pending</span><strong>{item.pending}</strong></div>
                            <div className="metric-row"><span>Claimed</span><strong>{item.claimed}</strong></div>
                            <div className="metric-row"><span>Failed</span><strong>{item.failed}</strong></div>
                          </div>
                        </article>
                      ))
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>System telemetry</h3>
                    <span className="subpanel-meta">CPU, GPU, network, and disk summaries per node</span>
                  </div>
                  <div className="list-column">
                    {observationItems.length === 0 ? (
                      <EmptyState title="No host observations" detail="Report observed state to populate telemetry." />
                    ) : (
                      observationItems.map((observation) => {
                        const cpu = observation.cpu_telemetry?.summary || {};
                        const gpu = observation.gpu_telemetry?.summary || {};
                        const network = observation.network_telemetry?.summary || {};
                        const disk = observation.disk_telemetry?.summary || {};
                        return (
                          <article className="list-card" key={`telemetry-${observation.node_name}`}>
                            <div className="card-row">
                              <strong>{observation.node_name}</strong>
                              <span className="tag">{observation.status}</span>
                            </div>
                            <div className="metric-grid">
                              <div className="metric-row"><span>CPU</span><strong>{cpu.utilization_pct !== undefined ? `${Math.round(cpu.utilization_pct)}%` : "n/a"}</strong></div>
                              <div className="metric-row"><span>Load avg</span><strong>{cpu.loadavg_1m !== undefined ? Number(cpu.loadavg_1m).toFixed(2) : "n/a"}</strong></div>
                              <div className="metric-row"><span>Memory</span><strong>{compactBytes(cpu.used_memory_bytes)} / {compactBytes(cpu.total_memory_bytes)}</strong></div>
                              <div className="metric-row"><span>GPU VRAM</span><strong>{gpu.used_vram_mb !== undefined ? `${gpu.used_vram_mb}/${gpu.total_vram_mb} MB` : "n/a"}</strong></div>
                              <div className="metric-row"><span>GPU util</span><strong>{gpu.device_count ? `${gpu.device_count} dev / ${gpu.owned_process_count ?? 0} proc` : "n/a"}</strong></div>
                              <div className="metric-row"><span>Network</span><strong>{compactBytes(network.rx_bytes)} rx / {compactBytes(network.tx_bytes)} tx</strong></div>
                              <div className="metric-row"><span>Interfaces</span><strong>{network.up_count ?? 0}/{network.interface_count ?? 0} up</strong></div>
                              <div className="metric-row"><span>Disk used</span><strong>{compactBytes(disk.used_bytes)} / {compactBytes(disk.total_bytes)}</strong></div>
                            </div>
                          </article>
                        );
                      })
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Rollout actions</h3>
                    <span className="subpanel-meta">Deferred scheduler workflow</span>
                  </div>
                  <div className="list-column">
                    {rolloutItems.length === 0 ? (
                      <EmptyState title="No rollout actions" />
                    ) : (
                      rolloutItems.map((action) => (
                        <article className="list-card" key={action.id}>
                          <div className="card-row">
                            <strong>{action.worker_name}</strong>
                            <span className="tag">{action.status}</span>
                          </div>
                          <div className="list-detail">
                            <div>step {action.step}</div>
                            <div>{action.action}</div>
                            <div>
                              {action.target_node_name || "n/a"}:{action.target_gpu_device || "n/a"}
                            </div>
                            <div>{action.reason || "n/a"}</div>
                          </div>
                        </article>
                      ))
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Rebalance plan</h3>
                    <span className="subpanel-meta">Current scheduler proposals</span>
                  </div>
                  <div className="list-column">
                    {rebalanceItems.length === 0 ? (
                      <EmptyState title="No rebalance entries" />
                    ) : (
                      rebalanceItems.map((item) => (
                        <article className="list-card" key={item.worker_name}>
                          <div className="card-row">
                            <strong>{item.worker_name}</strong>
                            <span className="tag">{item.decision}</span>
                          </div>
                          <div className="list-detail">
                            <div>{item.state}</div>
                            <div>{item.action || "n/a"}</div>
                            <div>
                              {(item.current_node_name || "n/a")}:{item.current_gpu_device || "n/a"} →{" "}
                              {(item.target_node_name || "n/a")}:{item.target_gpu_device || "n/a"}
                            </div>
                            <div>{item.gate_reason || `score ${item.score}`}</div>
                          </div>
                        </article>
                      ))
                    )}
                  </div>
                </section>

                <section className="subpanel">
                  <div className="subpanel-header">
                    <h3>Disk state</h3>
                    <span className="subpanel-meta">Desired vs realized storage</span>
                  </div>
                  <div className="list-column">
                    {diskItems.length === 0 ? (
                      <EmptyState title="No disk state" />
                    ) : (
                      diskItems.map((item) => (
                        <article className="list-card" key={`${item.disk_name}@${item.node_name}`}>
                          <div className="card-row">
                            <strong>{item.disk_name}</strong>
                            <span className="tag">{item.realized_state || "unknown"}</span>
                          </div>
                          <div className="list-detail">
                            <div>{item.node_name}</div>
                            <div>{item.kind || item.desired_state || "disk"}</div>
                            <div>
                              used {compactBytes(item.usage_bytes?.used_bytes)} / total{" "}
                              {compactBytes(item.usage_bytes?.total_bytes)}
                            </div>
                            <div>
                              io {compactBytes(item.io_bytes?.read_bytes)} read /{" "}
                              {compactBytes(item.io_bytes?.write_bytes)} write
                            </div>
                          </div>
                        </article>
                      ))
                    )}
                  </div>
                </section>

                <section className="subpanel event-panel">
                  <div className="subpanel-header">
                    <h3>Recent events</h3>
                    <span className="subpanel-meta">Plane-scoped persisted event log</span>
                  </div>
                  <div className="event-list">
                    {events.length === 0 ? (
                      <EmptyState title="No events" />
                    ) : (
                      events.map((event) => (
                        <article className={`event-card ${eventSeverityClass(event.severity)}`} key={event.id}>
                          <div className="card-row">
                            <strong>
                              {event.category}.{event.event_type}
                            </strong>
                            <span className="tag">{event.severity}</span>
                          </div>
                          <div className="event-meta">
                            <span>{formatTimestamp(event.created_at)}</span>
                            {event.node_name ? <span>{event.node_name}</span> : null}
                            {event.worker_name ? <span>{event.worker_name}</span> : null}
                          </div>
                          <p className="event-message">{event.message || "n/a"}</p>
                        </article>
                      ))
                    )}
                  </div>
                </section>
              </div>
            </>
          )}
        </section>
      </main>
    </div>
  );
}

export default App;
