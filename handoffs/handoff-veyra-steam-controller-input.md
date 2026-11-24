# Handoff: Steam Controller sends no input to the Xbox console

## Summary

The user's Steam Controller produces no input on the console during Veyra Xbox
streaming, in both of its modes: registered as a Steam Controller, and converted
to an XInput/X360 device. The conversion is done by a third-party app,
**SteamlessController**, not by Veyra. Veyra only reads local gamepads through
SDL3 and forwards them over the WebRTC input channel.

Research (this session, read-only) traced the full input path and the SDL 3.4.14
driver behavior, and inspected the user's live machine. The leading, code-grounded
cause is **device acquisition**: `ControllerInput::poll()` opens the *first*
gamepad SDL enumerates, with no preference and no user selection, while the user's
machine exposes several pads at once. Veyra can therefore read an idle or silent
device while the user presses buttons on another, and nothing in the UI reveals
which pad it opened. This has **not** been reproduced on hardware; the plan is
diagnosis-first (a probe), then a targeted fix.

User decisions (answered on a form this session):
- Scope: **controller picker + connected-pad status** in the Xbox dialog (not the
  minimal name-only option).
- Steam is **closed** when testing the Steam Controller (rules out Steam holding
  the pad exclusively during the test).
- A regular Xbox controller has **not been tried** in Veyra; the plan must test one
  to isolate transport vs. device selection.

## Goal

Make the Steam Controller (and any pad the user picks) reliably send input to the
console during Xbox streaming, and make the active controller visible and
selectable so a wrong/idle device can never silently win. Keep Moonlight (PC
streaming) working on the shared reader. Do not change the wire protocol, the
button mapping, the 250 Hz poll / 33 ms heartbeat, or the focus gate without
evidence.

## Environment facts (verified this session)

Machine: Windows 11 x64, MSVC 14.51 (VS "18" Community), Qt 6.8.3, CMake + Ninja.
Configured build tree: `build-local/x64-release` (Ninja, `CMAKE_PREFIX_PATH` =
`C:\veyra-deps\qt\6.8.3\msvc2022_64;C:\veyra-deps\xbox-installed\x64-windows-static;C:\veyra-deps\vcpkg\installed\x64-windows-static`,
`SDL3_DIR=C:/veyra-deps/xbox-installed/x64-windows-static/share/sdl3`,
`VEYRA_ENABLE_XBOX=ON`, `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON`,
`VEYRA_FFMPEG_ROOT=C:/veyra-deps/vcpkg/installed/x64-windows-static`).
`veyra_qml_ui.exe` and `veyra_xbox_tests.exe` already exist there.

SDL3 is **3.4.14 EXACT** (`CMakeLists.txt:1732`), static, from
`C:\veyra-deps\xbox-installed\x64-windows-static`. Source for reference:
`C:\veyra-deps\vcpkg\buildtrees\sdl3\src\ase-3.4.14-af942a6848.clean\src`.
Build config: `SDL_JOYSTICK_HIDAPI`, `SDL_JOYSTICK_RAWINPUT`, `SDL_JOYSTICK_WGI`,
`SDL_JOYSTICK_XINPUT`, `SDL_JOYSTICK_DINPUT` all compiled in; `SDL_HIDAPI_LIBUSB=OFF`
(Windows hidapi backend). Driver registration order
(`SDL_joystick.c:51`): **HIDAPI first**, then RAWINPUT, then WINDOWS
(DINPUT+XInput), then WGI.

Live devices on the user's machine (`Get-PnpDevice`):
- `USB\VID_28DE&PID_1304` — Valve **Proteus dongle** for the **new Steam Controller
  (2025, "Triton")**, not the 2015 pad. HID interfaces MI_00..MI_06 (serial, mouse,
  keyboard, vendor-defined). SDL's Triton driver claims MI_02..MI_05.
- `USB\VID_045E&PID_028E\01` + `HID\VID_045E&PID_028E&IG_00` — **ViGEm virtual Xbox
  360 pad** created by SteamlessController (X360 mode). `IG_` marks it XInput-capable.
- `ROOT\VIBESHINEVIRTUALGAMEPAD\0000` — Vibeshine virtual gamepad (another app).
- `USB\VID_9886&PID_0042` — an Xbox-compatible controller (ASTRO vendor id; a real
  or emulated Xbox pad).
