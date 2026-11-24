# Handoff: Xbox 1440p, PS5 removal, English-only fork

## Summary

This is the build plan for a fresh agent. The GitHub fork exists, the desktop clone is clean, and no implementation code has been changed. I used the Pi Planner prompt at `C:/Users/mozi/Documents/GitHub/Agents/pi/prompts/planner.md`; I did not use the handoff skill. This Markdown file is the only local file I added.

The requested implementation is larger than replacing two constants. It has three coupled parts: make Xbox the only console-streaming source, add every current Xbox Remote Play quality tier including 1440p, and make the whole first-party repository English-only with verified translations. The existing enhancement graph already applies to Xbox, so NR, SR, frame generation, ANR FG, HDR, colour, subtitles, export, virtual camera and OBS support stay on the shared `FrameSource` path.

## Goal

Produce a GPL-3.0 Veyra fork under `mozoii` that:

1. Removes PS5 Remote Play and Chiaki entirely.
2. Makes Xbox home streaming the primary console source.
3. Exposes Auto, 720p, 720p HQ, 1080p, 1080p HQ and 1440p, with 1440p as the ceiling.
4. Verifies actual 1440p delivery on a physical Series X or Series S enrolled in Alpha or Alpha Skip-Ahead.
5. Keeps all other Veyra features working.
6. Replaces every first-party non-English prose string with an exact English translation, then independently double-checks it.
7. Leaves legal texts, vendor notices, URLs, code identifiers and binary assets untouched.
8. Adds no ANR Beta runtime, model, DLL or installer to the public repository.

## Current state

- Fork: https://github.com/mozoii/Veyra-NRVideo
- Parent: https://github.com/Likely7/Veyra-NRVideo
- GitHub account: `mozoii`
- Desktop clone: `C:/Users/mozi/Desktop/Veyra-NRVideo`
- Branch: `main`
- Head: `fe8df53 docs: replace static star history with GitTrend live chart`
- Remotes: `origin` points to the fork, `upstream` points to the parent.
- Commits, pushes, implementation edits and branch creation: none yet.
- Forgejo: not used.
- GitHub device login: completed by the user; no credential appears in this document.
- `git status` was clean before this file was written.

Use a new branch `feature/xbox-1440p-english` for implementation. Do not commit or push unless the user asks. If a commit is later requested, use the configured Git identity, Conventional Commits, and no AI or harness attribution.

## Decisions already made by the user

- Use GitHub, not Forgejo.
- Use the Planner prompt, not the handoff skill.
- Remove PS5 support entirely.
- Translate the whole fork to English with legal exclusions.
- Double-check translation correctness.
- Do not implement code in this planning pass.

## Investigation findings

### Xbox source already exists

Veyra is not a PS5-only client. `src/xbox/` already implements device-code sign-in, console discovery, session creation, SDP and ICE exchange, WebRTC, input, rumble, Opus audio, H.264 decoding and reconnect handling. `src/source/XboxSessionSource.cpp` publishes decoded frames into the shared enhancement graph. `README.md` documents Xbox as an unofficial experimental source, and `docs/CURRENT_STATUS.md` says real-console Xbox long runs still need field retesting.

Relevant files:

- `src/xbox/Account.cpp`, `src/xbox/Https.cpp`, `src/xbox/StreamApi.cpp`, `src/xbox/WebRtcSession.cpp`
- `include/veyra/xbox/`
- `src/source/XboxSessionSource.cpp`
- `include/veyra/source/XboxSessionSource.h`
- `src/ui/XboxModel.cpp`, `include/veyra/ui/XboxModel.h`
- `qml/Veyra/XboxSourceDialog.qml`
- `tests/xbox/XboxTests.cpp`
- `cmake/VeyraXbox.cmake`
- `docs/XBOX_STREAM_CORRUPTION_RECOVERY.md`

### 1440p is a service-side gate

Microsoft began testing up to 1440p Remote Play on 2026-09-09 for Alpha and Alpha Skip-Ahead Series X|S consoles. The console display output must be 1440p or 4K, the receiving device must support the tier, and Auto selects the highest supported resolution and bitrate. I found no public stable-rollout announcement as of 2026-11-21. The official Xbox Wire article is the primary source; secondary reporting is in `https://www.thurrott.com/games/341437/xbox-remote-play-is-adding-1440p-streaming-support` and `https://gamenewsplus.net/news/xbox-insiders-get-1440p-remote-play-custom-startup-animations-and-more-38779/`.

