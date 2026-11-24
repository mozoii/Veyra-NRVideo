# Handoff: merge upstream Xbox PRs #274, #287 and #289

Date: 2026-11-23
Branch: `feature/xbox-1440p-english`
Status: planning complete, Phase A and Phase 2 (WebView2 with 1440p support) in scope, no implementation started
Upstream: `Likely7/Veyra-NRVideo`
Local PR refs already fetched: `upstream/pr/274`, `upstream/pr/287`, `upstream/pr/289`

## Summary

The three PRs do not have equal relevance to this fork.

- **PR #274 (SenjuWoo)** implements an independent 1440p path that this fork already has, but it also contains one genuinely new and useful change: software H.264 decode at the stream's native decoded size instead of scaling into a fixed requested size. Port that delta only.
- **PR #287 (kevinmx21)** is fully applicable. It fixes gamepad input in GameInput titles by sending per-frame physicality bits, reporting zero touch points, and declining touch-control transactions.
- **PR #289 (RyanBB)** contributes both phases. Phase A is the V1 work: RTP NACK/reordering loss recovery, streaming token refresh, keepalive pulse, full Teredo expansion, optional STUN, D3D11VA decode, transport settings and HUD counters. Phase 2 is the WebView2 transport, now **in scope**, with 1440p research and support as a requirement. The shipped player bundle hardcodes a 1920x1080 `dimensionschanged` payload and contains no `VideoPreference`, `userRequestedResolutionUpdate` or 1440 handling. Phase 2 therefore includes two support routes: patch and rebuild the player source, or inject a WebView2 pre-script shim that rewrites the dimensions and adds the control messages. V1 stays the 1440p path until V2 1440p is verified.

Do not merge the PR branches directly. Port the relevant hunks by hand onto the fork's `ResolutionMode` model and its current input path.

## Goal

1. Land the relevant changes from #274, #287 and #289 without regressing the fork's 1440p tier model or its uncommitted Xbox work.
2. Credit the three authors in a durable English document and in short file headers.
3. Keep the fork's existing defaults: Auto resolution, V1 native transport, 1080p and below as the tested path, 1440p still provisional.
4. Amend the fork's notice rule narrowly, build the V2 WebView2 transport as Phase 2 with V1 retained and selectable, and research and implement 1440p support on V2. Keep 1440p on V1 until V2 1440p is verified.

## Read first

- `docs/DEVELOPMENT.md` for the build and validation order.
- `docs/CURRENT_STATUS.md` for the fork's current state and the 1440p caveat.
- `docs/xbox-1440p-negotiation.md` for the wire-value evidence and the ban on presenting provisional values as verified.
- `docs/XBOX_INPUT_RESEARCH_2026-11-23.md` for why the input path was reshaped and what was already tested on the console.
- `handoffs/handoff-veyra-xbox-1440p-english.md` for the fork's origin and the physical-capture gate.
- `AGENTS.md`, in particular the `Paths and preservation` notice rule this plan amends.

## Current state that constrains the work

- The working tree is **not clean**. Twenty-two files are modified, including `include/veyra/xbox/Protocol.h`, `src/xbox/WebRtcSession.cpp`, `src/source/XboxSessionSource.cpp`, `src/ui/XboxModel.cpp`, `qml/Veyra/XboxSourceDialog.qml`, `tests/xbox/XboxTests.cpp` and `CMakeLists.txt`. These are the Steam Controller picker, the Greenlight-shaped 16 ms input queue, the latency instrumentation, the HUD ungate and the source-faithful NR switch.
- Several of those files are exactly the files the upstream patches touch. Do not run `git checkout`, `git restore` or `git reset` on them.
- The fork already has its own 1440p implementation: `include/veyra/xbox/ResolutionMode.h` with six tiers, `VideoPreference` and `userRequestedResolutionUpdate` in `src/xbox/WebRtcSession.cpp`, dimensions per tier in `src/xbox/StreamApi.cpp`, and SDL controller input wired for Xbox-only builds.
- The fork already has the HUD ungate that #289 contains in `ff205d71`. Local work did it independently in `src/ui/bridge/Streaming.cpp` and added a test. No code change is needed; credit it in the merge record.
- The fork's input path is ahead of upstream: combined metadata and gamepad packets, 16 ms tick, no local dedup. Upstream #287 and #289 must be adapted to that path, not the other way around.

## Relevance matrix

| PR | Author | Adopted | Already present in this fork | Not adopted, and why |
| --- | --- | --- | --- | --- |
| #274 | SenjuWoo (`@SenjuWoo`) | Native-size software H.264 decode; decoder-owned NV12 frame when the caller passes no target; tests for the native size; the stale `xboxActive` clear in `EngineController` | 1440p tier table, `VideoPreference`, `userRequestedResolutionUpdate`, per-tier device info, Xbox-only SDL/CMake wiring | `StreamResolution.h` and its `Auto/1080/1440` enum (duplicates `ResolutionMode`), the `play(resolution)` API, the CMake reordering, the PS5 `ControllerInput` clock change, the virtual-camera CMake change |
| #287 | Kevin (`@kevinmx21`) | Per-frame physicality bits, virtual physicality 0, zero touch points, touch-controls decline, test updates | Nothing; the fork still sends constant physicality 1 and big-endian virtual 1 | Only the upstream `Co-Authored-By: Claude` trailer, which must not be carried |
| #289 | RyanBB (`@RyanLookchard`) | Phase A: RTP NACK and reordering loss recovery with stats; streaming token refresh; keepalive pulse; full Teredo expansion; optional STUN; optional `b=AS` bitrate cap; D3D11VA decode option; transport settings and HUD counters; load settings before the first write. Phase 2: V2 WebView2 transport, plus 1440p research and support | Stream HUD un-gate (`moonlightStatsVisible`) and its test | Not adopted: `QualityTier` and the fixed 1080p HQ profile; the `www.xbox.com` browser identity; custom TURN with a plaintext password; removal of V1; the V2 probes, README and PR templates. The shipped bundle is 1080p-only; Phase 2 extends it to 1440p through a source patch or a pre-script shim |

