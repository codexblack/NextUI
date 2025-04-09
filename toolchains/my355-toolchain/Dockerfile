FROM debian:buster-slim
ENV DEBIAN_FRONTEND=noninteractive

ENV TZ=America/New_York
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get -y update && apt-get -y install \
    bc \
    build-essential \
    bzip2 \
    bzr \
    cmake \
    cmake-curses-gui \
    cpio \
    git \
    libncurses5-dev \
    libsdl1.2-dev \
    libsdl-image1.2-dev \
    libsdl-ttf2.0-dev \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-ttf-dev \
    locales \
    make \
    rsync \
    scons \
    tree \
    unzip \
    wget \
    libsamplerate-dev \
    libsqlite3-dev \
    libjansson-dev \
    libfuzzy-dev \
    python3 \
    python3-pip \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    libbluetooth-dev \
  && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /root/workspace
WORKDIR /root

COPY support .
RUN cat setup-env.sh >> .bashrc

VOLUME /root/workspace
WORKDIR /root/workspace

CMD ["/bin/bash"]
