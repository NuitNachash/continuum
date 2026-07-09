#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "Config.h"
#include "ContinuumEngine.h"

std::string getDefaultConfigDir() {
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdgConfig ? xdgConfig : std::string(std::getenv("HOME")) + "/.config";
    return base + "/continuum/";
}

std::vector<std::string> loadPlaylistFile(const std::string& path) {
    std::vector<std::string> paths;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] != '#')
            paths.push_back(line);
    }
    return paths;
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::ofstream logFile;

void log(const std::string& msg) {
    std::string line = "\n[" + timestamp() + "] " + msg;
    std::cout << line << "\n";
    if (logFile.is_open()) {
        logFile << line << "\n";
        logFile.flush();
    }
}

void printUsage() {
    std::cout << 
        "Usage: continuum [options]\n\n"
        "Required (one of):\n"
        "  --playlist <path>       Playlist file, one media path per line\n"
        "  --media <path>          Single media file (use with --once for testing)\n\n"
        "Options:\n"
        "  --config <path>         Config file (default: config.ini)\n"
        "  --output <url>          RTMP output URL (overrides config file)\n"
        "  --once                  Play a single file and exit (requires --media)\n"
        "  --control-file <path>   Control command file (default: control.txt)\n"
        "  --add-file <path>       Dynamic playlist-add file (default: add_video.txt)\n"
        "  --status-file <path>    Status output file (default: status.json)\n"
        "  --status-interval <n>   Seconds between status writes (default: 5)\n"
        "  --help                  Show this message\n";
}

int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_ERROR);
    logFile.open(getDefaultConfigDir() + "continuum.log", std::ios::app);

    std::string configPath = getDefaultConfigDir() + "config.ini";
    std::string playlistPath;
    std::string mediaPath;
    std::string outputUrl;
    std::string controlFile = getDefaultConfigDir() + "control.txt";
    std::string addFile = getDefaultConfigDir() + "add_video.txt";
    std::string statusFile = getDefaultConfigDir() + "status.json";
    int statusInterval = 5;
    bool onceMode = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
            configPath = argv[++i];
        else if (arg == "--playlist" && i + 1 < argc)
            playlistPath = argv[++i];
        else if (arg == "--media" && i + 1 < argc)
           mediaPath = argv[++i];
        else if (arg == "--output" && i + 1 < argc)
            outputUrl = argv[++i];
        else if (arg == "--once")
            onceMode = true;
        else if (arg == "--control-file" && i + 1 < argc)
            controlFile = argv[++i];
        else if (arg == "--add-file" && i + 1 < argc)
            addFile = argv[++i];
        else if (arg == "--status-file" && i + 1 < argc)
            statusFile = argv[++i];
        else if (arg == "--status-interval" && i + 1 < argc)
            statusInterval = std::stoi(argv[++i]);
        else if (arg == "--help") {
            printUsage();
            return 0;
        }
        else {
        log(std::string("Unknown argument: ") + arg); 
        printUsage();
        return 1;
        }
    }

    std::remove(controlFile.c_str());
    std::remove(addFile.c_str());
    std::remove(statusFile.c_str());

    if (onceMode && mediaPath.empty()) {
        log("Error: --once requires --media <path>");
        return 1;
    }

    if (playlistPath.empty() && mediaPath.empty()){
        log("Error: Provide --playlist or --media");
        printUsage();
        return 1;
    }

    try {
        config cfg = loadconfig(configPath);
        if (!outputUrl.empty())
            cfg.rtmpUrl = outputUrl;

        std::vector<std::string> paths;
        if (!mediaPath.empty()) {
            paths.push_back(mediaPath);
        } else {
            paths = loadPlaylistFile(playlistPath);
        }
        if (paths.empty()) {
            log("Error: Playlist is empty");
            return 1;
        }
        cfg.mp4Path = paths[0];   // first file for construction

        ContinuumEngine engine(cfg);
        engine.setOnceMode(onceMode);
        engine.loadPlaylist(paths);

{
        EngineStatus s = engine.getStatus();
        std::string tmpFile = statusFile + ".tmp";
        {
            std::ofstream sf(tmpFile);
            sf << "{ \"current_path\": \"" << s.current_path << "\", "
                << "\"video_pts\": " << s.video_pts << ", "
                << "\"audio_pts\": " << s.audio_pts << ", "
                << "\"paused\": " << (s.paused ? "true" : "false") << ", "
                << "\"running\": " << (s.running ? "true" : "false") << ", "
                << "\"video_pts_since_switch\": " << s.video_pts_since_switch << ", "
                << "\"current_duration\": " << s.current_duration << " }";
        }
        std::rename(tmpFile.c_str(), statusFile.c_str());
}

        std::thread engineThread([&]() {
            engine.start();
        });
        
        auto last_status_write = std::chrono::steady_clock::now();
        
        

        while(true) {
            {
                std::ifstream f(addFile);
                std::string newPath;
                if(std::getline(f, newPath) && !newPath.empty()) {
                    engine.addMedia(newPath);
                    log(std::string("Added to playlist: ") + newPath);
                    std::remove(addFile.c_str());
                }
            }

            bool stopped = false;
            {
                std::ifstream f(controlFile);
                std::string cmd;
                if (std::getline(f, cmd) && !cmd.empty()) {
                    if (cmd == "PAUSE") {
                        engine.pause();
                        log("[PAUSE] Video is paused, use resume command to resume video");
                    }
                    else if (cmd == "RESUME") {
                        engine.resume();
                        log("[RESUME] Video has been resumed.");
                    }
                    else if (cmd == "SKIP") {
                        engine.skip();
                        log("[SKIP] Skipping to next video in playlist.");
                    }
                    else if (cmd == "STOP") {
                        engine.stop();
                        log("[STOP] Stream is stopped and will exit.");
                        stopped = true;
                    }
                    else log(std::string("[Control] Unknown command: ") + cmd);
                    std::remove(controlFile.c_str());
                }
            }

            auto now = std::chrono::steady_clock::now();
            
            EngineStatus s = engine.getStatus();

            std::string tmpFile = statusFile + ".tmp";
            {
                std::ofstream sf(tmpFile);
                sf << "{ \"current_path\": \"" << s.current_path << "\", "
                    << "\"video_pts\": " << s.video_pts << ", "
                    << "\"audio_pts\": " << s.audio_pts << ", "
                    << "\"paused\": " << (s.paused ? "true" : "false") << ", "
                    << "\"running\": " << (s.running ? "true" : "false") << ", "
                    << "\"video_pts_since_switch\": " << s.video_pts_since_switch << ", "
                    << "\"current_duration\": " << s.current_duration << " }";
            }
            std::rename(tmpFile.c_str(), statusFile.c_str());

            if (stopped)
                break;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        engineThread.join();
    }
    catch (const std::exception& e) {
        log(std::string("[Fatal] ") + e.what());
        return 1;
    }
    return 0;
}