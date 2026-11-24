# Handoff: Blackwell (RTX 5090) performance plan, 1440p60 Xbox stream to 4K120

Date: 2026-11-22. Planner session on branch `feature/xbox-1440p-english` (HEAD `32d3fb04`, dirty tree preserved: `handoffs/`, `i18n-scripts/`, `main.obj`, `scripts/i18n/*` untracked; do not clean or overwrite).

## Summary

The user wants to exploit Blackwell on this machine's RTX 5090 to cut per-stage GPU ms for the concrete target pipeline: **Xbox 1440p60 H.264 stream in, DLSS SR to 4K, frame generation 2X, 4K120 out**, with two receiver-side directives added 2026-11-22: confirm the incoming bitrate is at its ceiling (Phase S; the user's 30.7 Mbps reading says it already is) and reduce the incoming stream's latency where the receiver controls it (Phase L). Workstreams chosen on the scope card: A (baseline measurement), B (DLSS SR/DLAA models and runtime), C (DLSS-G Lite and dynamic MFG), E (optical flow and FP16 shaders), F (decode and encode). G (NR re-baseline) was not selected. **VFG (D) returned to scope on 2026-11-22** when the user reported a live measurement (below); it is measurement-first, with behavior changes gated on evidence and sign-off.

## User-reported data point (2026-11-22, not yet reproduced by us)

30 fps game content, VFG quality 3 (High+Medium mixed) at 2X, Auto HUD Lock on: **7.1 ms** (assumed the UI FG GPU time = `FgBatch` envelope; working extent unconfirmed, likely post-SR 4K). Same session context: **DLSS SR 1.7 ms, DLAA 1.3 ms, optical flow 1.6 ms** (extents unconfirmed; DLAA runs at its instance's own size, pre- or post-SR). **Windows receive rate: 30.7 Mbps** (user's own network counter; the Xbox stream HUD showed nothing). Facts from code that interpret it:

- At 2X a mixed tier runs only its first model: `vfgPrimarySubframe(2)==1`, `second=(mixed&&sub!=primary)` is false for the pair's only subframe (`src/pipeline/VfgBackend.cpp:342`, `include/veyra/engine/EnhancementSettings.h:69-81`). High+Medium 2X == High 2X in execution; the Medium effect is loaded (~1.9 GB at 4K) and never evaluated. Mixed tiers only differentiate at 3X+.
- The `FgBatch` span includes the GPU shot-change test, CUDA dependency wait, output copy, HUD Lock update and compose (`src/pipeline/EnhanceGraph.cpp:4167-4233`); it is not pure model time. Decompose with `VEYRA_TEST_VFG_GPU_PROFILE=1` (`vfg-gpu` log, per-model CUDA-event ms) and a HUD Lock on/off A/B.
- Fast-model fallback loads only for single-model tiers (`VfgBackend.cpp:623`, `fastFallback&&!p.mixed&&quality>0`), so `vfgAutoRegulate` (2X-only, `EngineController.cpp:2517`) cannot rescue a mixed-tier config.
- Budget: 30 fps content half-interval 16.67 ms, 7.1 ms fits. At the 60->120 target the half-interval is 8.33 ms; 7.1 + base + blit crosses it and enters the `liveFramesShareRefresh` drop-to-source-rate mode (`LivePresentationTiming.h:107-113`).

The core finding: the heavy inference (DLSS SR/FG, NvVFX) lives inside NVIDIA runtimes that already ship Blackwell kernels; Veyra never patches providers on RTX 50 (`include/veyra/ngx/DlssgTransfusion.h:30`, "Never applied on Blackwell"). The levers are model/preset choice, runtime currency, scheduling/admission constants, our own shaders, and the decode/encode blocks. All existing ms numbers in `docs/` are RTX 5070-era and must be re-measured here.

## Verified environment (this session)

