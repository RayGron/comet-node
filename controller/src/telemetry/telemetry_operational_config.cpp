#include "telemetry/telemetry_operational_config.h"

#include <algorithm>

#include "telemetry/telemetry_environment_reader.h"

namespace naim::controller {

TelemetryRetentionConfig TelemetryOperationalConfig::RetentionFromEnvironment(
    TelemetryRetentionConfig retention) const {
  const TelemetryEnvironmentReader env;
  retention.hot_history_capacity =
      env.Size("NAIM_TELEMETRY_HOT_HISTORY_CAPACITY")
          .value_or(retention.hot_history_capacity);
  retention.stream_batch_limit =
      env.Size("NAIM_TELEMETRY_STREAM_BATCH_LIMIT")
          .value_or(retention.stream_batch_limit);
  retention.durable_history_capacity =
      env.Size("NAIM_TELEMETRY_DURABLE_HISTORY_CAPACITY")
          .value_or(retention.durable_history_capacity);
  retention.warm_bucket_ms =
      env.Uint64("NAIM_TELEMETRY_WARM_BUCKET_MS").value_or(retention.warm_bucket_ms);
  retention.cold_bucket_ms =
      env.Uint64("NAIM_TELEMETRY_COLD_BUCKET_MS").value_or(retention.cold_bucket_ms);
  return NormalizeRetention(retention);
}

TelemetryAlertThresholds TelemetryOperationalConfig::ThresholdsFromEnvironment(
    TelemetryAlertThresholds thresholds) const {
  const TelemetryEnvironmentReader env;
  thresholds.stale_warning_ms =
      env.Uint64("NAIM_TELEMETRY_STALE_WARNING_MS")
          .value_or(thresholds.stale_warning_ms);
  thresholds.stale_critical_ms =
      env.Uint64("NAIM_TELEMETRY_STALE_CRITICAL_MS")
          .value_or(thresholds.stale_critical_ms);
  thresholds.ingest_warning_ms =
      env.Uint64("NAIM_TELEMETRY_INGEST_WARNING_MS")
          .value_or(thresholds.ingest_warning_ms);
  thresholds.queue_warning_ms =
      env.Uint64("NAIM_TELEMETRY_QUEUE_WARNING_MS")
          .value_or(thresholds.queue_warning_ms);
  thresholds.browser_apply_warning_ms =
      env.Uint64("NAIM_TELEMETRY_BROWSER_APPLY_WARNING_MS")
          .value_or(thresholds.browser_apply_warning_ms);
  return thresholds;
}

TelemetryRetentionConfig TelemetryOperationalConfig::NormalizeRetention(
    TelemetryRetentionConfig retention) const {
  retention.hot_history_capacity =
      std::max<std::size_t>(1, retention.hot_history_capacity);
  retention.stream_batch_limit =
      std::max<std::size_t>(1, retention.stream_batch_limit);
  retention.durable_history_capacity =
      std::max<std::size_t>(1, retention.durable_history_capacity);
  retention.warm_bucket_ms = std::max<std::uint64_t>(1, retention.warm_bucket_ms);
  retention.cold_bucket_ms =
      std::max<std::uint64_t>(retention.warm_bucket_ms, retention.cold_bucket_ms);
  return retention;
}

}  // namespace naim::controller
