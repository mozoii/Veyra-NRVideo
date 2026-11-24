# Current status

This is the fork's working state, not a release note. The parent project's release records (v2.2.0, 2.1.9) are historical and kept under `docs/history/`.

## Upstream PR merges and V2 transport (2026-11-23)

- Ported the relevant deltas of upstream PRs #274 (SenjuWoo), #287 (kevinmx21)
  and #289 (RyanBB). Full credits, adopted and non-adopted lists, and the
  verification record are in `docs/UPSTREAM_PR_MERGES_2026-11-23.md`.
- Landed: per-frame gamepad physicality, 0 touch points and touch-controls
  decline; native-size software H.264 decode; one-shot streaming-token refresh,
  keepalive pulse, optional STUN, full Teredo and bitrate cap with all defaults
  historical; RTP NACK/reordering loss recovery with HUD counters; D3D11VA
  decode option; settings-load fix. Loss recovery, STUN, full Teredo, bitrate
  cap and D3D11VA are new and need console/hardware checks.
- V2 WebView2 transport is implemented as Phase 2: shared `IXboxSource`,
  `WebView2Host`, `XboxWebSource`, the reference player bundle and a 30-check
  gamepad mapping test. V1 remains the default and selectable. See
  `docs/XBOX_V2_TRANSPORT.md`.
