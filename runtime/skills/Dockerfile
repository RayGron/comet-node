FROM comet/base-runtime:dev

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
    && rm -rf /var/lib/apt/lists/*

COPY runtime/skills /runtime/skills
COPY build/linux/x64/comet-skillsd /runtime/bin/comet-skillsd

RUN chmod +x /runtime/skills/entrypoint.sh /runtime/bin/comet-skillsd

WORKDIR /runtime
