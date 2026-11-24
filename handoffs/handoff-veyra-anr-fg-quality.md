# Handoff: ANR FG quality program (6 items)

Planner session 2026-11-22, repo `C:/Work/Projects/Veyra-NRVideo`, branch at planning time
`feature/xbox-1440p-english` (HEAD `a211eb39`). This is a plan only; no product code was
changed. The Build agent executes from this document.

## Summary

Veyra's ANR FG looks far worse than in-game ANR/DLSS-G/VFG because of the inputs, not the
adapter. Ranked causes (evidence in the investigation below): a constant-0.9 depth plane,
dense estimated motion with borrowed vectors in disocclusions, HUD interpolated with the
scene, B15-era weights, synthetic camera parameters, and frequent recurrent-state resets.
Six changes address them. Items 1-3 and 6 are small; item 4 (real depth guidance) is the
large one; item 5 (HUD-less color) is deliberately last and gated.

The user asked for an implementation plan covering all six. Ordered execution:
**1 → 6 (measure) → 2 → 3 → 4 → 5**, each on its own `feature/` branch, each validated
before the next begins. Item 6's measurements decide the parameter values used by 2-4.

## Goal

Close the observable quality gap between Veyra's ANR FG output and (a) ANR FG driven by
game-native inputs and (b) NVIDIA VFG, on the same content, without breaking the shared
graph contracts, other FG backends, NR/SR consumers, or existing defaults.

Done criteria (program level):

- On the occlusion ground-truth harness (new, item 2) and on at least one nr-record replay
  comparison, ANR-with-Veyra-inputs measurably approaches ANR-with-native-inputs; the
  remaining gap is documented with numbers, not adjectives.
- Every existing test that passes today still passes; failures kept as evidence per repo
  discipline. No default settings changed without explicit user sign-off.
- No ANR runtime/model assets enter Git, the source ZIP, or any public package.

## Facts established during planning (verified, with references)

Investigation record: memory entry `ANR FG quality gap in Veyra` (2026-11-22) and the two
prior conversation rounds. Key code facts the Build agent can rely on:

- Depth: `src/pipeline/EnhanceGraph.cpp:1183` fills `depthTex_` with `0.9f` once
  (`initZeroAndDepthTextures`); nothing ever writes it again. FG eval passes it at :4337.
  ANR requires depth (upstream `record/FORMAT.md`: backbuffer/depth/motion Required;
  provider DLL error strings "no depth or motion"; `nr_replay.cpp:212` rejects recordings
  without depth; model inputs `color0/color1/depth0/depth1` + `src_motion`).
- Motion: NVOF grid 4 (`nvofGrid_`, test hook `VEYRA_TEST_NVOF_GRID` accepts 1/2/4,
  EnhanceGraph.cpp:1123) → `FlowRefine.hlsl` (RGBA16F out: xy vector, z cost,
  w forward-backward disagreement) → `FlowFill.hlsl` (RG16F out; borrows neighbor vectors
  where `filled()` = cost > threshold or disagreement > consistencyThreshold) →
  `FlowAdapt.hlsl` upscales to `fgBaseFlow_` (work extent, RG16F).
  `fgMotionProbe` (EnhanceGraph.h:564) overrides the FG motion input in tests.
- Invalid-motion plumbing exists but is unused: `DlssFgBackend.cpp:412` sets
  `motionVectorsInvalidValue = FLT_MAX`; no texel ever carries it. The ANR DLL reads
  `DLSSG.MvecInvalidValue` (string table verified in B15 and B20 binaries).
- HUD: `DlssFgBackend.cpp` create declares HUDLess/UI/UIAlpha never-provided (:262 area).
  ANR then generates from "the backbuffer with its HUD" (its own log wording). HudLock
  (`include/veyra/pipeline/HudLock.h`, `shaders/HudLockUpdate.hlsl` /
  `HudLockCompose.hlsl`) computes per-pixel stillness+contrast state (RG16F `state_`)
  each real frame and composes locked pixels from the held real frame after generation.
  Default off (`EnhancementSettings.h:512`); compose is wired at EnhanceGraph.cpp:4382.
  Ordering fact: `hudLock_->update` runs at :4254, after the flow section (:3466-3615)
  and before FG eval (:4322+). A motion-masking pass consuming HudLock state therefore
  belongs in the FG section, not the flow section, or reads one-frame-old state.
