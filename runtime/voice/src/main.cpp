#include <whisper.h>

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

struct Config {
  std::string host = "0.0.0.0";
  int port = 8080;
  std::string model_path;
  std::string tmp_dir = "/tmp/naim-voice-module";
  std::string wake_phrase = "Hey Jex";
  std::string ffmpeg_bin = "ffmpeg";
  std::string language = "auto";
  std::string plane_name;
  std::string instance_name;
  std::string node_name;
  std::string runtime_status_path;
  int threads = std::max(1u, std::thread::hardware_concurrency());
  int max_body_bytes = 16 * 1024 * 1024;
  bool translate = false;
};

struct AudioPcm {
  std::vector<float> samples;
  int sample_rate = 16000;
};

std::atomic<uint64_t> g_request_seq{0};
std::mutex g_whisper_mutex;

std::string getenv_string(const char *name, const std::string &fallback = "") {
  const char *value = std::getenv(name);
  if (!value || !*value) {
    return fallback;
  }
  return value;
}

int getenv_int(const char *name, int fallback) {
  const std::string value = getenv_string(name);
  if (value.empty()) {
    return fallback;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

bool getenv_bool(const char *name, bool fallback) {
  std::string value = getenv_string(name);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

Config load_config() {
  Config config;
  config.host = getenv_string("HOST", config.host);
  config.port = getenv_int("PORT", getenv_int("NAIM_VOICE_MODULE_PORT", config.port));
  config.model_path = getenv_string("WHISPER_MODEL_PATH");
  config.tmp_dir = getenv_string("VOICE_ASR_TMP_DIR", getenv_string("NAIM_VOICE_MODULE_TMP_DIR", config.tmp_dir));
  config.wake_phrase = getenv_string("VOICE_LISTENER_WAKE_PHRASE", getenv_string("NAIM_VOICE_LISTENER_WAKE_PHRASE", config.wake_phrase));
  config.ffmpeg_bin = getenv_string("VOICE_ASR_FFMPEG_BIN", config.ffmpeg_bin);
  config.language = getenv_string("VOICE_ASR_LANGUAGE", config.language);
  config.plane_name = getenv_string("NAIM_PLANE_NAME");
  config.instance_name = getenv_string("NAIM_INSTANCE_NAME");
  config.node_name = getenv_string("NAIM_NODE_NAME");
  config.runtime_status_path = getenv_string("NAIM_VOICE_MODULE_RUNTIME_STATUS_PATH");
  config.threads = getenv_int("VOICE_ASR_THREADS", config.threads);
  config.max_body_bytes = getenv_int("VOICE_BODY_LIMIT_BYTES", config.max_body_bytes);
  config.translate = getenv_bool("VOICE_ASR_TRANSLATE", config.translate);
  return config;
}

std::string json_escape(const std::string &input) {
  std::ostringstream out;
  for (char c : input) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          out << c;
        }
    }
  }
  return out.str();
}

std::string make_json_error(const std::string &message) {
  return "{\"error\":\"" + json_escape(message) + "\"}";
}

void write_runtime_status(const Config &config, const std::string &phase, bool ready) {
  if (config.runtime_status_path.empty()) {
    return;
  }
  const std::filesystem::path status_path(config.runtime_status_path);
  if (status_path.has_parent_path()) {
    std::filesystem::create_directories(status_path.parent_path());
  }
  std::ofstream out(status_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "failed to write voice-module runtime status: "
              << status_path.string() << "\n";
    return;
  }
  const int pid = static_cast<int>(getpid());
  out << "{\n"
      << "  \"plane_name\": \"" << json_escape(config.plane_name) << "\",\n"
      << "  \"instance_name\": \"" << json_escape(config.instance_name) << "\",\n"
      << "  \"instance_role\": \"voice-module\",\n"
      << "  \"node_name\": \"" << json_escape(config.node_name) << "\",\n"
      << "  \"runtime_backend\": \"whisper.cpp\",\n"
      << "  \"runtime_phase\": \"" << json_escape(phase) << "\",\n"
      << "  \"model_path\": \"" << json_escape(config.model_path) << "\",\n"
      << "  \"runtime_pid\": " << pid << ",\n"
      << "  \"engine_pid\": " << pid << ",\n"
      << "  \"ready\": " << (ready ? "true" : "false") << ",\n"
      << "  \"launch_ready\": " << (ready ? "true" : "false") << ",\n"
      << "  \"inference_ready\": " << (ready ? "true" : "false") << ",\n"
      << "  \"active_model_ready\": " << (ready ? "true" : "false") << "\n"
      << "}\n";
}

