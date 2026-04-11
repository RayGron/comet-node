FROM ubuntu:24.04

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
      ca-certificates \
      docker.io \
      tini \
    && rm -rf /var/lib/apt/lists/*

COPY build/linux/x64/naim-hostd /runtime/bin/naim-hostd

RUN chmod +x /runtime/bin/naim-hostd \
    && mkdir -p /naim/state /naim/runtime /naim/storage

WORKDIR /naim

ENTRYPOINT ["/usr/bin/tini", "--", "/runtime/bin/naim-hostd"]
