FROM comet/base-runtime:dev

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
      curl \
    && rm -rf /var/lib/apt/lists/*

COPY runtime/browsing /runtime/browsing
COPY build/linux/x64/comet-browsingd /runtime/bin/comet-browsingd

RUN chmod +x /runtime/browsing/entrypoint.sh /runtime/bin/comet-browsingd

WORKDIR /runtime