- GPU: RTX 5090, 32607 MiB, driver **621.35**. Desktop on the 5090 is 1920x1080 @ **180 Hz**; a Sunshine virtual display reports 3840x2160 @ 240 Hz. A 120 Hz-capable output exists, but which monitor the user wants for panel validation is unresolved (Phase 0 question).
- **`E:\项目\Veyra` does not exist on this machine** (no E: drive; D: is near-empty). AGENTS.md artifact paths are from another machine. Use `build-local/` (already holds `x64-release` with staged `runtime_local`) and keep new artifacts under the session task directory; confirm the artifact root with the user before writing outside the repo.
- Staged runtimes: `runtime_local/nvidia/nvngx_dlss.dll` and `nvngx_dlssg.dll` both **310.9.16.0**; `nr-original/nvngx_dlssnr.dll` 310.9.16 (266,424,944 bytes). FG Lite subdirectory `runtime_local/nvidia/dlssg-lite/` is absent (Lite needs a user-supplied nvapp_override build; see C1). VFG stack staged at NVVideoEffects 1.3.0.0 (Phase D, measurement-first).
- Vendored libdatachannel v0.24.5 at `C:/veyra-deps/xbox-installed/x64-windows-static` (source tree under `C:/veyra-deps/vcpkg/buildtrees/libdatachannel/src/v0.24.5-28e86f3053.clean`); REMB and `b=AS` support verified in that source for Phase S.
- MSVC 14.51 + Qt 6.8.3 built and ran targets here on 2026-11-22 (`docs/CURRENT_STATUS.md:17`); the toolchain is available.

## Frame-budget facts for the target pipeline

- Output interval at 120 Hz: 8.333 ms; generated frame deadline at 2X: ~4.17 ms after its pair anchor, plus the 10 ms grace (`include/veyra/engine/LivePresentationTiming.h:105`, `LiveFgAdmission.h`).
- DLSS SR preset cost on 5090, NVIDIA-published via Digital Foundry (Jan 2026), 1080p->4K Performance mode: preset K 0.87 ms, preset M 1.05 ms (+20.7%). Ada pays up to +39% for M because it lacks the FP8 path Blackwell uses. Our 1440p->4K ratio is smaller; measure, do not extrapolate.
- FG Lite on RTX 5070 (2026-10-14, 2K60 NV12 + NR): DLSS-G GPU P95 4X 5.6 -> 2.8 ms, 6X 8.0 -> 3.3 ms (`include/veyra/ngx/DlssgLite.h:6-9`). Largest FG lever on record; 2X numbers unknown, measure.
- DLSS SR/DLAA pin `perfQuality=1` for every ratio (`src/pipeline/EnhanceGraph.cpp:1842,1891`) and always set `IsHDR|AutoExposure` (`src/ngx/DlssSrBackend.cpp:116-119`). Model presets: 0=K, 1=M, 2=L pinned across all perf modes (`DlssSrBackend.cpp:123-135`); default `dlssSrModel=0`.
- Measurement plumbing already exists: `include/veyra/diagnostics/GpuTimer.h` (direct-queue timestamps, stages incl. Sr/Flow/Nr/Fg1..7/FgBatch/Blit/AaNode), `player-timing` P95 log each second (`src/engine/EngineController.cpp:3256`), `veyra_fg_sustained_tests` prints per-stage P95 and submit fps (`tests/integration/FgSustainedTests.cpp`, target `CMakeLists.txt:1044`; usage: `<source> <logdir> <2|4|6> on|off|nr|sr <30..240>s [minRatio] [profile]`, profile `live-1440p` forces FlowQuality::Performance + DLSS SR + 4K target). File replay through the live scheduler: `captureReplayForTest` + `VEYRA_TEST_REPLAY_AS_CARD=1`.
- Corpus generator: `veyra_clip_gen --scenario <name> --width 2560 --height 1440 --fps 60 --seconds N --output ...` (`tools/clip_generator/main.cpp`, CLI already parameterized; `--make-corpus` only writes 1080p/4K, so generate 1440p clips per-scenario).

