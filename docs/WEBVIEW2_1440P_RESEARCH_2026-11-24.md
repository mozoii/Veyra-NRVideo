# Web research: pushing 1440p through the WebView2 (V2) stream

Date: 2026-11-24. Question: our V2 player returns a 1920x1080 decode in the HUD
when 1440p is selected, while V1 reaches 1440p on the same physical Series X.
What does a client have to do to get 1440p from a Chromium-based player, and
what is our shim missing?

## Primary sources

1. Greenlight PR #1752, "Add a stream resolution setting (Auto, 720p, 1080p,
   1440p)" by iu5gal, open against `unknownskl/greenlight`.
   - PR metadata and body: https://api.github.com/repos/unknownskl/greenlight/pulls/1752
   - Patch: https://patch-diff.githubusercontent.com/raw/unknownskl/greenlight/pull/1752.patch
   - The actual helper: https://raw.githubusercontent.com/iu5gal/greenlight/feature/stream-resolution/packages/desktop/renderer/lib/streamresolution.ts
2. XStreaming-player at the exact base commit of our bundle,
   `8e178aef067ec31625311865e5ee51a6924823e2`:
   - tree: https://api.github.com/repos/Geocld/XStreaming-player/git/trees/main?recursive=1
   - `src/Channel/Message.ts`, `src/Channel/Control.ts`, `src/index.ts` under
     https://raw.githubusercontent.com/Geocld/XStreaming-player/main/
3. XStreaming issue #110, "Unable to set 1080HQ":
   https://github.com/Geocld/XStreaming/issues/110
4. XJAUS 1.2.4, `src/stream-options.ts`:
   https://raw.githubusercontent.com/perjesus111-hub/xjaus/main/src/stream-options.ts

## What the working clients actually do

Greenlight PR #1752 is the clearest primary source. It states:

- `xbox-xcloud-player` 0.2.11 has no resolution option and always tells the
  console it is 1920x1080.
- The fix is applied **after `bind()`, before the message-channel handshake**.
- It wraps two **channel instance methods**, not WebRTC prototypes:
  `control.start` sends `VideoPreference` and `userRequestedResolutionUpdate`
  immediately after the original `start()` (which sends `authorizationRequest`);
  `message.generateMessage` rewrites
  `/streaming/characteristics/dimensionschanged`.
- "Both messages are needed. Without them, a console set to 1440p or 4K streams
  at 1440p even when 1080p is announced. With `userRequestedResolutionUpdate`
  alone, 1080p is still ignored."
- The play request's `X-MS-Device-Info` display dimensions become
  `height * 16 / 9` by `height`, i.e. 2560x1440 for 1440p.
- Tested on macOS with Remote Play to a Series console; decoded sizes were
  2560x1440, 1920x1080 and 1280x720 for the three explicit settings.

The helper's exact payloads are:

```js
control.send(JSON.stringify({
  message: 'VideoPreference', type: 'VideoPreference',
  resolution: { width, height }, dimensions: { width, height }, maxFPS: 60,
}));
control.send(JSON.stringify({
  message: 'userRequestedResolutionUpdate', resolutionAlias: String(height),
}));
```

and `dimensionschanged` gets `horizontal`, `vertical`, `preferredWidth`,
`preferredHeight`, `safeAreaRight`, `safeAreaBottom` set to the chosen extent.

XJAUS independently uses a `{ width: 2560, height: 1440 }` profile and raises
the default bitrate to 30 Mbps for that profile
(`recommendedBitrate`), which matches the fork's P1440 tier of 30,000 kbps.

## What the bundled player contains

Reading `src/index.ts` of XStreaming-player at the base commit shows the config
interface has UI, input, audio, gamepad and FSR options but **no resolution or
viewport option**. `Channel/Message.ts` still sends a hardcoded
`{horizontal:1920, vertical:1080, preferredWidth:1920, preferredHeight:1080,
safeAreaRight:1920, safeAreaBottom:1080, supportsCustomResolution:true}`.
`Channel/Control.ts` sends `authorizationRequest`, `gamepadChanged` and the
keyframe request only. So no upstream source patch exists to consume; any fix
has to patch the player (our Route 1) or intercept at runtime (our Route 2).

## Comparison with our V2 shim

Our `WebView2Host::resolutionShim()` rewrites the same six `dimensionschanged`
fields and injects the same two control messages with the same shape, in the
same order (authorizationRequest, then VideoPreference, then
userRequestedResolutionUpdate). `XboxWebSource` sends the play request through
the fork's `StreamApi::play(..., resolutionMode)`, so `X-MS-Device-Info`
carries 2560x1440 and `settings.osName` comes from the tier. On paper our
payloads match the proven Greenlight implementation.

The differences that remain, in order of likelihood:

1. **The shim may not be firing.** It patches `RTCDataChannel.prototype.send`
   in the page. Greenlight deliberately patches channel **instance** methods
   after `bind()` and notes "No prototypes are patched". Our host logs already
   emit `veyra shim: ...` lines when the wrapper and the rewrites run; absence
   of those lines in a live session proves the patch never caught a send.
2. **The play request may not be 1440p.** If `resolutionMode` was Auto at
   launch, the device-info header and control messages would both be 1080p.
   The host log line `V2 viewport shim: ...` records the extent actually fed to
   the shim.
3. **Service/region decides.** XStreaming issue #110 reports that 1440p forcing
   works on some servers and not others, that Microsoft does not support
   forcing 1080p HQ on some servers, and that available tiers can be only 720p
   or 1440p. Greenlight's PR likewise says the console's display must be set
   to 1440p or 4K UHD. This is consistent with the handoff's warning that the
   service, not the SDP or the shim log, chooses the encoded size.

## Recommended next step

Decision (2026-11-24): both client-side routes were tried and removed. The
`RTCDataChannel.prototype.send` patch was replaced with Greenlight's
instance-level wrapping, applied to the bundled player at build time and
verified by an offline harness; a physical Series X still returned a 1920x1080
decode for a 1440p V2 selection. V2 is therefore a 1080p-class engine, 1440p
routes to V1, and the pinned bundle is embedded unchanged. This document is
kept as the record of why. WebView2's Chromium is not established as the
blocker; Greenlight runs in Electron/Chromium and gets 1440p, so a full source
rebuild of the player remains the only untried client-side route.

Secondary/context sources (not used to establish the mechanism): Microsoft
Insider release notes and community reports on 1440p Remote Play availability
(2026-10/11), and Digital Foundry's 2025-09 note that xCloud 1440p is real but
selectively deployed.
