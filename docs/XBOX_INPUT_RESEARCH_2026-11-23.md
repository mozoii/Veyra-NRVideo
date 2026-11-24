# Xbox streaming input: client comparison research (2026-11-23)

Question: why does a controller read correctly in Veyra but produce no input on the
console, while other remote-play clients work? This note records primary-source
comparisons and the local evidence gathered on the same machine. It does not
claim the controller works: no console reaction was observed in any test.

## Local evidence (this machine, this branch)

With temporary tagged logging (since removed) at three boundaries:

- `input` channel: `focus changed focused=1` while streaming, with the opened pad
  `03002854de2800000413000002006800` (Steam Controller) and later an Xbox 360 pad.
- SDL read boundary: `buttons=0x00000001` when A was held on the Steam Controller.
- Source send boundary: the mapped frame carried `buttons=16` (Xbox BtnA).
- Data-channel boundary: `delivered=1` for every report, 38 bytes, correct
  type/count/index, button mask, axes, triggers and physicality bytes; sequence
  increasing. `delivered=1` means libdatachannel accepted the send, not that the
  console processed it.

Two different pads failed identically. The same failure occurred with gamepad
index 0 and index 1, with ~250 sends/s and with the 33 ms heartbeat cadence, and
with physicality `1/1` and `0/0`. All trial changes were reverted; the build is
back to the original wire values and `veyra_xbox_tests` passes 137/0.

## Primary-source comparison

### Greenlight `main-v2` (working reference client)

- `packages/player/src/client/lib/channel/input.ts`: `getChannelConfig()` returns
  `{ordered: true, protocol: '1.0'}`. Veyra uses the same two values.
  Source: https://github.com/unknownskl/greenlight/blob/main-v2/packages/player/src/client/lib/channel/input.ts
- `packages/player/src/client/lib/channel/input/packet.ts`:
  - `_writeGamepadData` writes index, a u16 LE button mask with A=16, four i16
    axes (Y negated), two u16 triggers, `PhysicalPhysicality = 1` LE and
    `VirtualPhysicality = 1` BE, 22 bytes per pad. Veyra's `gamepadReport` is
    byte-for-byte equivalent.
  - `toBuffer` writes u16 LE report type, u32 LE sequence, f64 LE `performance.now()`
    timestamp at offset 6, 14-byte header, count at offset 14. Same as Veyra.
  - The input channel config in the SDP exchange uses
    `input: {minVersion: 1, maxVersion: 8}`; Veyra sends the same.
  Source: https://github.com/unknownskl/greenlight/blob/main-v2/packages/player/src/client/lib/channel/input/packet.ts
  SDP config: https://github.com/unknownskl/greenlight/blob/main-v2/packages/desktop/main/helpers/streammanager.ts
- `packages/player/src/client/lib/channel/control.ts`: `sendAuthorization()`
  sends `authorizationRequest`, then `gamepadChanged` add and remove. Veyra
  sends those plus a final add, matching the attachment path in `gamepad.ts`.
  Source: https://github.com/unknownskl/greenlight/blob/main-v2/packages/player/src/client/lib/channel/control.ts
- `packages/player/src/client/lib/channel/message.ts`: after `HandshakeAck` the
  client sends systemUi, clientAppInstallId, orientation, touchInputEnabled,
  clientDeviceCapabilities and dimensions messages. Veyra's `sendClientConfig()`
  sends the same set.
  Source: https://github.com/unknownskl/greenlight/blob/main-v2/packages/player/src/client/lib/channel/message.ts
- `packages/player/src/client/lib/channel/input/queue.ts`: gamepad frames are
  queued at roughly 60 Hz with a 33 ms heartbeat; Veyra's reader polls at 250 Hz
  and its source layer now dedups with a tolerance to preserve that heartbeat.
  Source: https://github.com/unknownskl/greenlight/blob/main-v2/packages/player/src/client/lib/channel/input/queue.ts

### `xbox-xcloud-player` 0.2.9, the release that fixed this exact symptom

Greenlight issue #1118 and #1117 describe the same failure: controller detected
locally, no input reaches the console. The fix was a dependency bump from
`xbox-xcloud-player` 0.2.8 to 0.2.9.
Source: https://github.com/unknownskl/greenlight/commit/a865f80b320eee55aaa3f5e1d45d493e1017ee65.patch
Issue: https://github.com/unknownskl/greenlight/issues/1118

In 0.2.9:

- `dist/Channel/Input/Packet.js` writes `PhysicalPhysicality = 0` and
  `VirtualPhysicality = 0`, unlike Greenlight `main-v2` (1/1).
- `dist/Driver/Gamepad.js` changed `requestState()` to `requestStates()` and the
  input path now uses `gpState[0]`; `run()` uses `setTimeout(..., 1000/60)`
  instead of `requestAnimationFrame`. The fix was state/index handling, not a
  new wire format.