A client can advertise 2560x1440, but the service decides what to send. Treat a real 1440p session as an acceptance gate, not a compile-time guarantee.

### Hardcoded 1080p values

- `src/xbox/StreamApi.cpp:20`: `X-MS-Device-Info` reports `1920x1080`.
- `src/xbox/WebRtcSession.cpp:278-279`: `/streaming/characteristics/dimensionschanged` reports `1920x1080`; `clientdevicecapabilities` is an empty object.
- `src/source/XboxSessionSource.cpp:143`: decoder starts with `width = 1920, height = 1080`. The decode loop later follows actual frame dimensions and server metadata, so this is an initial hint, not a hard cap.
- `include/veyra/source/XboxSessionSource.h`: `XboxConnectDesc::locale` defaults to `zh-CN`.
- `include/veyra/xbox/StreamApi.h`: `play()` defaults to `zh-CN`.
- `src/xbox/StreamApi.cpp:98-106`: `/v5/sessions/home/play` sends `settings.osName = "windows"`.

### SDP and capability messages are incomplete for 1440p

`src/xbox/WebRtcSession.cpp:129-131` offers H.264 Main, Constrained Baseline and Baseline at level 3.1: `4d001f`, `42e01f`, `42001f`. `tests/xbox/XboxTests.cpp:417` asserts the first two. Veyra currently sends no `VideoPreference`, no `userRequestedResolutionUpdate`, no rich `clientdevicecapabilities`, and no `b=AS` bitrate line.

Third-party evidence, not yet verified against a physical console:

- `https://raw.githubusercontent.com/andreencar/iGreenLight/main/Sources/App/Settings.swift` defines five tiers: 720p, 720p HQ, 1080p, 1080p HQ and 1440p. It maps aliases `720`, `720HQ`, `1080`, `1080HQ` and `1440`, and maps `os.name` to `android`, `windows` or `tizen` by tier. It also lists H.264 Main L5.1 as `4d0033`.
- `https://vipersoftworks.com/gamewarp/` independently advertises the same five modes up to 1440p.
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/streaming_config.rs` defines `Auto`, `p720`, `p1080`, `p1440` and `p1080HQ`; it does not define `p720HQ`. It uses `quality_tier_profile` bitrates of 10000, 20000 and 30000 kbps, and resolution aliases `Auto`, `720`, `1080`, `1440` and `1080HQ`.
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/channels/control.rs` pins `VideoPreference` and `userRequestedResolutionUpdate` JSON.
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/channels/message.rs` pins a rich `clientdevicecapabilities` block containing `supportsCustomResolution`, `supportsHevc`, `supportsHdr`, `supportsFps`, `maxWidth`, `maxHeight`, `maxBitrateKbps` and a nested `video` object.
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/preferences.rs` pins a browser-style `X-MS-Device-Info` builder and `settings.osName`. Its OS mapping differs from iGreenLight for 1440p and 1080p HQ, so do not hardcode either mapping until the official client is captured.
- RFC 6184 says `profile-level-id` encodes `profile_idc`, `profile-iop` and `level_idc`; `max-recv-level` can advertise a higher receive level. FFmpeg's `libavcodec/h264_levels.c` lists level 5.0 as 22080 MaxFS / 589824 MaxMBPS and level 5.1 as 36864 MaxFS / 983040 MaxMBPS. A 2560x1440 frame is 14400 macroblocks; at 60 fps it needs 864000 macroblocks per second, so H.264 level 5.1 is the candidate. Candidate `level_idc` is `0x33`. Verify against the official SDP before changing the offer.

### ANR and frame generation already work from estimated video inputs

`src/ngx/AnrProvider.cpp` loads ANR FG through the DLSS-G style NGX entry points. `docs/ANR_FG_EXPERIMENT_2026-11-12.md` records 2X to 6X, SDR/HDR, List/Node modes, presets and export. The UI strings call it estimated video input. `README.md:182` warns that submitted FPS is not physical display FPS or end-to-end latency. No new interpolation or bitstream motion-vector work is needed. `docs/BUILD_2.2.0.md` and `docs/BUILD_2.1.9.md` say ANR Beta runtime assets are excluded from public packages; upstream forbids third-party redistribution. Do not add them.

### PS5 removal surface

424 files mention PS5, Chiaki or Remote Play. `VEYRA_ENABLE_REMOTEPLAY` is OFF by default, so full removal means deleting the option and all conditional code, not just changing the default.

Key paths:

