#!/usr/bin/env python3
import base64
import io
import json
import math
import os
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


HOST = os.environ.get("HOST", "0.0.0.0")
PORT = int(os.environ.get("PORT", os.environ.get("NAIM_VOICE_MAKER_PORT", "18150")))
MODEL_PATH = os.environ.get("OMNIVOICE_MODEL_PATH", "/models/omnivoice")
DEFAULT_LANGUAGE = os.environ.get("OMNIVOICE_LANGUAGE", "auto")
DEFAULT_VOICE_MODE = os.environ.get("OMNIVOICE_VOICE_MODE", "design")
DEFAULT_INSTRUCT = os.environ.get(
    "OMNIVOICE_INSTRUCT", "neutral, clear voice, medium pitch, calm style"
)
DEFAULT_FORMAT = os.environ.get("OMNIVOICE_OUTPUT_FORMAT", "wav")
FAKE_MODE = os.environ.get("NAIM_VOICE_MAKER_FAKE", "0") == "1"
STATUS_PATH = os.environ.get(
    "NAIM_VOICE_MAKER_RUNTIME_STATUS_PATH",
    "/tmp/naim-voice-maker-runtime-status.json",
)

_model = None
_model_error = None


def _write_status(status, ready, detail=""):
    payload = {
        "status": status,
        "ready": ready,
        "engine": "omnivoice",
        "model_path": MODEL_PATH,
        "fake": FAKE_MODE,
        "detail": detail,
        "updated_at_ms": int(time.time() * 1000),
    }
    try:
        os.makedirs(os.path.dirname(STATUS_PATH), exist_ok=True)
        with open(STATUS_PATH, "w", encoding="utf-8") as output:
            json.dump(payload, output)
    except OSError:
        pass


def _json_response(handler, status, payload):
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def _wav_bytes(samples, sample_rate):
    pcm = bytearray()
    for sample in samples:
        clipped = max(-1.0, min(1.0, float(sample)))
        pcm.extend(int(clipped * 32767.0).to_bytes(2, "little", signed=True))
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(bytes(pcm))
    return output.getvalue()


def _fake_audio(text):
    sample_rate = 24000
    duration = min(3.0, max(0.4, len(text) / 45.0))
    count = int(sample_rate * duration)
    freq = 220.0
    return [
        0.15 * math.sin(2.0 * math.pi * freq * index / sample_rate)
        for index in range(count)
    ], sample_rate


def _load_model():
    global _model, _model_error
    if FAKE_MODE:
        return None
    if _model is not None:
        return _model
    if not MODEL_PATH or not os.path.exists(MODEL_PATH):
        _model_error = "OMNIVOICE_MODEL_PATH does not exist: " + MODEL_PATH
        raise RuntimeError(_model_error)
    try:
        import torch
        from omnivoice import OmniVoice

        device = "cuda:0" if torch.cuda.is_available() else "cpu"
        dtype = torch.float16 if device.startswith("cuda") else torch.float32
        _model = OmniVoice.from_pretrained(MODEL_PATH, device_map=device, dtype=dtype)
        _model_error = None
        return _model
    except Exception as error:
        _model_error = str(error)
        raise


def _synthesize(payload):
    text = str(payload.get("text", "")).strip()
    if not text:
        raise ValueError("text is required")
    output_format = str(payload.get("format") or DEFAULT_FORMAT).lower()
    if output_format != "wav":
        raise ValueError("only wav output is supported in this runtime contract")

    started = time.monotonic()
    if FAKE_MODE:
        samples, sample_rate = _fake_audio(text)
    else:
        model = _load_model()
        kwargs = {"text": text}
        voice = payload.get("voice") if isinstance(payload.get("voice"), dict) else {}
        ref_audio = voice.get("reference_audio_path") or payload.get("ref_audio")
        ref_text = voice.get("reference_text") or payload.get("ref_text")
        instruct = voice.get("instruct") or payload.get("instruct") or DEFAULT_INSTRUCT
        if ref_audio:
            kwargs["ref_audio"] = ref_audio
            if ref_text:
                kwargs["ref_text"] = ref_text
        elif DEFAULT_VOICE_MODE == "design" and instruct:
            kwargs["instruct"] = instruct
        for key in ("speed", "duration", "num_step", "language"):
            if key in payload:
                kwargs[key] = payload[key]
        generated = model.generate(**kwargs)
        samples = generated[0]
        sample_rate = 24000

    audio = _wav_bytes(samples, sample_rate)
    duration_ms = int((time.monotonic() - started) * 1000)
    return {
        "status": "ok",
        "format": "wav",
        "sample_rate": sample_rate,
        "audio": {"encoding": "base64", "content_base64": base64.b64encode(audio).decode("ascii")},
        "metrics": {
            "duration_ms": duration_ms,
            "input_chars": len(text),
            "audio_bytes": len(audio),
        },
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "naim-voice-maker/0.1"

    def do_GET(self):
        if self.path.split("?", 1)[0] == "/health":
            ready = FAKE_MODE or (os.path.exists(MODEL_PATH) and _model_error is None)
            _json_response(
                self,
                200,
                {
                    "status": "ok",
                    "engine": "omnivoice",
                    "model_path": MODEL_PATH,
                    "ready": ready,
                    "fake": FAKE_MODE,
                },
            )
            return
        if self.path.split("?", 1)[0] == "/v1/status":
            _json_response(
                self,
                200,
                {
                    "status": "ok",
                    "engine": "omnivoice",
                    "model_path": MODEL_PATH,
                    "loaded": _model is not None,
                    "fake": FAKE_MODE,
                    "error": _model_error,
                    "language": DEFAULT_LANGUAGE,
                    "voice_mode": DEFAULT_VOICE_MODE,
                    "format": DEFAULT_FORMAT,
                },
            )
            return
        _json_response(self, 404, {"status": "error", "message": "not found"})

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/v1/synthesize":
            _json_response(self, 404, {"status": "error", "message": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
            _json_response(self, 200, _synthesize(payload))
        except ValueError as error:
            _json_response(self, 400, {"status": "error", "message": str(error)})
        except Exception as error:
            _write_status("failed", False, str(error))
            _json_response(self, 503, {"status": "error", "message": str(error)})

    def log_message(self, fmt, *args):
        print("voice-maker " + fmt % args, flush=True)


if __name__ == "__main__":
    _write_status("running", FAKE_MODE or os.path.exists(MODEL_PATH))
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