std::string request_id() {
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
  ).count();
  return "asr_" + std::to_string(now) + "_" + std::to_string(++g_request_seq);
}

std::string shell_quote(const std::filesystem::path &path) {
  std::string input = path.string();
  std::string output = "'";
  for (char c : input) {
    if (c == '\'') {
      output += "'\\''";
    } else {
      output += c;
    }
  }
  output += "'";
  return output;
}

std::optional<std::string> content_type_boundary(const std::string &content_type) {
  const std::string marker = "boundary=";
  const auto index = content_type.find(marker);
  if (index == std::string::npos) {
    return std::nullopt;
  }
  std::string boundary = content_type.substr(index + marker.size());
  if (!boundary.empty() && boundary.front() == '"') {
    const auto end = boundary.find('"', 1);
    if (end != std::string::npos) {
      boundary = boundary.substr(1, end - 1);
    }
  } else {
    const auto end = boundary.find(';');
    if (end != std::string::npos) {
      boundary = boundary.substr(0, end);
    }
  }
  return boundary.empty() ? std::nullopt : std::optional<std::string>(boundary);
}

std::string extract_multipart_audio(const std::string &body, const std::string &content_type) {
  const auto boundary_opt = content_type_boundary(content_type);
  if (!boundary_opt) {
    return body;
  }

  const std::string delimiter = "--" + *boundary_opt;
  size_t cursor = 0;
  while (true) {
    const auto begin = body.find(delimiter, cursor);
    if (begin == std::string::npos) {
      break;
    }
    const auto part_begin = begin + delimiter.size();
    if (body.compare(part_begin, 2, "--") == 0) {
      break;
    }
    const auto header_begin = body.compare(part_begin, 2, "\r\n") == 0 ? part_begin + 2 : part_begin;
    const auto header_end = body.find("\r\n\r\n", header_begin);
    if (header_end == std::string::npos) {
      break;
    }
    const std::string headers = body.substr(header_begin, header_end - header_begin);
    const auto data_begin = header_end + 4;
    auto next = body.find(delimiter, data_begin);
    if (next == std::string::npos) {
      next = body.size();
    }
    auto data_end = next;
    if (data_end >= 2 && body.compare(data_end - 2, 2, "\r\n") == 0) {
      data_end -= 2;
    }
    const bool is_audio_field =
      headers.find("name=\"file\"") != std::string::npos ||
      headers.find("name=\"audio\"") != std::string::npos;
    if (is_audio_field) {
      return body.substr(data_begin, data_end - data_begin);
    }
    cursor = next;
  }

  throw std::runtime_error("multipart payload does not contain file or audio field");
}

std::string extract_request_audio(const httplib::Request &req) {
  for (const auto *field_name : {"file", "audio"}) {
    if (req.form.has_file(field_name)) {
      return req.form.get_file(field_name).content;
    }
  }

  const std::string content_type = req.get_header_value("Content-Type");
  return extract_multipart_audio(req.body, content_type);
}

std::filesystem::path write_temp_file(const Config &config, const std::string &prefix, const std::string &data) {
  std::filesystem::create_directories(config.tmp_dir);
  const auto path = std::filesystem::path(config.tmp_dir) / (prefix + "-" + request_id());
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("failed to create temporary file");
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  return path;
}

class TempFile {
 public:
  explicit TempFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~TempFile() {
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove(path_, ec);
    }
  }
  const std::filesystem::path &path() const { return path_; }

 private:
  std::filesystem::path path_;
};

