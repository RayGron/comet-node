#include "telemetry/telemetry_live_store.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

#include <sqlite3.h>

#include "naim/state/sqlite_statement.h"

namespace naim::controller {
namespace {

std::uint64_t CurrentUnixMillis() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t ControllerIngestDelayMs(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t now_ms) {
  return frame.sequence > 0 && now_ms >= frame.sequence ? now_ms - frame.sequence : 0;
}

std::string PlaneKeyForFrame(const naim::HostTelemetryFrame& frame) {
  return frame.plane_name.empty() ? std::string{"unassigned"} : frame.plane_name;
}

double GpuUtilizationAverage(const naim::HostTelemetryFrame& frame) {
  if (frame.gpu.devices.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (const auto& device : frame.gpu.devices) {
    sum += static_cast<double>(device.gpu_utilization_pct);
  }
  return sum / static_cast<double>(frame.gpu.devices.size());
}

void ExecuteSqlite(sqlite3* db, const std::string& sql) {
  char* error_message = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message);
  if (rc != SQLITE_OK) {
    std::string message = error_message != nullptr ? error_message : sqlite3_errmsg(db);
    sqlite3_free(error_message);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

class SqliteConnection final {
 public:
  explicit SqliteConnection(const std::string& db_path) {
    if (sqlite3_open_v2(
            db_path.c_str(),
            &db_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
      std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "unknown";
      if (db_ != nullptr) {
        sqlite3_close(db_);
      }
      throw std::runtime_error("sqlite open failed: " + message);
    }
    sqlite3_busy_timeout(db_, 1000);
  }

  ~SqliteConnection() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  sqlite3* get() const {
    return db_;
  }

 private:
  sqlite3* db_ = nullptr;
};

void EnsureTelemetryPersistenceSchema(sqlite3* db) {
  ExecuteSqlite(
      db,
      "CREATE TABLE IF NOT EXISTS telemetry_ring_buffer ("
      "sequence INTEGER PRIMARY KEY,"
      "node_name TEXT NOT NULL,"
      "plane_name TEXT,"
      "schema_version TEXT NOT NULL,"
      "sampled_at TEXT,"
      "frame_json TEXT NOT NULL,"
      "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
  ExecuteSqlite(
      db,
      "CREATE INDEX IF NOT EXISTS idx_telemetry_ring_buffer_plane_sequence "
      "ON telemetry_ring_buffer(plane_name, sequence)");
}

std::int64_t SafeSequenceForSqlite(std::uint64_t sequence) {
  return static_cast<std::int64_t>(std::min<std::uint64_t>(
      sequence,
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
}

std::optional<std::uint64_t> EnvUint64(const char* key) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(std::stoull(value));
}

std::optional<std::size_t> EnvSize(const char* key) {
  const auto value = EnvUint64(key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

std::string SanitizeMetricLabel(std::string value) {
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

}  // namespace

TelemetryLiveStore& TelemetryLiveStore::Instance() {
  static TelemetryLiveStore store;
  return store;
}

void TelemetryLiveStore::ConfigurePersistence(
    const std::string& db_path,
    const std::size_t retention_capacity) {
  std::lock_guard<std::mutex> lock(mutex_);
  ConfigurePersistenceLocked(db_path, retention_capacity);
}

void TelemetryLiveStore::ConfigureFromEnvironment(const std::string& db_path) {
  RetentionConfig retention = retention_;
  retention.hot_history_capacity =
      EnvSize("NAIM_TELEMETRY_HOT_HISTORY_CAPACITY")
          .value_or(retention.hot_history_capacity);
  retention.stream_batch_limit =
      EnvSize("NAIM_TELEMETRY_STREAM_BATCH_LIMIT")
          .value_or(retention.stream_batch_limit);
  retention.durable_history_capacity =
      EnvSize("NAIM_TELEMETRY_DURABLE_HISTORY_CAPACITY")
          .value_or(retention.durable_history_capacity);
  retention.warm_bucket_ms =
      EnvUint64("NAIM_TELEMETRY_WARM_BUCKET_MS")
          .value_or(retention.warm_bucket_ms);
  retention.cold_bucket_ms =
      EnvUint64("NAIM_TELEMETRY_COLD_BUCKET_MS")
          .value_or(retention.cold_bucket_ms);

  AlertThresholds thresholds = thresholds_;
  thresholds.stale_warning_ms =
      EnvUint64("NAIM_TELEMETRY_STALE_WARNING_MS")
          .value_or(thresholds.stale_warning_ms);
  thresholds.stale_critical_ms =
      EnvUint64("NAIM_TELEMETRY_STALE_CRITICAL_MS")
          .value_or(thresholds.stale_critical_ms);
  thresholds.ingest_warning_ms =
      EnvUint64("NAIM_TELEMETRY_INGEST_WARNING_MS")
          .value_or(thresholds.ingest_warning_ms);
  thresholds.queue_warning_ms =
      EnvUint64("NAIM_TELEMETRY_QUEUE_WARNING_MS")
          .value_or(thresholds.queue_warning_ms);
  thresholds.browser_apply_warning_ms =
      EnvUint64("NAIM_TELEMETRY_BROWSER_APPLY_WARNING_MS")
          .value_or(thresholds.browser_apply_warning_ms);

  ConfigureOperationalPolicy(retention, thresholds);
  ConfigurePersistence(db_path, retention.durable_history_capacity);
}

void TelemetryLiveStore::ConfigureOperationalPolicy(
    RetentionConfig retention,
    AlertThresholds thresholds) {
  std::lock_guard<std::mutex> lock(mutex_);
  retention.hot_history_capacity = std::max<std::size_t>(1, retention.hot_history_capacity);
  retention.stream_batch_limit = std::max<std::size_t>(1, retention.stream_batch_limit);
  retention.durable_history_capacity =
      std::max<std::size_t>(1, retention.durable_history_capacity);
  retention.warm_bucket_ms = std::max<std::uint64_t>(1, retention.warm_bucket_ms);
  retention.cold_bucket_ms = std::max<std::uint64_t>(
      retention.warm_bucket_ms,
      retention.cold_bucket_ms);
  retention_ = retention;
  thresholds_ = thresholds;
  for (auto& node : nodes_) {
    while (node.history.size() > retention_.hot_history_capacity) {
      node.last_pruned_sequence = node.history.front().sequence;
      node.history.pop_front();
      ++node.dropped_frames_total;
      ++dropped_frames_total_;
    }
  }
}

void TelemetryLiveStore::ResetForTests() {
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.clear();
  latest_sequence_ = 0;
  dropped_frames_total_ = 0;
  retention_ = RetentionConfig{};
  thresholds_ = AlertThresholds{};
  persistence_ = PersistenceState{};
  streams_ = StreamMetrics{};
}

bool TelemetryLiveStore::Upsert(naim::HostTelemetryFrame frame) {
  if (frame.node_name.empty()) {
    return false;
  }
  if (frame.channel.empty()) {
    frame.channel = "host.telemetry.v1";
  }
  if (frame.schema_version.empty()) {
    frame.schema_version = "host.telemetry.v2";
  }
  if (frame.source.empty()) {
    frame.source = "hostd";
  }
  if (frame.node_id.empty()) {
    frame.node_id = frame.node_name;
  }
  if (frame.plane_id.empty()) {
    frame.plane_id = frame.plane_name;
  }
  if (frame.monotonic_timestamp_ms == 0) {
    frame.monotonic_timestamp_ms = frame.monotonic_ms;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const bool updated = UpsertInMemoryLocked(frame);
  if (updated) {
    PersistFrameLocked(frame);
  }
  return updated;
}

bool TelemetryLiveStore::UpsertInMemoryLocked(naim::HostTelemetryFrame frame) {
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
    while (it->history.size() > retention_.hot_history_capacity) {
      it->last_pruned_sequence = it->history.front().sequence;
      it->history.pop_front();
      ++it->dropped_frames_total;
      ++dropped_frames_total_;
    }
  }
  return true;
}

void TelemetryLiveStore::ConfigurePersistenceLocked(
    const std::string& db_path,
    const std::size_t retention_capacity) {
  persistence_.enabled = false;
  persistence_.db_path = db_path;
  persistence_.retention_capacity = std::max<std::size_t>(1, retention_capacity);
  try {
    const auto frames = LoadPersistedFramesLocked(db_path, persistence_.retention_capacity);
    for (auto frame : frames) {
      UpsertInMemoryLocked(std::move(frame));
    }
    persistence_.loaded_frames_total += frames.size();
    persistence_.enabled = true;
    persistence_.last_error.clear();
  } catch (const std::exception& error) {
    persistence_.error_count += 1;
    persistence_.last_error = error.what();
  }
}

void TelemetryLiveStore::PersistFrameLocked(const naim::HostTelemetryFrame& frame) {
  if (!persistence_.enabled || persistence_.db_path.empty()) {
    return;
  }
  try {
    SqliteConnection connection(persistence_.db_path);
    EnsureTelemetryPersistenceSchema(connection.get());
    {
      naim::SqliteStatement statement(
          connection.get(),
          "INSERT INTO telemetry_ring_buffer("
          "sequence, node_name, plane_name, schema_version, sampled_at, frame_json) "
          "VALUES(?, ?, ?, ?, ?, ?) "
          "ON CONFLICT(sequence) DO UPDATE SET "
          "node_name=excluded.node_name,"
          "plane_name=excluded.plane_name,"
          "schema_version=excluded.schema_version,"
          "sampled_at=excluded.sampled_at,"
          "frame_json=excluded.frame_json");
      statement.BindInt64(1, SafeSequenceForSqlite(frame.sequence));
      statement.BindText(2, frame.node_name);
      statement.BindText(3, frame.plane_name);
      statement.BindText(4, frame.schema_version);
      statement.BindText(5, frame.sampled_at);
      statement.BindText(6, naim::SerializeHostTelemetryFrameJson(frame));
      statement.StepDone();
    }
    {
      naim::SqliteStatement prune(
          connection.get(),
          "DELETE FROM telemetry_ring_buffer "
          "WHERE sequence NOT IN ("
          "SELECT sequence FROM telemetry_ring_buffer "
          "ORDER BY sequence DESC LIMIT ?)");
      prune.BindInt64(1, static_cast<std::int64_t>(persistence_.retention_capacity));
      prune.StepDone();
      const int changed = sqlite3_changes(connection.get());
      if (changed > 0) {
        persistence_.pruned_frames_total += static_cast<std::uint64_t>(changed);
      }
    }
    persistence_.persisted_frames_total += 1;
    persistence_.last_error.clear();
  } catch (const std::exception& error) {
    persistence_.error_count += 1;
    persistence_.last_error = error.what();
  }
}

std::vector<naim::HostTelemetryFrame> TelemetryLiveStore::LoadPersistedFramesLocked(
    const std::string& db_path,
    const std::size_t retention_capacity) {
  SqliteConnection connection(db_path);
  EnsureTelemetryPersistenceSchema(connection.get());
  naim::SqliteStatement statement(
      connection.get(),
      "SELECT frame_json FROM telemetry_ring_buffer "
      "ORDER BY sequence DESC LIMIT ?");
  statement.BindInt64(1, static_cast<std::int64_t>(retention_capacity));
  std::vector<std::string> payloads;
  while (statement.StepRow()) {
    const unsigned char* text = sqlite3_column_text(statement.raw(), 0);
    if (text != nullptr) {
      payloads.emplace_back(reinterpret_cast<const char*>(text));
    }
  }
  std::vector<naim::HostTelemetryFrame> frames;
  for (auto it = payloads.rbegin(); it != payloads.rend(); ++it) {
    frames.push_back(naim::DeserializeHostTelemetryFrameJson(*it));
  }
  return frames;
}

nlohmann::json TelemetryLiveStore::BuildSnapshot(
    const std::optional<std::string>& plane_name,
    const int history_seconds) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now_ms = CurrentUnixMillis();
  std::vector<const NodeBuffer*> matched_buffers;
  nlohmann::json nodes = nlohmann::json::array();
  nlohmann::json history = nlohmann::json::array();
  for (const auto& buffer : nodes_) {
    if (!MatchesPlane(buffer.latest, plane_name)) {
      continue;
    }
    matched_buffers.push_back(&buffer);
    auto node = FrameToJson(buffer.latest);
    const auto controller_ingest_delay_ms =
        ControllerIngestDelayMs(buffer.latest, now_ms);
    node["telemetry_health"] = BuildTelemetryHealth(
        buffer.latest,
        buffer,
        now_ms,
        controller_ingest_delay_ms);
    node["controller_dropped_frames_total"] = buffer.dropped_frames_total;
    node["controller_last_pruned_sequence"] = buffer.last_pruned_sequence;
    nodes.push_back(std::move(node));
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
  const bool overloaded = dropped_frames_total_ > 0;
  const auto alerts = BuildAlerts(
      matched_buffers,
      persistence_,
      streams_,
      thresholds_,
      dropped_frames_total_,
      now_ms);
  const std::string status =
      !alerts.empty() ? "degraded" : overloaded ? "overloaded" : "ok";
  return nlohmann::json{
      {"schema_version", "telemetry.snapshot.v2"},
      {"service", "naim-controller"},
      {"transport", "sse-primary"},
      {"delivery_mode", "streaming-first"},
      {"plane_name", plane_name.has_value() ? nlohmann::json(*plane_name) : nlohmann::json(nullptr)},
      {"latest_sequence", latest_sequence_},
      {"telemetry_overloaded", overloaded},
      {"dropped_frames_total", dropped_frames_total_},
      {"history_capacity", retention_.hot_history_capacity},
      {"stream_batch_limit", retention_.stream_batch_limit},
      {"persistence", BuildPersistenceStatusLocked()},
      {"streams", BuildStreamStatusLocked()},
      {"alerts", alerts},
      {"retention",
       nlohmann::json{
           {"hot_history_capacity", retention_.hot_history_capacity},
           {"durable_history_capacity", persistence_.retention_capacity},
           {"stream_batch_limit", retention_.stream_batch_limit},
           {"warm_bucket_ms", retention_.warm_bucket_ms},
           {"cold_bucket_ms", retention_.cold_bucket_ms},
       }},
      {"telemetry_health",
       nlohmann::json{
           {"status", status},
           {"last_frame_age_ms",
            latest_sequence_ > 0 && now_ms >= latest_sequence_ ? now_ms - latest_sequence_ : 0},
           {"dropped_frames_total", dropped_frames_total_},
           {"stream_batch_limit", retention_.stream_batch_limit},
       }},
      {"planes", BuildPlaneAggregates(matched_buffers, now_ms)},
      {"nodes", std::move(nodes)},
      {"history", std::move(history)},
      {"downsampled_history",
       BuildDownsampledHistory(
           matched_buffers,
           history_seconds,
           now_ms,
           retention_.warm_bucket_ms,
           retention_.cold_bucket_ms)},
  };
}

nlohmann::json TelemetryLiveStore::BuildHealth(
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now_ms = CurrentUnixMillis();
  std::vector<const NodeBuffer*> matched_buffers;
  for (const auto& buffer : nodes_) {
    if (MatchesPlane(buffer.latest, plane_name)) {
      matched_buffers.push_back(&buffer);
    }
  }
  const auto alerts = BuildAlerts(
      matched_buffers,
      persistence_,
      streams_,
      thresholds_,
      dropped_frames_total_,
      now_ms);
  std::string status = "ok";
  if (!alerts.empty()) {
    status = "degraded";
    for (const auto& alert : alerts) {
      if (alert.value("severity", std::string{}) == "critical") {
        status = "critical";
        break;
      }
    }
  }
  return nlohmann::json{
      {"schema_version", "telemetry.health.v1"},
      {"service", "naim-controller"},
      {"plane_name", plane_name.has_value() ? nlohmann::json(*plane_name) : nlohmann::json(nullptr)},
      {"status", status},
      {"observed_nodes", matched_buffers.size()},
      {"latest_sequence", latest_sequence_},
      {"last_frame_age_ms",
       latest_sequence_ > 0 && now_ms >= latest_sequence_ ? now_ms - latest_sequence_ : 0},
      {"dropped_frames_total", dropped_frames_total_},
      {"stream_batch_limit", retention_.stream_batch_limit},
      {"retention",
       nlohmann::json{
           {"hot_history_capacity", retention_.hot_history_capacity},
           {"durable_history_capacity", persistence_.retention_capacity},
           {"stream_batch_limit", retention_.stream_batch_limit},
           {"warm_bucket_ms", retention_.warm_bucket_ms},
           {"cold_bucket_ms", retention_.cold_bucket_ms},
       }},
      {"persistence", BuildPersistenceStatusLocked()},
      {"streams", BuildStreamStatusLocked()},
      {"thresholds",
       nlohmann::json{
           {"stale_warning_ms", thresholds_.stale_warning_ms},
           {"stale_critical_ms", thresholds_.stale_critical_ms},
           {"ingest_warning_ms", thresholds_.ingest_warning_ms},
           {"queue_warning_ms", thresholds_.queue_warning_ms},
           {"browser_apply_warning_ms", thresholds_.browser_apply_warning_ms},
       }},
      {"planes", BuildPlaneAggregates(matched_buffers, now_ms)},
      {"alerts", alerts},
  };
}

std::string TelemetryLiveStore::BuildOpenMetrics(
    const std::optional<std::string>& plane_name) const {
  const auto health = BuildHealth(plane_name);
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
  if (frames.size() > retention_.stream_batch_limit) {
    const auto erase_count =
        static_cast<std::ptrdiff_t>(frames.size() - retention_.stream_batch_limit);
    frames.erase(frames.begin(), frames.begin() + erase_count);
  }
  return frames;
}

TelemetryLiveStore::StreamDelta TelemetryLiveStore::LoadStreamDeltaAfter(
    const std::uint64_t sequence,
    const std::optional<std::string>& plane_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  StreamDelta delta;
  delta.requested_sequence = sequence;
  delta.latest_sequence = latest_sequence_;
  delta.dropped_frames_total = dropped_frames_total_;
  std::vector<naim::HostTelemetryFrame> frames;
  std::uint64_t first_available = 0;
  for (const auto& buffer : nodes_) {
    if (!MatchesPlane(buffer.latest, plane_name)) {
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
  if (frames.size() > retention_.stream_batch_limit) {
    delta.replay_required = true;
    if (delta.replay_reason.empty()) {
      delta.replay_reason = "stream-batch-truncated";
    }
    const auto erase_count =
        static_cast<std::ptrdiff_t>(frames.size() - retention_.stream_batch_limit);
    frames.erase(frames.begin(), frames.begin() + erase_count);
  }
  delta.frames = std::move(frames);
  return delta;
}

std::uint64_t TelemetryLiveStore::LatestSequence() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_sequence_;
}

void TelemetryLiveStore::RecordStreamOpened(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  RecordStreamOpenedLocked(stream_name);
}

void TelemetryLiveStore::RecordStreamClosed(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = MutableStreamStateLocked(stream_name);
  if (state.active_clients > 0) {
    --state.active_clients;
  }
  ++state.closed_total;
}

void TelemetryLiveStore::RecordStreamReplayRequired(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++MutableStreamStateLocked(stream_name).replay_required_total;
}

void TelemetryLiveStore::RecordStreamSendFailure(const std::string& stream_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++MutableStreamStateLocked(stream_name).send_failure_total;
}

void TelemetryLiveStore::RecordStreamOpenedLocked(const std::string& stream_name) {
  auto& state = MutableStreamStateLocked(stream_name);
  if (state.opened_total > 0) {
    ++state.reconnect_total;
  }
  ++state.opened_total;
  ++state.active_clients;
}

TelemetryLiveStore::StreamState& TelemetryLiveStore::MutableStreamStateLocked(
    const std::string& stream_name) {
  if (stream_name == "live") {
    return streams_.live;
  }
  return streams_.telemetry;
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

nlohmann::json TelemetryLiveStore::BuildPersistenceStatusLocked() const {
  return nlohmann::json{
      {"enabled", persistence_.enabled},
      {"backend", persistence_.enabled ? "sqlite" : "memory"},
      {"db_path", persistence_.db_path},
      {"retention_capacity", persistence_.retention_capacity},
      {"loaded_frames_total", persistence_.loaded_frames_total},
      {"persisted_frames_total", persistence_.persisted_frames_total},
      {"pruned_frames_total", persistence_.pruned_frames_total},
      {"error_count", persistence_.error_count},
      {"last_error", persistence_.last_error},
  };
}

nlohmann::json TelemetryLiveStore::BuildStreamStatusLocked() const {
  const auto encode = [](const StreamState& state) {
    return nlohmann::json{
        {"active_clients", state.active_clients},
        {"opened_total", state.opened_total},
        {"closed_total", state.closed_total},
        {"reconnect_total", state.reconnect_total},
        {"replay_required_total", state.replay_required_total},
        {"send_failure_total", state.send_failure_total},
    };
  };
  return nlohmann::json{
      {"telemetry", encode(streams_.telemetry)},
      {"live", encode(streams_.live)},
  };
}

nlohmann::json TelemetryLiveStore::BuildAlerts(
    const std::vector<const NodeBuffer*>& buffers,
    const PersistenceState& persistence,
    const StreamMetrics& streams,
    const AlertThresholds& thresholds,
    const std::uint64_t dropped_frames_total,
    const std::uint64_t now_ms) {
  nlohmann::json alerts = nlohmann::json::array();
  if (!persistence.enabled) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.persistence.disabled"},
        {"severity", "warning"},
        {"message", "sqlite telemetry ring buffer is disabled"},
    });
  }
  if (persistence.error_count > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.persistence.errors"},
        {"severity", "warning"},
        {"message", "sqlite telemetry ring buffer reported errors"},
        {"count", persistence.error_count},
        {"last_error", persistence.last_error},
    });
  }
  if (dropped_frames_total > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.controller.dropped_frames"},
        {"severity", "warning"},
        {"message", "controller telemetry ring buffer pruned live frames"},
        {"count", dropped_frames_total},
    });
  }
  if (streams.telemetry.send_failure_total > 0 || streams.live.send_failure_total > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.stream.send_failures"},
        {"severity", "warning"},
        {"message", "telemetry stream clients observed send failures"},
        {"telemetry_failures", streams.telemetry.send_failure_total},
        {"live_failures", streams.live.send_failure_total},
    });
  }
  if (streams.telemetry.replay_required_total > 0 || streams.live.replay_required_total > 0) {
    alerts.push_back(nlohmann::json{
        {"code", "telemetry.stream.replay_required"},
        {"severity", "warning"},
        {"message", "telemetry stream replay was required"},
        {"telemetry_replay_required_total", streams.telemetry.replay_required_total},
        {"live_replay_required_total", streams.live.replay_required_total},
    });
  }
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto& frame = buffer->latest;
    const auto ingest_ms = ControllerIngestDelayMs(frame, now_ms);
    if (ingest_ms >= thresholds.stale_critical_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.node.stale"},
          {"severity", "critical"},
          {"node_name", frame.node_name},
          {"plane_name", PlaneKeyForFrame(frame)},
          {"age_ms", ingest_ms},
      });
    } else if (ingest_ms >= thresholds.stale_warning_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.node.slow"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", PlaneKeyForFrame(frame)},
          {"age_ms", ingest_ms},
      });
    }
    if (ingest_ms >= thresholds.ingest_warning_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.controller.ingest_delay"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", PlaneKeyForFrame(frame)},
          {"delay_ms", ingest_ms},
      });
    }
    if (frame.publisher_queue_delay_ms >= thresholds.queue_warning_ms) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.publisher.queue_delay"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", PlaneKeyForFrame(frame)},
          {"delay_ms", frame.publisher_queue_delay_ms},
      });
    }
    if (frame.publish_error_count > 0) {
      alerts.push_back(nlohmann::json{
          {"code", "telemetry.publisher.errors"},
          {"severity", "warning"},
          {"node_name", frame.node_name},
          {"plane_name", PlaneKeyForFrame(frame)},
          {"count", frame.publish_error_count},
          {"last_error", frame.last_publish_error},
      });
    }
  }
  return alerts;
}