- `CMakeLists.txt:69`, `825-830`, `1950-1988`, `2028-2034`
- `cmake/VeyraRemotePlay.cmake`
- `src/remoteplay/`, `include/veyra/remoteplay/`
- `src/source/RemotePlaySource.cpp`, `src/source/RemotePlaySessionSource.cpp`
- `include/veyra/source/RemotePlaySource.h`, `include/veyra/source/RemotePlaySessionSource.h`
- `src/ui/bridge/RemotePlay.cpp`
- `apps/veyra/ui/RemotePlayPanel.cpp`, `apps/veyra/ui/RemotePlayPanel.h`
- `qml/Veyra/Ps5SourceDialog.qml`
- `qml/Veyra/HomePage.qml`, `qml/Veyra/CaptureSourceDialog.qml`
- `include/veyra/ui/QmlPlayerBridge.h` and the matching `ps5*` properties and methods in `src/ui/QmlPlayerBridge.cpp` and bridge modules
- `tests/remoteplay/`, `tests/integration/Ps5*.cpp`, `tests/qml/quick/tst_ps5_account_lookup.qml`
- `scripts/remoteplay/`, `scripts/ffmpeg/ps5-h264-slices.patch`, `scripts/package-remoteplay-source.py`
- `docs/remoteplay/`, PS5-specific files under `docs/` and `docs/history/`
- `licenses/remoteplay/`
- `THIRD_PARTY_NOTICES.md` sections covering Chiaki, SDL DualSense integration, PSN authorization and Remote Play dependency notices

Keep `include/veyra/remoteplay/Types.h`. `include/veyra/xbox/Protocol.h` and `include/veyra/source/XboxSessionSource.h` include it for `ControllerState`, `ControllerFeedback` and timing types. Those are shared controller abstractions, not PS5 prose. Other `remoteplay/` headers and sources may be retired only after a full include and CMake dependency scan.

Removing Chiaki removes the AGPL-3.0-only with OpenSSL exception combination. Update license and notice text accordingly. Do not delete the patched FFmpeg or its corresponding source, patch and license until the Xbox-only build proves the PS5 H.264 slice patch is unused; `AGENTS.md` currently requires retaining it.

### Translation surface

A read-only scan excluding `.git`, `licenses/`, `third_party/` and binary assets found 1450 first-party text files containing non-Latin script, about 1.86 million characters. Markdown accounts for about 1.49 million characters; JSON about 308 thousand; C++, QML, Python, JavaScript, PowerShell, Inno Setup and headers account for the rest. `docs/history/`, `i18n/catalog.json`, `docs/WORKLOG.md` and `docs/HISTORY.md` are the largest files.

`i18n/catalog.json` has 3172 entries with `zh`, `src`, `zh-TW`, `en`, `ja`, `ko`, `es`, `fr`, `de`, `pt-BR` and `ru` keys. Baseline `python -B scripts/i18n/extract.py --check` passes with 0 missing translations and 0 placeholder problems. Five `en` values still contain foreign language names; 349 `en` values contain non-ASCII characters that need review, mostly punctuation or symbols. There are 2164 Chinese `qsTr` literals across 53 QML files.

The i18n runtime is source-language bound:

- `src/ui/UiLanguage.cpp` keys translations by the `zh` field and `hasHan()`.
- `scripts/i18n/extract.py` collects Chinese source strings using a Han regex; `LANGS` expects nine translated columns.
- `scripts/i18n/to_traditional.py` generates Traditional Chinese from Simplified Chinese.
- `scripts/i18n/wrap_qml.py` wraps Chinese QML strings in `qsTr()`.
- `qml/Veyra/SettingsPage.qml:314-331` exposes ten languages plus Auto.
- `tests/unit/UiLanguageTests.cpp` asserts Chinese source strings and non-English translations.

A simple search-and-replace of catalog values will not satisfy the request. The Build agent must migrate the source language to English or collapse the runtime to an English-only identity path, then update tests and scripts.

### Build environment

`cmake`, `ninja`, vcpkg and Python 3.11 are not available on this machine. `python` is 3.15.2 and runs the i18n check. `docs/DEVELOPMENT.md` and `scripts/acceptance/capture-xbox-build.py` hardcode an upstream build root, Visual Studio BuildTools path and `py -3.11`. `cmake/VeyraXbox.cmake` defines `veyra_xbox_tests`; acceptance scripts build and run it. The Build agent needs a Windows 11 x64 environment with Qt 6.8.3, DirectX 12 dependencies, libdatachannel, nlohmann-json, Opus, patched FFmpeg and libdovi, or must adapt the build scripts to a reproducible local environment.

