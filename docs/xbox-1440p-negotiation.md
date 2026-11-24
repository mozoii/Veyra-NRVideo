# Xbox 1440p negotiation findings

Status: **blocked at the physical capture**. This is the research gate of
`handoffs/handoff-veyra-xbox-1440p-english.md` step 2. The third-party clients
disagree on the 1440p wire values, so per the handoff the mapping must not be
guessed from them. Nothing in this document is baked into the client as fact.

Date: 2026-11-22. Branch: `feature/xbox-1440p-english` (from `main` at `fe8df53`).

## Physical console observations (user-reported 2026-11-22, not a wire capture)

The user confirmed that 1440p60 Remote Play is live on stable and works from a
physical Series X. During that session the Windows-side receive counter read
**30.7 Mbps**, above the advertised `maxBitrateKbps=30000` in
`include/veyra/xbox/ResolutionMode.h` (provisional) and above iGreenLight's
17-28 Mbps comment. This is a host network-counter reading, not an SDP capture
and not the official client's wire trace; the exact offer/answer SDP, fmtp and
capability block from the physical console are still uncaptured. It does answer
one question: the console is not being held below the advertised ceiling by the
receiver's current advertisement, so REMB / `b=AS` / L5.1 fmtp changes are
confirmations at most, must not carry a value under the observed plateau, and
are not shipped without a recorded rollback. No tier constant changed from this
evidence. The stream stats HUD was fixed in the same session (it was compiled
out when `VEYRA_ENABLE_MOONLIGHT=OFF`); its payload Mbps reads slightly below the
wire counter because it counts depacketized bytes.

## What the official announcement says (secondary reporting)

The official Xbox Wire article
(`https://news.xbox.com/en-us/2026/09/09/xbox-insiders-october-2026-console-features-3/`)
is not fetchable from this machine (HTTP 403). The following is reported by
secondary coverage:

- Microsoft began testing up to 1440p Remote Play on 2026-09-09.
- Scope: Xbox Series X|S consoles in the Alpha and Alpha Skip-Ahead Insider
  rings only. No stable public rollout announcement as of 2026-11-22.
- Prerequisite: console display output set to 1440p or 4K
  (Settings > General > TV & display options), and a receiving device that
  supports the improved quality.
- With streaming quality set to Auto, Remote Play selects the highest
  supported resolution and bitrate, even if the on-screen setting looks lower.
- Remote Play requires no Game Pass subscription; it is separate from Xbox
  Cloud Gaming.
- Sources: Thurrott (2026-09-09), Game News Plus (2026-09-09).

## Third-party client evidence (not yet verified against a physical console)

### iGreenLight (Swift, `Sources/App/Settings.swift`, `Sources/Streaming/Channels.swift`)

- Tiers: 720p / 720p HQ / 1080p / 1080p HQ / 1440p.
- Control-channel alias per tier: `720` / `720HQ` / `1080` / `1080HQ` / `1440`.
- `os.name` (spoofed device profile): 720p/720p HQ → `android`; 1080p →
  `windows`; 1080p HQ and 1440p → `tizen`. The comment in the file notes
  720p HQ has no known official profile and reuses the 720p one.
- `requestedSize` (dimensionschanged + displayInfo): 720p → 1280x720 (or
  1280x800 with request1610), 1080p → 1920x1080 (or 1920x1200 with request1610),
  1440p → 2560x1440 (or 2560x1600 with request1610), 1080p HQ → 1920x1080.
- Typical bitrates noted in the file comment: 720p ≈ 2 Mbps, 1080p ≈ 9 Mbps,
  1080p HQ ≈ 12 Mbps, 1440p ≈ 17–28 Mbps.
- H.264 Main L5.1 fmtp: `4d0033` (listed as an option, default is Main 4d0033?
  see note below).

### openxcloud (Rust port of a Qt client, `xcloud-proto`)

- Modes: `Auto` / `p720` / `p1080` / `p1440` / `p1080HQ` (no 720HQ).
- Aliases for `userRequestedResolutionUpdate`: `Auto` / `720` / `1080` /
  `1440` / `1080HQ`.
- Bitrates (`quality_tier_profile`): p720 → 10000 kbps, Auto/p1080 → 20000 kbps,
  p1080HQ/p1440 → 30000 kbps. FPS 60.
- `osName`: Auto/p720 → `android`; p1080/p1440 → `windows`; p1080HQ → `tizen`.
- Advertised display dims: Auto/p720 → 1280x720, p1080 → 1920x1080,
  p1440 → 2560x1440, p1080HQ → 3840x2160 (seen only in this source; contradicts
  the 1080p HQ reading and the iGreenLight reading).
- `clientdevicecapabilities` content: `supportsCustomResolution`,
  `supportsHevc`, `supportsHdr`, `supportsFps`, `maxWidth`, `maxHeight`,
  `maxBitrateKbps` plus a nested `video` object (width/height/maxWidth/maxHeight/
  maxBitrateKbps).
- Message-channel `dimensionschanged`: horizontal/vertical/preferredWidth/
  preferredHeight/safeArea*/supportsCustomResolution (supportsCustomResolution
  always true).
- Control channel: `authorizationRequest` → `VideoPreference` (resolution +
  dimensions + maxFPS, optional lowercased `colorRange`) → later
  `gamepadChanged` + `userRequestedResolutionUpdate` (alias).
- `VideoPreference` JSON: `{"dimensions":{"height":H,"width":W},"maxFPS":N,
  "message":"VideoPreference","resolution":{"height":H,"width":W},
  "type":"VideoPreference"}` (+ `colorRange` if set, lowercased).
