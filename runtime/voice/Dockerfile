ARG BASE_IMAGE=nvidia/cuda:13.0.1-runtime-ubuntu24.04
FROM ${BASE_IMAGE}

ENV HOST=0.0.0.0 \
    PORT=18140 \
    NAIM_VOICE_MODULE_TMP_DIR=/tmp/naim-voice-module \
    VOICE_ASR_TMP_DIR=/tmp/naim-voice-module \
    VOICE_ASR_FFMPEG_BIN=ffmpeg \
    VOICE_ASR_LANGUAGE=auto \
    VOICE_BODY_LIMIT_BYTES=16777216 \
    VOICE_LISTENER_WAKE_PHRASE="Hey Jex"

RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates curl ffmpeg libgomp1 \
  && rm -rf /var/lib/apt/lists/* \
  && mkdir -p /models /tmp/naim-voice-module /runtime/bin

COPY build/linux/x64/naim-voice-moduled /runtime/bin/naim-voice-moduled

EXPOSE 18140

CMD ["/runtime/bin/naim-voice-moduled"]