## Decisions

1. **Keep V1 as the default transport and the 1440p path.** V2 is in scope as Phase 2. V1 stays selectable and the default until the V2 A/B and the 1440p question are settled.
2. **V2 must be extended for 1440p.** The PR bundle has no 1440p or `VideoPreference` handling and hardcodes 1920. Phase 2 includes the source-patch and pre-script routes. Until one route is verified on the console, a 1440p selection stays on V1, and V2 is not described as 1440p-capable.
3. **Keep `ResolutionMode` as the only tier table.** Do not introduce `StreamResolution` or `QualityTier`. Any upstream code that needs a viewport gets it from `resolutionDesc(mode)`.
4. **New transport options default to the historical behaviour**: `useStun = false`, `fullTeredo = false`, `videoBitrateKbps = 0`, `decodeBackend = Auto`. The user can turn them on in the Xbox dialog.
5. **No TURN support in Phase A.** Upstream stores the TURN password in `settings.json`; that is a secret in a plaintext user file and is not justified for home streaming.
6. **No `www.xbox.com` device-identity change.** It would alter every tier's play request and is not covered by the fork's physical capture. It stays a separate capture-gated experiment.
7. **The fork rules may be amended.** `AGENTS.md` gets a narrow change: legal and vendor notices stay untranslated, but `THIRD_PARTY_NOTICES.md` and `licenses/` may be updated to add or correct a notice for a component that is actually present in the build, with review. Existing notices are not translated, restyled or removed. Any broader rule change needs the user's explicit scope.
8. **Credit humans, never the AI trailer.** The upstream #287 commit carries `Co-Authored-By: Claude Opus 5.5`. Do not copy it. Do not add any AI or harness attribution to commits.
9. **Phase 2 moves `XboxConnectDesc` and `XboxStats` to `include/veyra/source/XboxStats.h`** so both `XboxSessionSource` and `XboxWebSource` implement `IXboxSource`. Keep the fields the fork added (`controllerGuid`, `resolutionMode`, `transport`).

## Step 0: preserve the work in progress and record a baseline

Files: none. This is a safety step.

1. Create a snapshot under the ignored build tree:
   ```powershell
   New-Item -ItemType Directory -Force build-local/wip-snapshots | Out-Null
   git diff > build-local/wip-snapshots/2026-11-23-before-upstream-prs.patch
   git status --short > build-local/wip-snapshots/2026-11-23-before-upstream-prs.status
   git diff --stat > build-local/wip-snapshots/2026-11-23-before-upstream-prs.stat
   ```
2. Confirm the existing configured build directory. Prefer `build-local/x64-release` if it has `CMakeCache.txt`; otherwise use the `docs/DEVELOPMENT.md` command. Do not reconfigure over the WIP unless the build is already broken.
3. Build and run the current tests so failures are attributable:
   ```powershell
   cmake --build build-local/x64-release --target veyra_xbox_tests veyra_capture_compressed_tests --parallel 2
   ```
   Record the exact check counts in the session note. The last known figures are `veyra_xbox_tests` 137 or 138 checks and 0 failures.
4. If the user wants isolated commits, ask for a checkpoint commit before Step 1. Do not stash and drop the WIP.

## Step 1: PR #287, gamepad physicality and touch points

Files:
- `include/veyra/xbox/Protocol.h`
- `src/xbox/WebRtcSession.cpp`
- `tests/xbox/XboxTests.cpp`

Changes:
1. Add the `GamepadPhysicality` bit enum and `gamepadPhysicality(const GamepadFrame&)` after the button enum. Use the exact bit layout from the PR, sourced from Geocld/XStreaming `abc02ff`. Set a bit for each button present, trigger non-zero, and stick axis non-zero. Return 0 for an idle frame.
2. In `gamepadReport()`, replace `put32(b, at + 14, 1)` and the big-endian virtual `1` with:
   ```cpp
   detail::put32(b, at + 14, gamepadPhysicality(p));
   detail::put32(b, at + 18, 0);
   ```
3. In the fork's `inputReport()` combined packet, apply the same two lines to its pad section. This is the critical adaptation: the PR only patched `gamepadReport`, and the fork sends most input through `inputReport`.
4. Change `clientMetadataReport()`'s default `maxTouchPoints` from `1` to `0` and add the comment about touch layouts. Change the explicit call in `WebRtcSession::onMessageChannel()` from `clientMetadataReport(0, nowMs(), 1)` to `0`.
5. In `onMessageChannel()`, add before the `serverInitiatedDisconnect` branch:
   ```cpp
   if (target.rfind("/streaming/touchcontrols", 0) == 0) {
       if (type == "TransactionStart")
           sendMessageJson(json{{"type", "ReceiverCancel"}, {"id", id}, {"cv", ""}}.dump());
   } else if (target == "/streaming/sessionLifetimeManagement/serverInitiatedDisconnect") {
   ```
6. Update the protocol test:
   - `clientMetadataReport(0, 12.5)` must report 0 touch points.
   - The `pad` physicality must be the computed bit set, not 1.
   - Virtual physicality must be four zero bytes.
   - `gamepadPhysicality(GamepadFrame{}) == 0`.
   - The combined `inputReport` pad section must carry the same physicality.
7. Add one loopback check in `webrtcTests`: send a `TransactionStart` with `target` starting `/streaming/touchcontrols` on the message channel and wait for a `ReceiverCancel` reply. Parse `console.messages` as JSON; the current fake console stores strings.

Tests: `veyra_xbox_tests`.

Commit message: `fix(xbox): send per-frame gamepad physicality and no touch points`

Do not claim this fixes the console input issue. The fork's research note records that constant `1/1` and `0/0` both failed on its console; this change is the semantically correct wire value and matches a client that is known to work, but it still needs a console check.

## Step 2: PR #274, native-size software decode

