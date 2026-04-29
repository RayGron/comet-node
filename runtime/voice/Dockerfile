FROM debian:bookworm-slim AS build

ARG WHISPER_CPP_REF=v1.8.1
ARG CPP_HTTPLIB_REF=v0.26.0

RUN apt-get update \
  && apt-get install -y --no-install-recommends ca-certificates cmake curl g++ git make pkg-config \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN git clone --depth 1 --branch "${WHISPER_CPP_REF}" https://github.com/ggml-org/whisper.cpp.git third_party/whisper.cpp
RUN mkdir -p third_party/cpp-httplib \
  && curl -fsSL "https://raw.githubusercontent.com/yhirose/cpp-httplib/${CPP_HTTPLIB_REF}/httplib.h" \
    -o third_party/cpp-httplib/httplib.h

COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  && cmake --build build -j

FROM debian:bookworm-slim

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

COPY --from=build /src/build/naim-voice-moduled /runtime/bin/naim-voice-moduled

EXPOSE 18140

CMD ["/runtime/bin/naim-voice-moduled"]
