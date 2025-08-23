FROM docker.io/ubuntu:24.04
LABEL maintainer="DeftDawg <DeftDawg@gmail.com>"

# Disable interactive package configuration and add retry mechanism
RUN echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections && \
    echo 'Acquire::Retries "3";' > /etc/apt/apt.conf.d/80-retries && \
    echo 'Acquire::http::Timeout "30";' >> /etc/apt/apt.conf.d/80-retries && \
    echo 'Acquire::ftp::Timeout "30";' >> /etc/apt/apt.conf.d/80-retries

# Use stable mirrors for better network connectivity
RUN sed -i 's|http://archive.ubuntu.com/ubuntu|http://mirrors.aliyun.com/ubuntu|g' /etc/apt/sources.list && \
    sed -i 's|http://ports.ubuntu.com/ubuntu-ports|http://mirrors.aliyun.com/ubuntu|g' /etc/apt/sources.list && \
    echo "deb http://mirrors.aliyun.com/ubuntu/ $(cat /etc/*release | grep VERSION_CODENAME | cut -d= -f2)-updates main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb http://mirrors.aliyun.com/ubuntu/ $(cat /etc/*release | grep VERSION_CODENAME | cut -d= -f2)-security main restricted universe multiverse" >> /etc/apt/sources.list 

RUN for i in {1..3}; do apt-get update && break || sleep 15; done && \
    for i in {1..3}; do apt-get install -y --no-install-recommends \
    autoconf \
    build-essential \
    cmake \
    curl \
    eglexternalplatform-dev \
    extra-cmake-modules \
    file \
    git \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-libav \
    libcairo2-dev \
    libcurl4-openssl-dev \
    libdbus-1-dev \
    libglew-dev \
    libglu1-mesa-dev \
    libgstreamer1.0-dev \
    libgstreamerd-3-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    libgtk-3-dev \
    libsecret-1-dev \
    libsoup2.4-dev \
    libssl3 \
    libssl-dev \
    libtool \
    libudev-dev \
    libwayland-dev \
    libwebkit2gtk-4.1-dev \
    libxkbcommon-dev \
    locales \
    locales-all \
    m4 \
    ninja-build \
    pkgconf \
    sudo \
    wayland-protocols \
    wget && break || (sleep 15 && false); done

# Change your locale here if you want.  See the output
# of `locale -a` to pick the correct string formatting.
ENV LC_ALL=en_US.utf8
RUN locale-gen $LC_ALL

# Set this so that Orca Slicer doesn't complain about
# the CA cert path on every startup
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt

# Build optimization environment variables
ENV CMAKE_BUILD_PARALLEL_LEVEL=4
ENV MAKEFLAGS="-j4"
ENV NINJA_STATUS="[%f/%t] %e "

COPY ./ OrcaSlicer

WORKDIR OrcaSlicer

# These can run together, but we run them seperate for podman caching
# Update System dependencies
RUN ./build_linux.sh -u

# Build dependencies in ./deps with retry mechanism
RUN for i in {1..3}; do \
        echo "Attempt $i to build dependencies..." && \
        ./build_linux.sh -dr && break || \
        (echo "Build attempt $i failed, cleaning and retrying..." && \
         rm -rf deps/build && \
         sleep 30); \
    done

# Build slic3r
RUN ./build_linux.sh -sr

# Build AppImage
ENV container=podman
RUN ./build_linux.sh -ir

# It's easier to run Orca Slicer as the same username,
# UID and GID as your workstation.  Since we bind mount
# your home directory into the container, it's handy
# to keep permissions the same.  Just in case, defaults
# are root.
SHELL ["/bin/bash", "-l", "-c"]
ARG USER=root
ARG UID=0
ARG GID=0
RUN if [[ "$UID" != "0" ]]; then \
      # Create group if it doesn't exist \
      groupadd -f -g $GID $USER; \
      # Check if user with this UID already exists \
      if getent passwd $UID > /dev/null 2>&1; then \
        echo "User with UID $UID already exists, skipping user creation"; \
      else \
        useradd -u $UID -g $GID $USER; \
      fi \
    fi
# Using an entrypoint instead of CMD because the binary
# accepts several command line arguments.
ENTRYPOINT ["/OrcaSlicer/build/package/bin/orca-slicer"]