Files:
- `include/veyra/source/CaptureCompressedDecoder.h`
- `src/source/CaptureCompressedDecoder.cpp`
- `src/source/XboxSessionSource.cpp`
- `src/engine/EngineController.cpp`
- `tests/unit/CaptureCompressedDecoderTests.cpp`

Changes:
1. Decoder header: document that a null `nv12Target` in the stream profile means a decoder-owned NV12 frame at the decoded size.
2. Decoder implementation:
   - Add `AVFrame* nativeNv12 = nullptr;` to `Impl`.
   - Free it in `close()`.
   - Remove `|| nv12Target == nullptr` from the `decode()` guard and adjust the error text.
   - In `convertToNv12()`, use `target->width` and `target->height` in `sws_getCachedContext`, not the decoder's requested `width/height`.
   - Before `convertToNv12`, when `nv12Target` is null, allocate or reuse `nativeNv12` at `frame->width/height`, `av_frame_get_buffer(..., 32)`, and assign it to the local target. If allocation fails, set a clear error and count a failure.
3. `XboxSessionSource.cpp`:
   - Delete `allocNv12()`.
   - Delete the `dummyTarget` member and its destructor free.
   - In `decodeLoop()`, delete `needsTarget` and the per-frame target allocation. Call `decode(unit.data.data(), unit.data.size(), pts, nullptr, &out, hardware)`.
   - On success, always clone `out` into the shared frame, for hardware and software. The decoder reuses `nativeNv12` on the next call, so the source must not hold the raw pointer.
   - Delete the `av_frame_free` calls that only existed for the old target.
   - Keep updating `p.width/p.height` from the decoded frame, and log the first change from the requested size.
   - Change `callbacks.serverVideoSize` to log only; do not write console metadata into `stats_.width/height`. The decoded frame is the size that reaches the graph.
4. `EngineController.cpp`: in `snapshot()`, before `if(copy.xboxActive&&activeXbox)`, add `if(copy.transport==TransportState::Empty)copy.xboxActive=false;`.
5. Test: port the PR's native path into `softwareScalingTest`, but adapt it to the fork's fixture. Verify that a null target returns a frame at the bitstream's native dimensions, that the retained clone still has the same first row checksum after `decoder.close()`, and that the fixed-target path still scales as before.

Do not import `StreamResolution.h`, the `play()` resolution argument, the control-channel messages, the CMake reordering or the PS5 controller clock change. The fork already has equivalents or does not have those files.

Tests: `veyra_capture_compressed_tests`, `veyra_xbox_tests`.

Commit message: `fix(xbox): decode software H.264 at the stream's native size`

## Step 3: PR #289, streaming resilience and ICE profile

Files:
- New `include/veyra/xbox/StreamProfile.h`
- New `src/xbox/StreamProfile.cpp`
- `include/veyra/xbox/StreamApi.h`
- `src/xbox/StreamApi.cpp`
- `include/veyra/xbox/Account.h`
- `src/xbox/Account.cpp`
- `include/veyra/xbox/WebRtcSession.h`
- `src/xbox/WebRtcSession.cpp`
- `src/source/XboxSessionSource.cpp`
- `include/veyra/source/XboxSessionSource.h`
- `cmake/VeyraXbox.cmake`
- `tests/xbox/XboxTests.cpp`

Changes:
1. New header with no duplicate tier enum:
   ```cpp
   enum class DecodeBackend : uint8_t { Auto = 0, D3D11VA = 1 };
   struct IceServerConfig { std::string url, username, credential; };
   struct TransportProfile {
       int videoBitrateKbps = 0;
       int audioBitrateKbps = 0;
       bool useStun = false;
       bool fullTeredo = false;
       DecodeBackend decodeBackend = DecodeBackend::Auto;
       std::vector<IceServerConfig> iceServers;
   };
   bool parseIceServerUrl(const std::string& url, std::string* host, uint16_t* port, bool* turn);
   const std::vector<IceServerConfig>& defaultStunServers();
   ```
   Keep the reference STUN list from the PR, seven entries.
2. `StreamApi`:
   - Add `setTransportProfile(const TransportProfile&)`; store it.
   - Add `setTokenRefresh(std::function<std::string()>)`.
   - In `call()`, on HTTP 401 or 403, refresh once, replace the bearer, retry once, and let a second refusal throw. Do not loop.
   - Add `keepAlivePulseInSeconds(sessionId)` reading `/v5/sessions/home/<id>/configuration` and returning 0 for missing or malformed values.
   - Extend `teredoIpv4(..., std::string* serverIpv4 = nullptr)`. Server IPv4 is bytes 4..7, unobfuscated; client IPv4 and port keep the existing XOR decoding.
   - Extend `parseIce(response, bool fullTeredo = false)`. `false` keeps the current two candidates byte for byte. `true` adds client/server address combinations on the decoded port, the candidate port and 3074, with deduplication.
   - `exchangeIce()` passes the stored profile's `fullTeredo`.
3. `Account`: add `refreshStreamingAccess()`, which clears the cached `streaming_` token under `mutex_` and calls `streamingAccess()` again.
4. `WebRtcSession`:
   - Add `setTransportProfile(const TransportProfile&)`; store it.
   - In `start()`, add public STUN servers when `useStun`, and add any valid custom ICE servers. Log and skip invalid URLs.
   - Add `b=AS` with `audio.setBitrate()` and `video.setBitrate()` only when the bitrate is greater than 0. Zero must produce the historical offer.
   - Use an 8 s gathering budget only when ICE servers are configured; keep 5 s otherwise.
5. `XboxSessionSource`:
   - Add `xbox::TransportProfile transport;` to `XboxConnectDesc`.
   - After creating `StreamApi`, call `setTransportProfile` and `setTokenRefresh([&account = p.desc.account] { return account->refreshStreamingAccess().gsToken; })`.
   - Call `p.rtc.setTransportProfile(p.desc.transport)` before `start`.
   - Replace the fixed 30 s keepalive with the configuration pulse: `min(30, max(1, pulse / 2))`, falling back to 30 s on any error. Keep the 200 ms sleep loop.
   - Log the selected profile once at session start.