- `SWD\SYNTHETICGAMEPADENUM\SYNTHETICGAMEPAD0` — Xbox synthetic gamepad bus.
- `ROOT\SYSTEM\0001` — Nefarius **ViGEmBus** driver installed.

Processes: `SteamlessController.exe` running from
`C:\Program Files\SteamlessController\` (also `SteamlessDeviceCycle.exe`,
`ViGEmBusProbe.exe`). Its logs at `%LOCALAPPDATA%\SteamlessController\`
(`cycle.log`, `cycle.status`) show it **cycling the dongle's HID interfaces**
(`mi_02&col03`, `mi_06`) to flip between Steam mode and X360 mode. `cycle.status`
last read `kind=2`. Steam itself was closed during the reported test.

## The input path (code-grounded)

1. UI timer, 4 ms (`Qt::PreciseTimer`), on the Win32 UI thread:
   `src/ui/bridge/Streaming.cpp:83-91` (Xbox) and `:184-192` (Moonlight).
   `focused = QGuiApplication::focusWindow() != nullptr`.
2. `m.controller.poll(focused)` → `src/input/ControllerInput.cpp:54`.
3. `m.engine.xboxController(state)` → `src/engine/EngineController.cpp:191` →
   `src/source/XboxSessionSource.cpp:670` `controller()`.
4. `xbox::gamepadFromController(state)` (`include/veyra/xbox/Protocol.h:102`) maps
   semantic buttons to Xbox report bits, flips stick Y, scales triggers 0..255 →
   0..65535.
5. `p.rtc.sendGamepad(frame)` (`src/xbox/WebRtcSession.cpp:360`) →
   `gamepadReport()` (`Protocol.h:78`) → binary on the `input` data channel.
   Gated by `gate_->open && handshakeDone_`.
6. Send policy (`XboxSessionSource.cpp:675-681`): only while `p.streaming` and
   `desc.gamepad`; dedup identical frames within 33 ms (heartbeat). On stop,
   `XboxSessionSource.cpp:750` sends a neutral frame to release the console pad.

Acquisition (`ControllerInput.cpp:62-70`): when no pad is open, enumerate
`SDL_GetGamepads()` and `SDL_OpenGamepad()` the **first** id that is not already
opened and (if `preferredDevice_` set) matches it. `preferredDevice_` is always 0
in practice: `acquirePad()` (`QmlPlayerBridge_p.h:388`) calls `controller.start()`
with no argument. Re-enumeration is throttled to 1 s (`enumerateAt_`). A pad is
closed only when `SDL_GamepadConnected()` returns false (`:60`). There is **no
device picker, no preferred-device wiring, and no UI showing the opened pad**.
`capabilities()` (`:207`) exists but is unused by the UI.

Shared reader: one `input::ControllerInput controller` in `Impl`
(`QmlPlayerBridge_p.h:385`), ref-counted by `padUsers` (`PadMoonlight=1`,
`PadXbox=2`); `acquirePad`/`releasePad` (`:388-396`). The last release calls
`controller.stop()`. Moonlight uses the same reader and `moonlight::mapPad`
(`include/veyra/moonlight/InputMap.h:34`), so acquisition changes affect both.

Focus gate: the native video window is `WS_CHILD` with `WM_NCHITTEST → HTTRANSPARENT`
(`apps/veyra-qml/main.cpp:163-198`), so clicks pass to the QML window and Qt keeps
focus; `focused` should be true while streaming. Verify in the probe, do not assume.

## SDL driver behavior (verified in 3.4.14 source)

- **RAWINPUT defaults OFF** (`SDL_rawinputjoystick.c:1025`,
  `SDL_GetHintBoolean(SDL_HINT_JOYSTICK_RAWINPUT, false)`) and nothing in SDL or
  Veyra enables it. So the RAWINPUT path (and its Steam-virtual-gamepad slot logic)
  is inactive; XInput devices come through the WINDOWS/XInput driver.
- **HIDAPI is highest priority** and claims:
  - The **Steam Controller / Proteus dongle** via the Triton driver
    (`SDL_hidapi_steam_triton.c`): `IsSupportedDevice` accepts Proteus/Nereid dongle
    interfaces 2..5 (`:446-450`). Enabled by default on Windows
    (`SDL_HINT_JOYSTICK_HIDAPI_STEAM` defaults to `SDL_HINT_JOYSTICK_HIDAPI`, true).
    The driver **disables lizard mode** while the pad is open and connected
    (`DisableSteamTritonLizardMode`, re-sent every 3 s, `:500-502`); on close the
    hardware watchdog re-enables lizard mode (`:678-681`). Opening the pad therefore
    takes it out of desktop mouse/keyboard mode — a real side effect to respect.
  - The **ViGEm virtual X360 pad** (`VID_045E&PID_028E`) via the Xbox360 HIDAPI
    driver (`SDL_hidapi_xbox360.c:129`, `type == SDL_GAMEPAD_TYPE_XBOX360`), reading
    HID state packets (`:376-386`). Whether ViGEm's emulation actually delivers
    those HID reports (vs. only XInput) is **unverified** and is a prime suspect for
    the X360-mode failure: HIDAPI claims the device, but if it yields no reports the
    pad reads idle while XInput would have worked.
- **Steam Virtual Gamepad** (`VID_28DE&PID_11FF`) is ignored unless
  `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD` is set
  (`SDL_gamepad.c:3289-3303`). Not the user's current path (SteamlessController uses
  ViGEm X360, not Steam's virtual gamepad), but relevant if Steam Input is ever used.
- XInput driver (`SDL_xinputjoystick.c`) is enabled by default
  (`SDL_HINT_XINPUT_ENABLED`, true), names pads "XInput Controller #N", path
  `XInput#<userid>`, and defers to RAWINPUT when RAWINPUT is enabled (`:152-163`).
  It reads guide via `XINPUT_GAMEPAD_GUIDE` and inverts stick Y (`:339-343`).

