ARG BASE_IMAGE=nvidia/cuda:12.8.1-runtime-ubuntu24.04
FROM ${BASE_IMAGE}

ENV HOST=0.0.0.0 \
    PORT=18150 \
    NAIM_VOICE_MAKER_TMP_DIR=/tmp/naim-voice-maker \
    NAIM_VOICE_MAKER_FAKE=0 \
    OMNIVOICE_MODEL_PATH=/models/omnivoice \
    OMNIVOICE_LANGUAGE=auto \
    OMNIVOICE_VOICE_MODE=design \
    OMNIVOICE_INSTRUCT="neutral, clear voice, medium pitch, calm style" \
    OMNIVOICE_OUTPUT_FORMAT=wav

RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates curl ffmpeg git libsndfile1 python3 python3-pip \
  && rm -rf /var/lib/apt/lists/* \
  && mkdir -p /models /tmp/naim-voice-maker /runtime/voice-maker \
  && python3 -m pip install --break-system-packages --no-cache-dir \
    torch==2.8.0+cu128 \
    torchaudio==2.8.0+cu128 \
    --extra-index-url https://download.pytorch.org/whl/cu128 \
  && python3 -m pip install --break-system-packages --no-cache-dir \
    numpy soundfile git+https://github.com/k2-fsa/OmniVoice.git

COPY runtime/voice-maker/server.py /runtime/voice-maker/server.py

EXPOSE 18150

CMD ["python3", "/runtime/voice-maker/server.py"]
