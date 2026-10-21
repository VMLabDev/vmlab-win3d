# Build container for the two host release assets, the vmlab QEMU
# (patches/host/build-qemu.sh) and the vmlab render server
# (patches/host/build-virglrenderer.sh). Both scripts run themselves inside
# this image; docs/release.md, "Supported hosts of the host assets", says why.
#
# Debian 12 (bookworm) is the oldest glibc (2.36) the assets are built against,
# so they run on every distribution with glibc 2.36 or newer. The base image
# is pinned by digest on the debian-bookworm line of patches/host/PINS and
# passed in as BASE; the packages are bookworm's current point release, and
# dpkg-query lists them in /opt/apt-packages.txt for BUILDINFO.
#
# QEMU master's configure needs Python 3.12 and bookworm has 3.11, so the
# image adds a python-build-standalone 3.12 (the python line of PINS, passed
# in as PYTHON_URL and PYTHON_SHA256) under /opt/python. It is a build tool
# only: nothing built here links it. virglrenderer's meson and PyYAML come
# from the wheels PINS names, installed into a venv by the script.
#
# The image tag is vmlab-win3d/host-build:<12 hex of sha256 over this file
# and the PINS lines it uses>; the build scripts compute it and build the
# image when it is missing.
ARG BASE=debian:bookworm
FROM ${BASE}
ARG PYTHON_URL
ARG PYTHON_SHA256
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 PATH=/opt/python/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    # toolchain
    build-essential binutils git ninja-build pkg-config patchelf curl ca-certificates \
    # QEMU: glib, pixman, epoxy and gbm (OpenGL / egl-headless), zlib (qcow2),
    # zstd (qcow2 zstd clusters), libusb (usb-host passthrough)
    libglib2.0-dev libpixman-1-dev libepoxy-dev libgbm-dev zlib1g-dev \
    libzstd-dev libusb-1.0-0-dev \
    # virglrenderer: libdrm (gbm platform), Vulkan headers (Venus; the
    # loader is dlopened at run time)
    libdrm-dev libvulkan-dev \
 && rm -rf /var/lib/apt/lists/* \
 && dpkg-query -W -f='${Package} ${Version}\n' >/opt/apt-packages.txt \
 && curl -fsSL -o /tmp/python.tar.gz "$PYTHON_URL" \
 && echo "$PYTHON_SHA256  /tmp/python.tar.gz" | sha256sum -c - \
 && mkdir -p /opt && tar -C /opt -xzf /tmp/python.tar.gz && rm /tmp/python.tar.gz \
 && python3 --version