## Goal

Measured, evidence-backed per-stage ms reduction for 1440p60 -> 4K SR + FG 2X -> 4K120 on this 5090, delivered as opt-in tiers and (only with explicit sign-off) 50-series default changes. Acceptance: sustained ~120 submit fps with zero budget skips on the replay harness, per-stage P95 table before/after, and no pixel regressions on untouched paths. Submitted fps is never scanout rate or end-to-end latency; keep that language.

## Ordered steps

### Phase S: confirm the incoming 1440p60 bitrate is at its ceiling (user directive 2026-11-22; scope reduced by the 30.7 Mbps reading)

User confirmed 1440p60 through a physical Xbox Series X is working, and Windows-side receive reads **30.7 Mbps**: already above the advertised `maxBitrateKbps=30000` (`include/veyra/xbox/ResolutionMode.h:40`, provisional) and above iGreenLight's typical 17-28 Mbps. The console is not being held down by our advertisement. The levers below are cheap confirmations, not expected gains; do them only after L1 explains the missing HUD:

- S1. Fix/verify the stats path first: the Xbox HUD exists (`qml/Veyra/StreamHud.qml:53-58`, toggle Ctrl+Alt+Shift+S, `qml/Veyra/Main.qml:699-703`) and `XboxModel::updateStream` carries `videoMbps`/`decodeMs`/`rttMs` (`src/ui/XboxModel.cpp:276`). The user saw no stats; find out why (HUD not toggled, `state.streaming` not true, tick gating at `src/ui/bridge/Streaming.cpp:110` needs 15 ticks, or `videoMbps` window not yet >= 1 s at `XboxSessionSource.cpp:721`). Compare HUD Mbps against the OS counter; `videoMbps` counts depacketized payload bytes, so it reads slightly below the wire rate (RTP/SRTP/UDP overhead), which is consistent with 30.7 wire vs ~29-30 payload.
- S2. REMB receiver request (confirmation only): `video_->requestBitrate(profile.maxBitrateKbps * 1000)` after handshake; libdatachannel v0.24.5 re-pushes REMB on every incoming SR (`rtcpreceivingsession.cpp:109-110`). Veyra only calls `requestKeyframe()` today (`WebRtcSession.cpp:383`). If Mbps does not move, keep or drop the call per evidence; it is harmless but must not become a rate cap if the console honors it downward.
- S3. SDP `b=AS` (confirmation only): `video.setBitrate(...)` (`description.cpp:981`, emits `b=AS:` at 1056). Same discipline: measure, keep rollback. A `b=AS` BELOW the console's current rate would cap it; do not ship a value under the observed plateau.
- S4. H.264 level fmtp L5.1 `4d0033` (confirmation only): offer advertises Main L3.1 `4d001f` (`WebRtcSession.cpp:130`); `level-asymmetry-allowed=1` is why 1440p60@30Mbps works anyway. Only worth an A/B if S1 shows the console renegotiating or capping.
- S5. Record the plateau and the answer SDP in `docs/xbox-1440p-negotiation.md` (physical-console evidence for bitrate/level questions; not a full official-client wire capture). Tier constants change only with recorded user sign-off.
- Files: `src/xbox/WebRtcSession.cpp`, `src/ui/XboxModel.cpp` / `src/ui/bridge/Streaming.cpp` (stats path), `include/veyra/xbox/ResolutionMode.h` (constants only with sign-off), `veyra_xbox_tests`.

### Phase L: reduce incoming Xbox stream latency (user directive 2026-11-22, measurement-first)

Goal: cut the arrival-to-glass latency the receiver controls, and state honestly what it does not. The receive path is already lean: 8-AU backlog cap with oldest-drop (`xbox::trimVideoInputs`, `XboxSessionSource.cpp:283`), single-slot latest-frame mailbox (`read()` at 641), `AV_CODEC_FLAG_LOW_DELAY` on the D3D12VA decoder (`FFmpegVideoDecoder.cpp:196-199`), MMCSS on the graph owner. FG at 2X structurally holds the real frame up to half an output interval (8.33 ms at 4K120) for its generated partner; that is a floor while FG is on, not a defect, and no receiver change removes it.