AudioPcm read_wav_pcm16_mono_16k(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("failed to open converted wav");
  }
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.size() < 44 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) {
    throw std::runtime_error("converted audio is not a WAV file");
  }

  size_t offset = 12;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  size_t data_offset = 0;
  uint32_t data_size = 0;

  while (offset + 8 <= bytes.size()) {
    const std::string chunk_id = bytes.substr(offset, 4);
    const uint32_t chunk_size =
      static_cast<unsigned char>(bytes[offset + 4]) |
      (static_cast<unsigned char>(bytes[offset + 5]) << 8) |
      (static_cast<unsigned char>(bytes[offset + 6]) << 16) |
      (static_cast<unsigned char>(bytes[offset + 7]) << 24);
    offset += 8;
    if (offset + chunk_size > bytes.size()) {
      break;
    }
    if (chunk_id == "fmt " && chunk_size >= 16) {
      audio_format = static_cast<unsigned char>(bytes[offset]) |
        (static_cast<unsigned char>(bytes[offset + 1]) << 8);
      channels = static_cast<unsigned char>(bytes[offset + 2]) |
        (static_cast<unsigned char>(bytes[offset + 3]) << 8);
      sample_rate = static_cast<unsigned char>(bytes[offset + 4]) |
        (static_cast<unsigned char>(bytes[offset + 5]) << 8) |
        (static_cast<unsigned char>(bytes[offset + 6]) << 16) |
        (static_cast<unsigned char>(bytes[offset + 7]) << 24);
      bits_per_sample = static_cast<unsigned char>(bytes[offset + 14]) |
        (static_cast<unsigned char>(bytes[offset + 15]) << 8);
    } else if (chunk_id == "data") {
      data_offset = offset;
      data_size = chunk_size;
    }
    offset += chunk_size + (chunk_size % 2);
  }

  if (audio_format != 1 || channels != 1 || sample_rate != 16000 || bits_per_sample != 16 || data_offset == 0) {
    throw std::runtime_error("converted WAV must be mono 16 kHz PCM16");
  }

  AudioPcm audio;
  audio.samples.reserve(data_size / 2);
  for (size_t i = data_offset; i + 1 < data_offset + data_size; i += 2) {
    const int16_t sample = static_cast<int16_t>(
      static_cast<unsigned char>(bytes[i]) |
      (static_cast<unsigned char>(bytes[i + 1]) << 8)
    );
    audio.samples.push_back(static_cast<float>(sample) / 32768.0f);
  }
  return audio;
}

AudioPcm decode_audio_to_pcm(const Config &config, const std::string &audio_bytes) {
  auto input_path = write_temp_file(config, "input", audio_bytes);
  TempFile input_file(input_path);
  auto output_path = std::filesystem::path(config.tmp_dir) / ("pcm-" + request_id() + ".wav");
  TempFile output_file(output_path);

  const std::string command =
    config.ffmpeg_bin +
    " -hide_banner -loglevel error -y -i " + shell_quote(input_file.path()) +
    " -ar 16000 -ac 1 -c:a pcm_s16le " + shell_quote(output_file.path());

  const int rc = std::system(command.c_str());
  if (rc != 0) {
    throw std::runtime_error("ffmpeg audio conversion failed");
  }
  return read_wav_pcm16_mono_16k(output_file.path());
}

