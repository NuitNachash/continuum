#pragma once
#include <string>

struct config {
    std::string rtmpUrl;
    int width;
    int height;
    int fps;
    int bitrate;
    int a_bitrate;
    int samplerate;
    std::string preset;
    std::string tune;
    std::string mp4Path;
};

config loadconfig(const std::string& filename);