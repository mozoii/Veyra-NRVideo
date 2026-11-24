# Blackwell performance baseline campaign — status and plan (2026-11-22)

This is the working record for `handoffs/handoff-veyra-blackwell-1440p60-to-4k120.md`.
It separates what was verified this session, what the user reported, and what is
still blocked. Numbers are not merged across those categories.

Target pipeline: **Xbox 1440p60 H.264 stream -> DLSS SR to 4K -> frame generation
2X -> 4K120**. Submitted fps is never scanout rate and never end-to-end latency;
per-hop values below are software residency only.

## Frame budget (from the handoff, not re-measured here)

- Output interval at 120 Hz: 8.333 ms.
- Generated-frame deadline at 2X: ~4.17 ms after the pair anchor, plus a 10 ms grace
  (`include/veyra/engine/LivePresentationTiming.h:105`, `LiveFgAdmission.h`).
- User's reported FG time at 30 fps content: 7.1 ms. That fits 30 fps (16.67 ms
  half-interval) but 7.1 + base + blit crosses 8.33 ms, entering
  `liveFramesShareRefresh` drop-to-source-rate mode
  (`LivePresentationTiming.h:107-113`).
- DLSS SR preset K 0.87 ms / M 1.05 ms are NVIDIA/Digital Foundry numbers for game
  renderers at 1080p->4K Performance, not measured for our 1440p->4K video path.
  Do not extrapolate.

## Verified environment (this session)

| Item | Value | Source |
| --- | --- | --- |
| Branch | `feature/xbox-1440p-english` at `0bf2e439` | git (`32d3fb04` in the handoff is one commit behind; `0bf2e439` adds the live-audio-anchor fix) |
| Working tree | Dirty: `handoffs/`, `i18n-scripts/`, `main.obj`, `scripts/i18n/*` untracked | git status; preserved, not cleaned |
| GPU | NVIDIA GeForce RTX 5090 (VRAM capacity 32607 MiB per handoff, not re-queried this session) | `Win32_VideoController` + handoff |
| Driver | 32.0.16.2135 = 621.35 | `Win32_VideoController` |
| Displays | RTX 5090 desktop 1920x1080; Sunshine virtual display 3840x2160; SudoMaker virtual display; AMD iGPU | `Win32_VideoController` |
| Toolchain | MSVC 14.51.36231 (`cl.exe`), CMake, Ninja at `C:/Users/mozi/.local/tools/ninja.exe` | `build-local/x64-release/CMakeCache.txt` |
| Build tree | `build-local/x64-release`, Release, `VEYRA_ENABLE_XBOX=ON`, FFmpeg 63.1.101 (vcpkg x64-windows-static) | CMakeCache |
| Run times | DLSS/DLSS-G 310.9.16.0 (58,956,912 / 7,506,544 bytes); NR original 310.9.16 (266,424,944 bytes); NR ampere 165,840,496 bytes; VFG VSR 59,120,752 bytes | staged files under `build-local/x64-release/runtime_local/nvidia/` |
| FG Lite | `runtime_local/nvidia/dlssg-lite/` absent | filesystem |
| `veyra_clip_gen` | Source present (`tools/clip_generator/main.cpp`, CMake target at `CMakeLists.txt:1808`) but **not built**; guarded on `VEYRA_CLIP_TOOLS_ROOT` | CMakeLists, build tree |
| openh264 toolchain | `C:\veyra-deps\tools-installed\x64-windows` absent; no `ffmpeg.exe`, no openh264 DLL/lib anywhere under `C:\veyra-deps` | filesystem |
| Corpus | No `corpus-manifest.json` in the repo; only `assets/veyra-2.0.0-promo.mp4` and `REAMDE MP4.mp4` (neither is a 1440p60 deterministic scenario clip) | filesystem |
| Artifact root | `E:\项目\Veyra` does not exist (drives C:, D: only). New artifacts go under `build-local/blackwell-2026-11-22/` in this repo | filesystem; handoff instruction to confirm before writing outside the repo |

Handoff-requested branch `feature/blackwell-perf-4k120` was **not** created: the
user directed that the work stay on `feature/xbox-1440p-english` directly.

## User-reported data (reported 2026-11-22, not reproduced here)