- **V2 is 1080p-class; 1440p is not supported on V2.** On 2026-11-24 a live
  Series X returned a 1920x1080 decode for a 1440p V2 selection. Both client
  attempts (an `RTCDataChannel.prototype.send` shim and a build-time port of
  Greenlight PR #1752 wrapping the player's channel methods) were removed after
  that result. The UI routes 1440p to V1 with a visible note. No wire constant
  changed and the V1 1440p path is unchanged.

## Fork state

- Branch: `feature/xbox-1440p-english` (from parent `main` at `fe8df53`).
- Xbox home streaming is the primary console source; PS5 Remote Play and Chiaki are removed.
- `VEYRA_ENABLE_XBOX=ON` by default. `VEYRA_ENABLE_REMOTEPLAY` is gone.
- English is the source language. `i18n/catalog.json` carries the English source plus nine translations (zh-CN, zh-TW, ja, ko, es, fr, de, pt-BR, ru), all filled, 0 stale, 0 missing, 0 placeholder problems.
- Resolution tiers: Auto (default), 720p, 720p HQ, 1080p, 1080p HQ, 1440p (ceiling). **1440p is provisional** — the wire-level values are from third-party client behaviour, not a physical console capture. See `docs/xbox-1440p-negotiation.md`.
- CI: none yet. `veyra_ui_i18n_tests` and `veyra_xbox_tests` need a Windows 11 x64 + Qt 6.8.3 + DirectX 12 machine; they were not run in this environment.

## What is not done

- **Physical console capture (Auto, every tier, NR/FG/HDR/audio/corruption)** — blocked on a physical Series X/S with Alpha or Alpha Skip-Ahead. 1080p and below are the tested path.
- **Executable validation** — no toolchain in this environment; catalog and Unicode checks run, unit/QML tests are not built here. The 2026-11-22 removal round was the exception: MSVC 14.51 and Qt 6.8.3 were available here, so `veyra_qml_ui`, `veyra_xbox_tests`, `veyra_ui_i18n_tests`, `veyra_ui_contract_tests`, `veyra_game_passthrough_tests` and `veyra_capture_audio_tests` were built and run.
- **Release** — no fork release has been published or tagged. Do not claim 1440p delivery or any 2.3.0 version.

## Recording, virtual camera, capture sources and OBS removed (2026-11-22)

Two rounds removed features from this fork. Live recording, the virtual camera and
instant replay went first: the encoder and replay-buffer engine code, the DirectShow
camera module and its system installer, the presentation capture hook they shared,
their settings, shortcuts, toolbar controls and translated strings.

The second round removed every capture source and the OBS integration: the capture-card
path (DirectShow enumeration, DeckLink, Magewell, AverMedia, Elgato HDR control, capture
format/colour/audio selection and its VB-CABLE setup), the screen/desktop capture path
(WGC/DXGI, screen crop, game-mode click-through), the Linux OBS filter plugin and the
OBS game-capture compatibility mode (setting, `--obs-game-capture` flag, renderer
reason). Sources that remain: local files and images, network streams, PC streaming
(moonlight) and Xbox streaming. Video export, image export and screenshots are
unchanged and keep the shared `FrameSource` path.

What this does not do: it does not uninstall an existing `C:/Program Files/VeyraVirtualCamera`
system component from machines that installed it with an earlier version, and
`THIRD_PARTY_NOTICES.md` still carries the OBS-derived component section plus the
capture SDK entries. Both are records of what earlier builds shipped; removing them is
a separate decision. The pre-removal capture code stays visible in git history.

## QML text layout (2026-11-22)

The English-source conversion left several QML slots measured for Chinese strings: node
card titles, the timing strip, the home source-card subtitle, the display-state value and
the save dialog's node column were cut or overlapped. They now size to their text. The
measured audit (58 page/dialog samples, English, 1280x800 and 1024x700 clean) and the
remaining gaps are in `docs/QML_TEXT_LAYOUT_2026-11-22.md`.

## What lives outside this file

- Negotiation findings: `docs/xbox-1440p-negotiation.md`
- PS5-era historical records: `docs/history/` (kept for audit; not current-state claims)
- Parent-project release notes: `docs/history/releases/`

## Docs translation stop (2026-11-22)

The first-party prose sweep was deliberately stopped at the user-facing/current
docs and code-of-truth files, not at every historical record:

- **Done**: `src/`, `include/`, `apps/`, `qml/`, `tests/`, `scripts/` (except historical records), `tools/`, root `README*.md`, `THIRD_PARTY_NOTICES.md`, `AGENTS.md`, `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md`, `docs/README.md`, `docs/ARCHITECTURE.md`, `docs/xbox-1440p-negotiation.md`, `CMakeLists.txt` comments, `i18n/` runtime and scripts (English source keys), the Xbox 1440p tier files and tests.
- **Left in place, deliberately**: dated historical records (`docs/history/`, `docs/WORKLOG.md`, `docs/HISTORY.md`, most `docs/` dated plans/reports/worklogs), the `loop/` loop journal/evidence/prompts, the `prototypes/ui-redesign-2026-09-25/` prototype sources, the two root technical contracts (`VEYRA_PRODUCT_SPEC_V1.md`, `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`) — all Chinese, all historical or design records. The two root contracts are the one real gap: they are the current product/technical boundary and should be translated with the user's sign-off, not silently machine-translated.
- **PS5-specific historical docs** were deleted (PS5/Chiaki surfaces); non-PS5 history is kept for audit.
- `i18n-scripts/` is a stale duplicate of `scripts/i18n/` and is intentionally untracked; the sweep helpers (`scripts/i18n/translate-cjk.py`, `zh-map.json`) remain untracked as sweep tooling; `scan-cjk.py` is committed as the detection tool.

## Steam Controller input: picker and probe (2026-11-22, hardware unverified)

Implemented from `handoffs/handoff-veyra-steam-controller-input.md` on this branch:

- `ControllerInput` uses SDL's GUID string as the persistent pad identity. Acquisition prefers that GUID and falls back to the first pad, so a single-pad setup keeps the old behaviour. A connected preferred pad is never silently replaced. New `setPreferredDevice()` and `devices()` (enumeration only, never opens a pad) keep one SDL owner. Each pad open logs guid, name, vid/pid and path; focus transitions log `focused` on the `input` channel.
- `XboxModel` persists `controllerGuid` in `settings.json`, exposes it in `state()`/`set()` and passes it through `XboxConnectDesc`. The bridge applies it before `acquirePad`. The same Auto + detected-pads choice is available in the Xbox dialog (`xbox-controller`), the Minimal cinema pill (`cine-controller`) and the Pro header (`pro-controller-button`); the dialog also has an active-pad status row (`xbox-controller-status`). Each picker refreshes the device list when opened. The `gamepad` switch remains the master gate.
- New `veyra_controller_probe`: per-device identity, a two-second live read of each pad, then the production reader with the exact `gamepadReport` bytes it would send. `--hint NAME=VALUE` repeats and `--sdl-log` allow driver hypotheses to be tested without rebuilding.
- New pure selection unit test (GUID match, fallback, empty list), wire protocol/cadence/focus gate/neutral release unchanged. No SDL hint was added to `ControllerInput::start()`.

Verified this session: `veyra_qml_ui`, `veyra_controller_probe`, `veyra_controller_selection_tests`, `veyra_xbox_tests`, `veyra_ui_i18n_tests` and `veyra_ui_contract_tests` build; xbox tests 137 checks / 0 failures, i18n tests 0 failures, UI contract tests pass, selection test 6/6; `extract.py --check` reports 2462 entries, 0 missing, 0 placeholder problems; CJK scan shows no new first-party prose from these files. Probe output is under `build-local/verify/controller-probe-*.txt`.

Not verified and still open: **input reaching the console is not claimed**. An early probe in this session saw `SDL_GetJoysticks()` and `SDL_GetGamepads()` return 0 while HIDAPI claimed four Proteus dongle interfaces (`SDL_JOYSTICK_HIDAPI_STEAM (ENABLED)`); SDL's Triton driver only registers a joystick after it sees a controller connect over the radio. Once the pad was awake, the picker selected it and a focused stream was tested with temporary tagged logs at the SDL read, source send and data-channel boundaries. Those logs showed SDL button presses (`buttons=0x00000001` for A), the mapped Xbox frame (`buttons=16`), `delivered=1`, correct 38-byte layout and 33 ms heartbeat frames. The same failure occurred with the Steam Controller and with an Xbox 360 controller, so neither pad selection nor the send path explains it. The report layout, channel protocol `1.0`, metadata and physicality bytes were checked against upstream Greenlight `main-v2` and match. The remaining gap is on the console/service side; no driver hint or wire value was changed without evidence. A trial that announced gamepad slot 1 and sent pad index 1 also failed and was reverted. The input path was then reshaped to match Greenlight's `InputChannel`: 16 ms ticks, a queued gamepad frame and video metadata flushed as one combined packet, and no local dedup. `veyra_xbox_tests` passes 138/0 with a new combined-packet check; console acceptance of that path is not yet verified. Research findings and sources are in `docs/XBOX_INPUT_RESEARCH_2026-11-23.md`.
