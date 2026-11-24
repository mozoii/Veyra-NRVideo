# Upstream PR merges (2026-11-23)

This fork ported the relevant hunks of three upstream `Likely7/Veyra-NRVideo`
pull requests by hand. No PR branch was merged directly, because the fork has
its own `ResolutionMode` tier table, input path and Xbox-only/English-only
scope. Each author is credited here and in short headers on the ported files.

## Adopted

### PR #274 — `Support 1440p Xbox Remote Play and preserve native software decode`

Author: **SenjuWoo** (`@SenjuWoo`), upstream head `b0755cfc`.

- Native-size software H.264 decode at the stream's decoded size. A null
  `nv12Target` now makes `CaptureCompressedDecoder` own and reuse an NV12 frame
  at `frame->width/height`; `XboxSessionSource` clones it before publishing and
  no longer allocates a fixed target. Fixed-size callers keep the old scaling.
- `CaptureCompressedDecoderTests.cpp` verifies the native extent and that the
  retained clone survives `decoder.close()`.
- `EngineController::snapshot()` clears a stale `xboxActive` when the transport
  is `Empty`.
- Not adopted: `StreamResolution.h` and its `Auto/1080/1440` enum (duplicates
  `ResolutionMode`), the `play(resolution)` API changes, the CMake reordering,
  the PS5 `ControllerInput` clock change, and the virtual-camera CMake change.
  The fork's 1440p tier table, `VideoPreference`,
  `userRequestedResolutionUpdate`, per-tier device info and Xbox-only SDL wiring
  predate this merge; only the native-decode delta was ported.

### PR #287 — `fix(xbox): send real gamepad physicality and no touch points`

Author: **Kevin** (`@kevinmx21`), upstream head `dc268283`.

- `GamepadPhysicality` bit enum and `gamepadPhysicality()` after the button enum,
  using the exact bit layout from Geocld/XStreaming `abc02ff`.
- `gamepadReport()` and the fork's combined `inputReport()` now send the
  per-frame computed physicality (u32 LE) and virtual physicality 0. The fork
  sends most input through `inputReport`, so the upstream single-function patch
  was adapted to both writers; they produce identical pad bytes.
- `clientMetadataReport()` defaults to 0 touch points, the handshake call passes
  0, and a `/streaming/touchcontrols` `TransactionStart` receives
  `ReceiverCancel` on the message channel.
- Tests cover the bit layout, idle pad, zero touch points, the combined packet
  and the transaction loopback.
- Not adopted: the upstream `Co-Authored-By: Claude Opus 5.5` trailer. No AI or
  harness attribution was carried into any commit.

### PR #289 — `Xbox: selectable V2 transport (Chromium reference player in WebView2) + HQ profile, D3D11VA option, loss recovery, HUD`

Author: **RyanBB** (`@RyanLookchard`), upstream head `1d317571`.

Phase A (default-off transport options and resilience):

- `StreamProfile.h/.cpp`: `TransportProfile` with `DecodeBackend`,
  `IceServerConfig`, `parseIceServerUrl()` and the seven reference STUN servers.
  No `QualityTier`: the viewport still comes from `resolutionDesc(mode)`.
- `StreamApi`: one-shot token refresh on HTTP 401/403 with a fresh gsToken;
  `keepAlivePulseInSeconds()`; `teredoIpv4()` also returns the server IPv4;
  `parseIce(..., fullTeredo)` adds the client/server x decoded/candidate/3074
  combinations with deduplication, while the default stays byte-identical.
- `Account::refreshStreamingAccess()` drops the cached token outside the mutex
  and signs in again.
- `WebRtcSession`: public STUN and valid custom ICE servers when configured,
  `b=AS` only when a bitrate is greater than zero, and an 8 s gathering budget
  only when ICE servers exist (5 s otherwise).
- `VideoLossRecovery`: bounded reorder buffer, RFC 4585 NACK on holes, whole
  frame drops after the wait window, and counters. The upstream debug
  `fprintf` in the receive path was removed. New unit tests cover in-order,
  reorder, NACK, retransmit and drop-whole; upstream shipped no tests.