## Ordered steps

1. Create `feature/xbox-1440p-english` from `main` at `fe8df53`. Confirm `git status` is clean and both remotes are correct.

2. Run the research gate before code changes. Capture the official 1440p client on a physical Series X or Series S in Alpha or Alpha Skip-Ahead, with console output set to 1440p or 4K and streaming quality Auto. Record the exact `X-MS-Device-Info`, `/v5/sessions/home/play` body, local offer SDP, answer SDP, `clientdevicecapabilities`, `dimensionschanged`, `VideoPreference`, `userRequestedResolutionUpdate`, `rateControlBitrateUpdate` if present, and the decoded frame size. Write the findings to a new Markdown document, for example `docs/xbox-1440p-negotiation.md`. Do not guess the tier mapping from the third-party clients.

3. Define one resolution model in a shared header. Suggested enum values are `Auto`, `P720`, `P720HQ`, `P1080`, `P1080HQ` and `P1440`. For each tier, map the alias, advertised width and height, spoofed OS name, maximum bitrate, H.264 level and locale. Keep `en-US` as the default locale. Treat `P720HQ` as provisional until the capture confirms it. Do not offer 4K; 1440p is the current Remote Play ceiling.

4. Refactor `StreamApi::play()` to build `X-MS-Device-Info` and the `/play` settings block from the selected mode. Remove the hardcoded 1080p string. Keep the locale, timezone, SDK type and OS name consistent with the captured official client. Add a pure `DeviceInfo` builder so tests can assert exact JSON fields.

5. Extend `WebRtcSession` with a stream-profile setter or constructor argument. Use it in `sendClientConfig()` for `clientdevicecapabilities` and `dimensionschanged`. Add `VideoPreference` and `userRequestedResolutionUpdate` on the control channel in the order the official client uses. Add rich capability fields from `openxcloud` as a starting point, then reconcile them with the capture. Add H.264 level 5.1 only if the official offer or answer requires it; update all three codec entries and tests together. Add an SDP `b=AS` line only if the captured native client uses one.

6. Update `XboxSessionSource` initial width, height and locale from `XboxConnectDesc`. Preserve `parseServerMetadata()` and the existing resize handling; the server's decoded dimensions remain authoritative. Keep the audio clock, corruption recovery, keyframe requests, gamepad heartbeat and reconnect policy intact.

7. Add the resolution selector to `XboxModel` and `qml/Veyra/XboxSourceDialog.qml`. Persist the mode next to `gamepad` and `softwareDecode`, expose it through `state()`, accept it in `set()`, and pass it into `Launch.desc`. Use Auto as the default and let the service pick 1440p when eligible. Keep the existing software-decode switch.

8. Update `tests/xbox/XboxTests.cpp` with literal expected values for every tier: device info, settings body, dimensions, capability JSON, H.264 fmtp lines, control-channel messages and server metadata handling. Update the existing `4d001f` and `42e01f` assertions only after step 2 justifies the replacement. Add negative tests for malformed modes and resolution changes. Build and run `veyra_xbox_tests`.

9. Remove PS5 entirely. Delete or orphan the PS5-only source, UI, CMake blocks, tests, scripts, docs and license directories listed above. Set `VEYRA_ENABLE_XBOX=ON` as the default for this fork. Remove the `VEYRA_ENABLE_REMOTEPLAY` option and all of its conditional blocks. Remove the PS5 card from `HomePage.qml`, the PS5 controller section from `CaptureSourceDialog.qml`, and every `ps5*` property, signal, Q_INVOKABLE, timer and bridge module from `QmlPlayerBridge`. Keep PC/Moonlight, capture card, screen capture, file and image sources. Keep shared `remoteplay/Types.h`. Update `THIRD_PARTY_NOTICES.md`, `AGENTS.md`, `docs/DEVELOPMENT.md` and `README.md` so no stale Chiaki build dependency remains.

10. Migrate the UI to English source strings. Replace Chinese `qsTr`, `tr`, `QCoreApplication::translate`, wide-string status messages, fixed labels, comments and log prose with exact English. Change `i18n/catalog.json` to an English-only shape or keep the current shape with every value set to the verified English source. Rewrite `src/ui/UiLanguage.cpp`, `include/veyra/ui/UiLanguage.h`, `qml/Veyra/SettingsPage.qml`, `tests/unit/UiLanguageTests.cpp` and the three `scripts/i18n/` scripts so the source language is English and no non-English locale columns are required. Preserve placeholders, disambiguation contexts and format slots exactly.