- `userRequestedResolutionUpdate`: `{"message":"userRequestedResolutionUpdate",
  "resolutionAlias":"<alias>"}`.
- `X-MS-Device-Info` shape (browser-style, `www.xbox.com` app info) and
  `settings.osName` block are built as a hand-assembled string; key order and
  formatting pinned in the port's tests.
- Note (openxcloud): `StreamPreferences` default is P1080, locale "en-US";
  `timezoneOffsetMinutes` is 120 in the port (Veyra currently sends 480).

### The two sources disagree (as the handoff anticipated)

- 1440p `osName`: `tizen` (iGreenLight) vs `windows` (openxcloud).
- 1080p HQ display dims: 1920x1080 (iGreenLight) vs 3840x2160
  (openxcloud) vs the 1080p tier in the capture-independent reading.
- Bitrates: iGreenLight comments (~17–28 Mbps for 1440p) vs openxcloud's
  30000 kbps.
- 720p HQ: explicitly unverified in iGreenLight (reuses the 720p profile) and
  absent from openxcloud entirely.
- `timezoneOffsetMinutes`: 480 (Veyra, Greenlight-derived) vs 120 (openxcloud).
- `X-MS-Device-Info`: Greenlight-derived `Microsoft.GamingApp` native profile
  (Veyra) vs the browser-style `www.xbox.com` profile (openxcloud).

### gamewarp (marketing page, not a protocol source)

`https://vipersoftworks.com/gamewarp/` advertises five modes up to 1440p. Not a
protocol source; shape only.

## What is missing and blocked on the physical capture

The official client must be captured on a physical Series X or Series S in
Alpha or Alpha Skip-Ahead, console output set to 1440p or 4K, streaming quality
Auto. Record:

- exact `X-MS-Device-Info` (key order, values, profile),
- `/v5/sessions/home/play` request body (settings block, osName, locale,
  timezoneOffsetMinutes, sdkType),
- local offer SDP (codecs, levels, `b=AS` presence and value),
- answer SDP,
- `clientdevicecapabilities`,
- `dimensionschanged`,
- `VideoPreference` and `userRequestedResolutionUpdate` messages and order,
- `rateControlBitrateUpdate` if present,
- the decoded frame width/height from the stats.

Without this capture, all 1440p-specific protocol values are provisional and
must not be presented as verified. A real 2560x1440 decoded session is the
acceptance gate; an SDP can advertise it, but the service decides what to send.

## Current client behavior (baseline, do not overwrite without the capture)

- `X-MS-Device-Info` (Greenlight-derived), `1900x1080` display, `os.name`
  "Windows 11", `sdktype` native, appInfo `Microsoft.GamingApp`.
- `/play` settings: `locale` (caller), `osName: "windows"`,
  `timezoneOffsetMinutes: 480`, `sdkType: "web"`.
- SDP offer: H.264 Main `4d001f`, Constrained Baseline `42e01f`, Baseline
  `42001f`; no `b=AS`; no `VideoPreference`/`userRequestedResolutionUpdate`;
  empty `clientdevicecapabilities`; dimensions 1920x1080.

## Native software decode and V2 (2026-11-23)

Native-size software decode no longer scales a 1440p stream into a fixed
requested target: a null `nv12Target` makes the decoder own an NV12 frame at
the bitstream's decoded size, and `XboxSessionSource` clones it before
publishing. The V1 tier mapping, `X-MS-Device-Info`, settings body and control
messages (`VideoPreference`, `userRequestedResolutionUpdate`,
`dimensionschanged`) are unchanged by that decode fix. The physical capture is
still the gate; do not treat the native-size test as proof of 1440p delivery.

The V2 transport (Chromium reference player in WebView2) adds a separate 1440p
question. The shipped bundle (728,046 bytes, SHA-256
`852C72B20E919420CB0D31C5ABC192485889DF0416E2838122B7BB2210DE8892`) has one
hardcoded `dimensionschanged` payload at 1920x1080 and contains no
`VideoPreference`, `userRequestedResolutionUpdate` or resolution-alias handling.
Its control channel sends `authorizationRequest` and `gamepadChanged` only;
`Channel/Base.ts` encodes data-channel messages as UTF-8 bytes, and
`Channel/Message.ts` sends `dimensionschanged` as `{type:"Message",
content:"<json>", id, target, cv}`. The player source at base `8e178aef` plus
patch `e4c12bd` is not published, so Route 1 (source patch and rebuild) is
blocked. Two Route 2 clients were tried and removed: an
`RTCDataChannel.prototype.send` shim, then a build-time port of Greenlight PR
#1752 that wrapped the player's channel **instance** methods after `bind()`.
Neither produced a 2560x1440 decode on a live console. The pinned bundle is
embedded unchanged.

On 2026-11-24 the user selected 1440p with V2 on a physical Series X. The HUD
reported a 1920x1080 decode, so V2 is a 1080p-class engine and is not supported
for 1440p. The decoded size in the HUD is the only proof, never the SDP or a
client-side log. The UI now routes a 1440p selection to V1 and says so. The V1
tier mapping and control messages are unchanged and 1440p there remains
provisional pending the physical capture.

## Decision recorded for the Build agent

No tier mapping, bitrate, H.264 level or SDP line is changed from this
document. The Xbox code changes that touch the wire (device info, settings
body, capability block, control-channel messages, SDP lines) are provisional
and must land behind the capture. See `handoffs/handoff-veyra-xbox-1440p-english.md`
step 2 and the risks section.