- Camera: `DlssFgBackend::evaluate` passes a stationary synthetic pinhole camera
  (FOV pi/2, near 0.1, far 1000, identity clipToPrevClip), `depthInverted=0`.
  SDK depth contract (`third_party_local/nvidia/DLSS_SDK_310.7.0/include/nvsdk_ngx_defs_dlssg.h:225`):
  linearized depth = `1/(1-depth)` when not inverted; `LinearizedDepth_Scale` default 1,
  `NearFarPartition` default 600, `MinRelativeLinearDepthObjectSeparation` default 40.
  Constant 0.9 → every pixel at linear depth 10, one surface, all "near".
- Runtime: staged/tested runtime is B15 (model `fbv-s0+silhouette_blend=true`, DLL SHA-256
  `ae7424b87d13a0e3a9a99dd02d1ea35b9de96c2caa9cd4300d73bac5bb0cf4e8`, 3,706,880 bytes).
  B16 shipped new weights (`fn-pc8s1-s0`) fixing tearing/doubling/flicker at speed;
  B17 stability; B18 24-bit-depth fix; B19 AVX2/MSFS fixes + up to 7% Blackwell speedup;
  B20 scRGB FP16 HDR. B20 addon provider SHA-256
  `138abe36a55003847dcf2b18c5e208138fb8e542b263cfeeb36613f362cee6f6` (from the release's
  own `nr_fg_package.json`). `AnrProvider::open` checks only the 6 exports +
  `DLSSG.MultiFrameCountMax`; B20 keeps both (export table verified).
  ANR keeps recurrent temporal state (DLL log: "the next pair starts without the
  recurrent state"); `resetFg` (EnhanceGraph.cpp:4198) fires on reset, first frame,
  history skip, and budget reseed.
- Upstream harness: `nr-record` / `nr-replay` (github.com/apex-compute/ANR, `record/`
  subtree, FORMAT.md + replay/README.md) record a game's DLSS-G inputs losslessly and
  replay them through any provider DLL offline with the recorded parameters. This is the
  A/B instrument for the whole program. Licensing: ANR author forbids Beta redistribution
  (issue #9 comment 2026-11-13); nr-record/nr-replay source is in the public repo, tools
  ship in official releases. User-supplied runtime only; never mirror or bundle.
- Depth model license: Depth-Anything-V2-Small is Apache-2.0 (upstream README); Base/
  Large/Giant are CC-BY-NC-4.0 and must not be used. ONNX exports exist
  (github.com/fabio-sim/Depth-Anything-ONNX); the exact artifact, revision and SHA-256
  must be pinned by the Build agent before any download code is written.
- Repo precedent for on-demand assets: `src/ui/TranslationAssets.{h,cpp}` (pinned URL +
  byte count + SHA-256, QNetworkAccessManager, lock file, staged extract, per-file hash
  re-check). Precedent for ONNX-on-D3D12-compute: `include/veyra/beauty/YuNetGpu.h`
  (hand-compiled kernels, no ORT dependency). Precedent for motion-compensated carry with
  mismatch fallback: `shaders/NrReuseWarp.hlsl` + `docs/NR_INFERENCE_REUSE_2026-11-07.md`.
- Existing test surface to reuse, not reinvent:
  - `tests/integration/FgMotionBoundaryTests.cpp` (`veyra_fg_motion_boundary_tests`):
    orbit/walk scenes, analytic mid-frame truth, motion variants (zero/product/ideal/
    blocky/zeroband/inverted/garbage). Currently `<dlss|fsr>` only (wmain :181-195);
    adding `anr` is a small extension.
  - `tests/integration/SrFgDepthResponseTests.cpp` (`veyra_srfg_depth_tests`): already
    uploads per-frame depth through `graph.diagnosticDepthResource(true)`
    (EnhanceGraph.h:852) with a moving-object scene and edge-band MAE. Modes 8/9 are
    per-frame object depth; this is the depth-response instrument for item 4.
  - `tests/integration/RepairFgTests.cpp` (`veyra_repair_fg_tests`): `-hud`, `-exact`,
    `-pan`, provider arg `anr`, debug layer. Translation truth only; passes with flat
    depth because translation has no disocclusion.
  - `tests/integration/DepthResponseTests.cpp`: NR depth-response, do not regress.
- Build/test infrastructure: `CMakeLists.txt` `veyra_add_compute_shader` (:276-373) per
  shader; new shaders need an entry there and a `scripts/acceptance/sync-built-shaders.py`
  sync into any test app copy. Settings plumbing pattern: `EnhancementSettings.h` →
  `activeVideoSettings` normalization (:555-615) → `GraphDescription.h:194` →
  `EnhanceGraphDesc` → `applySettings` live-vs-rebuild checks (EnhanceGraph.cpp:4520+) →
  `src/ui/bridge/FrameGeneration.cpp` + `src/ui/QmlPlayerBridge.cpp` + QML
  (`qml/Veyra/ProPage.qml`, `NodePage.qml`) → `i18n/catalog.json` via
  `scripts/i18n/extract.py`. GPU timing: `GpuStage` enum
  (`include/veyra/diagnostics/FrameMetrics.h:10`) has static_asserts on Fg1..Fg7/Batch
  ordering; append new stages before `Count`, never reorder.

## Environment constraints (binding)

- The working tree is dirty with unrelated work (virtual-camera removal, i18n sweep,
  `src/ui/bridge/App.cpp` untracked). Do not commit, stash, reset or overwrite any of it.
  All program work happens in separate worktrees under
  `E:/项目/Veyra/worktrees/<task>` branched from `feature/xbox-1440p-english`.
- Artifacts (build/tests/logs/reports/downloads) only under
  `E:/项目/Veyra/{build,tests,logs,reports,tmp,downloads,...}/<task>`. The E: task root
  was not present on this machine during planning; the Build agent must confirm it
  exists (or ask the user where the task root lives) before creating anything. Nothing
  on C:, Desktop, Downloads or system Temp.
- Build concurrency ≤ 2, ≤ 900 s per invocation; each test ≤ 300 s. Private data dirs for
  every test run. No global environment changes; `VEYRA_TEST_*` hooks are set per child
  process only.
- No push, no release, no issue comments, no machine shutdown without explicit user
  request. Commits only when the user asks; Conventional Commits, configured Git
  identity, no AI attribution, no `--no-verify`.
- ANR runtime files are user-supplied. The Build agent may download the official release
  ZIP to the task downloads dir for local experiments only if the user authorizes the
  download; verify SHA-256 against the release's own `nr_fg_package.json`/SHA256SUMS.
  Never commit, mirror, or repackage them.
- Do not change existing defaults (HudLock off, Edge Aegis on, GPU-high, VFG auto off,
  UI refresh cadences) without explicit user sign-off.

## Ordered steps

### Item 1 — Stage the B20 runtime (no product code)

Branch: none (runtime files only). Task name suggestion: `anr-b20-stage`.

1. Ask the user to supply `anr-fg-V.B20-addon.zip` (or authorize the official download to
   `E:/项目/Veyra/downloads/<task>/`). URL:
   `https://github.com/apex-compute/ANR/releases/download/FG-V.B20/anr-fg-V.B20-addon.zip`.
2. Verify the provider DLL SHA-256 equals
   `138abe36a55003847dcf2b18c5e208138fb8e542b263cfeeb36613f362cee6f6` and every file
   against the release SHA256SUMS; record the full file list + hashes in
   `reports/<task>/ANR-RUNTIME.json` (same shape as the B15 experiment record).
3. Preserve the existing B15 directory byte-for-byte (move to
   `E:/项目/Veyra/archives/<task>/anr-b15/`, do not delete). Stage B20's
   `Apex Neural Rendering FG/dlssg/*` into the test app's
   `runtime_local/nvidia/anr/` (path contract: `AnrProvider::directory` =
   `<runtimeRoot>/anr`, `RuntimePaths.cpp:43`). Keep `nr_fg.ini` as shipped
   (`scene_cut = 1`, `max_generated = 5`); do not rename or modify any DLL.
4. Run the existing ANR checks against B20 with the current product EXE (no rebuild
   needed): `veyra_repair_fg_tests` (`-exact`, `-pan`, `-hud`, provider `anr`),
   `veyra_fg_lifecycle_tests`, and one real UI launch via a private-config launcher.
   Confirm `Init result=0x1` and `runtimeMax=5` in the log, 0 debug-layer errors.
5. Record B15-vs-B20 output differences on the same fixtures (MAE tables, screenshots).

Acceptance: B20 loads, creates, evaluates 2X-6X, all existing ANR checks pass, hashes
recorded, B15 archived. Rollback: restore the archived B15 directory.
Risk: B20 could change the depth/motion state contract; if the debug layer reports new
errors, keep the failure evidence and stop; the adapter's COPY_DEST bridge
(DlssFgBackend.cpp:426) is the first place to look.

### Item 6 — Measurement harness and parameter probes (before 2-4)

Branch: `feature/anr-quality-harness`. Task: `anr-quality-harness`.

1. Extend `tests/integration/FgMotionBoundaryTests.cpp` wmain to accept `anr` (map to
   `FrameGenerationBackend::Anr`; runtime path arg like RepairFgTests). Run orbit+walk ×
   {zero, product, ideal, blocky, zeroband} on B20. This quantifies how much ANR uses the
   fed vectors vs its own flow, per scene. Keep every variant's numbers.
2. nr-record/nr-replay comparison (needs one user-recorded game capture; ask the user to
   record with the official nr-record 1.2 in a DLSS-FG title, or authorize a local
   recording session). Replay the same recording three ways:
   (a) provider B20 with native inputs; (b) provider B20 with depth replaced by the
   constant 0.9 plane and motion replaced by Veyra-style estimated flow (a small Python
   or C++ preprocessing step over the recording, or replay through the product graph via
   RepairFgTests-style harness if simpler); (c) NVIDIA DLSS-G with native inputs.
   Produce side-by-side videos + MAE/edge-band tables under `reports/<task>/`.
   This is the program's north-star metric; re-run it after items 2, 3, 4.
3. Parameter probes, offline only, no product defaults touched:
   - Depth linearization: in a test-only eval path (extend `EvalDesc` with optional
     overrides, default = current behavior), sweep `LinearizedDepth_Scale`
     {1, 0.1}, `NearFarPartition` {600, 4000}, constant depth {0.5, 0.9, 0.99} on the
     occlusion fixture. Purpose: find whether any constant-depth parameterization
     meaningfully helps before real depth lands, and pick the mapping constants for
     item 4 (DAV2 outputs relative inverse depth; decide `depthInverted` and scale so
     the linearized range straddles the partition sensibly).
   - Flow grid: `VEYRA_TEST_NVOF_GRID=2` vs 4 on the same fixtures; record MAE and
     `gpuFlowP95Ms`. Grid 2 doubles NVOF cost; adopt only if the quality delta is real.
   - Reset frequency: instrument `resetFg` causes (counter per cause in FrameMetrics,
     log-only) and measure on a live-preview run with budget pressure. If ANR's
     recurrent-state loss dominates, propose (do not implement unilaterally) keeping
     history across budget skips for ANR only.
4. Write `reports/<task>/PROBES.md` with the numbers and the chosen constants.

Acceptance: harness runs green on the current build; B20 native-vs-estimated gap
quantified; probe decisions recorded with evidence. Files:
`tests/integration/FgMotionBoundaryTests.cpp`, `include/veyra/ngx/DlssFgBackend.h`
(optional eval overrides, test-only), `src/ngx/DlssFgBackend.cpp`,
`include/veyra/diagnostics/FrameMetrics.h` (reset-cause counters),
`src/pipeline/EnhanceGraph.cpp` (counter increments), new scripts under
`scripts/acceptance/` for the replay preprocessing.

### Item 2 — Invalid-motion sentinel for disocclusions (ANR consumer boundary)

Branch: `feature/anr-motion-invalid`. Task: `anr-motion-invalid`.

Design decision (made here): do not change `FlowFill.hlsl` output or shared flow. The
fill mask is recomputable from `fgFlowRefined_` (z cost, w disagreement) with the same
thresholds FlowFill uses. Add one ANR-only compute pass that produces the FG motion
input texture:

1. New shader `shaders/AnrMotionMask.hlsl`: inputs `fgFlowRefined_` (flow extent,
   RGBA16F) and the adapted `fgBaseFlow_` (work extent, RG16F); output a new work-extent
   RG16F texture `anrMotion_`. Per work pixel: sample the refined texel at the mapped
   flow coordinate; if `filled()` (cost > threshold OR, when consistency is on,
   disagreement > consistencyThreshold) write the sentinel (65504.0, half max) in both
   channels, else copy the adapted vector. Constants mirror the FlowFill values
   (threshold 0.06, consistency 2.0) passed from the graph, not hardcoded twice.
   Register the shader in CMake (`veyra_add_compute_shader`).
2. Graph wiring (`EnhanceGraph.cpp`): allocate `anrMotion_` + pass only when
   `desc_.frameGenerationBackend==Anr && flowRefine_`; run it in the FG section just
   before eval (after `fgBaseFlow_` is final); FG motion selection at :4322 uses
   `anrMotion_` for ANR when present. When refine is off or flow is absent, keep today's
   input (no sentinel without a mask). `EvalDesc` gains `float mvecInvalidValue`
   (default FLT_MAX = current behavior); the graph sets 65504 for the masked path;
   `DlssFgBackend::evaluate` writes it instead of the literal at :412.
   Non-ANR backends: zero change. NR/SR: zero change.
3. Settings: `EnhancementSettings::fgMotionInvalid` (bool). Default decision: propose
   ON for ANR only (normalization forces it off for other backends), but the default
   flip needs user sign-off; ship the setting default-off first, measure, then ask.
   Plumb through GraphDescription, applySettings rebuild check (texture allocation
   changes graph shape → rebuild), UI toggle next to Edge Aegis, i18n extract.
4. Tests: new occlusion ground-truth mode in `veyra_fg_motion_boundary_tests` (the
   orbit/walk scenes already have analytic disocclusion strips; add a `masked` variant
   using the product mask path) + `veyra_repair_fg_tests -hud` regression. Success =
   edge-band and disocclusion-strip MAE drop vs `product` on B20, interior MAE not
   worse, DLSS/FSR/VFG outputs bit-identical to before (their paths untouched), 0 debug
   errors. Also run the item-6 replay comparison with the mask applied to the estimated
   motion input.

Acceptance: measured MAE improvement on the occlusion fixtures; no regression on any
other backend; setting round-trips (save/restore/preset); rollback = setting off (graph
rebuilds to the old input) or branch revert.
Risks: ANR may treat the sentinel as literal motion if it ignores `MvecInvalidValue`
(the string exists in the DLL, behavior unverified) — the `garbage`/`inverted` control
variants from item 6 detect this; if ANR mishandles it, fall back to zeroing masked
texels instead (still better than borrowed vectors) and record the finding.
Half-float 65504 is exactly representable in RG16F; do not use FLT_MAX in the texture.

### Item 3 — HUD motion zeroing from HudLock state (pre-generation)

Branch: `feature/anr-hud-motion-lock`. Task: `anr-hud-motion-lock`.

Design decision: reuse HudLock's `state_` texture; do not duplicate the stillness
detector. The compose shader's lock predicate (stillness smoothstep 0.80-0.95 × 3x3
edge growth) is the mask definition; extract it into a shared `.hlsli`
(`shaders/HudLockMask.hlsli`) included by both `HudLockCompose.hlsl` and the new pass so
the two can never drift.

1. `HudLock` gains a read accessor for `state_` (or a `maskMotion(list, tracker, motion,
   width, height)` method that runs the new pass; prefer the method, it keeps descriptor
   staging inside HudLock where the state SRV already lives).
2. New shader `shaders/HudLockMotion.hlsl`: input motion (work extent RG16F, the item-2
   `anrMotion_` when present, else `fgBaseFlow_`) + HudLock state; output the same
   texture the FG eval will read. Where locked (shared predicate), write zero vector;
   elsewhere copy. If item 2's sentinel is active, locked pixels get zero, never the
   sentinel (a still HUD pixel has valid zero motion).
3. Ordering: run in the FG section after `hudLock_->update` (:4254) and before eval
   (:4322). When HudLock is off, nothing changes. When on but the backend is not
   ANR/DLSS/FSR graph FG, keep today's behavior (compose still applies to all graph FG;
   motion zeroing may apply to all flow-fed graph FG backends — decide from item-6
   numbers; ANR-only is the conservative start).
