# Build environment for the viogpu3d KMD. The toolchain (llvm-mingw) and the
# WDK/SDK NuGet packages are not baked in: build.sh downloads and verifies
# them on the host and mounts them read-only, so this image only carries the
# small tools the build scripts call.
FROM docker.io/library/debian@sha256:2c037a04925515fdd6ea85ea14a682d0e79931f5e9f5d07b6dbfc6ba12f9e858
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      python3=3.11.2-1+b1 xz-utils=5.4.1-1+deb12u2 patch=2.7.6-7 unzip=6.0-28+deb12u1 \
 && rm -rf /var/lib/apt/lists/*