- L1. Per-hop instrumentation before any change. The data already exists but never reaches a HUD row: `Impl::Meta` carries arrival/decoded/submitted/presented host times (`XboxSessionSource.cpp:513,571`), `videoPresented` feeds them back (`:654`), `CpuStage::DecodedQueue` is timed for streams (`EngineController.cpp:2777`), and `FrameFlowWindow` computes software residency mean/P95 (`include/veyra/engine/FrameFlowWindow.h:161`, shown in `apps/veyra/ui/LiveStatusPanel.h:71`). Add an Xbox latency breakdown: queue wait (arrival->decode start), decode ms (already in HUD), decoded->graph pickup, graph residency, present wait; surface in `StreamHud.qml` Xbox row + a periodic `xbox-latency` log line. No new readback or CPU waits; reuse the existing counters.
- L2. Decoder output latency audit: verify the D3D12VA H.264 path emits one frame per input AU (no reorder hold). `waitingForInput()` exists (`XboxSessionSource.cpp:526`); log its per-frame pattern for a session. If a one-frame hold exists, evaluate `AV_CODEC_FLAG_LOW_DELAY` effectiveness on the hw path and `has_b_frames`/`delay` from the codec context; change only with evidence.
- L3. Queue-hop tightening: with L1 numbers, decide whether the 8-AU cap or the decode-thread wake latency contributes measurably. The cap is a latency/robustness trade (dropped AUs force keyframe recovery, `:479-489`); do not shrink it blindly.
- L4. Presentation-side: the FG hold is the floor (above). What IS tunable: `LivePairLatency`/`LiveDeadlineMargin` readiness margins and the capacity wait (`LivePresentationTiming.h:133-145`) were tuned on capture cards; re-check their constants against Xbox stream arrival jitter (L1 data) before touching. `pacingRepair` and `fgStrictAdmission` stay at their defaults.
- L5. Console-side (document, do not promise): encode+send latency lives on the console; RTT (`WebRtcStats.rttMs`) and arrival cadence are the observable proxies. Wired Ethernet vs Wi-Fi on both ends is the user's lever, worth one A/B if RTT jitter shows in L1.
- Acceptance: per-hop table before/after on the physical Series X; any change ships only if it moves a measured hop without raising drops/keyframe requests; end-to-end claims stay "software residency", never panel latency.
- Files: `src/source/XboxSessionSource.cpp`, `src/ui/XboxModel.cpp`, `qml/Veyra/StreamHud.qml`, `src/engine/EngineController.cpp` (log line only), possibly `src/media/FFmpegVideoDecoder.cpp` (L2, evidence-gated).

### Phase 0: decisions and preconditions (no code)
1. Confirm with the user: artifact root on this machine (E: absent), which display to validate 4K120 on (physical panel unknown; the Sunshine virtual display is not a panel), and whether the FG Lite runtime will be user-supplied. The user's physical Series X now streams 1440p60 successfully (2026-11-22); Phase S uses it directly. Replay-as-card remains the path for repeatable GPU measurement runs.
2. Branch `feature/blackwell-perf-4k120` from `feature/xbox-1440p-english`. Preserve the dirty tree.

