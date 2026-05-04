#include "telemetry/telemetry_live_store.h"

#include <algorithm>
#include <chrono>

namespace naim::controller {

TelemetryLiveStore& TelemetryLiveStore::Instance() {
  static TelemetryLiveStore store;
  return store;
}

bool TelemetryLiveStore::Upsert(naim::HostTelemetryFrame frame) {
  if (frame.node_name.empty()) {
    return false;
  }
  if (frame.channel.empty()) {
    frame.channel = "host.telemetry.v1";
  }
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(
      nodes_.begin(),
      nodes_.end(),
      [&](const NodeBuffer& candidate) {
        return candidate.latest.node_name == frame.node_name;
      });
  if (it != nodes_.end() && frame.sequence <= it->latest.sequence) {
    return false;
  }
  latest_sequence_ = std::max(latest_sequence_, frame.sequence);
  if (it == nodes_.end()) {
    NodeBuffer buffer;
    buffer.latest = frame;
    buffer.history.push_back(std::move(frame));
    nodes_.push_back(std::move(buffer));
  } else {
    it->latest = frame;
    it->history.push_back(std::move(frame));
    while (it->history.size() > 300) {
      it->history.pop_front();
    }
  }
  return true;
}

nlohmann::json TelemetryLiveStore::BuildSnapshot(
    const std::optional<std::string>& plane_name,
    const int history_seconds) const {
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json nodes = nlohmann::json::array();
  nlohmann::json history = nlohmann::json::array();
  for (const auto& buffer : nodes_) {
    if (!MatchesPlane(buffer.latest, plane_name)) {
      continue;
    }
    nodes.push_back(FrameToJson(buffer.latest));
    if (history_seconds <= 0) {
      continue;
    }
    const std::size_t max_samples =
        static_cast<std::size_t>(std::max(1, history_seconds / 2));
    const std::size_t begin =
        buffer.history.size() > max_samples ? buffer.history.size() - max_samples : 0;
    for (std::size_t index = begin; index < buffer.history.size(); ++index) {
      history.push_back(FrameToJson(buffer.history[index]));
    }
  }
  return nlohmann::json{
      {"service", "naim-controller"},
      {"plane_name", plane_name.has_value() ? nlohmann::json(*plane_name) : nlohmann::json(nullptr)},
      {"latest_sequence", latest_sequence_},
      {"nodes", std::move(nodes)},
      {"history", std::move(history)},
  };
}

std::vector<naim::HostTelemetryFrame> TelemetryLiveStore::LoadFramesAfter(
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<naim::HostTelemetryFrame> frames;
  for (const auto& buffer : nodes_) {
    for (const auto& frame : buffer.history) {
      if (frame.sequence > sequence && MatchesPlane(frame, plane_name)) {
        frames.push_back(frame);
      }
    }
  }
  std::sort(
      frames.begin(),
      frames.end(),
      [](const auto& left, const auto& right) {
        return left.sequence < right.sequence;
      });
  return frames;
}

std::uint64_t TelemetryLiveStore::LatestSequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_sequence_;
}

bool TelemetryLiveStore::MatchesPlane(
    const naim::HostTelemetryFrame& frame,
    const std::optional<std::string>& plane_name) {
  if (!plane_name.has_value() || plane_name->empty()) {
    return true;
  }
  if (frame.plane_name == *plane_name) {
    return true;
  }
  return std::any_of(
      frame.instance_runtime.begin(),
      frame.instance_runtime.end(),
      [&](const naim::RuntimeProcessStatus& status) {
        return status.instance_name.rfind(*plane_name + "-", 0) == 0;
      });
}

nlohmann::json TelemetryLiveStore::FrameToJson(
    const naim::HostTelemetryFrame& frame) {
  auto payload = nlohmann::json::parse(naim::SerializeHostTelemetryFrameJson(frame));
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const std::uint64_t expires_at_ms = frame.sequence + ttl_ms;
  const bool stale = frame.sequence == 0 || ttl_ms == 0 || expires_at_ms <= now_ms;
  payload["stale"] = stale;
  payload["expires_in_ms"] = stale ? 0 : expires_at_ms - now_ms;
  return payload;
}

}  // namespace naim::controller
