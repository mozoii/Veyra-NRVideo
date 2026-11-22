# Veyra Agent rules (fork)

This is the Xbox-only, English-only fork of Veyra-NRVideo. PS5 Remote Play and Chiaki are removed. The source language is English; UI text, comments and docs are English first. The handoff at `handoffs/handoff-veyra-xbox-1440p-english.md` is the record of the fork's origin.

## Entry and current state

- Fork: `mozoii/Veyra-NRVideo`. Branch `feature/xbox-1440p-english` from `main` at `fe8df53`. Upstream (parent): `Likely7/Veyra-NRVideo`.
- Read `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md` and `docs/xbox-1440p-negotiation.md` before working. Do not re-run parent-project session loops or stale scope guards.
- **Do not claim 1440p is verified.** The default tier is Auto; 1080p and below are the tested path. 1440 wire values are provisional pending a physical Series X/S capture.
- ANR Beta runtime files are user-supplied and excluded from the public fork.

## Working discipline

1. Do not lie; separate facts, inference, historical results, this session's tests and user feedback. Submitted FPS is not physical refresh rate or end-to-end latency. Do adversarial review before delivery.
2. On start: check git status, the working tree, branch, processes and applicable rules. Do not overwrite uncommitted work, stop user processes or grab capture cards or host connections. Preserve confirmed user settings.
3. Single agent. Build concurrency ≤ 2, ≤ 900 s per build; each test ≤ 300 s. Run relevant checks for the change; keep failures as evidence.
4. Tests use private data-dir/log/cache. TEMP/TMP only for child processes; do not change global environment, real configuration, drivers, credentials, runtimes or models.
5. Minimal-responsibility fixes; do not rewrite the whole project. Restructure while keeping interfaces, config numbers/format, defaults, signals and handling. UI statistics 33 ms / status 250 ms refresh, GPU-high default, VFG auto out-of-the-box-off and similar existing decisions are not to change unilaterally.
6. Do not send messages/emails/group notifications, comment/close issues, push/release or shut down machines unless the user explicitly asks. System component updates wait for Windows administrator confirmation.

## Paths and preservation

- New build/test/download/diagnostic/temporary/packaging/extract artifacts go under the task directory (E:/项目/Veyra/{build,tests,logs,reports,tmp,downloads,deps,test-packages,releases,verify,archives,worktrees}). Source and controlled scripts/docs go in the source tree. Do not create R&D artifacts in Desktop, Downloads, C drive or system Temp.
- System Virtual Camera at `C:/Program Files/VeyraVirtualCamera` is a real system component; do not delete it as R&D garbage.
- The real patched FFmpeg with its corresponding source, patch and license is retained: the high-slice-capacity H.264 patch (originally a PS5-era fix, still valid for the shared FFmpeg build), dav1d and the zlib needed for compressed PGS subtitles. Do not revert to the unpatched base package.
- Legal and vendor notices stay untranslated and unmodified: `LICENSE`, `licenses/`, `THIRD_PARTY_NOTICES.md`, `UPSTREAM.json`, vendor notices.

## Product and implementation contracts

- Windows x64, C++20, D3D12, Qt/QML, direct NGX; no Streamline. Playback/capture/images share enhancement logic, color, guidance and reset.
- Capture cards and regular video get only final pixels. Estimated optical flow/depth is not game-native input; A/B frame generation requires B to actually arrive.
- Keep the existing defaults and authorized live NR→SR path; FG runs after effects; OSD/subtitles follow the existing overlay contract. Export, paused single-frame and images must not reuse the real-time preview frame-drop policy.
- All inputs explicitly parse range/matrix/transfer, unified working texture; the sink does exactly one output transform. HDR proxy enhancement and native HDR model inference are described separately.
- Seek/transition/PTS discontinuity/capture drop/resize/source change/pause-resume/device loss explicitly reset; queues and history are bounded. Do not fabricate PTS or mute or loosen alarms to hide underruns.
- Normal enhancement/video export must not add pixel readback, per-pass CPU fence waits or unbounded queues. The virtual camera is the explicitly authorized compatibility output boundary: when enabled it runs GPU NV12 conversion / bounded async readback / shared queue without making the render thread wait for consumers; camera video and audio are handled separately, and XeSS display-side generated frames are never presented as in-image output.
- Guidance follows current→previous, post-SR working-extent pixels, low-confidence decay; depth/confidence format, resource state and NGX parameters follow the existing contract.
- RAII and reverse teardown, restricted absolute-path loading, HRESULT/NGX errors, size/format and GPU timing logs stay. Smooth Motion is user-managed; do not change drivers, do not present Smooth Motion-generated frames as software recording output.
- Language names remain in their own script (简体中文/繁體中文/日本語/한국어); never translate those.

## Sources, runtime and delivery

- Proprietary SDKs, DLLs, models, headers, LIBs, captured frames, credentials and configs do not enter source Git/LFS/resources/base64. Do not modify, re-sign or mask binaries from game/driver caches.
- Users may replace DLLs; the software does not refuse replacements by hash or signature, but default release components are audited file-by-file. Do not expand the public distribution scope automatically; ANR Beta assets stay excluded.
- OBS/libdshowcapture camera source is documented in THIRD_PARTY_NOTICES and UPSTREAM.json. ReShade/RenoDX add-ons do not enter the main line or runtime package.
- BGM confirmed for distribution goes only in the runtime package media directory, never in source Git or the source ZIP.
- README/Release both-QR codes side by side at width 220, user-specified homepage and zero-start tutorial stay; editing release assets still needs this round's authorization.
- Record actual commands, results, failures/fixes, paths, space and unverified scope in the session note; do not treat interface stubs, duplicated same-frame results or theoretical FPS as a completion proof.