### Phase A: 5090 baseline campaign (prerequisite for every later default)
3. Build `veyra_fg_sustained_tests`, `veyra_clip_gen`, `veyra_qml_ui` (concurrency <= 2, <= 900 s per invocation).
4. Generate 2560x1440@60 H.264 clips (all scenarios) plus one user-content 1440p60 clip if supplied.
5. Matrix at 2X: DLSS SR model K/M/L x {NR off, NR on}; record per-stage P95 (Sr, Flow, Nr, Fg1, FgBatch, Blit), submit fps, `fgSkippedBeforeEval`, `generatedExpiredAfterEval`, admission logs. Run each <= 300 s, private data-dir/log/cache, TEMP/TMP child-only. Include FG backend rows: VFG High (q2) vs Medium (q1) vs the user's High+Medium (q3, to prove it equals q2 at 2X), each with Auto HUD Lock on/off and `VEYRA_TEST_VFG_GPU_PROFILE=1`, at 30 fps content (reproduce the user's 7.1 ms) and at 60 fps content (the target).
6. Panel-side 120 Hz run via `veyra_qml_ui` (windowed 4K output on the chosen display) for `liveFramesShareRefresh`/latch behavior; log-only, no latency claims without optics.
7. Write `docs/BLACKWELL_PERF_BASELINE_2026-11-22.md` with the table, exact commands, driver/runtime identity, and unverified scope. This is the frame-budget answer the user asked for.

### Phase B: DLSS SR/DLAA model and parameter tuning
8. From A's table decide the 50-series SR/DLAA model recommendation (M is a quality-per-ms trade, not a pure speedup; L may win at high ratios). Any default change is a separate user sign-off; implement as adapter-gated selection or keep the existing user setting, per decision.
9. Audit `perfQuality=1` vs ratio-appropriate modes and the AutoExposure estimator cost for fixed video ratios (`DlssSrBackend.cpp`); measure, then change only with evidence. Keep the 1:1 bypass and DLAA input==output contract.
10. Runtime currency check: staged 310.9.16 vs newest public NVIDIA/DLSS tag; report, do not bundle or swap without authorization (ANR assets stay excluded).
- Files: `src/ngx/DlssSrBackend.cpp`, `include/veyra/ngx/DlssSrBackend.h`, `include/veyra/engine/EnhancementSettings.h` (only if a new persisted field is approved; schema/preset writers must stay compatible), `src/pipeline/EnhanceGraph.cpp` (create sites 1842/1891).

### Phase C: FG Lite and dynamic MFG
11. FG Lite on 5090: user places the 310.9.16 nvapp_override `nvngx_dlssg.dll` in `runtime_local/nvidia/dlssg-lite/`; state machine already exists (`EnhanceGraph.cpp:1564-1570`, states 1 active / 2 missing / 3 compat-GPU). Measure 2X (and 4X/6X for reference) with `dlssgLite` on/off; stays default-off. Verify the create-time-only parameter contract and that RTX 40/30 keep 310.9.1 (transfusion identity pins).
12. Dynamic count validation: `fgAdaptive` already varies per-pair multiplier; confirm against the 310.9.16 runtime that changing `MultiFrameCount`/`MultiFrameIndex` per evaluate keeps history valid (`DlssFgBackend.h:82`, pair-indivisibility comment in `LiveFgAdmission.h:8`), and research the 310.9.x dynamic-MFG parameters in the SDK headers at `VEYRA_DLSS_SDK_ROOT` before touching anything. Test: count switches mid-stream without resets/artifacts.
- Files: `src/ngx/DlssFgBackend.cpp`, `include/veyra/ngx/DlssgLite.h` (docs only), tests under `tests/integration/`.

### Phase D: VFG cost decomposition and rescue paths (measurement-first, re-added 2026-11-22)
12a. Reproduce and decompose the user's 7.1 ms: per-model CUDA ms vs shot-change/copy/HUD-Lock shares at the user's extent; confirm q3==q2 behavior at 2X on this card. Record the Medium-model ms as the candidate rescue tier.
12b. If Medium fits the 8.33 ms half-interval where High does not: propose (sign-off required) either a UI hint that mixed tiers do nothing at 2X, or extending fast-model loading to mixed tiers so `vfgAutoRegulate` can downgrade the pair's single High subframe to the already-loaded Medium effect. The latter touches `VfgBackend.cpp:623` gating and `EngineController.cpp:2517-2528` cost accounting (`vfgFastFgMs` learning already exists); defaults stay off, `vfgAutoRegulate` stays default-off.
12c. VRAM gate re-check on 32 GB: the 4.3 GB free-budget gate for the fast model (`VfgBackend.cpp:619-621`) was tuned on a 12 GB card; verify it never blocks on the 5090 and leave the constant alone unless evidence says otherwise.
- Files: `src/pipeline/VfgBackend.cpp`, `src/engine/EngineController.cpp` (only with sign-off), tests under `tests/integration/`. No provider/model patching; NvVFX SDK stays user-supplied and audited.