- `dist/Library.js` data-channel config and SDP `input` version range are the
  same as Veyra's.
Sources: https://unpkg.com/xbox-xcloud-player@0.2.9/dist/Channel/Input/Packet.js ,
https://unpkg.com/xbox-xcloud-player@0.2.9/dist/Driver/Gamepad.js ,
https://unpkg.com/xbox-xcloud-player@0.2.9/dist/Library.js

### Better xCloud

`src/modules/patcher/patches/src/poll-gamepad.ts` passes gamepad mappings with
`PhysicalPhysicality: 0`, `VirtualPhysicality: 0` and `Dirty: true` into the
official player's `inputSink.onGamepadInput`. `patcher.ts` also patches a
`StreamGate` to report ready, showing the official player has a readiness gate
that can suppress input; Veyra has no equivalent gate in its WebRTC session
beyond `gate_->open && handshakeDone_`, both of which are open in the logs.
Source: https://github.com/redphx/better-xcloud/blob/typescript/src/modules/patcher/patches/src/poll-gamepad.ts
Source: https://github.com/redphx/better-xcloud/blob/typescript/src/modules/patcher/patcher.ts

## Conclusion

No client-side difference was found that distinguishes Veyra from working
clients: channel protocol and ordering, SDP version range, report layout,
timestamps, sequence numbers, button/axis/trigger encoding, handshake messages,
pad announcement and cadence all match at least one working client. The two
wire differences that did exist, physicality `1/1` vs `0/0` and gamepad index 0
vs 1, were both tested on this console and neither changed the result.

The evidence places the failure after `sendBinary` returns success and before
the console acts on the report. A capture of a working client's handshake and
first non-neutral gamepad packet on this same console is the next step that can
distinguish a session-level gate from a console-side rejection. Further wire
edits without that comparison would be guesses.

## Steam Controller use in current Greenlight (follow-up research)

Yes, others use a Steam Controller with Greenlight, including the 2025/2026 pad.
Greenlight issue #1759 (opened 2026-11-15) reports exactly this hardware: Steam
Controller (2026) via USB puck `28DE:1304`, Steam Input "Gamepad" template,
Greenlight 2.4.2. On macOS the virtual gamepad was invisible to
`navigator.getGamepads()` because Chromium 148 skipped virtual X360 devices when
`XboxUseGameControllerDataFetcherMac` was enabled. The tested accommodation was
to launch Greenlight with `--disable-features=XboxUseGameControllerDataFetcherMac`;
the pad then appeared as `GamePad-1 (STANDARD GAMEPAD Vendor: 045e Product: 028e)`,
mapping `standard`, index 0, and worked in an xHome stream. A follow-up test
confirmed all buttons including Guide and rumble work with that switch on
Bluetooth, while wired Xbox controllers need the opposite setting, so the switch
is a Steam launch option, not a default.
Source: https://github.com/unknownskl/greenlight/issues/1759

The accommodation is therefore the usual Steam Input shape: a virtual X360 pad
(`VID_045E / PID_028E`) exposed as a standard gamepad at index 0. Veyra already
reads that same shape through SDL, and the user selected it in the picker; it
failed identically to the native Triton mode. No additional Steam Controller
mapping step is documented in Greenlight.

Two current Greenlight issues describe our exact symptom, controller detected
locally but no input in a remote stream, on Steam Deck: #1197 and #1632. Both are
open. That is further evidence the symptom is not unique to Veyra's input code.
Sources: https://github.com/unknownskl/greenlight/issues/1197 ,
https://github.com/unknownskl/greenlight/issues/1632

## Implemented Greenlight-shaped input path

Veyra now follows the same path as Greenlight `InputChannel`:

- `WebRtcSession::queueGamepad()` stores the latest frame instead of sending it
  inline; `videoPresented()` queues the video metadata frame instead of sending
  its own packet.
- `WebRtcSession::flushInput()` runs on a 16 ms tick, builds one `inputReport`
  with report type `Metadata | Gamepad` when both are pending, increments the
  sequence once, and sends one packet. This mirrors `InputQueue.sendQueue()`.
- `XboxSessionSource::controller()` queues and flushes; the local 33 ms dedup and
  the 250 Hz UI poll for Xbox are gone (the Xbox timer is 16 ms). Moonlight's
  reader and timer are unchanged.
- The neutral release on stream stop still queues and flushes a neutral frame.

`Protocol.h` gained `inputReport()` for the combined packet; the standalone
`gamepadReport()` and `videoFeedbackReport()` remain unchanged for the existing
tests. `veyra_xbox_tests` passes 138 checks / 0 failures with a new combined
packet assertion. Hardware acceptance is still pending: no console reaction has
been observed with this path yet.
