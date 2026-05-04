#include "telemetry/telemetry_frame_ring_buffer.h"

#include <algorithm>
#include <cstddef>

namespace naim::controller {

bool TelemetryFrameRingBuffer::Upsert(
    std::vector<TelemetryNodeBuffer>& nodes,
    std::uint64_t& latest_sequence,
    std::uint64_t& dropped_frames_total,
    const TelemetryRetentionConfig& retention,
    naim::HostTelemetryFrame frame) const {
  auto it = std::find_if(
      nodes.begin(),
      nodes.end(),
      [&](const TelemetryNodeBuffer& candidate) {
        return candidate.latest.node_name == frame.node_name;
      });
  if (it != nodes.end() && frame.sequence <= it->latest.sequence) {
    return false;
  }
  latest_sequence = std::max(latest_sequence, frame.sequence);
  if (it == nodes.end()) {
    TelemetryNodeBuffer buffer;
    buffer.latest = frame;
    buffer.history.push_back(std::move(frame));
    nodes.push_back(std::move(buffer));
  } else {
    it->latest = frame;
    it->history.push_back(std::move(frame));
  }
  ApplyHotRetention(nodes, dropped_frames_total, retention);
  return true;
}

void TelemetryFrameRingBuffer::ApplyHotRetention(
    std::vector<TelemetryNodeBuffer>& nodes,
    std::uint64_t& dropped_frames_total,
    const TelemetryRetentionConfig& retention) const {
  for (auto& node : nodes) {
    while (node.history.size() > retention.hot_history_capacity) {
      node.last_pruned_sequence = node.history.front().sequence;
      node.history.pop_front();
      ++node.dropped_frames_total;
      ++dropped_frames_total;
    }
  }
}

std::vector<naim::HostTelemetryFrame> TelemetryFrameRingBuffer::LoadFramesAfter(
    const std::vector<TelemetryNodeBuffer>& nodes,
    const TelemetryRetentionConfig& retention,
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::vector<naim::HostTelemetryFrame> frames;
  for (const auto& buffer : nodes) {
    for (const auto& frame : buffer.history) {
      if (frame.sequence > sequence && matcher_.MatchesPlane(frame, plane_name)) {
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
  if (frames.size() > retention.stream_batch_limit) {
    const auto erase_count =
        static_cast<std::ptrdiff_t>(frames.size() - retention.stream_batch_limit);
    frames.erase(frames.begin(), frames.begin() + erase_count);
  }
  return frames;
}

TelemetryStreamDelta TelemetryFrameRingBuffer::LoadStreamDeltaAfter(
    const std::vector<TelemetryNodeBuffer>& nodes,
    const TelemetryRetentionConfig& retention,
    const std::uint64_t latest_sequence,
    const std::uint64_t dropped_frames_total,
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  TelemetryStreamDelta delta;
  delta.requested_sequence = sequence;
  delta.latest_sequence = latest_sequence;
  delta.dropped_frames_total = dropped_frames_total;
  std::vector<naim::HostTelemetryFrame> frames;
  std::uint64_t first_available = 0;
  for (const auto& buffer : nodes) {
    if (!matcher_.MatchesPlane(buffer.latest, plane_name)) {
      continue;
    }
    if (!buffer.history.empty()) {
      const auto node_first = buffer.history.front().sequence;
      first_available = first_available == 0 ? node_first : std::min(first_available, node_first);
      if (sequence > 0 && sequence < node_first && buffer.last_pruned_sequence > 0) {
        delta.replay_required = true;
        delta.replay_reason = "requested-sequence-pruned";
      }
    }
    for (const auto& frame : buffer.history) {
      if (frame.sequence > sequence) {
        frames.push_back(frame);
      }
    }
  }
  delta.first_available_sequence = first_available;
  std::sort(
      frames.begin(),
      frames.end(),
      [](const auto& left, const auto& right) {
        return left.sequence < right.sequence;
      });
  if (frames.size() > retention.stream_batch_limit) {
    delta.replay_required = true;
    if (delta.replay_reason.empty()) {
      delta.replay_reason = "stream-batch-truncated";
    }
    const auto erase_count =
        static_cast<std::ptrdiff_t>(frames.size() - retention.stream_batch_limit);
    frames.erase(frames.begin(), frames.begin() + erase_count);
  }
  delta.frames = std::move(frames);
  return delta;
}

}  // namespace naim::controller
