#pragma once

#include <cstdint>
#include <vector>

#include "telemetry/telemetry_clock.h"
#include "telemetry/telemetry_frame_normalizer.h"
#include "telemetry/telemetry_frame_ring_buffer.h"
#include "telemetry/telemetry_health_builder.h"
#include "telemetry/telemetry_live_store_types.h"
#include "telemetry/telemetry_open_metrics_exporter.h"
#include "telemetry/telemetry_operational_config.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_snapshot_builder.h"
#include "telemetry/telemetry_state_types.h"
#include "telemetry/telemetry_stream_metrics_service.h"

namespace naim::controller {

class TelemetryLiveStoreState final {
 public:
  TelemetryLiveStoreState();

  std::vector<TelemetryNodeBuffer> nodes;
  std::uint64_t latest_sequence = 0;
  std::uint64_t dropped_frames_total = 0;
  TelemetryRetentionConfig retention;
  TelemetryAlertThresholds thresholds;
  TelemetryPersistenceState persistence;
  TelemetryStreamMetrics streams;
  TelemetryClock clock;
  TelemetryFrameNormalizer frame_normalizer;
  TelemetryOperationalConfig operational_config;
  TelemetryFrameRingBuffer ring_buffer;
  TelemetryPersistenceRepository persistence_repository;
  TelemetryStreamMetricsService stream_metrics_service;
  TelemetryHealthBuilder health_builder;
  TelemetrySnapshotBuilder snapshot_builder;
  TelemetryOpenMetricsExporter open_metrics_exporter;
};

}  // namespace naim::controller