## Root-cause hypotheses (ranked; to be confirmed by the probe)

1. **Wrong device opened (leading).** With the dongle, ViGEm 360, Vibeshine and an
   Xbox pad all present, first-device-wins can open a pad the user is not touching.
   Fits "no input in both modes" and the multi-pad environment. Fix: picker +
   preferred-device + status.
2. **HIDAPI claims the ViGEm X360 pad but reads no reports.** In X360 mode the pad
   may only answer XInput, while SDL's HIDAPI Xbox360 driver owns it and gets
   silence. Fix direction: route XInput-capable virtual pads to the XInput driver
   (e.g. disable the HIDAPI Xbox360 driver via `SDL_HINT_JOYSTICK_HIDAPI_XBOX_360=0`,
   or enable RAWINPUT) — **only after the probe shows which driver owns the pad and
   whether it reports**. Do not flip hints blind.
3. **Steam Controller dongle not opened / not reporting.** Triton driver may fail to
   open the dongle interface, or SteamlessController's interface cycling may leave
   the pad in a state SDL cannot read. The probe's per-device open + live read will
   show this.
4. **Focus gate false during streaming.** Less likely given the click-through video
   window, but the probe must log `focused` and `inputActive` to rule it out.
5. **Transport/handshake gate.** `sendGamepad` drops frames unless
   `handshakeDone_ && gate_->open`; `controller()` drops unless `streaming &&
   desc.gamepad`. The probe + a log line at the send site will confirm frames leave.

The wire protocol itself (`gamepadReport`, button map, physicality bytes) is covered
by `tests/xbox/XboxTests.cpp:68-108` and matches the Greenlight reference; it is not
the suspect and must not be changed without new evidence.

## Ordered steps

### Phase 0 — Reproduce and isolate (no source changes)
1. Build the current tree (`veyra_qml_ui`, `veyra_xbox_tests`) and run a real Xbox
   stream with a **regular Xbox controller** (the user has not tried one). Record
   whether input reaches the console. This splits transport bugs from device bugs.
2. Run the new probe (Phase 1) in each SteamlessController mode (Steam mode, X360
   mode) with Steam closed, and with the regular pad, capturing its output.
3. Read `%LOCALAPPDATA%\Veyra\logs\veyra-qml.log` (or `VEYRA_LOG_FILE`) for the
   `input` "device opened" line and any `xbox-input` warnings during a failed run.