nlohmann::json TelemetryLiveStore::FrameToJson(
    const naim::HostTelemetryFrame& frame) {
  auto payload = nlohmann::json::parse(naim::SerializeHostTelemetryFrameJson(frame));
  const auto now_ms = CurrentUnixMillis();
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const std::uint64_t expires_at_ms = frame.sequence + ttl_ms;
  const bool stale = frame.sequence == 0 || ttl_ms == 0 || expires_at_ms <= now_ms;
  payload["stale"] = stale;
  payload["expires_in_ms"] = stale ? 0 : expires_at_ms - now_ms;
  const auto controller_ingest_delay_ms = ControllerIngestDelayMs(frame, now_ms);
  payload["controller_ingest_delay_ms"] = controller_ingest_delay_ms;
  payload["last_frame_age_ms"] = controller_ingest_delay_ms;
  payload["telemetry_health_status"] =
      stale ? "stale"
            : frame.telemetry_dropped_frames > 0 || frame.publish_error_count > 0
                ? "degraded"
                : "ok";
  payload["latency_breakdown"] = BuildLatencyBreakdown(frame, controller_ingest_delay_ms);
  payload["transport"] = nlohmann::json{
      {"primary", "sse"},
      {"fallback", "snapshot-poll"},
      {"sequence", frame.sequence},
      {"schema_version", frame.schema_version},
  };
  return payload;
}