11. Translate all first-party docs, comments, test names, scripts and UI text. Use the existing `en` catalog column as the canonical English for UI strings, but verify every entry against the original source because some English translations are poor. For files without a catalog counterpart, produce a translation and then perform an independent back-translation or bilingual review. Record the reviewer, source file and decision in a Markdown verification log. Do not translate `LICENSE`, `licenses/`, `THIRD_PARTY_NOTICES.md`, vendor notices, code identifiers, URLs, paths, format placeholders or binary assets. Do not rename non-English identifiers.

12. Update user-facing docs. `README.md`, `README_CN.md`, `README_EN.md`, `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md`, `AGENTS.md`, `docs/XBOX_STREAM_CORRUPTION_RECOVERY.md` and `THIRD_PARTY_NOTICES.md` must describe an Xbox-only, English-only fork with all quality tiers and no PS5 dependency. Remove or translate PS5-specific historical docs; do not leave stale claims of PS5 support. Add a short note that ANR Beta runtime files are user-supplied and excluded from the public fork.

13. Run validation in this order: `python -B scripts/i18n/extract.py --check`, a Unicode scan for non-English prose in first-party text, `veyra_ui_i18n_tests`, `veyra_xbox_tests`, a QML UI smoke test, then a real console session. On the console, test Auto, every tier, a 30 fps game with 2X frame generation, NR, SR, HDR if available, controller feedback, audio sync, corruption recovery and a long run. Capture decoded width and height from the stats, not from the UI label. Confirm 2560x1440 with the 1440 tier on the eligible Insider console.

14. Run a final review for licensing, stale PS5 references, non-English text, secrets, proprietary runtime files and unintended behavior changes. Commit or push only if the user asks.

## Files to modify

- `CMakeLists.txt`, `cmake/VeyraXbox.cmake`, optionally `CMakePresets.json`
- `include/veyra/xbox/`, `src/xbox/`
- `include/veyra/source/XboxSessionSource.h`, `src/source/XboxSessionSource.cpp`
- `include/veyra/ui/XboxModel.h`, `src/ui/XboxModel.cpp`, `qml/Veyra/XboxSourceDialog.qml`
- `tests/xbox/XboxTests.cpp`
- `include/veyra/ui/UiLanguage.h`, `src/ui/UiLanguage.cpp`, `src/ui/QmlPlayerBridge_p.h`
- `i18n/catalog.json`, `scripts/i18n/extract.py`, `scripts/i18n/to_traditional.py`, `scripts/i18n/wrap_qml.py`
- `tests/unit/UiLanguageTests.cpp`
- `qml/Veyra/SettingsPage.qml`, `qml/Veyra/HomePage.qml`, `qml/Veyra/CaptureSourceDialog.qml`, all other QML files with `qsTr`
- `src/ui/`, `src/engine/`, `src/pipeline/`, `src/source/`, `apps/veyra/` and `scripts/` where Chinese prose appears
- `README.md`, `README_CN.md`, `README_EN.md`, `AGENTS.md`, `THIRD_PARTY_NOTICES.md`
- `docs/` and `docs/history/` where first-party prose appears
- New Markdown files under `docs/` and `handoffs/` for the negotiation capture and translation verification log

## Files to delete or retire

- PS5-only source, UI, dialogs, bridge methods, tests, scripts, docs and license notices listed in the PS5 removal section.
- `VEYRA_ENABLE_REMOTEPLAY` and all its CMake blocks.
- Traditional-Chinese-only generation logic in `scripts/i18n/to_traditional.py` and Chinese-source assumptions in `scripts/i18n/wrap_qml.py`.
- Non-English locale columns from `i18n/catalog.json` if the catalog is collapsed to English-only.

## Files to keep

- `include/veyra/remoteplay/Types.h` and the shared controller types Xbox needs.
- The shared enhancement graph, ANR adapter source, NR/SR/FG backends, export, subtitles, virtual camera and OBS compatibility.
- PC/Moonlight, capture card, screen capture, file and image sources.
- `LICENSE` and every vendor license file.
- The patched FFmpeg source, patch and license until an Xbox-only build proves they are unused.

## Risks and mitigations

