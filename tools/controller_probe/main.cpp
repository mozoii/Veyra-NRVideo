// Read-only diagnostic for the local SDL gamepad path used by Xbox/Moonlight streaming.
// Lists every pad's identity, reads each one in turn to find a claimed-but-silent
// device, then runs the production ControllerInput and prints the exact frame the
// Xbox input channel would send. It opens pads only to read and closes them again,
// which restores the Steam Controller's desktop mode on SDL close.
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_guid.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include "veyra/Log.h"
#include "veyra/input/ControllerInput.h"
#include "veyra/xbox/Protocol.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

std::string hex16(uint16_t value) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "%04X", value);
    return text;
}

std::string guidOf(SDL_JoystickID id) {
    char text[33]{};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
    return text;
}

struct Reading {
    uint32_t buttons = 0;
    int16_t axes[6]{};

    bool operator==(const Reading&) const = default;
};

Reading readPad(SDL_Gamepad* pad) {
    struct Button { SDL_GamepadButton from; uint32_t bit; };
    static constexpr Button buttons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, 1u << 0}, {SDL_GAMEPAD_BUTTON_EAST, 1u << 1},
        {SDL_GAMEPAD_BUTTON_WEST, 1u << 2}, {SDL_GAMEPAD_BUTTON_NORTH, 1u << 3},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 1u << 4}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 1u << 5},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, 1u << 6}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 1u << 7},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 1u << 8}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 1u << 9},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, 1u << 10}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 1u << 11},
        {SDL_GAMEPAD_BUTTON_START, 1u << 12}, {SDL_GAMEPAD_BUTTON_BACK, 1u << 13},
        {SDL_GAMEPAD_BUTTON_GUIDE, 1u << 14}, {SDL_GAMEPAD_BUTTON_TOUCHPAD, 1u << 15},
    };
    static constexpr SDL_GamepadAxis axes[] = {
        SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
        SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    };
    Reading r;
    for (const auto& b : buttons) if (SDL_GetGamepadButton(pad, b.from)) r.buttons |= b.bit;
    for (size_t i = 0; i < std::size(axes); ++i) r.axes[i] = SDL_GetGamepadAxis(pad, axes[i]);
    return r;
}

void printFrame(const veyra::input::ControllerState& state) {
    const auto frame = veyra::xbox::gamepadFromController(state);
    const auto bytes = veyra::xbox::gamepadReport(0, 0.0, {frame});
    std::printf("  wire: buttons=0x%04X axes=(%d,%d,%d,%d) triggers=(%u,%u) bytes=%zu:",
                frame.buttons, frame.leftX, frame.leftY, frame.rightX, frame.rightY,
                frame.leftTrigger, frame.rightTrigger, bytes.size());
    for (unsigned char b : bytes) std::printf(" %02X", b);
    std::printf("\n");
}

void logSdlInput(void*, int category, SDL_LogPriority priority, const char* message) {
    if (category == SDL_LOG_CATEGORY_INPUT)
        std::printf("  sdl-input[p=%d] %s\n", int(priority), message);
}
} // namespace

