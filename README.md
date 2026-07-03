<p align="center">
  <img src="Prism_icon.png" alt="CutWire Prism icon" width="128" height="128">
</p>

<h1 align="center">CutWire Prism</h1>

<p align="center">
  A beginner-friendly live media trigger and overlay tool built with Qt 6, FFmpeg, and OpenGL.<br>
  Perfect for school events, small concerts, and sports—combining the simplicity of instant clip triggering with the power of live visual control.
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Prism">GitHub</a> ·
  <a href="https://github.com/CutWire-Studios/Prism/issues">Issues</a>
</p>

## Features

- **Node-based Clip Canvas**: Visual clip graph with groups, transform contexts, and audio routing — not locked to a fixed grid
- **Multiple Media Source Types**: Video files, images, slideshows, webcams, screen/window capture, custom canvases, GLSL shaders, HTML/QML overlays, text, Lua-scripted sources, NDI inputs, and phone cameras (WebRTC)
- **Live A/B Deck Mixing**: Crossfade between two sources with per-deck speed control
- **Master Audio Routing**: Audio-input capture nodes routed to master-output nodes for per-device audio mixing inside the node graph
- **Asset Library**: Reusable panel of imported media that can be dropped onto the clip canvas
- **Dockable Control Area**: The deck / crossfader / panic control strip is a dock widget that can be floated or repositioned
- **Clip Editor**: Per-clip trim, crop, transform, and overlay editing via a dedicated dialog
  - **Trim**: Set in/out points with frame-accurate playback preview
  - **Crop**: Visual move/resize crop selector with numeric spinbox controls
  - **Base Canvas**: Drag/resize the clip image inside a fixed canvas
  - **Overlays**: Composited text and image overlays with font size, color, opacity, and visibility controls
- **GLSL Shader Sources**: Built-in visual generators plus custom fragment shaders, including audio-reactive presets
- **HTML / QML Overlays**: Dynamic scoreboards, clocks, and countdown timers via Qt WebEngine
- **Lua Scripting**: Script nodes that generate live text/data overlays via an embedded Lua runtime (sol2)
- **Phone Camera (WebRTC)**: Stream a smartphone camera into CutWire Prism over LAN or a public relay, paired by QR code
- **Audio FFT Visualization**: Real-time spectrum analysis with kissfft-driven shader inputs
- **Hotkey Grid**: Keyboard trigger map (1–0, Q–P, A–L, Z–M) with Shift variants for Deck B
- **Session Management**: Save/load sessions, autosave, portable asset paths, and smart asset relinking
- **Panic Controls**: Emergency blackout, freeze-frame hold, and “stay tuned” overlay
- **Program Output Hub**: Mirror windows, optional NDI program output, virtual-camera output, and program video recording with markers plus FLAC program-audio recording
- **OBS Integration**: Optional WebSocket connection for scene switching and per-clip OBS scene links
- **Remote Control**: Built-in server for triggering clips and decks from another device on the network
- **Real-time Playback**: FFmpeg-powered video decoding with low-latency OpenGL compositing
- **Drag & Drop**: Load media files directly onto clip cards
- **Dark VJ Theme**: Resolume Arena-inspired UI with cyan accents, optimized for low-light events

## Architecture

### UI Layout

```
┌────────────────────────────────────────────────────────────┐
│  CutWire Prism — Live Media Control (menubar: Media/Add/View)    │
├────────────────────────────────────────────────────────────┤
│  Clip Node Canvas (scrollable graph of clip cards)         │
│  [Clip][Clip][Group]...[Add Element ▼]                  │
├──────────────────────────────┬─────────────────────────────┤
│  A Deck    │   Crossfader    │  B Deck                     │
│  [Play]    │  ◄────●────►   │  [Play]                     │
│  Speed: 100%      50/50        Speed: 100%                 │
├──────────────────────────────┴─────────────────────────────┤
│  Panic: [Blackout] [Freeze Frame] [Stay Tuned]             │
└────────────────────────────────────────────────────────────┘
                  ↕ (separate window(s))
┌────────────────────────────────────────────────────────────┐
│  Output Monitor / Mirror Windows / NDI / Recording         │
│                    (fullscreen capable)                    │
└────────────────────────────────────────────────────────────┘
```

### Tech Stack

| Layer | Technology |
|-------|-----------|
| Framework | Qt 6 Widgets — cross-platform GUI |
| Video Decode | FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample) |
| Rendering | OpenGL via `QOpenGLWidget` |
| Camera / Screen | Qt Multimedia; Linux screen capture uses PipeWire portal + GStreamer; Windows/macOS use `QScreenCapture` / `QWindowCapture` |
| Web Overlays | Qt WebEngine |
| Scripting | Lua 5.4 + sol2 (optional) |
| Audio Analysis | kissfft (FFT spectrum for shaders) |
| Pattern Matching | RE2 |
| Session Bundles | libzip (portable `.prism` packages) |
| Streaming Output | NDI SDK (optional) |
| Phone Camera | libdatachannel (WebRTC) + OpenSSL + Qt WebSockets (optional) |
| OBS / Remote Control | Qt WebSockets / Qt Network (optional) |
| QR Codes | vendored qrcodegen (`third_party/`) |
| Build System | CMake 3.16+, C++20 |
| License | GPLv3 (compatible with FFmpeg GPL) |

