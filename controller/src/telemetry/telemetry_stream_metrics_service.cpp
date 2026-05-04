#include "telemetry/telemetry_stream_metrics_service.h"

namespace naim::controller {

void TelemetryStreamMetricsService::RecordOpened(
    TelemetryStreamMetrics& streams,
    const std::string& stream_name) const {
  auto& state = MutableStreamState(streams, stream_name);
  if (state.opened_total > 0) {
    ++state.reconnect_total;
  }
  ++state.opened_total;
  ++state.active_clients;
}

void TelemetryStreamMetricsService::RecordClosed(
    TelemetryStreamMetrics& streams,
    const std::string& stream_name) const {
  auto& state = MutableStreamState(streams, stream_name);
  if (state.active_clients > 0) {
    --state.active_clients;
  }
  ++state.closed_total;
}

void TelemetryStreamMetricsService::RecordReplayRequired(
    TelemetryStreamMetrics& streams,
    const std::string& stream_name) const {
  ++MutableStreamState(streams, stream_name).replay_required_total;
}

void TelemetryStreamMetricsService::RecordSendFailure(
    TelemetryStreamMetrics& streams,
    const std::string& stream_name) const {
  ++MutableStreamState(streams, stream_name).send_failure_total;
}

nlohmann::json TelemetryStreamMetricsService::BuildStatus(
    const TelemetryStreamMetrics& streams) const {
  return nlohmann::json{
      {"telemetry", EncodeState(streams.telemetry)},
      {"live", EncodeState(streams.live)},
  };
}

TelemetryStreamState& TelemetryStreamMetricsService::MutableStreamState(
    TelemetryStreamMetrics& streams,
    const std::string& stream_name) const {
  if (stream_name == "live") {
    return streams.live;
  }
  return streams.telemetry;
}

nlohmann::json TelemetryStreamMetricsService::EncodeState(
    const TelemetryStreamState& state) const {
  return nlohmann::json{
      {"active_clients", state.active_clients},
      {"opened_total", state.opened_total},
      {"closed_total", state.closed_total},
      {"reconnect_total", state.reconnect_total},
      {"replay_required_total", state.replay_required_total},
      {"send_failure_total", state.send_failure_total},
  };
}

}  // namespace naim::controller