6. `cmake/VeyraXbox.cmake`: add `StreamProfile.cpp` to `veyra_xbox_protocol`.
7. Tests:
   - Token refresh: 401 then 200 retries once with the new bearer; a second 401 throws; no refresh callback means no retry.
   - Keepalive pulse: values read, missing and malformed both return 0.
   - ICE URL parsing: STUN with port, TURN default 3478, bracketed IPv6, and rejection of unknown schemes, empty hosts and invalid ports.
   - Full Teredo: the PR's five combinations and the 3074 dedup case. Keep the existing default-parse test at four candidates.
   - `play()` still defaults to the fork's Auto tier and its device info is unchanged.

Do not add a custom TURN UI or persist TURN credentials. The `IceServerConfig` type may exist for a later feature, but leave it empty and do not expose a password field.

Commit message: `feat(xbox): refresh streaming tokens and follow the keepalive pulse`

## Step 4: PR #289, RTP loss recovery

Files:
- New `include/veyra/xbox/VideoLossRecovery.h`
- New `src/xbox/VideoLossRecovery.cpp`
- `include/veyra/xbox/WebRtcSession.h`
- `src/xbox/WebRtcSession.cpp`
- `include/veyra/source/XboxSessionSource.h`
- `src/source/XboxSessionSource.cpp`
- `cmake/VeyraXbox.cmake`
- `tests/xbox/XboxTests.cpp`

Changes:
1. Port the PR's `VideoLossRecovery` handler as its own class: before the stock `H264RtpDepacketizer`; bounded reorder buffer; RFC 4585 NACK on holes; drop incomplete frames after the wait window; counters.
2. Remove the PR's debug line:
   ```cpp
   std::fprintf(stderr, "FLUSH ts=%u ...");
   ```
   It is not acceptable in a live receive path.
3. Chain it in `WebRtcSession::start()`:
   ```cpp
   auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::LongStartSequence);
   lossRecovery_ = std::make_shared<VideoLossRecovery>();
   video_->setMediaHandler(depacketizer);
   video_->chainMediaHandler(lossRecovery_);
   video_->chainMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
   ```
4. Add `nacksSent`, `packetsRecovered`, `framesDroppedIncomplete` to `WebRtcStats`, fill them from `lossRecovery_->stats()`, and copy them into `XboxStats`.
5. Add unit tests. Upstream shipped none for this class, so this is the main new verification:
   - An in-order complete frame passes through unchanged and in order.
   - Out-of-order packets are reordered before the depacketizer.
   - A missing middle packet produces one or more NACK messages through the `send` callback and does not release the frame before the wait window.
   - A retransmitted missing packet before the deadline completes the frame; a frame still incomplete after the deadline is dropped whole.
   - Use a short `maxWait` (10 to 20 ms) with generous sleeps, and keep the test under a few seconds. If timing proves flaky, add a clock injection to the class rather than loosening the assertion.
6. Verify the installed `libdatachannel` 0.24.5 headers expose `rtc::RtcpNack`, `rtc::RtpHeader`, `rtc::MediaHandler` and `chainMediaHandler`. If they do not, stop and report, do not hand-roll RTCP.

Tests: `veyra_xbox_tests`, plus a live console check of `nacks` and `recovered` counters.

Commit message: `feat(xbox): recover from RTP loss with NACK and reordering`

## Step 5: PR #289, D3D11VA decode option

Files:
- `include/veyra/source/CaptureCompressedDecoder.h`
- `src/source/CaptureCompressedDecoder.cpp`
- `src/source/XboxSessionSource.cpp`
- `include/veyra/source/XboxSessionSource.h`
- `include/veyra/ui/XboxModel.h`
- `src/ui/XboxModel.cpp`
- `qml/Veyra/XboxSourceDialog.qml`
- `i18n/catalog.json`

Changes:
1. Decoder: add `setPreferD3D11VA(bool)`. In `open()`, use `openD3D11VA` for `codec == Hevc || preferD3D11VA` in the stream profile. Update `backendName()`.
2. Source: include `AV_PIX_FMT_D3D11` in `pixelFormatOf()`; set `info.hardwareDecodeActive` for D3D12 or D3D11; set `info.videoDecodePath = decoder.backendName()`; call `hardwareSurface(packet.hardwareSurface)` for D3D11 frames and fail the frame if the borrowed view is missing.
3. `XboxModel`: add `decodeBackend_` (0 Auto, 1 D3D11VA), persist as `decodeBackend`, expose in `state()`, accept in `set()`, pass through `XboxConnectDesc::transport`.
4. Dialog: a select row after "Software video decoding":
   - Label: `Hardware decode API`
   - Hint: `Auto uses D3D12VA. Pick D3D11VA (DXVA) if Intel integrated graphics corrupts the picture; both are hardware decode. Reconnect to apply.`
   - Options: `Auto (D3D12VA)`, `D3D11VA (DXVA)`.
5. Run `python -B scripts/i18n/extract.py`, then fill the nine translations in `i18n/catalog.json`. Do not leave missing entries.
6. Offline test: the option reaches the descriptor and `backendName()` reports the selected path when a hardware device is available. No Intel iGPU is available here, so mark the visual corruption fix as console-hardware-unverified.

Tests: `veyra_xbox_tests`, `veyra_qml_ui` build, `veyra_ui_i18n_tests`.

Commit message: `feat(xbox): add a D3D11VA decode option for Intel iGPUs`

## Step 6: PR #289, transport options in the model and dialog

Files:
- `include/veyra/ui/XboxModel.h`
- `src/ui/XboxModel.cpp`
- `include/veyra/source/XboxSessionSource.h`
- `qml/Veyra/XboxSourceDialog.qml`
- `i18n/catalog.json`

