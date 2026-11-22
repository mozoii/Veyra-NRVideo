# Architecture

This fork is behavior-preserving in structure: no processing algorithm, configuration format or public QML interface was replaced. The engine, pipeline, capture/decode, audio, runtime and shaders are unchanged.

## C++: stable QML entry, implementation split by domain

`include/veyra/ui/QmlPlayerBridge.h` still defines the original properties, methods and signals. `src/ui/QmlPlayerBridge.cpp` keeps only startup, lifecycle and polling wiring; `src/ui/QmlPlayerBridge_p.h` holds the original private state, internal shared declarations and stateless text conversion, and is not a new public API.

| Implementation file (src/ui/bridge) | Responsibility |
|---|---|
| Hardware.cpp | Adapters, capability detection and cache, available-backend selection |
| Capture.cpp | Capture format/audio settings and async queries, screen capture and game-mode wiring |
| Streaming.cpp | Moonlight / Xbox streaming entry and controller-control wiring |
| Playback.cpp | File/network playback, source opening and playback commands |
| Enhancement.cpp | NR/SR/AA/beauty parameter mapping and validation |
| FrameGeneration.cpp | Frame-generation backend, multiplier, flow, display sync/VRR selection |
| Colour.cpp | Colour-curve parameter table, curves, LUTs and look |
| Subtitles.cpp | Subtitle load/style and audio sync; GDI+ initialization is a single instance |
| Recording.cpp | Real-time recording UI control and status publishing |
| Export.cpp | Image/video export settings, task and status |
| Chain.cpp | Processing-chain/node editing and preset operations |
| Preferences.cpp | Preferences, shortcuts, component info and the extended history/export row UI entry |
| Status.cpp | GPU/stage time, load and runtime state |
| View.cpp | Protected areas, comparison and picture layout selection |
| VirtualCamera.cpp | Virtual-camera preview/recording setup and control |
| VirtualCaptureAudioSetup.cpp | Virtual-capture audio setup wiring |
| PlaybackServer.cpp | Opt-in local HTTP playback server |

Each file is a separate compilation unit, listed explicitly in CMake. Stateful hardware cache, GDI+ initialization and lifecycle ownership are not copied into headers; shared state layout and method bodies stay. Future feature work should find the domain file first, then decide whether the private state/service needs to be split further. Do not pile the implementation back into the startup file.

## QML: separate dialogs, pages reuse small components

- `DialogHost.qml` (139 lines) does only routing, scrim and the unified host; `DialogLayer.qml` preserves the original size, animation and native-video occlusion contract.
- Network/capture/PC/Xbox/screen entries each live in a named `SourceDialog`; subtitles, audio, capture location and presets each have their own component. Shared host via an explicit `dialogHost` and the capture page `legacyPicker` dependency; no dialog action or control-name changes.
- `DialogSection/Group/Note/TextField` are unified appearance components; the `DialogHost.VTextField` compatibility name is kept.
- `ProPage.qml` is 2315 lines. Audio and display panels are `ProAudioPanel` and `ProDisplayPanel`; HDR overlay observation remains under the page/tab/card visibility constraint.
- Status bar, performance orbs, presentation selection and overlay buttons each have a Pro-prefixed reusable component; legacy `ProPage.PerfOrb/PerfOrbs/StatusSummary/PresentationRows` entry points are kept as compatibility wrappers; the node page reuses the same.

## FrameSource path

Capture-card and regular-video sources, the Xbox streaming source and moonlight all feed the shared `FrameSource` path: the same enhancement graph, guidance, reset and parameter contract apply regardless of source. Super resolution, frame generation, ANR FG, HDR, colour, subtitles, export, virtual camera and OBS share this path; only the frame acquisition differs.

## Translation and validation

The translation extractor covers the `src/ui` subdirectory and the bridge private-state header; catalog entries and their English/translation columns are kept consistent. Structural migration verification compares 681 bridge method bodies before and after the move.

Structural consistency does not replace runtime verification. Build, contract/unit checks, Qt Quick tests and real render/preference launches were run per release; actual results and failures are in the execution records under `docs/history/`.