nlohmann::json TelemetryLiveStore::BuildLatencyBreakdown(
    const naim::HostTelemetryFrame& frame,
    const std::uint64_t controller_ingest_delay_ms) {
  const std::uint64_t total_observed_ms =
      frame.collector_duration_ms + frame.publisher_queue_delay_ms +
      frame.publish_duration_ms + controller_ingest_delay_ms;
  return nlohmann::json{
      {"collect_ms", frame.collector_duration_ms},
      {"queue_ms", frame.publisher_queue_delay_ms},
      {"publish_ms", frame.publish_duration_ms},
      {"controller_ingest_ms", controller_ingest_delay_ms},
      {"total_observed_ms", total_observed_ms},
  };
}

nlohmann::json TelemetryLiveStore::BuildTelemetryHealth(
    const naim::HostTelemetryFrame& frame,
    const NodeBuffer& buffer,
    const std::uint64_t now_ms,
    const std::uint64_t controller_ingest_delay_ms) {
  const std::uint64_t ttl_ms =
      frame.ttl_ms > 0 ? static_cast<std::uint64_t>(frame.ttl_ms) : 0;
  const bool stale =
      frame.sequence == 0 || ttl_ms == 0 || frame.sequence + ttl_ms <= now_ms;
  std::string status = "ok";
  if (stale) {
    status = "stale";
  } else if (
      buffer.dropped_frames_total > 0 || frame.telemetry_dropped_frames > 0 ||
      frame.publish_error_count > 0 || !frame.degraded_reason.empty()) {
    status = "degraded";
  }
  return nlohmann::json{
      {"status", status},
      {"last_frame_age_ms", controller_ingest_delay_ms},
      {"dropped_frames_total", buffer.dropped_frames_total},
      {"publish_error_count", frame.publish_error_count},
      {"publish_error", frame.last_publish_error},
      {"degraded_reason", frame.degraded_reason},
  };
}