Changes:
1. `XboxModel` fields: `videoBitrateKbps_` (0 to 40000), `useStun_` (false), `fullTeredo_` (false). Persist, expose in `state()`, accept in `set()`, and copy into `launch.desc.transport`.
2. Dialog rows:
   - `Bitrate cap (Mbps)`: text field, 0 to 40, default 0. Hint: `0 lets the console decide. Do not set this below the rate the console already sends; a lower b=AS can cap the stream.`
   - `Public STUN`: switch. Hint: `Adds the reference clients' public STUN servers for NAT traversal. Leave off on a local network.`
   - `Full Teredo`: switch. Hint: `Adds the extra IPv4 candidates the reference clients try. Leave off unless connecting across NATs fails.`
3. Run the i18n extractor and fill the new translations.
4. No TURN fields. If the user later asks for TURN, the credential must not be written to `settings.json` in plaintext.

Tests: `veyra_xbox_tests`, `veyra_qml_ui` build, `veyra_ui_i18n_tests`.

Commit message: `feat(xbox): expose ICE and bitrate transport options`

## Step 7: PR #289, HUD counters and the settings-load fix

Files:
- `include/veyra/source/XboxSessionSource.h`
- `src/source/XboxSessionSource.cpp`
- `src/ui/XboxModel.cpp`
- `qml/Veyra/StreamHud.qml`
- `i18n/catalog.json`

Changes:
1. Add `nacks`, `recovered` and `incompleteDrops` to the Xbox stream map in `XboxModel::updateStream()`.
2. Add them to the Xbox HUD line in `StreamHud.qml`, after the keyframe count:
   - `retransmit recovered`
   - `whole-frame drops`
   Keep the PC branch untouched.
3. Add `if (!loaded_) { loadSettings(); loaded_ = true; }` at the top of `XboxModel::set()`. This is the PR's fix for a fresh model clobbering saved settings on the first write.
4. Do not add the PR's "profile tier" label. The HUD already shows the decoded resolution, which is the real result.

Tests: `veyra_xbox_tests`, `veyra_qml_ui` build, `veyra_ui_i18n_tests`.

Commit message: `fix(xbox): show loss recovery in the HUD and keep saved settings on first write`

## Step 8: amend the notice rule, credits, status and final verification

Files:
- `AGENTS.md`
- New `docs/UPSTREAM_PR_MERGES_2026-11-23.md`
- `docs/CURRENT_STATUS.md`
- `docs/DEVELOPMENT.md` if the test target list changed
- `docs/xbox-1440p-negotiation.md`
- `THIRD_PARTY_NOTICES.md` only for a component that is actually present in the build. Phase A adds no component, so no notice change is expected. Phase 2 adds the player bundle and WebView2, and their notices go in under the amended rule.

Rule amendment:
1. In `AGENTS.md`, under `Paths and preservation`, replace this line:
   ```text
   - Legal and vendor notices stay untranslated and unmodified: `LICENSE`, `licenses/`, `THIRD_PARTY_NOTICES.md`, `UPSTREAM.json`, vendor notices.
   ```
   with:
   ```text
   - Legal and vendor notices stay untranslated: `LICENSE`, `licenses/`, `THIRD_PARTY_NOTICES.md`, `UPSTREAM.json`, vendor notices. They may be updated to add or correct a notice for a component that is actually present in the build, with the change reviewed and limited to the affected notice. Do not translate, restyle or remove existing notices.
   ```
2. Do not widen the rule further. The V2 player bundle and WebView2 are the only components this authorization covers; Phase 2 is in scope, so their notices go in when Phase 2 lands.
3. If the user later asks for another notice change, get the same explicit authorization first.

Content of the merge record:
- PR number, title, author, GitHub handle, upstream commit, adopted files and a one-line description of the adaptation.
- A "not adopted" list with the reasons from the matrix.
- A note that #274's 1440p implementation was already present in the fork and that only the native decode delta was ported.
- A note that #289's HUD ungate was already present from local work.
- A note that the V2 transport is Phase 2 and why.
- A note that the notice rule was amended at the user's instruction, and that Phase A adds no component requiring a notice entry.
- The exact verification results and remaining gaps.

Code headers: add one short attribution comment at the top of each ported file or function, for example:
```cpp
// Ported from Veyra-NRVideo PR #289 by RyanBB (@RyanLookchard), adapted to
// this fork's ResolutionMode profile and 16 ms input path.
```
Keep it to one or two lines. Do not write a history essay in every function.

Update `docs/CURRENT_STATUS.md` with:
- What landed from each PR.
- That 1440p remains provisional and no wire constant changed.
- That loss recovery, STUN, full Teredo, the bitrate cap and D3D11VA are new and need console/hardware checks.
- That the V2 WebView2 transport is Phase 2, what it adds, and that V2 1440p stays unverified until the console returns a 2560x1440 decode.

Update `docs/xbox-1440p-negotiation.md` with a short note: native decode no longer scales a 1440p stream into a fixed target; the V1 tier mapping and control messages are unchanged; the capture gate still stands. Add a V2 section: the bundled player's hardcoded 1920x1080 `dimensionschanged`, the two support routes (source patch or pre-script shim), the research questions and the fact that V2 1440p remains unverified until the console returns a 2560x1440 decode.

## Author credits

Use this wording in `docs/UPSTREAM_PR_MERGES_2026-11-23.md`:

- **PR #274**, `Support 1440p Xbox Remote Play and preserve native software decode`, by **SenjuWoo** (`@SenjuWoo`). Adopted: native-size software decode and its tests. The fork's 1440p tier table and control messages predate this merge.
- **PR #287**, `fix(xbox): send real gamepad physicality and no touch points`, by **Kevin** (`@kevinmx21`). Adopted: physicality bits, zero touch points, touch-controls decline and tests.
- **PR #289**, `Xbox: selectable V2 transport (Chromium reference player in WebView2) + HQ profile, D3D11VA option, loss recovery, HUD`, by **RyanBB** (`@RyanLookchard`). Adopted: loss recovery, token refresh, keepalive pulse, Teredo expansion, optional STUN, optional bitrate cap, D3D11VA, HUD counters and the settings-load fix. Phase 2: the V2 WebView2 transport.

