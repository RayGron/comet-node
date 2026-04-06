FROM comet/base-runtime:dev

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash \
      curl \
    && rm -rf /var/lib/apt/lists/*

COPY runtime/browsing /runtime/browsing
COPY build/linux/x64/comet-browsingd /runtime/bin/comet-browsingd
COPY build/linux/x64/libcef.so /runtime/bin/libcef.so
COPY build/linux/x64/chrome_100_percent.pak /runtime/bin/chrome_100_percent.pak
COPY build/linux/x64/chrome_200_percent.pak /runtime/bin/chrome_200_percent.pak
COPY build/linux/x64/icudtl.dat /runtime/bin/icudtl.dat
COPY build/linux/x64/resources.pak /runtime/bin/resources.pak
COPY build/linux/x64/v8_context_snapshot.bin /runtime/bin/v8_context_snapshot.bin
COPY build/linux/x64/locales /runtime/bin/locales

RUN chmod +x /runtime/browsing/entrypoint.sh /runtime/bin/comet-browsingd

WORKDIR /runtime
