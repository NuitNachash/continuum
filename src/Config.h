#pragma once
#include <string>

// Stores all runtime settings loaded from the application's
// configuration file
struct config {
    // RTMP destination for the live stream
    std::string rtmpUrl;

    // Video output settings
    int width;
    int height;
    int fps;
    int bitrate;

    // Audio encoding settings
    int a_bitrate;
    int samplerate;

    // Optional encoder tuning parameters
    std::string preset;
    std::string tune;

    // Path to the input file
    std::string mp4Path;
};

config loadconfig(const std::string& filename);