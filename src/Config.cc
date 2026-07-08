#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

config loadconfig(const std::string& filename) {
    config cfg;

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file\n";
        return cfg;
    }

    std::string line;

    while (std::getline(file, line)) {

        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream ss(line);
        std::string key, value;

        if (std::getline(ss, key, '=') &&
            std::getline(ss, value)) {

            if (key == "width") cfg.width = std::stoi(value);
            else if (key == "height") cfg.height = std::stoi(value);
            else if (key == "fps") cfg.fps = std::stoi(value);
            else if (key == "bitrate") cfg.bitrate = std::stoi(value);
            else if (key == "rtmpUrl") cfg.rtmpUrl = value;
            else if (key == "mp4Path") cfg.mp4Path = value;
            else if (key == "a_bitrate") cfg.a_bitrate = std::stoi(value);
            else if (key == "samplerate") cfg.samplerate = std::stoi(value);            
        }
    }

    return cfg;
}