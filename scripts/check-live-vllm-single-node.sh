#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

plane_name="${1:-qwen35-9b-min}"
controller_url="${COMET_CONTROLLER_URL:-http://127.0.0.1:18080}"
web_ui_url="${COMET_WEB_UI_URL:-http://127.0.0.1:18081}"

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

status_json="${tmp_dir}/status.json"
models_json="${tmp_dir}/models.json"
chat_request_json="${tmp_dir}/chat-request.json"
chat_response_json="${tmp_dir}/chat-response.json"
stream_request_json="${tmp_dir}/stream-request.json"
stream_response.txt="${tmp_dir}/stream-response.txt"
long_request_json="${tmp_dir}/long-request.json"
long_response_json="${tmp_dir}/long-response.json"

echo "[check-live-vllm] ensuring plane ${plane_name} is running"
"${repo_root}/scripts/run-plane.sh" "${plane_name}" >/dev/null

echo "[check-live-vllm] checking interaction status"
curl -fsS "${controller_url}/api/v1/planes/${plane_name}/interaction/status" >"${status_json}"
python3 - <<'PY' "${status_json}"
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text())
assert payload.get("ready") is True, payload
runtime_status = payload.get("runtime_status") or {}
assert runtime_status.get("runtime_backend") == "worker-vllm", runtime_status
print("status=ok")
PY

echo "[check-live-vllm] checking model listing"
curl -fsS "${controller_url}/api/v1/planes/${plane_name}/interaction/models" >"${models_json}"
python3 - <<'PY' "${models_json}"
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text())
models = payload.get("data") or []
assert models, payload
print("models=" + ",".join(item.get("id", "") for item in models))
PY

echo "[check-live-vllm] checking basic chat completion"
python3 - <<'PY' "${chat_request_json}"
import json
import pathlib
import sys

pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "model": "qwen3.5-9b",
    "messages": [{"role": "user", "content": "Reply with PONG only."}],
    "max_tokens": 16,
}))
PY
curl -fsS \
  -H 'Content-Type: application/json' \
  --data-binary "@${chat_request_json}" \
  "${controller_url}/api/v1/planes/${plane_name}/interaction/chat/completions" \
  >"${chat_response_json}"
python3 - <<'PY' "${chat_response_json}"
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text())
content = payload["choices"][0]["message"]["content"].strip()
assert content == "PONG", content
print("chat=PONG")
PY

echo "[check-live-vllm] checking streaming chat completion"
python3 - <<'PY' "${stream_request_json}"
import json
import pathlib
import sys

pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "model": "qwen3.5-9b",
    "messages": [{"role": "user", "content": "Reply with PONG only."}],
    "max_tokens": 16,
    "stream": True,
}))
PY
curl -sN --max-time 20 \
  -H 'Content-Type: application/json' \
  --data-binary "@${stream_request_json}" \
  "${controller_url}/api/v1/planes/${plane_name}/interaction/chat/completions/stream" \
  >"${stream_response.txt}"
python3 - <<'PY' "${stream_response.txt}"
import json
import pathlib
import re
import sys

text = pathlib.Path(sys.argv[1]).read_text()
assert "event: complete" in text, text
assert "data: [DONE]" in text, text
deltas = []
for match in re.finditer(r"event: delta\ndata: (.+)", text):
    payload = json.loads(match.group(1))
    deltas.append(payload.get("delta", ""))
joined = "".join(deltas).strip()
assert joined == "PONG", joined
print("stream=PONG")
PY

echo "[check-live-vllm] checking long-form semantic completion"
python3 - <<'PY' "${long_request_json}"
import json
import pathlib
import sys

pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "model": "qwen3.5-9b",
    "messages": [{
        "role": "user",
        "content": (
            "Write a detailed guide in English about how to design and operate a single-node GPU "
            "inference service for LLMs. The answer must be around 1200 words, structured with "
            "sections and checklists. Split across multiple messages if needed, but finish the "
            "full artifact completely."
        ),
    }],
}))
PY
curl -fsS \
  -H 'Content-Type: application/json' \
  --data-binary "@${long_request_json}" \
  "${controller_url}/api/v1/planes/${plane_name}/interaction/chat/completions" \
  >"${long_response_json}"
python3 - <<'PY' "${long_response_json}"
import json
import pathlib
import re
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text())
session = payload["session"]
text = payload["choices"][0]["message"]["content"]
words = re.findall(r"\b\w+\b", text)
assert len(words) >= 900, len(words)
assert session["segment_count"] >= 2, session
assert session["continuation_count"] >= 1, session
assert session["stop_reason"] == "semantic_completion_marker", session
assert session["marker_seen"] is True, session
print(
    "long_form="
    + f"words={len(words)} segments={session['segment_count']} continuations={session['continuation_count']}"
)
PY

echo "[check-live-vllm] checking web UI health"
curl -fsS -o /dev/null "${web_ui_url}/"
echo "[check-live-vllm] web_ui=ok"

echo "[check-live-vllm] success"
