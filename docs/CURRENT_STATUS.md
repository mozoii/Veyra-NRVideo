# Current status

This is the fork's working state, not a release note. The parent project's release records (v2.2.0, 2.1.9) are historical and kept under `docs/history/`.

## Fork state

- Branch: `feature/xbox-1440p-english` (from parent `main` at `fe8df53`).
- Xbox home streaming is the primary console source; PS5 Remote Play and Chiaki are removed.
- `VEYRA_ENABLE_XBOX=ON` by default. `VEYRA_ENABLE_REMOTEPLAY` is gone.
- English is the source language. `i18n/catalog.json` carries the English source plus nine translations (zh-CN, zh-TW, ja, ko, es, fr, de, pt-BR, ru), all filled, 0 stale, 0 missing, 0 placeholder problems.
- Resolution tiers: Auto (default), 720p, 720p HQ, 1080p, 1080p HQ, 1440p (ceiling). **1440p is provisional** — the wire-level values are from third-party client behaviour, not a physical console capture. See `docs/xbox-1440p-negotiation.md`.
- CI: none yet. `veyra_ui_i18n_tests` and `veyra_xbox_tests` need a Windows 11 x64 + Qt 6.8.3 + DirectX 12 machine; they were not run in this environment.

## What is not done

- **Physical console capture (Auto, every tier, NR/FG/HDR/audio/corruption)** — blocked on a physical Series X/S with Alpha or Alpha Skip-Ahead. 1080p and below are the tested path.
- **Executable validation** — no toolchain in this environment; catalog and Unicode checks run, unit/QML tests are not built here.
- **Release** — no fork release has been published or tagged. Do not claim 1440p delivery or any 2.3.0 version.

## What lives outside this file

- Negotiation findings: `docs/xbox-1440p-negotiation.md`
- PS5-era historical records: `docs/history/` (kept for audit; not current-state claims)
- Parent-project release notes: `docs/history/releases/`
