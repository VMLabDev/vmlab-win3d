# Signing tools: openssl, osslsigncode, and LINBIT's generate-cat-file built
# from the commit pinned in PINS (its source is mounted at build time by
# docker build's context, see tools.sh).
FROM docker.io/library/debian@sha256:2c037a04925515fdd6ea85ea14a682d0e79931f5e9f5d07b6dbfc6ba12f9e858
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      osslsigncode=2.5-4 openssl=3.0.22-1~deb12u1 \
      gcc=4:12.2.0-3 make=4.3-4.1 libc6-dev=2.36-9+deb12u14 \
 && rm -rf /var/lib/apt/lists/*
COPY generate-cat-file /opt/generate-cat-file
RUN make -C /opt/generate-cat-file generate-cat-file strip-pe-image CFLAGS="-O2 -Wall"
ENV PATH=/opt/generate-cat-file:$PATH
