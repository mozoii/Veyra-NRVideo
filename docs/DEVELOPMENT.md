# Build and validation

This fork is Xbox-only and English-only. PS5 Remote Play and Chiaki are removed. The source language is English; translations live in `i18n/catalog.json` (en + zh-CN, zh-TW, ja, ko, es, fr, de, pt-BR, ru).

## Source tree

- Fork: `mozoii/Veyra-NRVideo` (branch `feature/xbox-1440p-english`, from `main` at `fe8df53`).
- Upstream (parent): `Likely7/Veyra-NRVideo`. Do not merge upstream PRs blindly; review each one against this fork's Xbox-only, English-only policy.
- This branch removes the PS5 path: `src/remoteplay/`, `include/veyra/remoteplay/` (except the shared controller types, relocated to `include/veyra/input/`), `qml/Veyra/Ps5SourceDialog.qml`, `apps/veyra/ui/RemotePlayPanel.*`, `scripts/remoteplay/`, `licenses/remoteplay/` and the `VEYRA_ENABLE_REMOTEPLAY` option.

## Build requirements

Windows 11 x64 with Qt 6.8.3, DirectX 12 dependencies, libdatachannel, nlohmann-json, Opus, patched FFmpeg (retained `scripts/ffmpeg/ps5-h264-slices.patch`) and libdovi. `vcpkg`-style dependency trees are expected at the paths the build scripts hardcode; the handoff notes this environment lacked cmake/ninja, so this fork's build must run on a Windows 11 x64 machine.

`VEYRA_ENABLE_XBOX=ON` is the default. The PS5 option no longer exists. Expected targets include `veyra_qml_ui`, `veyra_xbox_tests` and `veyra_ui_i18n_tests`.

```powershell
cmake --preset x64-release -S . -B build/x64-release -DVEYRA_ENABLE_XBOX=ON
cmake --build build/x64-release --target veyra_qml_ui veyra_xbox_tests veyra_ui_i18n_tests --parallel 2
```

Keep build concurrency ≤ 2 and ≤ 900 s per build invocation when building locally. Tests must be short-lived; per-test ≤ 300 s.

## Validate

In order:

1. `python -B scripts/i18n/extract.py --check` — catalog must match sources with 0 missing translations and 0 placeholder problems.
2. Unicode scan for non-English prose in first-party text files (excluding `.git`, `licenses/`, `third_party/`, `docs/history/` and `i18n/catalog.json`'s translation columns).
3. `veyra_ui_i18n_tests` — the English-source runtime contract (exact, pattern, segment and file-status string translations; `en` pass-through).
4. `veyra_xbox_tests` — console discovery, 1440p wire values, negative mode tests.
5. QML UI smoke test for Xbox dialogs, Home, Settings.
6. Physical Series X/S capture — **pending**, see `docs/xbox-1440p-negotiation.md`. 1080p and below are the tested path; 1440p is provisional.

## History and records

Historical task notes and release records under `docs/history/` describe the parent project's PS5-era work. They are kept for audit and are not a description of the current fork. Do not copy PS5-era build commands into this fork's runbooks.
