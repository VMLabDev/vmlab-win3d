# Shared cross toolchain for vmlab-win3d Windows builds.
# llvm-mingw (ucrt) for x86_64 and i686 Windows, Meson, CMake, Ninja and the
# Python modules the builds need. Every input is pinned; see PINS.
FROM ubuntu:24.04@sha256:ac3a3d13e90c660a1cb3e89ec695533399f7378dc1ae5fbdc28eb5781d561594

# Ubuntu archive snapshot: fixes the apt package set to one point in time.
# The snapshot service is HTTPS-only and the base image has no CA bundle, so
# ca-certificates is fetched first without peer verification. apt still checks
# every index and package against the archive's GPG signatures.
ARG UBUNTU_SNAPSHOT=20261010T000000Z
RUN sed -i \
      -e "s|http://archive.ubuntu.com/ubuntu/|https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/|" \
      -e "s|http://security.ubuntu.com/ubuntu/|https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT}/|" \
      /etc/apt/sources.list.d/ubuntu.sources \
 && apt-get -o Acquire::https::Verify-Peer=false update \
 && DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::https::Verify-Peer=false \
      install -y --no-install-recommends ca-certificates \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bison cmake curl file flex git glslang-tools make \
      ninja-build patch pkg-config python3 python3-venv unzip xz-utils zstd \
 && dpkg-query -W -f '${Package}=${Version}\n' > /opt/apt-packages.txt \
 && rm -rf /var/lib/apt/lists/*

ARG LLVM_MINGW=llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
ADD --checksum=sha256:5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4 \
    https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/${LLVM_MINGW}.tar.xz \
    /tmp/llvm-mingw.tar.xz
RUN tar -C /opt -xf /tmp/llvm-mingw.tar.xz \
 && mv /opt/${LLVM_MINGW} /opt/llvm-mingw \
 && rm /tmp/llvm-mingw.tar.xz

COPY requirements.txt /tmp/requirements.txt
RUN python3 -m venv /opt/py \
 && /opt/py/bin/pip install --no-cache-dir --no-deps --require-hashes -r /tmp/requirements.txt \
 && rm /tmp/requirements.txt

COPY cross/ /opt/cross/

ENV PATH=/opt/py/bin:/opt/llvm-mingw/bin:$PATH \
    SOURCE_DATE_EPOCH=0 \
    LC_ALL=C.UTF-8 \
    TZ=UTC
# Builds run as the caller's uid with no home directory of their own.
ENV HOME=/tmp
RUN git config --system --add safe.directory '*'