### Phase E: optical flow and FP16 shaders
13. NVOF cost table on 5090: grid 4 vs other supported grids, perf FAST/MEDIUM/SLOW (`src/ngx/NvOfSession.cpp:252-254`); Flow is shared by SR/DLAA/NR/FG so savings multiply. Balanced default stays; report only unless a tier change is approved.
14. FP16 microbenchmark first: verify the claimed 2x min16float ALU rate on this card with a standalone compute test before any shader work. If it does not hold, stop E14 and record that.
15. If it holds: cs_6_2 FP16 variants of the bandwidth-light post passes (FlowRefine/FlowFill, NrTemporal, SMAA weights, AaTemporalResolve) behind a capability check (`CheckFeatureSupport` HighestShaderModel + `D3D12_FEATURE_DATA_D3D12_OPTIONS4` Native16BitShaderOpsSupported), opt-in setting, pixel-diff gate vs the FP32 path. New variant helper in `cmake/VeyraShaders.cmake` (today everything is `cs_6_0`, lines 46/73/96); fallback keeps existing DXIL. Most post passes are bandwidth-bound; expect small gains, gate on measurement.

### Phase F: decode and encode
16. Decode: Xbox stream is H.264 (`src/xbox/WebRtcSession.cpp:130-132`) and already D3D12VA (`CaptureCompressedDecoder.cpp:256`), so no target-path change. Re-test the HEVC D3D12VA fault workaround (`src/source/MediaFileSource.cpp:872-887`, RTX 5070 + 616.56 era) on 621.35 with HEVC test files; if fixed, make the D3D11VA detour conditional with the explicit software fallback intact. This helps file playback, not the Xbox target; keep it scoped.
17. Encode (export path only, never preview): AV1 on the 5090's NVENC via `NV_ENC_CODEC_AV1_GUID` in `src/sink/NvencD3D12Encoder.cpp` (today H264/HEVC only, line 78-80); container/writer check in `src/engine/VideoExportJob.cpp` (libavformat mux). New format = user decision on UI/settings scope; export keeps its own frame policy (no preview frame-drop reuse). Split-frame across the three NVENC engines only if single-engine throughput measurably limits 4K120 export; research `NV_ENC_CAPS_NUM_MAX_...`/split-encode caps first.

## Cross-cutting constraints (from AGENTS.md, non-negotiable)

- Single agent, build concurrency <= 2, <= 900 s/build, <= 300 s/test; private data-dir/log/cache per test; no global env, driver, credential or runtime changes; preserve user settings and defaults (GPU-high, VFG auto off, 33 ms stats / 250 ms status).
- Do not claim physical-panel latency; submit-fps and HUD-Mbps evidence labeled as such. 1440p60 delivery itself is now user-confirmed working on a physical Series X (2026-11-22); the wire-level tier values remain provisional until Phase S measurements land in the negotiation doc.
- No provider patching on Blackwell; no runtime/model/SDK files into Git; user-supplied runtime swaps allowed but audited file-by-file.
- Reset contracts (seek/PTS discontinuity/source change/resize) stay explicit; no readback or CPU fence waits added to the normal enhancement path; bounded queues.
- i18n: any new UI strings go through `i18n/catalog.json` (extract.py --check, 0 missing); language names stay in their own script.
- Commit only when asked; Conventional Commits; `feature/` branch; no attribution trailers.