Do not add `Co-Authored-By: Claude` or any other AI/harness trailer to commits. Credit the human authors in the document and in code comments.

## Phase 2: PR #289 V2 WebView2 transport, including 1440p research and support

V2 is in scope and follows Phase A as its own commit group. The user wants WebView2, and 1440p support on V2 is part of the requirement, not a fallback note.

### What the shipped bundle does

The PR bundle (`veyra-xbox-player.js`, 1,456,100 bytes on the PR head) hardcodes the message-channel payload:
`dimensionschanged: {horizontal:1920, vertical:1080, preferredWidth:1920, preferredHeight:1080, safeArea*:1920/1080, supportsCustomResolution:true}`.
It contains no `VideoPreference`, `userRequestedResolutionUpdate`, `resolutionAlias` or 1440 handling. The C++ `XboxWebSource` passes codec, codec profiles, bitrate, polling, vibration, deadzone and TURN in its config; it does not pass a tier or dimensions. So the service sees the V1 `play` tier, but the player then reports a 1080p viewport. V2 as shipped cannot deliver 1440p.

### Research questions

Answer these before writing the support:
1. Does the `xstreaming-player` source at base `8e178aef` expose a resolution or dimension config? Where are `Channel/Message.ts` and the control-channel `authorizationRequest` built? Is `VideoPreference` absent there too?
2. Does the Veyra patch `e4c12bd` add a host-to-player control path that can carry a resolution? The bundle's `VeyraBridge` only sends state/offer/ice/frame/rumble to the host; it does not expose a generic "send control message" command.
3. Is the hardcoded 1920 in the bundle the only source of the message-channel dimensions, or is there a second path, for example a `config` read, that a C++-provided value could override?
4. Does the player's control channel send `authorizationRequest` and `gamepadChanged` only? Where would `VideoPreference` and `userRequestedResolutionUpdate` go, and in what order relative to the handshake?
5. Does the console accept the 1440p `VideoPreference` when `X-MS-Device-Info` and `settings.osName` already carry the 1440p tier? The fork's 1440p doc says the service decides; the physical capture is the gate.

Use `docs/xbox-1440p-negotiation.md` and PR #274's explicit `VideoPreference` and `dimensionschanged` code as the starting evidence. The fork's `ResolutionDesc` for 1440p is provisional: 2560x1440, alias `1440`, osName `windows`, 30000 kbps. Do not invent a new tier or a new osName. The V2 work changes the player's viewport and control messages, not the tier table. Read the player source in a task directory outside the source tree, and record the findings in the 1440p doc; do not vendor the source tree.

### Support routes

Route 1, source patch and rebuild (ideal if the player patch can be obtained):
- Obtain the player fork or patch at `8e178aef` + `e4c12bd`. Reconstructing the Veyra bridge from the minified bundle is not realistic, so treat this route as blocked until the patch is published.
- Add a resolution object to `startSession(config)` or to the page bootstrap: `{width, height, alias}`.
- Replace the hardcoded `dimensionschanged` values with the selected extent.
- Send `VideoPreference` and `userRequestedResolutionUpdate` on the control channel in the same order as the V1 session.
- Build, record the source, patch and output SHA-256, and vendor the rebuilt bundle.

Route 2, WebView2 pre-script shim (primary route with the shipped bundle; no rebuild required):
- In `WebView2Host::playerPageHtml()`, inject a small script before the bundle. It reads `window.__veyraResolution` (written from C++ for the selected tier) and wraps `RTCDataChannel.prototype.send`.
- For `label === "message"`, parse the JSON string. When `target === "/streaming/characteristics/dimensionschanged"`, parse `content`, replace `horizontal`, `vertical`, `preferredWidth`, `preferredHeight`, `safeAreaRight`, `safeAreaBottom` with the selected extent, re-serialize, and call the original `send`.
- For `label === "control"`, observe `authorizationRequest`; after it, call the original `send` with `{"message":"VideoPreference",...}` and `{"message":"userRequestedResolutionUpdate","resolutionAlias":"<alias>"}` if the player has not already sent them. Keep a one-shot guard.
- Do not intercept the `input` channel.
- This is brittle against a bundle update. Pin the bundle hash, and add a startup log of the selected extent and a warning if the bundle's `dimensionschanged` payload is not the expected 1920 shape.

Route 3, fallback only: keep V2 as a 1080p-class engine and keep 1440p on V1. Do not silently downgrade a 1440p selection. The dialog must say V2 is 1080p-class or switch to V1. This is the fallback, not the goal.

### Steps