## Project Structure

Headers live under `include/` and implementations under `src/`, mirroring the
same `core/` and `ui/` subtree layout. Code is built into a `prism_core`
static library that both the `CutWire Prism` app and the unit tests link against.

```
src/
  ├── main.cpp
  ├── core/
  │   ├── sources/        # MediaSource interface + every source type
  │   │     VideoFileSource, ImageSource, SlideshowSource, CameraSource,
  │   │     ScreenSource, WindowCaptureSource, CanvasSource, ShaderSource,
  │   │     HtmlSource/HtmlWorkspace, TextSource, NdiSource, WebRtcSource
  │   ├── media/          # VideoPlayer, AudioDecoder, AudioPlayer,
  │   │                   # AudioAnalyzer (FFT), ThumbnailExtractor
  │   ├── project/        # ClipManager, OverlayItem, AssetPathResolver,
  │   │                   # ProjectPackager (portable .prism session bundles)
  │   ├── scripting/      # Lua script runtime (sol2) for Script nodes
  │   └── webrtc/         # Phone-camera signaling, pairing, TLS, RTP depack
  └── ui/
      ├── mainwindow/     # MainWindow, DeckController, SourceFactory/Prompt
      ├── nodes/          # ClipNodeEditor, ClipNodeModel, ClipCard, ClipEditDialog
      ├── canvas/         # VideoWidget (OpenGL), crop/transform/group editors,
      │                   # HTML preview & workspace canvas
      ├── editors/        # Shader / HTML / Script / Text edit dialogs
      ├── transitions/    # Crossfader and transition logic
      ├── output/         # OutputHub, OutputWindow, mirror windows,
      │                   # NDI + virtual-camera program sinks
      ├── recording/      # ProgramRecorder and recording settings
      ├── obs/            # OBS WebSocket scene control
      ├── hotkeys/        # VJ hotkey grid + editor
      ├── remote/         # Remote control server + protocol
      ├── session/        # Session save/load, autosave, recovery dialog
      └── common/         # AssetLibrary, ThumbHelper, MaterialSymbols, QrCodeHelper

include/                  # Public headers, mirroring the src/ subtree
forms/                    # Qt Designer .ui files
third_party/qrcodegen/    # Vendored QR code generator (WebRTC pairing)
third_party/softcam/      # Windows virtual-camera backend (fetched + built)
scripts/                  # webrtc_signaling_server.py (relay), build-windows.ps1
tests/                    # Qt Test unit tests (linked against prism_core)
docs/                     # webrtc-phone-camera.md and other notes
resources/
  ├── styles/dark.qss     # Dark VJ theme stylesheet
  ├── fonts/              # Material Symbols icon font
  ├── shaders/            # Built-in GLSL presets + slideshow transitions
  ├── scripts/            # Sample Lua scripts (clock, counter, …)
  ├── html/               # HTML overlay templates
  └── qml/                # QML overlay templates

flatpak/                  # Flatpak packaging (org.cutwire.CutWire Prism)
CMakeModules/             # FindFFmpeg / FindNDI / Findre2
.github/workflows/        # GitHub Actions CI (build.yml, flatpak.yml)
```

## Design Philosophy

CutWire Prism prioritizes **simplicity over features**. Every button should feel intuitive even for users touching VJ software for the first time. Unlike Resolume Arena (complex, $$$) or TouchDesigner (steep learning curve), CutWire Prism is:

- **Node-based**: A visual clip graph with groups, transform contexts, and audio routing — click-to-trigger, no scenes or playlists to manage
- **Tactile**: Real-time feedback, instant controls (< 50ms latency)
- **Open**: Free, community-driven, built on open-source (FFmpeg, Qt)
- **Modular**: Abstract `MediaSource` interface makes adding new source types straightforward

## Building

### Prerequisites

**All platforms**

- **CMake 3.16+**
- **Qt 6.5+** — Widgets, OpenGL, Multimedia, WebEngine, Network (and WebSockets for OBS / WebRTC)
- **FFmpeg** — `avcodec`, `avformat`, `avutil`, `swscale`, `swresample`
- **RE2**, **libzip**
- **kissfft** and **sol2** — fetched automatically by CMake when not installed

**Linux only**

