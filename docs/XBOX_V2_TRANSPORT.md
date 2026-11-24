# Xbox V2 transport (WebView2 reference player)

V2 is a selectable Xbox transport, not the default. It terminates WebRTC in an
invisible Microsoft Edge WebView2 running the reference XStreaming player and
exports encoded H.264 access units and Opus packets before the shared decode
and enhancement chain. V1 (native libdatachannel) remains the default and the
tested path. See `docs/UPSTREAM_PR_MERGES_2026-11-23.md` for PR credit.

## Build

- WebView2 SDK: vcpkg port `webview2` (formerly `unofficial-webview2`). In this
  tree vcpkg was in manifest mode, so the install command was
  `vcpkg install webview2:x64-windows-static --classic`, run from the vcpkg
  root. It provides `find_package(unofficial-webview2 CONFIG REQUIRED)` and
  `unofficial::webview2::webview2` (static loader).
- The Evergreen WebView2 runtime must be installed on the test/runtime machine.
  The SDK loader is statically linked; no loader DLL ships.
- `cmake/VeyraXboxWeb.cmake` builds `veyra_xbox_web` (host + embedded bundle),
  `cmake/VeyraXboxWebEmbed.cmake` generates the byte array from
  `resources/xbox-player/veyra-xbox-player.js`; `XboxWebSource.cpp` is part of
  `veyra_sources`. `veyra_xbox_web_gamepad_tests` is an offline test target.

## Engine selection

`XboxConnectDesc::transportEngine` selects 1 (V1, default) or 2 (V2).
`EngineController` holds `shared_ptr<IXboxSource>` and creates the matching
source. Both sources share `XboxStats.h`, `IXboxSource.h`, the decoder, audio
session and `FramePacket` contract, so the enhancement chain is
transport-independent. The Xbox dialog exposes the engine. V2 is a
1080p-class engine; the UI routes a 1440p selection to V1 with a visible note.

## Player bundle and notices

- Bundle: 728,046 bytes, SHA-256
  `852C72B20E919420CB0D31C5ABC192485889DF0416E2838122B7BB2210DE8892`.
- Provenance: `resources/xbox-player/README.md`; MIT text:
  `licenses/xbox/xstreaming-player-MIT.txt`; WebView2 text:
  `licenses/xbox/webview2.txt`. Notices added to `THIRD_PARTY_NOTICES.md` under
  the amended notice rule.

## 1440p decision

The shipped bundle has exactly one hardcoded `dimensionschanged` payload
(1920x1080) and no `VideoPreference` / `userRequestedResolutionUpdate`. Two
client-side attempts were made and removed: an `RTCDataChannel.prototype.send`
shim and a build-time port of Greenlight PR #1752 that wrapped the player's
channel instance methods. On 2026-11-24 a physical Series X still returned a
1920x1080 decode for a 1440p V2 selection. V2 is therefore treated as a
1080p-class engine, the pinned bundle is embedded unchanged, and 1440p uses V1.
The findings are kept in `docs/WEBVIEW2_1440P_RESEARCH_2026-11-24.md`.

## Verification and gaps

Verified here: V2 builds with WebView2 SDK and the encoded bundle; the gamepad
mapping test passes 30 checks; i18n and UI contract tests pass. On 2026-11-24
the user selected 1440p with V2 on a physical Series X and the HUD showed a
1920x1080 decode. Both client-side patches were removed after that result.
Still open: the V1/V2 A/B, physical-console input, and the FG stop/catch-up
report. V2 1440p is not supported; the HUD decode size is the evidence.