4. The masked motion texture must not alias `fgBaseFlow_` (NR/SR/DLAA read it). Write
   into `anrMotion_` when it exists; otherwise allocate a second work-extent RG16F
   `fgLockedMotion_` used by the flow-fed graph FG backends when HudLock is on.
5. Settings: no new toggle; this rides the existing `hudLock` setting (its UI hint text
   updates to say it now also steadies HUD motion before generation). i18n extract.
6. Tests: `veyra_repair_fg_tests -hud` with hudLock on/off × ANR/DLSS/FSR; success =
   ANR UI-region MAE drops toward DLSS's 0.84 baseline, edge/interior not worse, real
   frames untouched, compose still passes its existing checks, 0 debug errors. Add a
   moving-HUD case (HUD element that starts moving must release within a frame — the
   state detector already does this; assert the generated frame follows).

Acceptance: measured UI MAE improvement with hudLock on; hudLock off is bit-identical
to today; rollback = revert branch (no default changed).
Risks: capture noise keeps pixels from settling (HudLock docs note this) — the pass
then does nothing, which is safe; one-frame state latency is avoided by the FG-section
placement; do not move `hudLock_->update` earlier without re-validating compose.

### Item 4 — Real depth guidance (the large item)

Branch: `feature/anr-depth-guidance`. Task: `anr-depth-guidance`. Multi-session.
Spec authority: `VEYRA_PRODUCT_SPEC_V1.md` §8.2 (read it in full; it is Chinese,
historical, and binding on behavior). This item implements that spec for the FG depth
input; it does not invent new behavior.

