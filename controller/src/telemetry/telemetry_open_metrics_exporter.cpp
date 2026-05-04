#include "telemetry/telemetry_open_metrics_exporter.h"

#include <cstdint>
#include <sstream>

namespace naim::controller {

std::string TelemetryOpenMetricsExporter::Build(
    const nlohmann::json& health,
    const std::optional<std::string>&) const {
  std::ostringstream out;
  const auto status = health.value("status", std::string{"ok"});
  const int status_value = status == "critical" ? 2 : status == "degraded" ? 1 : 0;
  out << "# TYPE naim_telemetry_health_status gauge\n";
  out << "naim_telemetry_health_status " << status_value << "\n";
  out << "# TYPE naim_telemetry_latest_sequence gauge\n";
  out << "naim_telemetry_latest_sequence "
      << health.value("latest_sequence", std::uint64_t{0}) << "\n";
  out << "# TYPE naim_telemetry_last_frame_age_ms gauge\n";
  out << "naim_telemetry_last_frame_age_ms "
      << health.value("last_frame_age_ms", std::uint64_t{0}) << "\n";
  out << "# TYPE naim_telemetry_dropped_frames_total counter\n";
  out << "naim_telemetry_dropped_frames_total "
      << health.value("dropped_frames_total", std::uint64_t{0}) << "\n";
  const auto persistence = health.value("persistence", nlohmann::json::object());
  out << "# TYPE naim_telemetry_persistence_enabled gauge\n";
  out << "naim_telemetry_persistence_enabled "
      << (persistence.value("enabled", false) ? 1 : 0) << "\n";
  out << "# TYPE naim_telemetry_persistence_errors_total counter\n";
  out << "naim_telemetry_persistence_errors_total "
      << persistence.value("error_count", std::uint64_t{0}) << "\n";
  out << "# TYPE naim_telemetry_persistence_persisted_frames_total counter\n";
  out << "naim_telemetry_persistence_persisted_frames_total "
      << persistence.value("persisted_frames_total", std::uint64_t{0}) << "\n";
  out << "# TYPE naim_telemetry_stream_active_clients gauge\n";
  out << "# TYPE naim_telemetry_stream_replay_required_total counter\n";
  out << "# TYPE naim_telemetry_stream_send_failures_total counter\n";
  const auto streams = health.value("streams", nlohmann::json::object());
  for (const auto& name : {std::string{"telemetry"}, std::string{"live"}}) {
    const auto stream = streams.value(name, nlohmann::json::object());
    out << "naim_telemetry_stream_active_clients{stream=\""
        << SanitizeMetricLabel(name) << "\"} "
        << stream.value("active_clients", std::uint64_t{0}) << "\n";
    out << "naim_telemetry_stream_replay_required_total{stream=\""
        << SanitizeMetricLabel(name) << "\"} "
        << stream.value("replay_required_total", std::uint64_t{0}) << "\n";
    out << "naim_telemetry_stream_send_failures_total{stream=\""
        << SanitizeMetricLabel(name) << "\"} "
        << stream.value("send_failure_total", std::uint64_t{0}) << "\n";
  }
  out << "# TYPE naim_telemetry_plane_nodes gauge\n";
  out << "# TYPE naim_telemetry_plane_stale_nodes gauge\n";
  out << "# TYPE naim_telemetry_plane_max_ingest_ms gauge\n";
  for (const auto& plane : health.value("planes", nlohmann::json::array())) {
    const std::string plane_name_label =
        SanitizeMetricLabel(plane.value("plane_name", std::string{"unassigned"}));
    out << "naim_telemetry_plane_nodes{plane=\"" << plane_name_label << "\"} "
        << plane.value("node_count", std::uint64_t{0}) << "\n";
    out << "naim_telemetry_plane_stale_nodes{plane=\"" << plane_name_label << "\"} "
        << plane.value("stale_nodes", std::uint64_t{0}) << "\n";
    const auto latency = plane.value("latency", nlohmann::json::object());
    out << "naim_telemetry_plane_max_ingest_ms{plane=\"" << plane_name_label << "\"} "
        << latency.value("max_controller_ingest_ms", std::uint64_t{0}) << "\n";
  }
  return out.str();
}

std::string TelemetryOpenMetricsExporter::SanitizeMetricLabel(std::string value) const {
  for (char& ch : value) {
    const bool ok =
        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
    if (!ok) {
      ch = '_';
    }
  }
  return value;
}

}  // namespace naim::controller