int main(int argc, char** argv) {
    int seconds = 5;
    bool sdlLog = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
            if (seconds < 1 || seconds > 3600) {
                std::fprintf(stderr, "--seconds must be between 1 and 3600\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--hint") == 0 && i + 1 < argc) {
            const std::string hint = argv[++i];
            const auto eq = hint.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::fprintf(stderr, "--hint expects NAME=VALUE\n");
                return 2;
            }
            SDL_SetHint(hint.substr(0, eq).c_str(), hint.substr(eq + 1).c_str());
            std::printf("hint: %s\n", hint.c_str());
        } else if (std::strcmp(argv[i], "--sdl-log") == 0) {
            sdlLog = true;
        } else {
            std::fprintf(stderr, "usage: veyra_controller_probe [--seconds N] [--hint NAME=VALUE]... [--sdl-log]\n");
            return 2;
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (sdlLog) {
        SDL_SetLogOutputFunction(logSdlInput, nullptr);
        SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_DEBUG);
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_InitSubSystem failed: %s\n", SDL_GetError());
        return 1;
    }
    veyra::log::info("controller-probe", "starting");

    // HID hotplug can register after init returns; give it up to a second.
    int joystickCount = 0;
    for (int attempt = 0; attempt < 10 && !joystickCount; ++attempt) {
        SDL_Delay(100);
        SDL_PumpEvents();
        SDL_UpdateJoysticks();
        SDL_free(SDL_GetJoysticks(&joystickCount));
    }
    auto* joystickIds = SDL_GetJoysticks(&joystickCount);
    std::printf("joysticks=%d\n", joystickCount);
    for (int i = 0; i < joystickCount && joystickIds; ++i) {
        const char* name = SDL_GetJoystickNameForID(joystickIds[i]);
        const char* path = SDL_GetJoystickPathForID(joystickIds[i]);
        std::printf("joystick[%d] name=%s vid=0x%s pid=0x%s type=%d gamepad=%s path=%s\n",
                    i, name ? name : "?", hex16(SDL_GetJoystickVendorForID(joystickIds[i])).c_str(),
                    hex16(SDL_GetJoystickProductForID(joystickIds[i])).c_str(),
                    int(SDL_GetJoystickTypeForID(joystickIds[i])),
                    SDL_IsGamepad(joystickIds[i]) ? "yes" : "no", path ? path : "?");
    }
    SDL_free(joystickIds);

    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    std::printf("enumerated=%d\n", count);
    std::vector<SDL_JoystickID> pads;
    for (int i = 0; i < count && ids; ++i) {
        const SDL_JoystickID id = ids[i];
        pads.push_back(id);
        const char* name = SDL_GetGamepadNameForID(id);
        const char* path = SDL_GetGamepadPathForID(id);
        std::printf("device[%d] id=%u name=%s vid=0x%s pid=0x%s path=%s type=%d guid=%s\n",
                    i, unsigned(id), name ? name : "?", hex16(SDL_GetJoystickVendorForID(id)).c_str(),
                    hex16(SDL_GetJoystickProductForID(id)).c_str(), path ? path : "?",
                    int(SDL_GetGamepadTypeForID(id)), guidOf(id).c_str());
    }
    SDL_free(ids);

    // A driver can own a device and still deliver no reports. Read each pad for
    // two seconds so a silent device is visible before the production path runs.
    for (size_t i = 0; i < pads.size(); ++i) {
        auto* pad = SDL_OpenGamepad(pads[i]);
        if (!pad) {
            std::printf("live[%zu] open FAILED: %s\n", i, SDL_GetError());
            continue;
        }
        const Reading first = readPad(pad);
        bool changed = false;
        Reading last = first;
        const auto deadline = Clock::now() + std::chrono::milliseconds(2000);
        while (Clock::now() < deadline) {
            SDL_PumpEvents();
            SDL_UpdateGamepads();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {}
            last = readPad(pad);
            changed = changed || !(last == first);
            SDL_Delay(10);
        }
        std::printf("live[%zu] guid=%s opened=yes changed=%s buttons=0x%08X axes=(%d,%d,%d,%d,%d,%d)\n",
                    i, guidOf(pads[i]).c_str(), changed ? "yes" : "no", last.buttons,
                    last.axes[0], last.axes[1], last.axes[2], last.axes[3], last.axes[4], last.axes[5]);
        SDL_CloseGamepad(pad);
    }

    // Now the exact reader the app uses, including its 1 s re-enumeration and
    // first-pad/Auto acquisition. focused=true is the in-app gate's open state.
    veyra::input::ControllerInput reader;
    if (!reader.start()) {
        std::fprintf(stderr, "ControllerInput::start failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        return 1;
    }
    veyra::input::ControllerState previous;
    unsigned changes = 0;
    std::printf("PRESS BUTTONS NOW: reading production ControllerInput for %d seconds\n", seconds);
    std::fflush(stdout);
    const auto deadline = Clock::now() + std::chrono::seconds(seconds);
    while (Clock::now() < deadline) {
        const auto state = reader.poll(true);
        if (!(state == previous)) {
            previous = state;
            const auto caps = reader.capabilities();
            std::printf("reader[%u] pad=%s guid=%s inputActive=%s buttons=0x%08X sticks=(%d,%d,%d,%d) triggers=(%u,%u)\n",
                        ++changes, caps.name.c_str(), caps.guid.c_str(), state.inputActive ? "yes" : "no",
                        state.buttons, state.leftX, state.leftY, state.rightX, state.rightY, state.l2, state.r2);
            printFrame(state);
        }
        SDL_Delay(4);
    }
    const auto caps = reader.capabilities();
    std::printf("summary: opened=%s guid=%s path=%s changes=%u preferred=%s\n",
                caps.connected ? "yes" : "no", caps.guid.c_str(), caps.path.c_str(), changes,
                reader.preferredDeviceGuid().c_str());
    reader.stop();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    veyra::log::info("controller-probe", "finished");
    return 0;
}