30 fps content, VFG quality 3 (High+Medium) at 2X, Auto HUD Lock on: FG 7.1 ms;
DLSS SR 1.7 ms; DLAA 1.3 ms; optical flow 1.6 ms; extents unconfirmed. Windows-side
receive counter read 30.7 Mbps (above the advertised `maxBitrateKbps=30000`).
Facts from code that interpret this are in memory and in the handoff; the key one:
at 2X a mixed tier executes only its first model, so High+Medium 2X == High 2X
(`vfgPrimarySubframe(2)==1`, `VfgBackend.cpp:342`, `EnhancementSettings.h:69-81`),
and `FgBatch` includes the shot-change test, CUDA wait, output copy, HUD Lock and
compose (`EnhanceGraph.cpp:4167-4233`) — it is not pure model time.

## Toolchain built (user-authorized 2026-11-22)

- Installed `openh264:x64-windows-static` (2.6.0#4, 22 s) and rebuilt
  `ffmpeg[openh264]:x64-windows-static` (9.0.1#1, 6 min) into
  `build-local/blackwell-2026-11-22/vcpkg-installed/`. The global vcpkg tree was
  left untouched (`--x-install-root`). `CONFIG_LIBOPENH264 1` confirmed.
- Reconfigured `build-local/x64-release` with `VEYRA_CLIP_TOOLS_ROOT` and built
  `veyra_clip_gen.exe`. Its committed link line did not cover the network/SChannel
  libs of this FFmpeg build, so the tool was linked once under the task directory
  with the extra system libs, openh264 and swresample. No source CMake was changed.
- Generated 2560x1440@60 H.264 clips (65 s) for all five scenarios into
  `build-local/blackwell-2026-11-22/clips/`. Generation is CPU-only and ran two
  at a time.
- Added opt-in env hooks to `tests/integration/FgSustainedTests.cpp`:
  `VEYRA_TEST_FG_BACKEND`, `VEYRA_TEST_VFG_QUALITY`, `VEYRA_TEST_HUD_LOCK`.
  Defaults are unchanged when unset.
- Added an additive `xbox-latency` log line to `XboxSessionSource.cpp` that
  derives queue-wait and decoded-to-present timing from clocks already kept;
  no readback, fence or extra wait, no UI strings, no default changes.

## Measured results (this session, 5090 / driver 621.35)

Harness: `veyra_fg_sustained_tests <clip> <logdir> 2 on 30 <effects> <ratio> live-1440p`,
`VEYRA_TEST_REPLAY_AS_CARD=1`, 2560x1440@60 source, DLSS SR to 4K, 2X, private
data-dir/log/cache. Submitted fps is not scanout rate.

DLSS-G 2X, translation clip, NR+SR on, 30 s: `SUSTAINED lifecycle=1`, mean submit
120 fps, `fgSkippedBeforeEval=1`, `generatedExpiredAfterEval=0`. The one skip is
the harness's deliberate 55 ms stall injection at t=15 s. Per-stage P95 at t=29 s:
Nr 3.16 ms, Sr 0.84 ms, Flow 1.60 ms, FgBatch 1.10 ms.

VFG 2X, translation clip, NR+SR on, HUD Lock on, `VEYRA_TEST_VFG_GPU_PROFILE=1`,
30 s. `runMeanMs` is the CUDA-event model time, `FgBatch` the UI span:

| VFG quality | runMeanMs | runMaxMs | FgBatch P95 | generated / frames | skips / expired |
| --- | --- | --- | --- | --- | --- |
| q1 Medium | 1.99 | 2.26 | ~1.1 | 1805 / 1808 | low |
| q2 High | 6.78 | 7.57 | ~7.2 | 1685 / 1809 | ~124 / ~8 |
| q3 High+Medium | 6.40-6.55 | 6.72-7.14 | 7.21-7.43 | 1684 / 1808 | 122 / 8 |

This reproduces the user's 7.1 ms figure closely (FgBatch ~7.2-7.4 ms; pure model
~6.4 ms; the rest is shot-change/copy/HUD-Lock/compose) and confirms the handoff's
code finding: at 2X, q3 executes only its first model and is indistinguishable
from q2, while the Medium effect is loaded but never evaluated. q1 Medium fits the
8.33 ms half-interval with room to spare; q2/q3 do not, losing about 124 generated
frames over 30 s.

Full DLSS-G 2X matrix: five scenarios x {off, nr, sr, on}, 30 s each, all 20 runs
`lifecycle=1`, `throughput=1`, submit fps 119.75-120. Values below are means over
the last 10 s of each run; `—` means that stage is inactive for that effect set.
CSV: `build-local/blackwell-2026-11-22/matrix-results.csv`.

| Case | Submit | Nr | Sr | Flow | FgBatch | Skips | Expired |
| --- | --- | --- | --- | --- | --- | --- | --- |
| translation-off | 120 | — | — | 1.65 | 1.00 | 1 | 0 |
| translation-nr | 120 | 3.23 | — | 1.64 | 1.08 | 1 | 0 |
| translation-sr | 120 | — | 0.86 | 1.64 | 1.32 | 0 | 0 |
| translation-on | 120 | 3.31 | 0.99 | 1.77 | 1.11 | 1 | 0 |
| occlusion-off | 120 | — | — | 1.68 | 1.21 | 0 | 0 |
| occlusion-nr | 120 | 3.31 | — | 1.68 | 1.13 | 1 | 0 |
| occlusion-sr | 120 | — | 0.91 | 1.70 | 1.35 | 0 | 0 |
| occlusion-on | 119.75 | 3.66 | 1.17 | 1.78 | 1.41 | 1 | 0 |
| cut-flash-duplicate-off | 120 | — | — | 1.81 | 1.17 | 0 | 0 |
| cut-flash-duplicate-nr | 120 | 3.37 | — | 1.75 | 1.18 | 1 | 0 |
| cut-flash-duplicate-sr | 120 | — | 0.94 | 1.85 | 1.36 | 0 | 0 |
| cut-flash-duplicate-on | 120 | 3.60 | 0.83 | 1.87 | 1.30 | 1 | 0 |
| particles-off | 120 | — | — | 1.57 | 1.00 | 1 | 0 |
| particles-nr | 120 | 3.50 | — | 1.61 | 1.30 | 1 | 0 |
| particles-sr | 120 | — | 0.87 | 1.62 | 1.42 | 0 | 0 |
| particles-on | 120 | 3.56 | 0.87 | 1.65 | 1.37 | 1 | 0 |
| ui-text-off | 120 | — | — | 1.62 | 1.18 | 0 | 0 |
| ui-text-nr | 120 | 3.41 | — | 1.50 | 1.25 | 1 | 0 |
| ui-text-sr | 119.75 | — | 0.86 | 1.55 | 1.44 | 0 | 0 |
| ui-text-on | 119.75 | 3.80 | 0.86 | 1.62 | 1.42 | 1 | 0 |

DLSS SR model comparison (translation clip, 2X, NR+SR on, mean over last 10 s):
model 0 / preset K (`renderPreset=11`) 1.05 ms; model 1 / preset M (`13`) 1.59 ms;
model 2 / preset L (`12`) 2.18 ms. All held 120 submit fps. K is fastest on this
video path; M and L are quality-per-ms trades, not speedups. These are our video
numbers, not the game-renderer figures from Digital Foundry. No default changed.

Skips of 1 in the nr/on rows occur once per run and are the harness's injected
55 ms stall; expired is 0 everywhere for DLSS-G. The Sunshine virtual display is
3840x2160 @ 240 Hz; `display-latency` logs recorded DXGI `refreshHz=240` and
present-to-scanout P95 of 4.7-7.0 ms, which are not photon measurements and not
physical-panel latency. A dedicated physical-panel run was not performed.

## Phase S1 finding: why the Xbox stats HUD can read empty

The stats path is wired and correct in code. `StreamHud.qml:17` shows the HUD only
when `state.streaming === true` **and** `veyra.moonlightStatsVisible` is true;
`Main.qml:700-702` toggles that on Ctrl+Alt+Shift+S. `XboxModel::updateStream`
(`src/ui/XboxModel.cpp:269-284`) maps `videoMbps`/`decodeMs`/`rttMs` into the model.
`QmlPlayerBridge::tickXbox` (`src/ui/bridge/Streaming.cpp:102-113`) refreshes stats
on the streaming transition and then every 15 ticks. `XboxSessionSource::stats`
(`src/source/XboxSessionSource.cpp:701-733`) only computes `videoMbps` once
`seconds >= 1.0`, so the first second shows a dash.

The user retested on the physical Series X and the HUD still did not appear.
Root cause: `VEYRA_ENABLE_MOONLIGHT=OFF` in this build, and both
`moonlightStatsVisible()` and `setMoonlightStatsVisible()` were inside
`#ifdef VEYRA_ENABLE_MOONLIGHT` in `src/ui/bridge/Streaming.cpp`. The backing
member in `QmlPlayerBridge_p.h` was inside the same guard. The QML shortcut
(`Main.qml:699-702`) therefore toggled a property whose setter was a no-op and
whose getter always returned false, so `StreamHud.shown` was always false for
Xbox too.

Fix: the shared stats-visible flag and its getter/setter are now unconditional;
the Moonlight-only fields remain guarded. Regression check added to
`tests/integration/AudioVolumeBridgeTests.cpp`. Feedback loop: with the check
added first, `veyra_audio_volume_bridge_tests` reported
`FAIL: stream stats toggle works without Moonlight support`; after the fix it
reports `SUMMARY: 18 checks, 0 failures`. `veyra_qml_ui.exe` was rebuilt.

`videoMbps` counts depacketized payload bytes, so it should read slightly below
the OS wire counter (~29-30 vs 30.7 Mbps). No wire change (REMB, `b=AS`, L5.1
fmtp) is made until the HUD readings are confirmed; any value below the observed
plateau could cap the rate downward.

## Phase L1 finding: per-hop counters that already exist

The data for an Xbox latency breakdown is present but never surfaced:

- `Impl::Meta` carries arrival / decoded / submitted / presented host times
  (`src/source/XboxSessionSource.cpp:513,571`); `videoPresented` feeds them back
  (`src/source/XboxSessionSource.cpp:654-668`).
- The 8-AU backlog cap with oldest-drop is `xbox::trimVideoInputs`
  (`XboxSessionSource.cpp`), and `read()` is a single-slot latest-frame mailbox
  (`:641-652`); `AV_CODEC_FLAG_LOW_DELAY` is set on the D3D12VA decoder
  (`FFmpegVideoDecoder.cpp:196-199`); MMCSS is on the graph owner.
- `FrameFlowWindow::snapshot` computes software residency mean/P95
  (`include/veyra/engine/FrameFlowWindow.h:161`) and is shown in
  `apps/veyra/ui/LiveStatusPanel.h:71`.
- `CpuStage::DecodedQueue` is timed for streams (`EngineController.cpp`).

Queue wait (arrival->decode start), decode ms, decoded->graph pickup, graph
residency and present wait can therefore be derived without new readback or CPU
fence waits. An additive `xbox-latency` log line was added for queue wait and
decoded-to-present residency; it reuses those clocks, adds no readback or fence,
and has no UI strings or default change.

Live Series X "before" sample (physical console, `veyra-qml.log`,
2026-11-23T00:32Z): `queueWaitMs=0.008`, `decodedToPresentMs=22.13-22.16`,
60 samples per second. The queue hold is effectively zero, so the 8-AU cap is
not adding latency and should not be shrunk (it is a keyframe-recovery trade).
The ~22 ms decoded-to-present figure is software residency through graph,
enhancement, FG hold and present queue; the FG half-interval hold (8.33 ms at
4K120) is a structural floor while FG is on, not a defect any receiver change
removes. Console encode/send latency is not observable here. No readiness
margin, backlog cap or pacing default was changed.

## Exact Phase A commands (for when the corpus is available)

Configure/build (concurrency 2, private build tree):

```powershell
$env:PATH = "C:\Users\mozi\.local\tools;C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64;$env:PATH"
cmake --build build-local/x64-release --target veyra_fg_sustained_tests veyra_qml_ui veyra_clip_gen --parallel 2
```

Generate 1440p60 clips per scenario (5 scenarios, 2560x1440@60):

```powershell
foreach ($s in 'translation','occlusion','cut-flash-duplicate','particles','ui-text') {
  veyra_clip_gen --scenario $s --width 2560 --height 1440 --fps 60 --seconds 30 --output build-local/blackwell-2026-11-22/clips/${s}_1440p60.mp4
}
```

2X matrix row (30 s, replay-as-card, `live-1440p` profile):

```powershell
veyra_fg_sustained_tests <clip> build-local/blackwell-2026-11-22/logs/<case> 2 on 30 on|off|nr|sr 0.98 live-1440p
```

`VEYRA_TEST_VFG_GPU_PROFILE=1` decomposes the VFG `FgBatch` span; HUD Lock on/off
A/B separates compose cost. Each run is <= 300 s with a private data-dir/log/cache;
TEMP/TMP are set for child processes only.

## Unverified scope and language

- No 5090 measurement in this document is ours. The 7.1/1.7/1.3/1.6 ms and 30.7
  Mbps figures are user-reported and unconfirmed here.
- 1440p60 delivery is user-confirmed on a physical Series X; the wire tier values
  remain provisional until the Phase S measurements land in
  `docs/xbox-1440p-negotiation.md`.
- No physical-panel latency claims. The Sunshine virtual display is 3840x2160 @
  240 Hz but is not a panel; the 120 Hz validation display is unresolved.
- No runtime, driver, model or SDK file was changed, added or bundled. Defaults are
  byte-identical; no provider patching on Blackwell.

## Colour-preserving NR configuration (user choice B, no code change)

The user wants NR detail and temporal stability without NR's colour change. This
is achievable with shipped settings on the NR layer editor, and was delivered as
a configuration recipe, not a product-code change. Defaults remain byte-identical.

- Image correction: Enable correction on, Correction mode Manual.
- Source color retention (`colorRetention`) 1.0: restores source hue and
  saturation in Oklab (`shaders/NrCorrection.hlsli:114-124`).
- Lightness retention (`luminanceRetention`) 0.0: keeps NR lightness and
  structure changes. Setting it to 1.0 cancels all NR effect.
- Hue protection 1.0, Chroma protection 1.0, Neutral color protection 1.0,
  Highlight protection 1.0, Local compression 0.75, Temporal stability 0.8.
  Temporal stability becomes the history gate while correction is on
  (`EnhancementSettings.h:308`, `NrLayerEditor.qml:392`).
- Optional: Residual Color change 0 removes the NR chroma delta before
  correction. Do not set residual total to 0, which bypasses all NR output.

The output stabilizer is a separate node (`nrHoldStrength`), needs NR enabled,
and holds the previous output where the source has not visibly changed. This
configuration is not pixel-identical to NR off: lightness and structure still
change by design. All control keys are covered by the existing QML contract
tests (`tests/qml/quick/tst_components.qml:561-571`).

## Preserve source look NR switch (user request 2026-11-23)

An opt-in switch "Preserve source look" was added at the top of the NR layer
editor (`qml/Veyra/NrLayerEditor.qml`). It reuses existing bridge keys only, so
there is no C++ or preset-schema change: on toggles NR residual `color` to 0
(source hue and saturation preserved), residual `luminance` to 0.3 (lightness
drift limited) and NR `temporal` on (flicker and smear reduction); off restores
`color` and `luminance` to 1. The switch reads its checked state from those
three values. Default is off, so existing configurations are untouched.

This was implemented as an NR setting rather than a preset at the user's
direction. Two earlier preset approaches were reverted: a correction-bearing
built-in forced every preset library to v9, and an output-stabilizer built-in
forced a v21 `.output-controls` sidecar. The output stabilizer remains a
separate node. i18n: two new strings for the switch and its hint in
`i18n/catalog.json` with all nine translations.

## Decisions needed to proceed

1. Clip corpus: supply the openh264 tool root, supply 1440p60 clips, or authorize
   installing openh264 and rebuilding the tools FFmpeg.
2. Physical Series X availability for a live Phase S/L session (the HUD-stats
   check and the per-hop before table cannot be done from replay).
3. Display for the 4K120 panel-side run (physical panel vs Sunshine virtual).
4. FG Lite runtime: whether the 310.9.16 `nvapp_override` dll will be supplied for
   Phase C; it stays default-off and unmeasured otherwise.
