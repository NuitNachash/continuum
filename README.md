# Continuum

A continuous media playout engine for 24/7 RTMP streaming, built directly on FFmpeg's libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`) rather than shelling out to the `ffmpeg` CLI.

Continuum plays a dynamic or static playlist of media files and streams them to an RTMP endpoint (e.g. Twitch) with a single, unbroken output timeline — no restart, no gap, no reconnect between files. It's designed for background, unattended, 24/7 broadcast use cases: rotating video libraries, archive replay channels, always-on streams — rather than one-off playback.

## Why not just use `ffmpeg` directly?

`ffmpeg -re -i file.mp4 -c copy -f flv rtmp://...` works fine for playing a single file once. Continuum exists for the case right after that: when the file ends, what happens next? Continuum keeps a persistent process alive, maintains a continuous PTS/DTS timeline across file switches, and exposes simple hooks (a control file, a playlist file) so an external program — a script, a bot, a scheduler — can decide what plays next without ever tearing down and restarting the stream.

## Features

- **Continuous playback across files** — no stream restart, no RTMP reconnect, no timestamp discontinuity when switching between media files.
- **Runtime-modifiable playlist** — add new media to the queue while the engine is already running and streaming.
- **Playback controls** — pause, resume, skip, and stop, controllable while the stream is live.
- **Status reporting** — a continuously updated status file exposing current file, playback position, and duration, so an external controller (script, bot, dashboard) can make timing decisions (e.g. "queue the next video when 30 minutes remain").
- **CPU-only, no GPU dependency** — pure software decode/encode via FFmpeg's libraries; runs on any Linux box with enough CPU headroom.
- **File-based IPC** — no daemon, no socket server (yet) — control and status flow through plain files, easy to script against from any language.
- **CLI-first** — a single binary, driven by flags, suitable for systemd services, cron-based restarts, or manual invocation.

## Requirements

- Linux (developed and tested on Debian)
- CMake 3.10+
- A C++17 compiler
- FFmpeg development libraries — `libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample` (version 6.0+ recommended; Continuum uses the modern `AVChannelLayout` API, which is not present in older FFmpeg builds)

## Installation

```bash
git clone https://github.com/nuitnachash/continuum.git
cd continuum
chmod +x scripts/*.sh
./scripts/install_deps.sh   # installs build tools + FFmpeg dev libraries via apt/dnf
./scripts/install.sh        # builds Continuum and installs it to /usr/local/bin
```

After installation:
- The `continuum` binary is available from any directory.
- A default config is created at `~/.config/continuum/config.ini` (or `$XDG_CONFIG_HOME/continuum/config.ini` if that variable is set), copied from `config.example.ini`.

Edit that config file with your RTMP URL and desired encoding settings before running.

