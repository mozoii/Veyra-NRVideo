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

## Docs translation stop (2026-11-22)

The first-party prose sweep was deliberately stopped at the user-facing/current
docs and code-of-truth files, not at every historical record:

- **Done**: `src/`, `include/`, `apps/`, `qml/`, `tests/`, `scripts/` (except historical records), `tools/`, root `README*.md`, `THIRD_PARTY_NOTICES.md`, `AGENTS.md`, `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md`, `docs/README.md`, `docs/ARCHITECTURE.md`, `docs/xbox-1440p-negotiation.md`, `CMakeLists.txt` comments, `i18n/` runtime and scripts (English source keys), the Xbox 1440p tier files and tests.
- **Left in place, deliberately**: dated historical records (`docs/history/`, `docs/WORKLOG.md`, `docs/HISTORY.md`, most `docs/` dated plans/reports/worklogs), the `loop/` loop journal/evidence/prompts, the `prototypes/ui-redesign-2026-09-25/` prototype sources, the two root technical contracts (`VEYRA_PRODUCT_SPEC_V1.md`, `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md`) — all Chinese, all historical or design records. The two root contracts are the one real gap: they are the current product/technical boundary and should be translated with the user's sign-off, not silently machine-translated.
- **PS5-specific historical docs** were deleted (PS5/Chiaki surfaces); non-PS5 history is kept for audit.
- `i18n-scripts/` is a stale duplicate of `scripts/i18n/` and is intentionally untracked; the sweep helpers (`scripts/i18n/translate-cjk.py`, `zh-map.json`) remain untracked as sweep tooling; `scan-cjk.py` is committed as the detection tool.