1. **Dependency.** `vcpkg install unofficial-webview2:x64-windows-static` into the same tree; verify `find_package(unofficial-webview2 CONFIG REQUIRED)` and `unofficial::webview2::webview2`. The Evergreen runtime must be installed on the test machine.
2. **Artifacts and notices.** Vendor the chosen bundle, `licenses/xbox/xstreaming-player-MIT.txt` and `licenses/xbox/webview2.txt`; record the SHA-256; update `THIRD_PARTY_NOTICES.md` under the amended rule.
3. **Shared types.** Move `XboxConnectDesc` and `XboxStats` to `include/veyra/source/XboxStats.h`; add `IXboxSource.h` using the fork's `input::ControllerState` and `input::ControllerFeedback` (not `remoteplay::`); add `transportEngine`; make `XboxSessionSource` implement the interface.
4. **Engine selection.** `EngineController` uses `shared_ptr<IXboxSource>`; choose V1 or V2 from `transportEngine`; keep the V1 path unchanged.
5. **Port the host.** `WebView2Host`, `XboxWebSource`, `GamepadBridge`, `VeyraXboxWeb.cmake`, `VeyraXboxWebEmbed.cmake`; add `XboxWebSource.cpp` to `veyra_sources`; link `veyra_xbox_web`.
6. **Profile and tier.** Feed V2 from `resolutionDesc(mode)` and `TransportProfile`; do not import `QualityTier`. Pass the selected extent and alias to the host bootstrap or config. Keep `StreamApi::play` on the fork's tier.
7. **1440p support.** Implement Route 1 or Route 2. Add the selected `ResolutionMode` to `XboxWebSource` and `WebView2Host`; inject the extent; log and verify the message payloads. Do not claim 1440p until the console returns a 2560x1440 decode.
8. **Gamepad.** Adapt `GamepadBridge` to `input::ControllerState`; keep the exact inverse, the Y pass-through and the `INT16_MIN` clamp to `-32767`; add `veyra_xbox_web_gamepad_tests` with the 30-check wire comparison.
9. **UI.** `transportEngine` persisted and selected; V1 remains the default. If the selected tier is 1440p and V2 has no verified 1440p support, show a note and route to V1 or refuse the combination. No silent downgrade.
10. **Tests and build.** `veyra_qml_ui`, `veyra_xbox_tests`, `veyra_xbox_web_gamepad_tests`, plus UI smoke for the engine switch and a V2 launch.
11. **Console A/B and 1440p.** V1 against V2 at 1080p; then V2 at 1440p if the support route works. Record the decoded size, SDP, control/message payloads, queue and present latency, recovered/dropped counts, and process/GPU cost. A real 2560x1440 session is the only proof. If V2 wins the A/B and 1440p is verified, flip the default to V2 in a follow-up commit; otherwise keep V1.
12. **Docs.** `docs/XBOX_V2_TRANSPORT.md`, a V2 section in `docs/xbox-1440p-negotiation.md`, `docs/CURRENT_STATUS.md`, the merge record and the notices.

### Files for Phase 2

- New `include/veyra/source/XboxStats.h`, `include/veyra/source/IXboxSource.h`
- New `include/veyra/xbox/WebView2Host.h`, `src/xbox/WebView2Host.cpp`
- New `include/veyra/source/XboxWebSource.h`, `src/source/XboxWebSource.cpp`
- New `include/veyra/xbox/GamepadBridge.h`, `tests/xbox/XboxWebGamepadTests.cpp`
- New `cmake/VeyraXboxWeb.cmake`, `cmake/VeyraXboxWebEmbed.cmake`
- New `resources/xbox-player/veyra-xbox-player.js`, `licenses/xbox/xstreaming-player-MIT.txt`, `licenses/xbox/webview2.txt`
- `include/veyra/source/XboxSessionSource.h`, `src/source/XboxSessionSource.cpp`
- `include/veyra/engine/EngineController.h`, `src/engine/EngineController.cpp`
- `include/veyra/ui/XboxModel.h`, `src/ui/XboxModel.cpp`
- `qml/Veyra/XboxSourceDialog.qml`, `qml/Veyra/StreamHud.qml`
- `CMakeLists.txt`, `cmake/VeyraXbox.cmake`, `THIRD_PARTY_NOTICES.md`
- `tests/xbox/XboxTests.cpp`, `scripts/ui-check/xbox-*.qml`
- `docs/XBOX_V2_TRANSPORT.md`, `docs/xbox-1440p-negotiation.md`, `docs/CURRENT_STATUS.md`, `docs/UPSTREAM_PR_MERGES_2026-11-23.md`

The V1 path plus Step 4 addresses the RTP loss and reordering gap that motivated V2. It does not reproduce Chromium's jitter buffer, which is why V2 is in scope rather than a duplicate. V2 1440p is a research and support requirement; the fallback in Route 3 is allowed only if both support routes fail, and it must be visible to the user.

## Risks and mitigations

1. **WIP collision.** Snapshot first, commit per PR group, and check `git diff --name-only` before each commit. Never reset the tree.
2. **Loss recovery is the highest-risk port.** It is 328 lines of reorder and NACK logic with no upstream unit tests. Keep it in its own commit, add the tests in Step 4, and remove the debug print. If a live stream stalls, revert only that commit and keep the rest.
3. **Physicality does not guarantee console input.** The fork's earlier constant-value tests failed. Do not claim a fix until a game reacts. The wire semantics are still correct and worth landing.
4. **Native decode ownership.** The decoder owns and reuses `nativeNv12`; the source must always clone. The test that checks the retained frame after `decoder.close()` is the guard.
5. **D3D11VA is hardware-dependent.** It cannot be validated fully on this machine. Default Auto, expose it, and mark the Intel fix unverified until a real iGPU test.
6. **`b=AS` can cap the stream.** The fork's 1440p note records a 30.7 Mbps receive plateau. Default 0, and the UI hint warns against setting a lower cap.
7. **STUN/Teredo change the candidate set.** Default off. The historical offer stays byte-identical unless the user enables them.
8. **Token refresh re-enters `Account`.** `refreshStreamingAccess()` clears the cache then calls `streamingAccess()`. Test the 401 replay and ensure no mutex is held across the HTTP call.
9. **i18n debt.** New QML rows require nine translations; `extract.py --check` must stay at 0 missing and 0 placeholder problems. If translations cannot be produced honestly, stop and ask the user rather than shipping missing entries.
10. **Notice edits are narrow and reviewed.** The amended rule allows adding or correcting a notice for a component that is actually in the build. Do not translate, restyle or remove existing notices, and do not use the amendment to add notices for components that are not shipped.
11. **Do not present 1440p as verified.** The physical capture is still missing. The native decode change does not prove 1440p delivery, and V2 1440p is unproven until a 2560x1440 decode is recorded.
12. **V2 1440p may still be refused by the service.** The player can send the right messages and the console can still choose 1080p. Treat the decoded size as the proof, not the SDP.
13. **The pre-script shim is brittle.** It relies on the bundle's exact `RTCDataChannel` usage and JSON shape. Pin the bundle hash, log the rewritten payload, and prefer the source rebuild if the bundle changes.
14. **WebView2 adds a process and a bridge hop.** Measure CPU, GPU and frame latency; do not assume Chromium is faster. The player bundle is minified, so failures are opaque.
15. **V1 and V2 input paths differ.** V1 uses the fork's 16 ms combined packet; V2 uses the player's input queue. Test the same games and report which engine was active.

