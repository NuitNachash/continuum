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
#include <csignal>
#include "Config.h"
#include "ContinuumEngine.h"
#include "logger.h"

// Returns the default application configuration directory
// Uses XDG_CONFIG_HOME when available, otherwise falls back to ~/.config
std::string getDefaultConfigDir() {
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    std::string base = xdgConfig ? xdgConfig : std::string(std::getenv("HOME")) + "/.config";
    return base + "/continuum/";
}

// Loads media paths from a playlist file
// Lines beginning with '#' are treated as comments
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

// Creates a human-readable timestamp for log messages
std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Global log file used by the logging helper
std::ofstream logFile;

// Writes messages to both console and log file
void log(const std::string& msg) {
    std::string line = "\n[" + timestamp() + "] " + msg;
    std::cout << line << "\n";
    if (logFile.is_open()) {
        logFile << line << "\n";

        // Flush immediately so logs remain available if the
        // application exits unexpectedly
        logFile.flush();
    }
}

// Prints command line usage information
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

static ContinuumEngine* g_engine = nullptr;
void signalHandler(int sig) {
    if (g_engine) {
        g_engine->stop();
        g_engine->forceClose();
    }
    std::exit(0);
}

int main(int argc, char** argv) {

    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);

    // Only show FFmpeg errors
    // Suppresses verbose internal logging
    av_log_set_level(AV_LOG_ERROR);

    // Open persistent application log
    Logger::instance().init("continuum.log");

    // Default file locations
    std::string configPath = getDefaultConfigDir() + "config.ini";
    std::string playlistPath;
    std::string mediaPath;
    std::string outputUrl;
    std::string controlFile = getDefaultConfigDir() + "control.txt";
    std::string addFile = getDefaultConfigDir() + "add_video.txt";
    std::string statusFile = getDefaultConfigDir() + "status.json";
    int statusInterval = 5;
    bool onceMode = false;

    // Parse command line arguments
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
        LOG_WARN("Unknown argument: " + arg); 
        printUsage();
        return 1;
        }
    }

    // Remove stale control/status files from previous runs
    std::remove(controlFile.c_str());
    std::remove(addFile.c_str());
    std::remove(statusFile.c_str());

    // Validate required arguments
    if (onceMode && mediaPath.empty()) {
        LOG_ERROR("Error: --once requires --media <path>");
        return 1;
    }

    if (playlistPath.empty() && mediaPath.empty()){
        LOG_ERROR("Error: Provide --playlist or --media");
        printUsage();
        return 1;
    }

    try {
        // Load application settings
        config cfg = loadconfig(configPath);

        // Command line output overrides configuration files
        if (!outputUrl.empty())
            cfg.rtmpUrl = outputUrl;

        // Build the list of media files
        std::vector<std::string> paths;
        if (!mediaPath.empty()) {
            paths.push_back(mediaPath);
        } else {
            paths = loadPlaylistFile(playlistPath);
        }
        if (paths.empty()) {
            LOG_ERROR("Error: Playlist is empty");
            return 1;
        }
        // First media file is required to initialize the sources
        cfg.mp4Path = paths[0];

        // Create the complete streaming engine
        ContinuumEngine engine(cfg);
        engine.setOnceMode(onceMode);
        for(size_t i = 1; i < paths.size(); i++)
            engine.addMedia(paths[i]);

        // Write initial status before streaming begins
        {
            EngineStatus s = engine.getStatus();
            std::string tmpFile = statusFile + ".tmp";
            {
                auto now_t = std::chrono::system_clock::now();
                std::time_t t = std::chrono::system_clock::to_time_t(now_t);
                std::ostringstream ts;
                ts << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");

                std::ofstream sf(tmpFile);
                sf << "{ \"current_path\": \"" << s.current_path << "\", "
                    << "\"video_pts\": " << s.video_pts << ", "
                    << "\"audio_pts\": " << s.audio_pts << ", "
                    << "\"paused\": " << (s.paused ? "true" : "false") << ", "
                    << "\"running\": " << (s.running ? "true" : "false") << ", "
                    << "\"video_pts_since_switch\": " << s.video_pts_since_switch << ", "
                    << "\"current_duration\": " << s.current_duration << ", "
                    << "\"timestamp\": \"" << ts.str() << "\"  }";
            }

            // Atomic replacement prevents readers from seeing
            // a partially written status file
            std::rename(tmpFile.c_str(), statusFile.c_str());
        }

        g_engine = &engine;

        // Run streaming in its own thread so the main thread can
        // handle commands and monitoring
        std::thread engineThread([&]() {
            engine.start();
        });
        
        auto last_status_write = std::chrono::steady_clock::now();
        
        // Main control loop
        while(true) {
            // Check for dynamically added playlist files
            {
                std::ifstream f(addFile);
                std::string newPath;
                if(std::getline(f, newPath) && !newPath.empty()) {
                    engine.addMedia(newPath);
                    LOG_INFO("Added to playlist: " + newPath);
                    std::remove(addFile.c_str());
                }
            }

            bool stopped = false;
            // Check for external control commands
            {
                std::ifstream f(controlFile);
                std::string cmd;
                if (std::getline(f, cmd) && !cmd.empty()) {
                    if (cmd == "PAUSE") {
                        engine.pause();
                        LOG_INFO("[PAUSE] Video is paused, use resume command to resume video");
                    }
                    else if (cmd == "RESUME") {
                        engine.resume();
                        LOG_INFO("[RESUME] Video has been resumed.");
                    }
                    else if (cmd == "SKIP") {
                        engine.skip();
                        LOG_INFO("[SKIP] Skipping to next video in playlist.");
                    }
                    else if (cmd == "STOP") {
                        engine.stop();
                        engine.forceClose();
                        LOG_INFO("[STOP] Stream is stopped and will exit.");
                        stopped = true;
                    }
                    else LOG_WARN("[Control] Unknown command: " + cmd);
                    std::remove(controlFile.c_str());
                }
            }

            auto now = std::chrono::steady_clock::now();
            
            // Update status information periodically
            EngineStatus s = engine.getStatus();

            std::string tmpFile = statusFile + ".tmp";
            {
                auto now_t = std::chrono::system_clock::now();
                std::time_t t = std::chrono::system_clock::to_time_t(now_t);
                std::ostringstream ts;
                ts << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");

                std::ofstream sf(tmpFile);
                sf << "{ \"current_path\": \"" << s.current_path << "\", "
                    << "\"video_pts\": " << s.video_pts << ", "
                    << "\"audio_pts\": " << s.audio_pts << ", "
                    << "\"paused\": " << (s.paused ? "true" : "false") << ", "
                    << "\"running\": " << (s.running ? "true" : "false") << ", "
                    << "\"video_pts_since_switch\": " << s.video_pts_since_switch << ", "
                    << "\"current_duration\": " << s.current_duration << ", "
                    << "\"timestamp\": \"" << ts.str() << "\"  }";
            }
            std::rename(tmpFile.c_str(), statusFile.c_str());

            if (stopped)
                break;

            // Avoid continously polling files
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        // Wait for streaming thread to finish cleanly
        engineThread.join();
    }
    catch (const std::exception& e) {
        LOG_ERROR("[Fatal] " + std::string(e.what()));
        return 1;
    }
    return 0;
}