Sub-steps, each independent and separately validated:

4a. **Asset pipeline.** Pin the DAV2-Small ONNX artifact (fabio-sim/Depth-Anything-ONNX
    or an own export from the Apache-2.0 `vits` checkpoint; record which, revision,
    SHA-256, input shape, FP16). New `src/ui/DepthAssets.{h,cpp}` modeled on
    `TranslationAssets` (pinned URL/bytes/SHA-256, lock, staged extract, per-file hash
    re-check, cancel). Download target under the user data dir, never the source tree.
    License file (Apache-2.0) + attribution into `THIRD_PARTY_NOTICES.md` and
    `licenses/` when the asset ships; weights themselves never enter Git. UI: a
    "Depth guidance" row in the FG settings area with install status, mirroring the
    translation model row. Gate: user approves the pinned artifact before download
    code is finalized.
4b. **Inference engine.** Decision (made here, flag for user confirmation): ONNX
    Runtime + DirectML EP, loaded dynamically from the downloaded runtime package
    (TranslationAssets precedent), sharing the graph's D3D12 device; ORT DML requires
    its own command queue — the spec's "same device/queue" wording is satisfied at
    device level, and the queue handoff uses GPU fences, never CPU readback in the
    steady state. Alternative if ORT/DML interop misbehaves: hand-compiled D3D12
    compute kernels (YuNetGpu precedent) — much larger effort, keep as fallback, do
    not start there. Fixed input shape (518×518 or the pinned export's shape), FP16,
    batch 1. Bounded: one inference in flight, no unbounded queue.
4c. **Graph integration.** New `pipeline/DepthGuidance.{h,cpp}`:
    - Input: the flow-extent color (`nvofInB_` is the encoded current frame; use the
      same source the flow uses, post-SR working color per spec §7).
    - Cadence: Balanced = every 4th real frame; Low Latency = off or every 8th (spec);
      between inferences, reproject the last depth with the current motion field
      (NrReuseWarp-style carry with mismatch fallback to the last estimate, never to
      stale depth across a cut).
    - Normalization: P02/P98 percentiles of the raw output with a time EMA → map to
      R32F relative depth in `depthTex_`. Percentile computation on GPU (bounded
      histogram pass), no per-frame CPU readback. Choose the mapping + `depthInverted`
      + `LinearizedDepth_Scale` from item 6's probe results so foreground/background
      straddle the near/far partition.
    - Reset: the existing epoch events (seek, cut, resize, source change, device loss)
      clear the EMA and the carried depth; until the first inference completes,
      `depthTex_` keeps the constant-0.9 fallback (today's behavior).
    - Auto fallback (spec): 2D/anime/text/fast-cut content or unreliable motion →
      motion-only (constant depth), decided from the existing scene/cadence signals;
      log the decision, never silently.
    - `depthTex_` becomes UAV-writable; the one-time upload stays as the initial value.
      `diagnosticDepthResource(true)` keeps working (SrFgDepthResponseTests uploads
      must still win when the test writes depth — keep the test hook authoritative).
    - Timing: new `GpuStage::Depth` (+ `DepthWarp`) appended before `Count`; include in
      the processing-time panel; static_asserts preserved.
    - The ANR eval path is the only consumer initially; FSR FG also reads `depthTex_`
      (:4358) — it gets the same improved depth for free, validate it too. NR's
      `nrZeroDepth_` is untouched.
4d. **Settings/UI.** `EnhancementSettings::depthGuidance` enum {Off, Auto, On} (default
    Off until user sign-off; Auto is the spec's target default). Live-vs-rebuild:
    off↔on changes graph shape → rebuild; cadence is live. Plumb the full chain
    (settings → GraphDescription → desc → applySettings → bridge → QML → i18n).
4e. **Validation.** `veyra_srfg_depth_tests` modes 8/9 already prove the graph responds
    to per-frame object depth; add a mode that runs DepthGuidance on the synthetic
    moving-object scene and asserts the estimated depth correlates with truth (object
    region vs background separation) and that FG edge-band MAE improves vs flat depth.
    Then the item-6 replay comparison with estimated depth substituted for native depth:
    the native-vs-estimated gap must shrink measurably. Perf gate: `gpuFlowP95Ms` +
    depth stage ≤ the agreed budget on the reference GPU (ask the user for the target;
    propose: depth adds ≤ 2 ms median at 1080p Balanced on the test machine, measured,
    not promised). Export path: depth guidance runs per-frame in export (no frame-drop
    policy reuse), verify export tests still pass.

Acceptance: spec §8.2 behaviors implemented and evidenced; occlusion MAE improvement on
both synthetic and replay fixtures; no CPU readback in the steady state; bounded queues;
resets verified; Off is bit-identical to today; model assets outside Git; license texts
in place. Rollback: setting Off (rebuild) or branch revert; the downloaded assets remain
user-local.
Risks: DAV2 relative depth is not metric and flickers on cuts (EMA + reset mitigate);
ORT/DML device interop is the main technical unknown — prototype it first in a standalone
test under the task dir before touching the graph; VRAM budget (the graph's estimate at
EnhanceGraph.cpp:859 must include the depth textures + ORT arena); 8 GB GPUs at 4K are
already tight (B15 release note). If inference cannot meet the budget, ship Auto with a
longer interval rather than dropping frames.

### Item 5 — HUD-less color + UI layer for ANR (gated, last)

Branch: `feature/anr-hudless`. Task: `anr-hudless`. Start only after items 2-4 are
validated and the user approves the remaining scope; upstream issues #24/#87 show this
is the hardest artifact class even for game-native integrations.

1. From HudLock state, build per-frame UI color + alpha textures (work extent): alpha =
   the shared lock predicate; color = the held real frame's pixels where locked.
2. HUD-less color: copy the backbuffer, inpaint locked regions from the motion-compensated
   previous frame (the background behind the HUD is usually visible in earlier frames;
   where it never is, fall back to a bounded neighborhood blur — and mark those pixels
   so the UI compose covers them). New shader(s), bounded passes, no readback.
3. `DlssFgBackend::create`: for ANR with the feature on, move HUDLess/UI/UIAlpha from
   never-provided to always-provided flags; `EvalDesc` gains the three textures; eval
   passes them. The SDK helper already writes the slots (verify
   `nvsdk_ngx_helpers_dlssg.h` covers HUDLess/UI/UIAlpha; the ANR DLL reads all three +
   `UserInterfaceRecompositionEnabled`). Resource-state contract for the new inputs:
   determine empirically with the debug layer (ANR's bridge expects COPY_DEST for
   depth/motion; HUD-less may differ — the DLL logs its HUD-less transfer checks).
4. ANR's own UI recomposition replaces HudLock compose for locked pixels when active;
   keep HudLock compose as the fallback when ANR reports it fell back to the backbuffer
   (its log line "our frames come from the backbuffer, with the game's HUD" is the
   signal; surface it through the disable-interpolation/status path or a log-driven
   metric, decide during implementation).
5. Setting: `EnhancementSettings::fgHudless` (default Off; user sign-off to change).
6. Tests: `-hud` fixture with hudLock+hudless on: UI MAE must beat item 3's result;
   inpaint quality measured on a moving-background-under-static-HUD fixture (new);
   never worse than item 3 anywhere; 0 debug errors; other backends untouched.

Acceptance: measured improvement over item 3 on the HUD fixtures with no new artifacts
in the inpainted regions; rollback = setting Off. If the inpaint ghosts, stop and report;
do not ship a worse-than-item-3 path.

## Cross-cutting validation (every item)

- `python -B scripts/i18n/extract.py --check` after any UI string change (0 missing,
  0 placeholder problems; language names stay in their own script).
- Narrowest relevant GPU tests per item (named above), debug layer on, 0 errors.
- `scripts/acceptance/sync-built-shaders.py` for any test app copy after shader changes.
- Re-run the item-6 north-star comparison after items 2, 3, 4; keep all receipts under
  `reports/<task>/`; failures are evidence, never deleted.
- No change to: UI statistics cadences, GPU-high default, VFG auto default, HudLock
  default, Edge Aegis default, existing backend IDs, config format. New settings are
  additive with off/conservative defaults pending user sign-off.

## Risks (program level)

- ANR core is closed; every behavioral claim about it (sentinel handling, depth
  weighting, HUD-less transfer) is inferred from strings/docs and must be confirmed by
  the item-6 harness before a product change depends on it.
- The dirty working tree: all work in separate worktrees; never touch the user's
  uncommitted virtual-camera/i18n changes.
- E: task root absent on this machine at planning time: confirm location with the user
  before creating artifacts.
- B20 contract drift vs B15 (item 1 gate catches it).
- ORT/DML interop and VRAM budget (item 4's prototype-first gate).
- Licensing: ANR assets user-supplied only; DAV2-Small Apache-2.0 only; no Base/Large.
- Do not present submitted FPS as physical refresh or end-to-end latency in any report.

## Acceptance criteria (program, restated compactly)

1. B20 staged with hash receipts; B15 archived; existing ANR checks green.
2. Native-vs-estimated gap quantified on one real recording; gap shrinks after each of
   items 2, 3, 4 with receipts.
3. Occlusion-strip and HUD-region MAE improvements on synthetic fixtures; no regression
   on DLSS/FSR/VFG/XeSS/NR/SR paths (bit-identical where untouched).
4. Depth guidance implements spec §8.2 (cadence, reprojection, EMA normalization, reset,
   Auto fallback), bounded and readback-free in steady state.
5. All new settings additive, default-off/conservative, round-trip through
   save/restore/presets; i18n complete.
6. No ANR/DAV2 binary assets in Git or public packages; license texts in place.
7. Session notes record actual commands, results, failures, paths, and unverified scope.

## Suggested skills for the Build agent

- `diagnosing-bugs` — when a probe or harness result contradicts the inferred ANR
  behavior (sentinel ignored, depth weighting unexpected).
- `tdd` — items 2/3/4: write the failing occlusion/HUD/depth-response fixture first,
  then the pass that makes it pass.
- `codebase-design` — item 4's DepthGuidance module boundary (deep module: cadence +
  reprojection + normalization behind one interface; the graph sees one texture).
- `research` — pinning the DAV2 ONNX artifact and ORT/DML interop facts before 4a/4b.
- `pr` — if the user asks for PRs per item.

Do not invoke `code-review` (it delegates to sub-agents; this repo is single-agent).

## References (read, do not duplicate)

- `VEYRA_PRODUCT_SPEC_V1.md` §7-8 (guidance contract, depth spec) — binding, Chinese.
- `docs/ANR_FG_EXPERIMENT_2026-11-12.md` (B15 integration facts, motion scale truth).
- `docs/FG_REFINED_FLOW_2026-11-13.md` (refine/fill design, HUD MAE baseline table).
- `docs/NR_INFERENCE_REUSE_2026-11-07.md` (motion-compensated carry precedent).
- `docs/DEVELOPMENT.md` (build/validation commands), `AGENTS.md` (discipline).
- Upstream: github.com/apex-compute/ANR — `record/FORMAT.md`, `record/replay/README.md`,
  releases FG-V.B16..B20, issues #9 (license), #14/#24/#27/#87 (HUD artifacts),
  #43 (24-bit depth), #68 (translucency/flow limits).
- Depth model: github.com/DepthAnything/Depth-Anything-V2 (license),
  github.com/fabio-sim/Depth-Anything-ONNX (export candidate, pin before use).