### Manual build (without the install script)

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./continuum --help
```

## Configuration

`config.ini` (or your custom `--config` path) uses simple `key=value` pairs:

```ini
rtmpUrl=rtmp://live.twitch.tv/app/YOUR_STREAM_KEY
width=1920
height=1080
fps=30
bitrate=6000000
a_bitrate=160000
samplerate=48000
preset=veryfast
tune=zerolatency
mp4Path=
```

| Key | Description |
|---|---|
| `rtmpUrl` | RTMP output URL, including stream key. Overridable with `--output`. |
| `width` / `height` | Output resolution. Input video is scaled to match regardless of source resolution. |
| `fps` | Output frame rate. |
| `bitrate` | Video bitrate (bits/sec) for the H.264 encoder. |
| `a_bitrate` | Audio bitrate (bits/sec) for the AAC encoder. Twitch requires at least 96000. |
| `samplerate` | Audio sample rate (Hz). 48000 is standard. |
| `preset` / `tune` | libx264 preset/tune values (e.g. `veryfast` / `zerolatency` for low-latency live streaming). |
| `mp4Path` | Only used internally on startup; set automatically by `--media`/`--playlist`. |

## Usage

### Continuous playlist mode (primary use case)

```bash
continuum --playlist playlist.txt --output rtmp://live.twitch.tv/app/YOUR_KEY
```

`playlist.txt` is a plain text file, one media path per line:
```
/path/to/video1.mp4
/path/to/video2.mp4
/path/to/video3.mp4
```

### Single file / testing mode

```bash
continuum --media video.mp4 --output rtmp://... --once
```

Plays exactly one file and exits when it ends. Useful for verifying encoder settings or RTMP connectivity without setting up a full playlist.

Without `--once`, `--media` will keep looping the single file indefinitely — useful as a starting point for an external controller that adds further videos to the queue via `--add-file` while the engine runs.

### All CLI flags

```
--config <path>          Config file (default: config.ini)
--playlist <path>        Playlist file, one media path per line
--media <path>           Single media file (use with --once for testing)
--output <url>           RTMP output URL (overrides config file)
--once                   Play a single file and exit (requires --media)
--control-file <path>    Control command file (default: control.txt)
--add-file <path>        Dynamic playlist-add file (default: add_video.txt)
--status-file <path>     Status output file (default: status.json)
--status-interval <n>    Seconds between status writes (default: 5)
--help                   Show usage
```

## Runtime control (file-based IPC)

Continuum is designed to run as a long-lived process, controllable from an external script while it's live — no need to restart the stream to change what's playing or queued.

### Adding to the playlist while running

Write a media path to the add-file (default `add_video.txt`):
```bash
echo "/path/to/next_video.mp4" > add_video.txt
```
Continuum picks this up within a couple of seconds, appends it to the internal playlist, and clears the file. The currently-playing video is unaffected — the new entry plays once the current file reaches its end.

### Playback control

Write one of the following commands to the control-file (default `control.txt`):

```bash
echo "PAUSE" > control.txt    # pause playback (see caution below)
echo "RESUME" > control.txt   # resume playback
echo "SKIP" > control.txt     # skip immediately to the next playlist entry
echo "STOP" > control.txt     # gracefully shut down the stream and exit
```

**Caution on `PAUSE`:** pausing halts all output to the RTMP endpoint entirely. Most RTMP ingest servers (including Twitch) will time out and drop the connection if no data arrives for an extended period (commonly on the order of 30–60 seconds). `PAUSE` is intended for brief interruptions, not indefinite holds. A "keep-alive" idle frame source for longer pauses is a possible future enhancement (see Roadmap).

### Status output

Continuum writes a JSON status file (default `status.json`) approximately once per tick, atomically (via temp-file-and-rename, so consumers never see a partial write):

```json
{
  "current_path": "/path/to/current_video.mp4",
  "video_pts": 123456,
  "audio_pts": 7890123,
  "video_pts_since_switch": 4321,
  "current_duration": 1800032667,
  "paused": false,
  "running": true
}
```

- `video_pts` / `audio_pts` are global, ever-increasing counters across the entire stream lifetime (never reset — this is what keeps RTMP timestamps monotonic across file switches).
- `video_pts_since_switch` resets to zero at the exact moment the engine switches to a new file — this is the field an external controller should use to determine "how far into the current file are we."
- `current_duration` is the current file's total duration, in the same units as pts (divide by `fps` for seconds).

An external Python (or any language) controller can poll this file to make timing decisions — for example, choosing and queuing the next video once a fixed amount of time remains in the current one.

## Architecture

- **VideoFrameSource / AudioFrameSource** — demux, decode, scale/resample each media file. Support hot file-switching (`switchFile()`) without tearing down the whole pipeline.
- **TimelineManager** — owns the global, monotonic PTS/DTS counters for both video and audio streams, ensuring continuity across file switches regardless of each file's own internal timestamps.
- **PlaylistManager** — a thread-safe queue of media paths. Supports runtime additions from the control thread while the playback thread is actively consuming from it.
- **Encoder** — wraps H.264 (libx264) and AAC encoding.
- **Streamer** — wraps the RTMP output muxer/writer.
- **ContinuumEngine** — owns all of the above, runs the playback loop on its own thread, and exposes `start()`, `pause()`, `resume()`, `skip()`, `stop()`, `addMedia()`, and `getStatus()`.

All engine state shared between the playback thread and the control-file-watching thread (pause/resume/skip flags, current file path, timeline counters) is protected via `std::atomic` or `std::mutex` as appropriate.

## Known limitations

- **CPU-only.** No hardware-accelerated encode/decode path currently. All video/audio processing runs in software via libx264/FFmpeg's software codecs.
- **No automatic RTMP reconnection.** If the RTMP connection drops (network interruption, ingest server issue), Continuum does not currently attempt to reconnect automatically. For long-running deployments, pair Continuum with an external restart mechanism (cron, systemd `Restart=on-failure`, or your own controller script) as a safety net.
- **File-based IPC only.** Control and status flow through plain files polled every few seconds — sufficient for slow-moving decisions (playlist queueing, occasional pause/skip) but not designed for sub-second control latency. A Unix socket-based control layer is a possible future direction.
- **No overlay/graphics compositing yet.** Continuum currently streams decoded video frames as-is (scaled to the configured output resolution). Text/image overlays are on the roadmap but not yet implemented.

## Roadmap

- Graceful handling of unreadable/corrupt playlist entries (skip and continue rather than crash)
- Static image overlay support (logo/watermark compositing)
- Text overlay support (via `libavfilter`)
- Potential Speech-To-Text overlay support
- Automatic RTMP reconnection on write failure
- Unix socket-based control/status as an alternative to file polling
- Optional hardware-accelerated encode path

## License

MIT — see [LICENSE](LICENSE).

## Contributing

Issues and pull requests are welcome. This project is under active development; expect breaking changes between minor versions until a 1.0 release.
