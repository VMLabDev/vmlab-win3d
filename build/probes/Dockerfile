# Build image for the vmlab-win3d test probes. Every download is pinned in
# PINS and checked by sha256 here; build.sh passes the values in.
ARG UBUNTU_DIGEST
FROM ubuntu@${UBUNTU_DIGEST}

ARG LLVM_MINGW_URL
ARG LLVM_MINGW_SHA256
ARG VULKAN_HEADERS_URL
ARG VULKAN_HEADERS_SHA256
ARG GLSLANG_URL
ARG GLSLANG_SHA256

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates curl xz-utils make python3 \
 && rm -rf /var/lib/apt/lists/*

RUN set -eu; cd /tmp; \
    curl -fsSL -o llvm-mingw.tar.xz "$LLVM_MINGW_URL"; \
    echo "$LLVM_MINGW_SHA256  llvm-mingw.tar.xz" | sha256sum -c -; \
    mkdir /opt/llvm-mingw; tar -xJf llvm-mingw.tar.xz -C /opt/llvm-mingw --strip-components=1; \
    curl -fsSL -o vulkan-headers.tar.gz "$VULKAN_HEADERS_URL"; \
    echo "$VULKAN_HEADERS_SHA256  vulkan-headers.tar.gz" | sha256sum -c -; \
    mkdir /opt/vulkan-headers; tar -xzf vulkan-headers.tar.gz -C /opt/vulkan-headers --strip-components=1; \
    curl -fsSL -o glslang.tar.gz "$GLSLANG_URL"; \
    echo "$GLSLANG_SHA256  glslang.tar.gz" | sha256sum -c -; \
    mkdir /opt/glslang; tar -xzf glslang.tar.gz -C /opt/glslang; \
    rm -f /tmp/*.tar.*

ENV PATH=/opt/llvm-mingw/bin:/opt/glslang/bin:$PATH