- **Qt DBus** (screen portal integration)
- **GStreamer 1.0** (`gstreamer-1.0`, `gstreamer-app-1.0`) — PipeWire screen capture pipeline

**Optional (all platforms when SDKs are present)**

- **Lua 5.4** — Lua Script nodes (`PRISM_WITH_LUA`, default ON)
- **libdatachannel** + **OpenSSL** + **Qt WebSockets** — WebRTC phone camera (`PRISM_WITH_WEBRTC`, default ON; libdatachannel fetched by CMake)
- **NDI SDK** — NDI input/output (`PRISM_WITH_NDI`, default ON; set `NDI_ROOT` on Windows)

#### Linux (Debian/Ubuntu)

```bash
sudo apt install -y \
  qt6-base-dev qt6-tools-dev qt6-multimedia-dev qt6-webengine-dev qt6-websockets-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
  libre2-dev libzip-dev libssl-dev liblua5.4-dev libgl1-mesa-dev \
  cmake build-essential pkg-config
```

#### macOS

```bash
brew install qt@6 ffmpeg gstreamer re2 libzip lua openssl
```

#### Windows

CutWire Prism builds natively on Windows with **Visual Studio 2022**, **vcpkg**, and the **Qt 6 MSVC kit**. Screen and window capture use Qt Multimedia (`QScreenCapture` / `QWindowCapture`) — no GStreamer or DBus required.

**1. Install tools**