std::string transcribe(whisper_context *ctx, const Config &config, const AudioPcm &audio) {
  whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
  params.n_threads = config.threads;
  params.translate = config.translate;
  params.print_progress = false;
  params.print_realtime = false;
  params.print_timestamps = false;
  params.print_special = false;
  params.no_context = true;
  params.single_segment = false;
  if (config.language != "auto") {
    params.language = config.language.c_str();
  }

  std::lock_guard<std::mutex> lock(g_whisper_mutex);
  const int rc = whisper_full(ctx, params, audio.samples.data(), static_cast<int>(audio.samples.size()));
  if (rc != 0) {
    throw std::runtime_error("whisper inference failed");
  }

  std::string text;
  const int segments = whisper_full_n_segments(ctx);
  for (int i = 0; i < segments; ++i) {
    const char *segment = whisper_full_get_segment_text(ctx, i);
    if (segment) {
      text += segment;
    }
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

std::string lowercase_copy(const std::string &value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::string strip_configured_wake_phrase(
  const std::string &transcript,
  const std::string &wake_phrase,
  bool *detected
) {
  if (detected) {
    *detected = false;
  }
  const std::string trimmed = trim_copy(transcript);
  const std::string lowered = lowercase_copy(trimmed);
  const std::string lowered_wake = lowercase_copy(trim_copy(wake_phrase));
  if (lowered_wake.empty() || lowered.rfind(lowered_wake, 0) != 0) {
    return trimmed;
  }
  std::string rest = trimmed.substr(lowered_wake.size());
  while (!rest.empty() && (std::isspace(static_cast<unsigned char>(rest.front())) ||
                           rest.front() == ',' || rest.front() == '.' ||
                           rest.front() == ':' || rest.front() == ';' ||
                           rest.front() == '-')) {
    rest.erase(rest.begin());
  }
  if (detected) {
    *detected = true;
  }
  return trim_copy(rest);
}

std::string make_transcription_json(
  const std::string &id,
  const std::string &transcript,
  const std::string &language,
  int duration_ms,
  size_t input_bytes,
  bool wake_phrase_detected,
  const std::string &wake_phrase
) {
  std::ostringstream out;
  out << "{"
      << "\"transcript\":\"" << json_escape(transcript) << "\","
      << "\"text\":\"" << json_escape(transcript) << "\","
      << "\"language\":\"" << json_escape(language) << "\","
      << "\"asr_confidence\":0,"
      << "\"segments\":[],"
      << "\"wake_phrase_detected\":" << (wake_phrase_detected ? "true" : "false") << ","
      << "\"wake_phrase\":\"" << json_escape(wake_phrase) << "\","
      << "\"request_id\":\"" << json_escape(id) << "\","
      << "\"duration_ms\":" << duration_ms << ","
      << "\"input_bytes\":" << input_bytes
      << "}";
  return out.str();
}

}  // namespace

int main() {
  const Config config = load_config();
  if (config.model_path.empty()) {
    std::cerr << "WHISPER_MODEL_PATH is required\n";
    write_runtime_status(config, "failed", false);
    return 2;
  }

  whisper_context_params cparams = whisper_context_default_params();
  whisper_context *ctx = whisper_init_from_file_with_params(config.model_path.c_str(), cparams);
  if (!ctx) {
    std::cerr << "failed to load whisper model: " << config.model_path << "\n";
    write_runtime_status(config, "failed", false);
    return 2;
  }
  write_runtime_status(config, "running", true);

  httplib::Server server;
  server.set_payload_max_length(static_cast<size_t>(config.max_body_bytes));

  server.Get("/health", [&](const httplib::Request &, httplib::Response &res) {
    res.set_content(
      "{\"status\":\"ok\",\"engine\":\"whisper.cpp\",\"api_owner\":\"naim\",\"model_path\":\"" +
        json_escape(config.model_path) + "\"}",
      "application/json"
    );
  });

  const auto transcribe_handler = [&](const httplib::Request &req, httplib::Response &res) {
    const auto started = std::chrono::steady_clock::now();
    const std::string id = request_id();
    try {
      std::string audio_bytes = extract_request_audio(req);
      if (audio_bytes.empty()) {
        res.status = 400;
        res.set_content(make_json_error("empty audio payload"), "application/json");
        return;
      }

      AudioPcm audio = decode_audio_to_pcm(config, audio_bytes);
      std::fill(audio_bytes.begin(), audio_bytes.end(), '\0');
      const std::string raw_transcript = transcribe(ctx, config, audio);
      bool wake_phrase_detected = false;
      const std::string transcript =
        strip_configured_wake_phrase(raw_transcript, config.wake_phrase, &wake_phrase_detected);
      std::fill(audio.samples.begin(), audio.samples.end(), 0.0f);
      const auto finished = std::chrono::steady_clock::now();
      const int duration_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count()
      );
      res.set_content(
        make_transcription_json(
          id,
          transcript,
          config.language,
          duration_ms,
          req.body.size(),
          wake_phrase_detected,
          config.wake_phrase),
        "application/json"
      );
    } catch (const std::exception &error) {
      const auto finished = std::chrono::steady_clock::now();
      const int duration_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count()
      );
      std::cerr << "{\"event\":\"voice_listener.error\",\"request_id\":\"" << id
                << "\",\"duration_ms\":" << duration_ms
                << ",\"error\":\"" << json_escape(error.what()) << "\"}\n";
      res.status = 500;
      res.set_content(make_json_error(error.what()), "application/json");
    }
  };

  server.Post("/v1/transcribe", transcribe_handler);
  server.Post("/api/asr/transcribe", transcribe_handler);

  std::cout << "NAIM voice-module runtime listening on "
            << config.host << ":" << config.port << "\n";
  std::cout << "whisper.cpp model: " << config.model_path << "\n";
  const bool ok = server.listen(config.host, config.port);
  whisper_free(ctx);
  return ok ? 0 : 1;
}