- Microsoft can change the unofficial xHome protocol or restrict the account. Capture the official client, fail with a clear message, and keep reconnect and corruption recovery intact.
- 1440p is Insider-only. Do not claim public support; document the ring and console prerequisite.
- Third-party tier mappings disagree. The research gate in step 2 is blocking.
- H.264 level 3.1 is too low for 1440p60 if the service honors SDP. Verify with the official offer and answer before changing codec fmtp lines.
- A richer `clientdevicecapabilities` block may change bitrate and decode behavior. Add it behind tests and compare decoded dimensions, bitrate and corruption recovery against the old path.
- Frame generation improves smoothness, not input latency. Keep the README warning and measure submitted FPS separately from end-to-end latency.
- ANR Beta runtime redistribution is forbidden. Keep the public fork free of those assets.
- PS5 removal can break shared controller and capture-card paths. Keep `remoteplay/Types.h`, run an include scan, and revalidate controller forwarding.
- Removing Chiaki changes combined licensing. Update notices and preserve vendor license texts.
- Translation exactness is the largest non-code risk. A single automated pass is insufficient; require independent review, back-translation for docs, placeholder checks and a Unicode scan.
- Full C++ validation cannot run on this machine. Use a prepared Windows build environment and report any check that cannot run.

## Migration and rollback

- Work only on `feature/xbox-1440p-english`.
- `main` at `fe8df53` is the rollback point.
- Use `git revert` for any later commit; do not force-push `main`.
- Keep upstream syncs separate from feature work.
- Preserve user settings when adding `resolutionMode`; default Auto if the key is missing.
- Keep the old 1080p path as the fallback when the service declines 1440p.
- Do not delete the patched FFmpeg until an Xbox-only build proves it is unused.

## Acceptance criteria

- `git grep` finds no first-party non-English prose outside the explicitly excluded legal, vendor, identifier, URL, path and placeholder categories.
- `python -B scripts/i18n/extract.py --check` passes with English source strings and zero placeholder mismatches.
- `veyra_ui_i18n_tests` and `veyra_xbox_tests` pass.
- The Xbox dialog exposes Auto, 720p, 720p HQ, 1080p, 1080p HQ and 1440p, with Auto as default.
- Each tier sends the verified device info, settings body, capability block, dimensions, `VideoPreference` and `userRequestedResolutionUpdate`.
- The SDP offer matches the captured official client for the selected tier.
- A real eligible Series X or Series S reports 2560x1440 decoded frames with the 1440 tier.
- A 30 fps game runs through the shared FG path at 60 submitted FPS; the documentation states this is not end-to-end latency.
- NR, SR, HDR where available, colour, export, subtitles, virtual camera, OBS compatibility, audio and controller feedback still work.
- No PS5 source card, dialog, CMake option, Chiaki dependency, PS5 test or AGPL Remote Play notice remains.
- No ANR Beta runtime, model, DLL or installer is committed or published.
- No token, password, PII or account data appears in source, docs or logs.
- No commit or push occurs without an explicit user request.

## Suggested skills for the Build agent

- `engineering/research`: verify the official 1440p SDP, device info and control-channel behavior against primary sources.
- `engineering/tdd`: add literal-value tests for every resolution tier and for the English-only i18n runtime.
- `engineering/implement`: execute the C++ and QML changes in small commits if the user later asks for commits.
- `engineering/to-tickets`: split the repository-wide translation review into reviewable batches.
- `engineering/code-review`: review protocol, licensing and translation diffs before any commit.
- `productivity/writing-for-agents`: keep the negotiation capture and verification log concise and consistent.

## References

- `README.md`, `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md`, `AGENTS.md`
- `docs/ANR_FG_EXPERIMENT_2026-11-12.md`, `docs/BUILD_2.2.0.md`, `docs/BUILD_2.1.9.md`
- `docs/XBOX_STREAM_CORRUPTION_RECOVERY.md`, `THIRD_PARTY_NOTICES.md`
- `https://news.xbox.com/en-us/2026/09/09/xbox-insiders-october-2026-console-features-3/`
- `https://www.rfc-editor.org/rfc/rfc6184.txt`
- `https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/h264_levels.c`
- `https://raw.githubusercontent.com/andreencar/iGreenLight/main/Sources/App/Settings.swift`
- `https://raw.githubusercontent.com/andreencar/iGreenLight/main/Sources/Streaming/Channels.swift`
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/streaming_config.rs`
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/channels/control.rs`
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/channels/message.rs`
- `https://raw.githubusercontent.com/shyim/openxcloud/main/crates/xcloud-proto/src/preferences.rs`
- `https://github.com/unknownskl/xbox-xcloud-player`
- `https://github.com/unknownskl/greenlight`
