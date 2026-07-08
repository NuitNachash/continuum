#!/bin/bash
set -e

echo "Installing build dependencies for Continuum..."

if command -v apt-get &>/dev/null; then
    sudo apt-get update
    sudo apt-get install -y \
        build-essential cmake pkg-config \
        libavformat-dev libavcodec-dev libavutil-dev \
        libswscale-dev libswresample-dev \
        ffmpeg
elif command -v dnf &>/dev/null; then
    sudo dnf install -y \
        gcc-c++ cmake pkgconfig \
        ffmpeg-devel ffmpeg
else
    echo "Unsupported package manager. Install FFmpeg dev libraries (libavformat, libavcodec, libavutil, libswscale, libswresample) and cmake/pkg-config manually."
    exit 1
fi

echo "Dependencies installed."