## Risks

- FP16 2x claim unverified on consumer Blackwell; E14 is a kill-gate, not a promise.
- Preset M/L quality/cost tradeoffs are content-dependent; DF numbers are game-renderer inputs, ours are video (no jitter, estimated flow); only our own measurements decide.
- FG Lite runtime provenance is an OTA override build; identity must be logged and audited, never redistributed.
- Dynamic MFG count-switch history semantics are undocumented for our usage; if the runtime resets history on count change, C12 ends as a documented negative result.
- The 180 Hz desktop and virtual displays make "120 Hz panel" claims environment-dependent; record the actual refresh grid per run.
- HEVC D3D12VA re-test could reproduce the device-removal fault; run it isolated with the fallback path armed, never on the user's live session.
- Phase L: shrinking the AU backlog cap trades latency for keyframe-recovery storms; the FG half-interval hold is structural while FG is on. Console encode/send latency is outside receiver control; do not promise end-to-end numbers the instrumentation cannot see.
- REMB/b=AS could cap the rate DOWNWARD if the console honors them below its current 30.7 Mbps; any shipped value must be at or above the observed plateau.

## Acceptance criteria

0. Phase S: HUD stats visible and matching the OS counter within protocol overhead; the 30.7 Mbps plateau documented in the negotiation doc; any wire change has answer-SDP evidence and a recorded rollback; tier constants change only with sign-off.
0b. Phase L: per-hop latency table (queue/decode/pickup/residency/present) before and after on the physical Series X; each shipped change moves a measured hop without raising drops or keyframe requests; FG's half-interval hold documented as a floor; claims stay software-residency, never panel latency.
1. Baseline doc with per-stage P95 + VFG/DLSS-G share of the 8.33 ms budget for the exact target matrix, exact commands, and identity of GPU/driver/runtimes.
2. Sustained 2X replay runs: mean submit fps >= 0.98 x 120 over >= 60 s, `fgSkippedBeforeEval` == 0 and `generatedExpiredAfterEval` == 0 after warmup, on the chosen baseline config.
3. Every shipped change has a before/after measurement on this card and a pixel-identity check on untouched paths (existing diff tools: `tools/diff_surfaces.py`).
4. Default changes only with recorded user sign-off; everything else opt-in with existing defaults byte-identical when off.
5. Relevant existing suites still pass (`veyra_fg_sustained_tests`, contract/settings tests touched by any new field, i18n check); failures kept as evidence.

## Suggested skills for the Build agent

- `tdd` for admission/scheduling logic changes and new unit coverage (count-switch, FP16 capability gate).
- `diagnosing-bugs` if the HEVC D3D12VA re-test faults or FG Lite misbehaves.
- `research` for the 310.9.x dynamic-MFG parameter surface and NVENC AV1/split-encode caps (primary sources: SDK headers at `VEYRA_DLSS_SDK_ROOT`, ffnvcodec headers, NVIDIA docs).
- `code-review` before delivery (adversarial review is a fork rule).
- `pr` if a PR body is requested.

## References (do not duplicate here)

- Fork origin and boundaries: `handoffs/handoff-veyra-xbox-1440p-english.md`, `AGENTS.md`, `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md`.
- 1440p wire status and open capture items: `docs/xbox-1440p-negotiation.md` (1440p Remote Play live on stable per user 2026-11-22; Phase S supersedes its "blocked" status for bitrate/level questions only).
- FG input-quality context (separate effort, not this plan): `handoffs/handoff-veyra-anr-fg-quality.md`.
- Historical 5070 measurements cited above: `docs/BUILD_2.1.6_2026-11-13.md`, `docs/DEDUP_STUTTER_REPAIR_2026-11-12.md`, `include/veyra/ngx/DlssgLite.h`, `include/veyra/engine/LivePresentationTiming.h`.