- `CaptureCompressedDecoder::setPreferD3D11VA()` and the
  `DecodeBackend::D3D11VA` option; D3D11 frames reach the graph through
  `hardwareSurface()`, are reported as hardware and use `backendName()`.
- `XboxModel`/`XboxSourceDialog`: bitrate cap (0..40 Mbps), Public STUN, Full
  Teredo and Hardware decode API. All default to the historical behaviour
  (0 / off / off / Auto).
- HUD: NACKs, retransmit recovered and whole-frame drops on the Xbox line; the
  PC branch is unchanged. `XboxModel::set()` now loads settings on first write
  so a fresh model cannot clobber saved values.
- Not adopted: `QualityTier` and its fixed 1080p HQ profile, the `www.xbox.com`
  browser identity, custom TURN with a plaintext password in `settings.json`,
  removal of V1, and the V2 probes, README and bilingual PR templates. The
  stream-stats HUD ungate was already present from local work (`ff205d71`
  upstream), so no code change was needed; credit only.

Phase 2 (V2 WebView2 transport, in scope):

- `XboxStats.h` and `IXboxSource.h` move the shared types out of
  `XboxSessionSource.h`; both V1 and V2 implement `IXboxSource` using this
  fork's `input::ControllerState`/`input::ControllerFeedback`.
- `WebView2Host` runs the invisible STA WebView2 and relays encoded H.264/Opus
  and signalling; `XboxWebSource` reuses `StreamApi`, the shared decoder and
  the audio session. `GamepadBridge` maps `input::ControllerState` to the
  player's InputFrame with the exact inverse, Y pass-through and `INT16_MIN`
  clamp, covered by `veyra_xbox_web_gamepad_tests` (30 checks).
- V1 remains the default and selectable. V2 is a 1080p-class engine: a physical
  Series X returned a 1920x1080 decode for a 1440p V2 selection, so 1440p routes
  to V1 with a visible note.
- 1440p support on V2 was attempted twice and removed: an
  `RTCDataChannel.prototype.send` shim and a build-time port of Greenlight PR
  #1752 wrapping the player's channel instance methods. Neither produced a
  2560x1440 decode. Source rebuild (Route 1) remains blocked because the patch
  at `e4c12bd` is not published. Findings are in
  `docs/WEBVIEW2_1440P_RESEARCH_2026-11-24.md`.

## Notice rule

`AGENTS.md` was amended at the handoff's instruction: legal and vendor notices
stay untranslated, but `THIRD_PARTY_NOTICES.md` and `licenses/` may be updated
to add or correct a notice for a component actually present in the build, with
review. Existing notices are not translated, restyled or removed. Phase A added
no component and changed no notice. Phase 2 added the WebView2 and
xstreaming-player entries only.

## Verification (this environment)

- `veyra_xbox_tests`: 167 checks, 0 failures (baseline 138/0).
- `veyra_xbox_web_gamepad_tests`: 30 checks, 0 failures.
- `veyra_capture_compressed_tests`: native-size software decode and retained
  frame pass with a local H.264 clip; the default fixture path
  `loop/local/fixed_clips/test_h264_1080p.mp4` is absent, so the default run
  SKIPs. With the local 1080p clip all checks pass. A 1440p clip shows one
  pre-existing `decoderReplacement` D3D12-frame-lifetime failure on this driver,
  unrelated to native decode.
- `veyra_qml_ui`, `veyra_ui_i18n_tests`, `veyra_ui_contract_tests` build/pass.
- `python -B scripts/i18n/extract.py --check`: 2483 entries, 0 missing, 0
  placeholder problems. `scan-cjk.py`: no new first-party prose from these files.

## Remaining gaps

Physical Series X capture: gamepad acceptance in a GameInput title, loss-counter
behaviour under real loss, Intel iGPU D3D11VA corruption, A/B of V1 vs V2, and
the V2 1440p decoded size (2560x1440) are all unverified. On 2026-11-24 the
user selected 1440p with V2 on a physical Series X and the HUD showed 1920x1080;
both V2 1440p client patches were removed after the live 1080p result. WebView2 V2 streaming
itself needs a live console session; only the build, mapping and offline pieces
are verified here.
