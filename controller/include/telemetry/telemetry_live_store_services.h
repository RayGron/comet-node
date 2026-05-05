#pragma once

#include "telemetry/telemetry_clock.h"
#include "telemetry/telemetry_frame_normalizer.h"
#include "telemetry/telemetry_frame_ring_buffer.h"
#include "telemetry/telemetry_health_builder.h"
#include "telemetry/telemetry_open_metrics_exporter.h"
#include "telemetry/telemetry_operational_config.h"
#include "telemetry/telemetry_persistence_repository.h"
#include "telemetry/telemetry_snapshot_builder.h"
#include "telemetry/telemetry_stream_metrics_service.h"

namespace naim::controller {

class TelemetryLiveStoreServices final {
 public:
  TelemetryLiveStoreServices();

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
