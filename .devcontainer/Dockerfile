FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

USER root

# Everything needed to compile DPF plugins (CLAP + VST3) headlessly. The X11/GL
# packages are build-time headers, not a running display: DPF links its NanoVG UI
# against them even when the container never shows a window.
RUN set -eux; \
    find /etc/apt/sources.list.d -name '*.sources' -exec sed -i 's|http://archive.ubuntu.com/ubuntu|http://azure.archive.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|http://azure.archive.ubuntu.com/ubuntu|g' {} +; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      curl \
      file \
      git \
      gnupg \
      jq \
      libasound2-dev \
      libgl1-mesa-dev \
      libjack-jackd2-dev \
      libx11-dev \
      libxcursor-dev \
      libxext-dev \
      libxrandr-dev \
      ninja-build \
      pkg-config \
      ripgrep \
      tmux \
      unzip \
      zip; \
    rm -rf /var/lib/apt/lists/*
