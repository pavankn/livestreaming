# Stage 1: Build FFmpeg
FROM ubuntu:22.04 AS builder
ARG FFMPEG_VERSION=8.0
RUN apt-get update && apt-get install -y \
    build-essential yasm nasm pkg-config wget tar zlib1g-dev \
    libx264-dev libx265-dev libmp3lame-dev libopus-dev libssl-dev librtmp-dev cmake \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*
RUN wget --no-check-certificate https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2 \
    && tar -xjf ffmpeg-${FFMPEG_VERSION}.tar.bz2 \
    && cd ffmpeg-${FFMPEG_VERSION} && \
       ./configure --prefix=/usr/local --enable-shared --enable-nonfree \
                   --enable-gpl --enable-libx264 --enable-libx265 --enable-librtmp \
                   --enable-libmp3lame --enable-libopus \
                   --disable-ffplay --disable-ffprobe --disable-doc && \
       make -j$(nproc) && make install

# Stage 2: Final runtime image
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    libx264-dev libx265-dev libmp3lame-dev libopus-dev libssl-dev librtmp-dev \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*
COPY --from=builder /usr/local /usr/local

RUN apt-get update && apt-get install -y pkg-config --no-install-recommends && \
    rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y iputils-ping

ARG CMAKE_VERSION=3.30.2
RUN apt-get update && apt-get install -y wget tar build-essential libssl-dev --no-install-recommends && \
    wget --no-check-certificate https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz && \
    tar -xzf cmake-${CMAKE_VERSION}.tar.gz && \
    cd cmake-${CMAKE_VERSION} && ./bootstrap && make -j$(nproc) && make install && \
    cd .. && rm -rf cmake-${CMAKE_VERSION} cmake-${CMAKE_VERSION}.tar.gz && \
    rm -rf /var/lib/apt/lists/*

# Add application
WORKDIR /giramelle
COPY CMakeLists.txt streams.csv Giramelle.cpp startStream.sh ./
COPY giramelle_4Mbps.mp4 ./
COPY TV_Giramelle_Vertical_4Mbps.mp4 ./
COPY TV_Giramelle_Vertical_Eng_4Mbps.mp4 ./
COPY TemChule_4Mbps.mp4 ./

# RUN cmake -S . -B build && cmake --build build   # Uncomment to build


# Add user and fix permissions
ARG USERNAME=ffmpeg_user
RUN useradd -m -s /bin/bash $USERNAME && \
    chown -R $USERNAME:$USERNAME /giramelle

USER $USERNAME