### Phase 1 — Diagnostic probe (new tool, the evidence gate)
4. Add `tools/controller_probe/main.cpp` + CMake target `veyra_controller_probe`
   (mirror `veyra_export_probe` wiring at `CMakeLists.txt:1713-1716`; link SDL3 the
   same way the bridge does — `$<TARGET_LINKER_FILE:SDL3::SDL3-static>` +
   `SDL3::Headers`, see `CMakeLists.txt:1741-1744` — and `veyra_base` for logging).
   The probe must, for a configurable duration:
   - `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` with the same hint Veyra sets
     (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1`); optionally accept `--hint
     NAME=VALUE` repeats to test driver hypotheses (e.g.
     `SDL_JOYSTICK_HIDAPI_XBOX_360=0`, `SDL_JOYSTICK_RAWINPUT=1`) without rebuilding.
   - Enumerate `SDL_GetGamepads()`; for each id print name
     (`SDL_GetGamepadNameForID`), VID/PID (`SDL_GetJoystickVendorForID` /
     `...ProductForID`), path (`SDL_GetGamepadPathForID`), type
     (`SDL_GetGamepadTypeForID`), GUID (`SDL_GetGamepadGUIDForID` +
     `SDL_GUIDToString`).
   - Open each pad in turn (`SDL_OpenGamepad`), poll ~2 s, and print whether any
     button/axis/trigger changes (this reveals a claimed-but-silent device).
   - Then run the production `ControllerInput` (start/poll) and print which pad it
     opens and the live `ControllerState` (buttons, sticks, triggers, `inputActive`)
     plus the resulting `xbox::gamepadFromController` frame, so the exact bytes that
     would go on the wire are visible.
   - Print `focused`-equivalent context is N/A headless; instead document that the
     in-app log (Phase 2 step 7) carries the focus value.
   - Exit codes: 0 ran, 2 bad args. Keep it read-only: it opens pads but must restore
     lizard mode by closing them (SDL does this on close).
5. Run the probe in both modes and with the regular pad. **Decision gate:** the
   probe output decides whether Phase 3 needs a driver hint (hypothesis 2/3) or only
   the picker (hypothesis 1). Record the output under the task dir (see Artifacts).

### Phase 2 — Device acquisition + identity in the shared reader
6. `include/veyra/input/ControllerInput.h` / `src/input/ControllerInput.cpp`:
   - Add a stable device key. Use the SDL GUID string (`SDL_GetGamepadGUIDForID` +
     `SDL_GUIDToString`) as the persistent identity (instance ids are not stable
     across replug). Keep `preferredDevice_` (instance id) for the live match, and
     add `preferredGuid_` (string) for persistence.
   - Change acquisition (`:62-70`) to: prefer the pad whose GUID matches
     `preferredGuid_` (resolve to its current instance id each enumeration); if none
     matches, fall back to the first pad (current behavior) so a single-pad user is
     unaffected. Do not silently switch away from a connected preferred pad.
   - Add `setPreferredDevice(const std::string& guid)` (re-resolves on next poll;
     if the current pad differs, close it and re-enumerate) and a
     `std::vector<Device> devices()` snapshot (guid, name, vid, pid, connected) for
     the picker. Extend `Capabilities` with the opened pad's guid/path if useful.
   - Keep the 1 s enumeration throttle, the focus gate, the trigger filter, sensors,
     rumble and the neutral-release behavior unchanged.
7. `src/ui/bridge/Streaming.cpp` (Xbox timer) and `src/ui/QmlPlayerBridge.cpp`:
   - Log once per opened pad: guid, name, vid/pid, and the `focused` value at poll
     time (throttled, e.g. on change), under the existing `input` / `xbox-input`
     channels. This is the in-app evidence for hypothesis 4.
   - Wire the preferred device from the model (Phase 3) into `controller` before
     `acquirePad`, and on picker change.

### Phase 3 — Picker + connected-pad status (user-chosen scope)
8. `include/veyra/ui/XboxModel.h` / `src/ui/XboxModel.cpp`:
   - Persist `controllerGuid` next to `gamepad`/`softwareDecode`/`resolutionMode` in
     `settings.json` (`loadSettings`/`saveSettings`, `:69-89`); default empty = auto.
   - Expose in `state()` (`:91-98`): `controllerGuid`, `controllers` (list of
     `{guid, name}` from the reader snapshot), and `controllerName` (opened pad, or
     empty). Accept `controllerGuid` in `set()` (`:220` area). Pass the guid into
     `Launch.desc` (add a field to `XboxConnectDesc` in
     `include/veyra/source/XboxSessionSource.h`) or hand it to the bridge directly.
   - The model needs access to the reader's device list. Cleanest seam: the bridge
     owns `controller`, so expose the list/name through the bridge
     (`QmlPlayerBridge`) and have the dialog read `veyra.controller*`, while the
     model only persists the chosen guid. Avoid giving `XboxModel` a second SDL
     reader. Decide the exact seam in implementation; keep one SDL owner.
9. `include/veyra/ui/QmlPlayerBridge.h` / `src/ui/QmlPlayerBridge.cpp` /
   `QmlPlayerBridge_p.h`:
   - Add `Q_PROPERTY(QVariantList controllers ...)`, `Q_PROPERTY(QString
     controllerName ...)`, and a `Q_INVOKABLE setController(const QString& guid)`
     (or route through `xbox.set("controllerGuid", ...)`), NOTIFY on a suitable
     signal. Refresh the list when the Xbox dialog opens and on device change.
   - Apply the persisted guid to `controller.setPreferredDevice()` at acquire and on
     change; if the chosen pad is absent, fall back to auto and surface that in
     `controllerName`/status.
10. `qml/Veyra/XboxSourceDialog.qml`:
    - Under the existing Controller row (`:136-140`), add a `VSelect` of controllers
      (Auto + each `{guid,name}`), bound to the persisted choice, and a status line
      showing the opened pad name or "No controller detected". Follow the existing
      `VRow`/`VSelect`/`VSwitch` patterns and `objectName` conventions
      (`xbox-controller`, `xbox-controller-status`).
    - Keep the `xbox-gamepad` enable switch as the master gate.
11. i18n: new English source strings go through `scripts/i18n/extract.py` into
    `i18n/catalog.json`; fill all nine translations (zh, zh-TW, ja, ko, es, fr, de,
    pt-BR, ru). Run `python -B scripts/i18n/extract.py --check` (0 missing/stale/
    placeholder problems). Language names stay in their own script. Do not translate
    vendor/device names returned by SDL.

### Phase 4 — Driver routing (only if the probe proves hypothesis 2 or 3)
12. If the probe shows HIDAPI owns the ViGEm X360 pad but reads no reports while
    XInput would: set `SDL_HINT_JOYSTICK_HIDAPI_XBOX_360=0` before
    `SDL_InitSubSystem` in `ControllerInput::start()` so the XInput driver handles
    X360 pads. Verify this does not regress a real wired Xbox 360 pad (test one if
    available) and document the trade-off. Alternatively enable RAWINPUT
    (`SDL_HINT_JOYSTICK_RAWINPUT=1`) — but that changes guide/trigger handling and
    correlates XInput; prefer the narrower Xbox360-driver disable unless evidence
    favors RAWINPUT.
13. If the probe shows the Triton dongle is not opened or not reporting: capture the
    SDL input debug log (`SDL_SetLogOutputFunction` + `SDL_SetLogPriorities` for
    `SDL_LOG_CATEGORY_INPUT`) in the probe to see driver claim/open errors, then fix
    narrowly (hint or open order). Do not patch SDL source; it is a pinned dependency.
14. Any hint added in `start()` must be set before `SDL_InitSubSystem` and must not
    disturb the existing `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` setting.

### Phase 5 — Validation
15. Offline: `veyra_xbox_tests` (protocol unchanged), `veyra_ui_i18n_tests`,
    `veyra_ui_contract_tests`, plus a new unit test for the acquisition preference
    logic if it is factored into a testable pure function (guid match / fallback).
    Add the test under `tests/` and wire it like the existing unit targets.
16. i18n: `python -B scripts/i18n/extract.py --check`; Unicode scan for non-English
    first-party prose (`scripts/i18n/scan-cjk.py`).
17. Hardware (the real acceptance): with the probe and the new UI, confirm in **both**
    SteamlessController modes and with a regular Xbox pad that:
    - the picker lists the pads and the opened pad name matches the one in use;
    - buttons, both sticks, both triggers, D-pad, Menu/View and the Steam/Guide button
      reach the console (navigate the Xbox guide, move in a game);
    - selecting a different pad in the picker switches the active device;
    - unplugging the active pad shows "No controller" and replug re-acquires it;
    - rumble from the console reaches the pad (feedback path unchanged);
    - Moonlight PC streaming still forwards the pad (shared reader regression).
    Record actual results, not assumptions. Submitted FPS is not latency; do not
    claim latency numbers.

## Files to modify

- `src/input/ControllerInput.cpp`, `include/veyra/input/ControllerInput.h` —
  preferred-device acquisition, device snapshot, identity, logging.
- `src/ui/bridge/Streaming.cpp` — wire preference, log opened pad + focus.
- `src/ui/QmlPlayerBridge.cpp`, `include/veyra/ui/QmlPlayerBridge.h`,
  `src/ui/QmlPlayerBridge_p.h` — controller list/name properties, setController,
  apply preference at acquire.
- `src/ui/XboxModel.cpp`, `include/veyra/ui/XboxModel.h` — persist `controllerGuid`,
  expose in state/set.
- `include/veyra/source/XboxSessionSource.h` — optional `controllerGuid` in
  `XboxConnectDesc` if the desc carries the choice.
- `qml/Veyra/XboxSourceDialog.qml` — picker + status row.
- `i18n/catalog.json` — new strings + nine translations.
- `CMakeLists.txt` — `veyra_controller_probe` target; new unit-test target if added.
- New: `tools/controller_probe/main.cpp`; new unit test under `tests/`.
- Conditional (Phase 4): `ControllerInput::start()` hints only.
- Docs: update `docs/CURRENT_STATUS.md` with the verified result once hardware-tested.

Do not modify: `include/veyra/xbox/Protocol.h` mapping/report layout,
`WebRtcSession` handshake/send gating, the poll/heartbeat cadence, the focus gate,
`TriggerFilter`, or SDL source. Preserve the neutral-release on stop
(`XboxSessionSource.cpp:750`).

## Build / run commands (this machine)

Configure/build reuse the existing tree; do not reconfigure from scratch unless needed.
```
cmake --build build-local/x64-release --target veyra_qml_ui veyra_xbox_tests veyra_controller_probe --parallel 2
```
If a fresh configure is required, mirror the cached vars (Ninja, the three-prefix
`CMAKE_PREFIX_PATH`, `VEYRA_ENABLE_XBOX=ON`, FFmpeg root, DLSSNR root). The tracked
`scripts/build.ps1 -Root . -Preset x64-release` writes under `out/build/x64-release`
and resolves MSVC via vswhere; either tree is fine, keep artifacts consistent.

Run the QML app with an isolated profile and log so the user's real settings are
untouched:
```
veyra_qml_ui.exe --data-dir <task>\tmp\profile --exit-after 60000
```
with `VEYRA_LOG_FILE=<task>\logs\veyra-qml.log` and, for Xbox account isolation,
`VEYRA_XBOX_DATA=<task>\tmp\xbox` (see `src/xbox/Account.cpp:105-113`). Note: an
isolated Xbox profile needs its own sign-in; for the hardware test the user may
prefer their real profile — ask before touching real settings, and never overwrite
uncommitted work or stop the user's processes (AGENTS.md).

Probe:
```
veyra_controller_probe.exe --seconds 5
veyra_controller_probe.exe --seconds 5 --hint SDL_JOYSTICK_HIDAPI_XBOX_360=0
```

## Artifacts and preservation (AGENTS.md)

- This checkout is `C:\Work\Projects\Veyra-NRVideo`. The AGENTS.md task-dir convention
  (`E:/项目/Veyra/{build,tests,logs,...}`) does not exist on this machine
  (`E:\项目\Veyra` absent). Use the gitignored `build-local/` tree for builds and put
  probe output / logs / reports under `build-local/verify/` (already present) or a
  new gitignored task subdir. Do not create R&D artifacts on Desktop/Downloads/C
  system Temp. Confirm the artifact location with the user if unsure.
- Do not stop SteamlessController, ViGEmBus, Vibeshine or Steam, and do not change
  drivers. The probe only opens/closes SDL pads.
- Tests use private data-dir/log/cache; TEMP/TMP only for child processes.
- Single agent, build concurrency <= 2, <= 900 s per build, each test <= 300 s.
- No commits/pushes/releases unless the user asks. New work on a `feature/` branch.

## Risks

- **Unverified root cause.** The picker fixes hypothesis 1 but not 2/3. The probe is
  the gate; do not ship a driver-hint change without probe evidence.
- **Shared reader regression.** Moonlight uses the same `ControllerInput`; any
  acquisition change must be re-tested on PC streaming.
- **HIDAPI vs XInput trade-off.** Disabling the HIDAPI Xbox360 driver could regress a
  real wired 360 pad's guide/LED handling; test or document. RAWINPUT changes
  guide/trigger semantics; prefer the narrower change.
- **Lizard mode side effect.** Opening the Steam Controller via HIDAPI disables its
  desktop mouse/keyboard mode while open; closing restores it. The picker must not
  leave the pad claimed when the user is not streaming (the reader already stops on
  last release — keep that).
- **Device identity stability.** Instance ids change across replug; persist GUID, not
  instance id. Two identical pads share a GUID — handle by falling back to instance
  order and noting the limitation.
- **Focus gate.** If the probe/log shows `focused=false` while streaming, that is a
  separate bug (video window focus); fix narrowly, do not loosen the gate globally.
- **i18n completeness.** Nine translations must be filled; `--check` must pass.
- **No hardware in CI.** `veyra_xbox_tests` and the QML tests need this Windows box;
  the console test needs the user's physical console and pad. State what was and was
  not verified.

## Acceptance criteria

1. Probe exists, runs headless, and prints per-device identity + live state + the
   would-be wire frame; its output is recorded for both modes and a regular pad.
2. Xbox dialog shows a controller picker (Auto + detected pads) and the opened pad
   name / "No controller detected"; the choice persists across restarts.
3. With the correct pad selected (or auto when only one is present), buttons, sticks,
   triggers, D-pad, Menu/View and Guide/Steam reach the console in **both**
   SteamlessController modes; verified on hardware, recorded.
4. A regular Xbox controller also works (transport regression check).
5. Switching pads in the picker changes the active device; unplug/replug is handled.
6. Moonlight PC streaming still forwards the pad.
7. `veyra_xbox_tests`, `veyra_ui_i18n_tests`, `veyra_ui_contract_tests` pass;
   `extract.py --check` clean; new acquisition unit test (if added) passes.
8. No change to the wire protocol, cadence, focus gate or neutral-release; no SDL
   source patch; no driver/process changes on the user's machine.
9. `docs/CURRENT_STATUS.md` updated with the verified outcome and any remaining
   unverified scope. Do not claim success without the hardware result.

## Suggested skills for the Build agent

- `diagnosing-bugs` — the probe-first loop and hypothesis gating match this skill;
  use it to run Phase 0/1 and interpret the evidence before changing behavior.
- `tdd` — for the acquisition-preference unit test and any new pure logic
  (red-green-refactor), and to keep the protocol tests intact.
- `codebase-design` — if the controller-list seam between `QmlPlayerBridge`,
  `XboxModel` and `ControllerInput` needs a cleaner interface (one SDL owner).
- `writing-for-agents` — only if updating AGENTS.md / agent docs (not expected here).

Do not load unrelated skills. The `research` skill is unnecessary: the SDL facts are
already gathered and cited above; re-verify against the pinned source if a hint
decision depends on it.

## References (paths, not duplicated content)

- Input reader: `src/input/ControllerInput.cpp`, `include/veyra/input/ControllerInput.h`,
  `include/veyra/input/ControllerTypes.h`, `include/veyra/input/TriggerFilter.h`.
- UI wiring: `src/ui/bridge/Streaming.cpp`, `src/ui/QmlPlayerBridge_p.h` (padUsers),
  `src/ui/XboxModel.cpp`, `qml/Veyra/XboxSourceDialog.qml`.
- Transport: `src/source/XboxSessionSource.cpp` (`controller`, stop release),
  `src/xbox/WebRtcSession.cpp` (`sendGamepad`, handshake), `include/veyra/xbox/Protocol.h`.
- Tests: `tests/xbox/XboxTests.cpp` (protocol), `tests/moonlight/ProtocolTests.cpp`
  (`mapPad`), `cmake/VeyraXbox.cmake` (test target).
- SDL pinned source: `C:\veyra-deps\vcpkg\buildtrees\sdl3\src\ase-3.4.14-af942a6848.clean\src\joystick\`
  (`SDL_joystick.c` driver order; `hidapi/SDL_hidapi_steam_triton.c`,
  `hidapi/SDL_hidapi_xbox360.c`, `windows/SDL_rawinputjoystick.c`,
  `windows/SDL_xinputjoystick.c`, `SDL_gamepad.c` ignore rules).
- Fork origin / contracts: `handoffs/handoff-veyra-xbox-1440p-english.md`,
  `docs/CURRENT_STATUS.md`, `docs/DEVELOPMENT.md`, `AGENTS.md`.
- SteamlessController logs: `%LOCALAPPDATA%\SteamlessController\cycle.log`,
  `cycle.status` (read-only evidence; do not modify the app).