#pragma once
#include "ControllerTypes.h"
#include "TriggerFilter.h"
#include <deque>
#include <optional>
#include <string>
#include <vector>
struct SDL_Gamepad;
struct SDL_Joystick;
struct SDL_AudioStream;
namespace veyra::input {
// A pad's persistent identity. SDL instance ids change on replug; the GUID
// string is what the picker stores in settings.
struct GamepadDevice {
    std::string guid, name, path;
    uint16_t vid=0, pid=0;
    bool connected=true;
};

// Selection rule, kept pure so the picker's preference can be tested without
// SDL: match the preferred GUID, otherwise fall back to the first device.
inline int preferredDeviceIndex(const std::vector<std::string>& guids, const std::string& preferred) {
    if (!preferred.empty())
        for (size_t i=0;i<guids.size();++i)
            if (guids[i]==preferred) return int(i);
    return guids.empty() ? -1 : 0;
}

// SDL calls stay on the Win32 UI owner, independently of GPU processing.
class ControllerInput {
public:
    ~ControllerInput(){stop();}
    bool start(uint32_t preferredDevice=0);
    void stop();
    ControllerState poll(bool focused);
    void feedback(ControllerFeedback,bool focused);
    void releaseEffects();
    bool calibrate();
    bool connected()const{return gamepad_!=nullptr;}
    // Empty guid means Auto (first enumerated pad). Changing the preference
    // closes a different open pad so the next poll selects the chosen device.
    void setPreferredDevice(const std::string& guid);
    const std::string& preferredDeviceGuid()const{return preferredGuid_;}
    // Enumeration only: never opens a pad, so it is safe to call from the
    // settings dialog without taking a device out of its desktop mode.
    std::vector<GamepadDevice> devices()const;
    struct Capabilities {bool connected=false,gyro=false,accel=false,touch=false,triggers=false,haptics=false,calibrating=false;std::string name,guid,path;};
    Capabilities capabilities()const;
private:
    void activate(SDL_Gamepad*);
    void deactivate();
    void applyLights();
    bool opened(uint32_t id)const;
    std::string openedGuid()const;
    SDL_Gamepad* gamepad_=nullptr;
    // Triton dongles expose four HID interfaces with separate mouse/keyboard
    // collections. SDL disables lizard mode only on the interface it reads, so
    // the siblings are opened (and silently ignored) to stop desktop input too.
    std::vector<SDL_Joystick*> companionJoysticks_;
    bool initialized_=false;
    HostTime enumerateAt_=0;uint32_t preferredDevice_=0;std::string preferredGuid_;
    std::optional<bool> lastFocused_;
    std::array<int8_t,2> touchIds_{-1,-1};uint8_t nextTouchId_=0;
    bool gyro_=false,accel_=false,effectsActive_=false,audioInitialized_=false,hapticAttempted_=false;
    SDL_AudioStream* hapticStream_=nullptr;
    // Console-set light bar and player indicator: kept while unfocused, applied to the read pad.
    std::array<uint8_t,3> ledColor_{};uint8_t player_=0;bool haveLed_=false,havePlayer_=false,lightsDirty_=false;
    std::array<ControllerState::Touch,2> touchState_{};
    std::deque<std::array<ControllerState::Touch,2>> touchQueue_;
    std::array<float,3> gyroBias_{},biasSum_{},sensorGyro_{},sensorAccel_{0,1,0};
    unsigned calibrationSamples_=0;bool calibrating_=false,haveGyro_=false,haveAccel_=false;
    HostTime calibrationStart_=0;uint64_t sensorStamp_=0;
    TriggerFilter triggerFilter_;


};
}