namespace {

struct BucketAccumulator {
  std::string node_name;
  std::string plane_name;
  std::uint64_t bucket_start_ms = 0;
  std::uint64_t bucket_ms = 0;
  std::uint64_t first_sequence = 0;
  std::uint64_t last_sequence = 0;
  std::uint64_t sample_count = 0;
  double cpu_utilization_sum = 0.0;
  double gpu_utilization_sum = 0.0;
  double max_gpu_utilization_pct = 0.0;
  std::uint64_t max_queue_delay_ms = 0;
  std::uint64_t max_publish_ms = 0;
  std::uint64_t max_controller_ingest_ms = 0;
};

}  // namespace

nlohmann::json TelemetryLiveStore::BuildDownsampledHistory(
    const std::vector<const NodeBuffer*>& buffers,
    const int history_seconds,
    const std::uint64_t now_ms,
    const std::uint64_t warm_bucket_ms,
    const std::uint64_t cold_bucket_ms) {
  if (history_seconds <= 0) {
    return nlohmann::json::array();
  }
  std::map<std::string, BucketAccumulator> buckets;
  const std::uint64_t horizon_ms =
      static_cast<std::uint64_t>(std::max(1, history_seconds)) * 1000ULL;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    for (const auto& frame : buffer->history) {
      if (frame.sequence == 0 || now_ms > frame.sequence + horizon_ms) {
        continue;
      }
      const std::uint64_t age_ms = now_ms >= frame.sequence ? now_ms - frame.sequence : 0;
      const std::uint64_t bucket_ms =
          age_ms <= warm_bucket_ms ? warm_bucket_ms : cold_bucket_ms;
      const std::uint64_t bucket_start_ms = (frame.sequence / bucket_ms) * bucket_ms;
      const auto key =
          frame.node_name + "|" + PlaneKeyForFrame(frame) + "|" +
          std::to_string(bucket_ms) + "|" + std::to_string(bucket_start_ms);
      auto& bucket = buckets[key];
      if (bucket.sample_count == 0) {
        bucket.node_name = frame.node_name;
        bucket.plane_name = PlaneKeyForFrame(frame);
        bucket.bucket_start_ms = bucket_start_ms;
        bucket.bucket_ms = bucket_ms;
        bucket.first_sequence = frame.sequence;
      }
      bucket.last_sequence = std::max(bucket.last_sequence, frame.sequence);
      bucket.sample_count += 1;
      bucket.cpu_utilization_sum += frame.cpu.utilization_pct;
      const double gpu_util = GpuUtilizationAverage(frame);
      bucket.gpu_utilization_sum += gpu_util;
      bucket.max_gpu_utilization_pct = std::max(bucket.max_gpu_utilization_pct, gpu_util);
      bucket.max_queue_delay_ms =
          std::max(bucket.max_queue_delay_ms, frame.publisher_queue_delay_ms);
      bucket.max_publish_ms = std::max(bucket.max_publish_ms, frame.publish_duration_ms);
      bucket.max_controller_ingest_ms = std::max(
          bucket.max_controller_ingest_ms,
          ControllerIngestDelayMs(frame, now_ms));
    }
  }
  nlohmann::json result = nlohmann::json::array();
  for (const auto& [_, bucket] : buckets) {
    const double count = static_cast<double>(std::max<std::uint64_t>(1, bucket.sample_count));
    result.push_back(nlohmann::json{
        {"node_name", bucket.node_name},
        {"plane_name", bucket.plane_name},
        {"bucket_start_ms", bucket.bucket_start_ms},
        {"bucket_ms", bucket.bucket_ms},
        {"sample_count", bucket.sample_count},
        {"first_sequence", bucket.first_sequence},
        {"last_sequence", bucket.last_sequence},
        {"avg_cpu_utilization_pct", bucket.cpu_utilization_sum / count},
        {"avg_gpu_utilization_pct", bucket.gpu_utilization_sum / count},
        {"max_gpu_utilization_pct", bucket.max_gpu_utilization_pct},
        {"max_queue_delay_ms", bucket.max_queue_delay_ms},
        {"max_publish_ms", bucket.max_publish_ms},
        {"max_controller_ingest_ms", bucket.max_controller_ingest_ms},
    });
  }
  return result;
}