## Acceptance criteria

- `veyra_xbox_tests` passes, with new checks for physicality, touch points, touch-controls decline, token refresh, keepalive pulse, Teredo expansion, loss recovery and native decode. Record the exact count.
- `veyra_capture_compressed_tests` passes, including the native-size software decode check and the retained-frame check.
- `veyra_qml_ui` builds. `veyra_ui_i18n_tests` and `veyra_ui_contract_tests` pass.
- `python -B scripts/i18n/extract.py --check` reports 0 missing and 0 placeholder problems.
- `python -B scripts/i18n/scan-cjk.py` reports no new first-party prose introduced by this work.
- `gamepadReport()` and `inputReport()` produce identical pad bytes for the same frame: dynamic physicality, virtual 0, 23 bytes per pad.
- `clientMetadataReport()` sends 0 touch points; a touch-controls `TransactionStart` receives `ReceiverCancel`.
- Software H.264 decode returns a frame at the decoded size, not the requested size, and the clone survives the next decode and `close()`.
- Loss recovery passes the reorder, NACK, retransmit and drop-whole tests; the live HUD counters increment.
- `AGENTS.md` carries the amended notice rule, and no existing notice text was translated, restyled or removed.
- Phase A adds no dependency, no WebView2 and no TURN credential. `THIRD_PARTY_NOTICES.md` is unchanged in Phase A; Phase 2 adds the player and WebView2 entries under the amended rule.
- The WIP from Step 0 is still present and verified.
- The merge record credits all three authors, lists Phase 2 and every non-adopted item with a reason, and records the notice-rule amendment.
- Phase 2: `veyra_qml_ui` builds with `VEYRA_ENABLE_XBOX=ON` and `unofficial-webview2`; `veyra_xbox_web_gamepad_tests` passes; the player bundle has a recorded SHA-256 and MIT license; V1 is selectable and is the default until the A/B is recorded; a V2 session reaches streaming on a physical Series X with decoded H.264 and Opus through the shared chain.
- Phase 2 1440p: the host log shows `dimensionschanged` with 2560x1440 and `VideoPreference` and `userRequestedResolutionUpdate` with the 1440 alias; a physical Series X returns a 2560x1440 decoded frame. Until that capture, V2 1440p is unverified and the UI routes 1440p to V1.
- The V1/V2 A/B result is recorded.
- No commit carries an AI or harness attribution.

## Verification commands

Use the existing configured build tree if it exists. The reference sequence from `docs/DEVELOPMENT.md` is:

```powershell
# Phase 2 prerequisite, once:
vcpkg install unofficial-webview2:x64-windows-static

cmake --preset x64-release -S . -B build/x64-release -DVEYRA_ENABLE_XBOX=ON
cmake --build build/x64-release --target veyra_qml_ui veyra_xbox_tests veyra_xbox_web_gamepad_tests veyra_capture_compressed_tests veyra_ui_i18n_tests veyra_ui_contract_tests --parallel 2
```

Then run:

```powershell
build\x64-release\veyra_xbox_tests.exe
build\x64-release\veyra_xbox_web_gamepad_tests.exe
build\x64-release\veyra_capture_compressed_tests.exe
build\x64-release\veyra_ui_i18n_tests.exe
build\x64-release\veyra_ui_contract_tests.exe
python -B scripts/i18n/extract.py --check
python -B scripts/i18n/scan-cjk.py
```

If the configured tree is `build-local/x64-release`, substitute that path. Keep build concurrency at 2 and each build invocation under 900 seconds.

Manual checks after the automated suite:

1. Start an Xbox stream and press every button in a GameInput title, including a game that previously ignored the pad. Record the result; do not infer it from the wire log.
2. Enable the HUD and confirm the loss counters appear. Force loss only if the network supports it; otherwise record the counters at rest.
3. If an Intel iGPU machine is available, select D3D11VA and confirm the corruption is gone. Otherwise record it as unverified.
4. Confirm a V1 1440p session still reports the decoded size in the HUD. The wire values are unchanged and still provisional.
5. Select V2 at 1080p and confirm a stream with decoded H.264 and Opus. Then select 1440p with V2. If the support route is implemented, confirm the host log sends 2560x1440 in `dimensionschanged` plus the 1440 `VideoPreference` and `userRequestedResolutionUpdate`, and the HUD reports a 2560x1440 decode. If the route is not verified, confirm the UI routes 1440p to V1 and says so. Do not record V2 1440p as delivered from the SDP alone.

## Rollback

- Each step is its own commit. Revert the offending commit with `git revert`; do not reset the branch.
- The Step 0 patch is the recovery source for the WIP if anything is accidentally reverted. Apply it with `git apply` only after inspecting the diff.
- If loss recovery is the only regression, revert Step 4 and keep Steps 1, 2, 3, 5 and 6.
- If the i18n step blocks, revert only the QML/model commit that introduced the strings, not the whole merge.
- Phase 2 is its own commit group. Revert it without touching Phase A. The player and WebView2 notice entries go with that group; the `AGENTS.md` rule amendment stays, because it is independent of Phase 2.
- If the 1440p support route fails, keep V2 as a 1080p-class engine and route 1440p to V1. Do not revert the whole phase for one route; revert only the failing patch or shim commit.

## Suggested skills

- `tdd` for the protocol, loss-recovery, native-decode and gamepad-bridge tests. Write the failing check first, then port the implementation.
- `research` for the Phase 2 player-source and 1440p work: the bundle's hardcoded dimensions, the player's message/control channels and the exact 1440p wire payloads need primary-source inspection.
- `code-review` after each PR group, with the standards axis against this fork's `AGENTS.md` and the spec axis against the PR bodies and the relevance matrix.
- `diagnosing-bugs` if a live Xbox stream regresses after the loss-recovery, D3D11VA or WebView2 changes.
- `pr` only if the user asks to open a PR for the merged work.