| Tool | Install |
|------|---------|
| Visual Studio 2022 | [Build Tools](https://visualstudio.microsoft.com/downloads/) with **Desktop development with C++** |
| CMake | `winget install Kitware.CMake` |
| Qt 6.5+ | [Qt Online Installer](https://www.qt.io/download) — MSVC 2022 64-bit, modules: *Qt Multimedia*, *Qt WebEngine*, *Qt WebSockets* |
| vcpkg | `git clone https://github.com/microsoft/vcpkg.git` then `.\bootstrap-vcpkg.bat` |

**2. Install native libraries (vcpkg)**

```powershell
cd C:\path\to\vcpkg
.\vcpkg install re2 ffmpeg libzip --triplet x64-windows
# Optional:
.\vcpkg install lua openssl --triplet x64-windows
```

**3. Configure and build**

```powershell
cd C:\path\to\CutWire Prism

cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64 `
  -DPRISM_WITH_NDI=OFF `
  -DPRISM_WITH_WEBRTC=OFF `
  -DPRISM_WITH_LUA=OFF

cmake --build build --config Release --parallel
```

**4. Deploy Qt DLLs and run**

```powershell
C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe --no-translations build\Release\CutWire Prism.exe
.\build\Release\CutWire Prism.exe
```

Or use the helper script (installs vcpkg deps, configures, builds):

```powershell
.\scripts\build-windows.ps1 -QtPath C:\Qt\6.8.0\msvc2022_64 -Deploy
```

**Windows platform notes**

| Feature | Windows |
|---------|---------|
| Video, images, shaders, HTML, webcam | Supported |
| Screen / window capture | `QScreenCapture` / `QWindowCapture` |
| NDI | Supported when [NDI SDK](https://ndi.video/) is installed (`-DNDI_ROOT="C:\Program Files\NDI\NDI 6 SDK"`) |
| Virtual camera output | Built-in via [softcam](https://github.com/tshino/softcam) (MIT) — appears as **DirectShow Softcam**; `softcam.dll` is copied next to `CutWire Prism.exe` at build time |
| WebRTC phone camera | Build with `-DPRISM_WITH_WEBRTC=ON` + OpenSSL; firewall rules are not auto-opened yet |

Enable virtual camera output from **View → Virtual Camera Output**. In OBS, Zoom, or a browser, pick the camera named **DirectShow Softcam**. Only one softcam sender can run at a time on the system.

### Build Steps

```bash
# Clone and enter project
git clone https://github.com/CutWire-Studios/Prism.git
cd CutWire Prism

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run
./build/CutWire Prism        # Linux/macOS
# build\Release\CutWire Prism.exe  # Windows
```

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `PRISM_WITH_NDI` | `ON` | Enable NDI program output/input when the NDI SDK is found |
| `PRISM_WITH_LUA` | `ON` | Enable Lua Script nodes (Linux: system Lua 5.4; Windows: vcpkg `lua`) |
| `PRISM_WITH_WEBRTC` | `ON` | Enable the WebRTC phone-camera source (libdatachannel fetched by CMake; needs OpenSSL + Qt WebSockets) |

OBS integration is enabled automatically when Qt WebSockets is present. Disable an
optional feature with e.g. `-DPRISM_WITH_WEBRTC=OFF`.

### Flatpak

CutWire Prism can be built and run as a Flatpak using the included manifest:

```bash
./flatpak/build.sh install   # build and install locally
./flatpak/build.sh run         # build and launch
./flatpak/build.sh export      # create a .flatpak bundle in flatpak/dist/
```

Requires `flatpak` and `flatpak-builder`.

## Quick Start

1. **Load Media**: Use **Media → Load Folder**, **Add Files**, or **Add Photos** to populate the clip canvas
2. **Trigger Clips**: Click a clip card’s **A** or **B** button, or use assigned hotkeys (Shift+key for Deck B)
3. **Add Sources**: Use **Add Element ▼** or the **Add** menu to insert video, photos, slideshows, cameras, screen/window capture, canvases, shaders, HTML overlays, or NDI sources
4. **Edit a Clip**: Open the clip editor to trim, crop, transform, or add overlays
5. **Mix with Crossfader**: Blend the A and B decks in real time
6. **Control Speed**: Adjust playback speed per deck (100% = normal)
7. **Open Output**: Use **View → Show Output** for projection, or enable NDI/recording from the **View** menu
8. **Save Your Show**: Use **Media → Save Session**; sessions autosave and relink missing assets on load
9. **Panic if Needed**: Use blackout, freeze-frame, or stay-tuned controls during live events
10. **Drag & Drop**: Drop video or image files onto clip cards to reassign them

## Supported Media

| Type | Formats / Notes |
|------|-----------------|
| Video | MP4, AVI, MOV, MKV, WebM, FLV, and any FFmpeg-supported container |
| Images | PNG, JPG/JPEG, BMP (displayed as stills) |
| Slideshow | Folders of images with configurable interval and GPU transition effects |
| Live | Webcam, display capture, window capture |
| Generator | Custom canvas (solid color, checkerboard, or transparent) |
| Shader | GLSL fragment shaders, including built-in audio-reactive presets |
| HTML / QML | Dynamic overlays (scoreboards, clocks, countdown timers) |
| Text | Styled text source for titles and lower thirds |
| Script | Lua-driven dynamic text/data overlays |
| Network | NDI sources (when NDI runtime is available) |
| Phone | Smartphone camera over WebRTC (LAN or public relay) — see [docs/webrtc-phone-camera.md](docs/webrtc-phone-camera.md) |

## Use Cases

- **School Events**: Instant highlight reels and replays during cricket/football matches
- **Live Concerts**: Music videos and audio-reactive shader visuals
- **Visual Performances**: Dance, theater, immersive installations
- **Sports Broadcasting**: HTML/QML score overlays, freeze-frame holds, and program recording

## Testing

Unit tests use Qt Test and link against the `prism_core` library. They run
headless (`QT_QPA_PLATFORM=offscreen`) and are registered with CTest:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

## Contributing

Contributions welcome! Please fork [CutWire-Studios/Prism](https://github.com/CutWire-Studios/Prism) and submit pull requests.

## License

CutWire Prism is licensed under GPLv3. See [LICENSE](LICENSE) for details.

## Troubleshooting

### FFmpeg not found

- **Linux**: `sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev`
- **macOS**: `brew install ffmpeg`
- **Windows**: `vcpkg install ffmpeg --triplet x64-windows` and pass `-DCMAKE_TOOLCHAIN_FILE=...\vcpkg.cmake`

### Qt not found

- Ensure Qt 6.5+ is installed with WebEngine and Multimedia modules
- **Linux/macOS**: set `CMAKE_PREFIX_PATH` or `Qt6_DIR` (e.g. `/opt/Qt/6.7.0/gcc_64`)
- **Windows**: `-DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64` (must match your MSVC kit)

### GStreamer not found (Linux only)

GStreamer is required on **Linux** for PipeWire screen capture. It is not used on Windows.

- **Linux**: `sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev`
- **macOS**: `brew install gstreamer`

### RE2 or libzip not found

- **Linux**: `sudo apt install libre2-dev libzip-dev`
- **macOS**: `brew install re2 libzip`
- **Windows**: `vcpkg install re2 libzip --triplet x64-windows`

### Visual Studio / MSVC not found (Windows)

- Install VS 2022 with the **Desktop development with C++** workload
- Open **x64 Native Tools Command Prompt for VS 2022**, or use the Visual Studio generator as shown above
- vcpkg requires a working MSVC toolchain before installing packages

### Missing DLLs at runtime (Windows)

Run `windeployqt` on `CutWire Prism.exe` after building (see Windows build steps). FFmpeg and other vcpkg DLLs may need to be copied from `vcpkg\installed\x64-windows\bin` into the same folder as the executable.

### NDI or OBS features disabled

- NDI requires the [NDI SDK](https://ndi.video/) installed and discoverable by CMake (`NDI_ROOT`)
- OBS integration requires `qt6-websockets`; without it, OBS menu actions are disabled at build time

### Build failures

```bash
rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Check the CMake output for missing dependencies.

## Credits

Built by [CutWire Studios](https://github.com/CutWire-Studios).