nlohmann::json TelemetryLiveStore::BuildPlaneAggregates(
    const std::vector<const NodeBuffer*>& buffers,
    const std::uint64_t now_ms) {
  struct PlaneAccumulator {
    std::string plane_name;
    std::uint64_t node_count = 0;
    std::uint64_t stale_nodes = 0;
    std::uint64_t degraded_nodes = 0;
    std::uint64_t dropped_frames_total = 0;
    std::uint64_t latest_sequence = 0;
    std::uint64_t max_last_frame_age_ms = 0;
    std::uint64_t max_queue_delay_ms = 0;
    std::uint64_t max_publish_ms = 0;
    std::uint64_t max_controller_ingest_ms = 0;
    std::uint64_t gpu_count = 0;
    std::uint64_t plane_instance_count = 0;
    std::uint64_t plane_ready_instance_count = 0;
    std::uint64_t plane_not_ready_instance_count = 0;
    double gpu_utilization_sum = 0.0;
  };
  std::map<std::string, PlaneAccumulator> planes;
  for (const auto* buffer : buffers) {
    if (buffer == nullptr) {
      continue;
    }
    const auto& frame = buffer->latest;
    auto& plane = planes[PlaneKeyForFrame(frame)];
    if (plane.plane_name.empty()) {
      plane.plane_name = PlaneKeyForFrame(frame);
    }
    const auto ingest_ms = ControllerIngestDelayMs(frame, now_ms);
    const auto health = BuildTelemetryHealth(frame, *buffer, now_ms, ingest_ms);
    plane.node_count += 1;
    plane.latest_sequence = std::max(plane.latest_sequence, frame.sequence);
    plane.dropped_frames_total += buffer->dropped_frames_total;
    plane.max_last_frame_age_ms = std::max(plane.max_last_frame_age_ms, ingest_ms);
    plane.max_queue_delay_ms = std::max(plane.max_queue_delay_ms, frame.publisher_queue_delay_ms);
    plane.max_publish_ms = std::max(plane.max_publish_ms, frame.publish_duration_ms);
    plane.max_controller_ingest_ms = std::max(plane.max_controller_ingest_ms, ingest_ms);
    plane.gpu_count += frame.gpu.devices.size();
    plane.plane_instance_count += frame.plane_instance_count;
    plane.plane_ready_instance_count += frame.plane_ready_instance_count;
    plane.plane_not_ready_instance_count += frame.plane_not_ready_instance_count;
    plane.gpu_utilization_sum += GpuUtilizationAverage(frame);
    if (health.value("status", std::string{"ok"}) == "stale") {
      plane.stale_nodes += 1;
    } else if (health.value("status", std::string{"ok"}) == "degraded") {
      plane.degraded_nodes += 1;
    }
  }
  nlohmann::json result = nlohmann::json::array();
  for (const auto& [_, plane] : planes) {
    const bool overloaded = plane.dropped_frames_total > 0;
    std::string status = "ok";
    if (plane.stale_nodes > 0) {
      status = "stale";
    } else if (plane.degraded_nodes > 0 || overloaded) {
      status = "degraded";
    }
    const double node_count = static_cast<double>(std::max<std::uint64_t>(1, plane.node_count));
    result.push_back(nlohmann::json{
        {"plane_name", plane.plane_name},
        {"status", status},
        {"node_count", plane.node_count},
        {"stale_nodes", plane.stale_nodes},
        {"degraded_nodes", plane.degraded_nodes},
        {"dropped_frames_total", plane.dropped_frames_total},
        {"latest_sequence", plane.latest_sequence},
        {"max_last_frame_age_ms", plane.max_last_frame_age_ms},
        {"latency",
         nlohmann::json{
             {"max_queue_delay_ms", plane.max_queue_delay_ms},
             {"max_publish_ms", plane.max_publish_ms},
             {"max_controller_ingest_ms", plane.max_controller_ingest_ms},
         }},
        {"gpu_count", plane.gpu_count},
        {"runtime",
         nlohmann::json{
             {"instance_count", plane.plane_instance_count},
             {"ready_instance_count", plane.plane_ready_instance_count},
             {"not_ready_instance_count", plane.plane_not_ready_instance_count},
         }},
        {"avg_gpu_utilization_pct", plane.gpu_utilization_sum / node_count},
    });
  }
  return result;
}

}  // namespace naim::controller
