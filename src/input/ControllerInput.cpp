#include "veyra/input/ControllerInput.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_guid.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_audio.h>
#include <cstring>
#include "veyra/Log.h"
#include <chrono>
namespace {
std::int64_t monotonic100ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()/100;
}
std::string hex16(uint16_t value) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "%04X", value);
    return text;
}
std::string guidString(SDL_JoystickID id) {
    char text[33]{};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
    return text;
}
void closeCompanions(std::vector<SDL_Joystick*>& pads) {
    for (auto* pad : pads) SDL_CloseJoystick(pad);
    pads.clear();
}
veyra::input::GamepadDevice describe(SDL_JoystickID id) {
    veyra::input::GamepadDevice d;
    d.guid = guidString(id);
    const char* name = SDL_GetGamepadNameForID(id);
    d.name = name ? name : "";
    const char* path = SDL_GetGamepadPathForID(id);
    d.path = path ? path : "";
    d.vid = SDL_GetJoystickVendorForID(id);
    d.pid = SDL_GetJoystickProductForID(id);
    d.connected = SDL_IsGamepad(id) != 0;
    return d;
}
}
namespace veyra::input {
bool ControllerInput::start(uint32_t preferredDevice){
    preferredDevice_=preferredDevice;
    if(initialized_)return true;
    // No SDL window is created. Our explicit foreground gate is authoritative.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    initialized_=SDL_InitSubSystem(SDL_INIT_GAMEPAD);return initialized_;
}
void ControllerInput::stop(){
    releaseEffects();if(hapticStream_){SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;}
    if(audioInitialized_){SDL_QuitSubSystem(SDL_INIT_AUDIO);audioInitialized_=false;}hapticAttempted_=false;
    touchQueue_.clear();touchState_={};haveGyro_=haveAccel_=false;calibrating_=false;gyroBias_={};enumerateAt_=0;
    closeCompanions(companionJoysticks_);
    if(gamepad_){SDL_CloseGamepad(gamepad_);gamepad_=nullptr;}
    if(initialized_){SDL_QuitSubSystem(SDL_INIT_GAMEPAD);initialized_=false;}
}
bool ControllerInput::opened(uint32_t id)const{
    return gamepad_&&SDL_GetGamepadID(gamepad_)==id;
}
std::string ControllerInput::openedGuid()const{
    return gamepad_?guidString(SDL_GetGamepadID(gamepad_)):std::string{};
}
void ControllerInput::setPreferredDevice(const std::string& guid){
    if(preferredGuid_==guid)return;
    const std::string previous=preferredGuid_;
    preferredGuid_=guid;
    preferredDevice_=0;
    // Do not leave the wrong pad claimed (and its desktop mode disabled).
    if(gamepad_&&!guid.empty()&&openedGuid()!=guid){
        deactivate();SDL_CloseGamepad(gamepad_);gamepad_=nullptr;enumerateAt_=0;
    }
    veyra::log::info("input","preferred device "+previous+" -> "+guid);
}
std::vector<GamepadDevice> ControllerInput::devices()const{
    // The dialog lists pads while no stream owns the reader, so borrow SDL when
    // needed and give it straight back. Enumeration never opens a pad.
    const bool borrowed=!initialized_;
    if(borrowed){
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))return {};
    }
    std::vector<GamepadDevice> out;
    int count=0;auto* ids=SDL_GetGamepads(&count);
    if(ids){
        out.reserve(count);
        for(int i=0;i<count;++i)out.push_back(describe(ids[i]));
        SDL_free(ids);
    }
    if(borrowed)SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return out;
}
void ControllerInput::activate(SDL_Gamepad* pad){
    gamepad_=pad;lightsDirty_=haveLed_||havePlayer_;
    triggerFilter_.reset();
    touchQueue_.clear();touchState_={};sensorStamp_=0;haveGyro_=haveAccel_=false;gyroBias_={};calibrating_=false;touchIds_={-1,-1};gyro_=SDL_GamepadHasSensor(gamepad_,SDL_SENSOR_GYRO)&&SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_GYRO,true);
    accel_=SDL_GamepadHasSensor(gamepad_,SDL_SENSOR_ACCEL)&&SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_ACCEL,true);
    const auto d=describe(SDL_GetGamepadID(gamepad_));
    std::string line=std::string("device opened guid=")+d.guid+" name="+d.name
        +" vid=0x"+hex16(d.vid)+" pid=0x"+hex16(d.pid)+" path="+d.path
        +" gyro="+(gyro_?"1":"0")+" accel="+(accel_?"1":"0")
        +" touchpads="+std::to_string(SDL_GetNumGamepadTouchpads(gamepad_));
    veyra::log::info("input",line);
    // A Proteus/Nereid dongle presents four HID interfaces, each with its own
    // mouse/keyboard collections. SDL sends the "lizard mode off" feature report
    // only to the interface it is reading. Open the siblings too so the desktop
    // stops receiving the same button and stick data; they are never polled.
    if (d.vid == 0x28de && (d.pid == 0x1304 || d.pid == 0x1305)) {
        int count = 0;
        auto* ids = SDL_GetJoysticks(&count);
        for (int i = 0; ids && i < count; ++i) {
            if (ids[i] == SDL_GetGamepadID(gamepad_)) continue;
            if (SDL_GetJoystickVendorForID(ids[i]) != d.vid
                || (SDL_GetJoystickProductForID(ids[i]) != d.pid)) continue;
            if (auto* sibling = SDL_OpenJoystick(ids[i])) companionJoysticks_.push_back(sibling);
        }
        SDL_free(ids);
        if (!companionJoysticks_.empty())
            veyra::log::info("input", "steam triton: opened "
                + std::to_string(companionJoysticks_.size()) + " companion interfaces to disable desktop mode");
    }
}
// The pad stops being read: its rumble/trigger effects and haptic stream end.
void ControllerInput::deactivate(){
    closeCompanions(companionJoysticks_);
    releaseEffects();if(hapticStream_){SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;}hapticAttempted_=false;
    if(gamepad_&&SDL_GamepadConnected(gamepad_)){
        if(gyro_)SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_GYRO,false);
        if(accel_)SDL_SetGamepadSensorEnabled(gamepad_,SDL_SENSOR_ACCEL,false);
    }
}
ControllerState ControllerInput::poll(bool focused){
    ControllerState state;if(!initialized_)return state;
    // One line per focus transition: this is the evidence that the explicit
    // foreground gate is open while a stream is being controlled.
    if(lastFocused_!=focused){
        lastFocused_=focused;
        std::string line=std::string("focus changed focused=")+(focused?"1":"0");
        if(gamepad_){const auto d=describe(SDL_GetGamepadID(gamepad_));line+=" device="+d.guid+" name="+d.name;}
        veyra::log::info("input",line);
    }
    SDL_PumpEvents();SDL_UpdateGamepads();
    // Only this module initializes SDL; drain its input events rather than
    // accumulating an unconsumed second application event queue.

    if(gamepad_&&!SDL_GamepadConnected(gamepad_)){deactivate();SDL_CloseGamepad(gamepad_);gamepad_=nullptr;enumerateAt_=0;}
    const auto now=monotonic100ns();
    if(!gamepad_&&now>=enumerateAt_){
        int count=0;auto* ids=SDL_GetGamepads(&count);
        uint32_t target=preferredDevice_;
        if(!preferredGuid_.empty()&&ids){
            std::vector<std::string> guids;guids.reserve(count);
            for(int i=0;i<count;++i)guids.push_back(guidString(ids[i]));
            const int pick=preferredDeviceIndex(guids,preferredGuid_);
            if(pick>=0)target=ids[pick];
        }
        for(int i=0;i<count&&!gamepad_;++i){
            if((target&&ids[i]!=target)||opened(ids[i]))continue;
            auto* pad=SDL_OpenGamepad(ids[i]);if(!pad)continue;
            activate(pad);
        }
        SDL_free(ids);enumerateAt_=now+10000000;
    }
    if(calibrating_&&focused&&gamepad_){
        if(!calibrationStart_)calibrationStart_=now;
        else if(now-calibrationStart_>100000000){calibrating_=false;veyra::log::warn("input","Calibration timed out; prior bias retained");}
    }
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(!focused||!gamepad_)continue;
        if((event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN||event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_UP||event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION)&&event.gtouchpad.which==SDL_GetGamepadID(gamepad_)){
            const auto& touch=event.gtouchpad;
            if(touch.touchpad!=0||touch.finger<0||touch.finger>=2||!std::isfinite(touch.x)||!std::isfinite(touch.y))continue;
            auto& value=touchState_[touch.finger];
            if(event.type==SDL_EVENT_GAMEPAD_TOUCHPAD_UP)value={};
            else{if(value.id<0)value.id=int8_t(nextTouchId_++&0x7f);value.x=uint16_t(std::clamp(touch.x,0.f,1.f)*1919);value.y=uint16_t(std::clamp(touch.y,0.f,1.f)*1079);}
            if(!touchQueue_.empty()&&touchQueue_.back()[0].id==touchState_[0].id&&touchQueue_.back()[1].id==touchState_[1].id)touchQueue_.back()=touchState_;
            else if(touchQueue_.size()<16)touchQueue_.push_back(touchState_);
            else{touchQueue_.clear();touchState_={};touchQueue_.push_back(touchState_);veyra::log::warn("input","Touch edge queue overflow; releasing touches");}
        }
        if(event.type==SDL_EVENT_GAMEPAD_SENSOR_UPDATE&&event.gsensor.which==SDL_GetGamepadID(gamepad_)){
            const auto& sample=event.gsensor;if(!std::all_of(std::begin(sample.data),std::end(sample.data),[](float v){return std::isfinite(v);}))continue;
            if(sample.sensor==SDL_SENSOR_ACCEL){for(int i=0;i<3;++i)sensorAccel_[i]=sample.data[i]/SDL_STANDARD_GRAVITY;haveAccel_=true;}
            else if(sample.sensor==SDL_SENSOR_GYRO){std::copy_n(sample.data,3,sensorGyro_.begin());haveGyro_=true;}
            else continue;
            sensorStamp_=sample.timestamp/1000;
            if(haveAccel_&&haveGyro_){
                if(calibrating_&&sample.sensor==SDL_SENSOR_GYRO){
                    float g2=0,a2=0;for(int i=0;i<3;++i){g2+=sensorGyro_[i]*sensorGyro_[i];a2+=sensorAccel_[i]*sensorAccel_[i];}
                    if(g2<.01f&&std::abs(a2-1)<.1f){for(int i=0;i<3;++i)biasSum_[i]+=sensorGyro_[i];++calibrationSamples_;}
                    else{calibrationSamples_=0;biasSum_={};}
                    if(calibrationSamples_>=120){for(int i=0;i<3;++i)gyroBias_[i]=biasSum_[i]/float(calibrationSamples_);calibrating_=false;veyra::log::info("input","Stationary gyro calibration completed");}
                }
                ControllerState::MotionSample reading;reading.accel=sensorAccel_;reading.timestampUs=sensorStamp_;
                for(int i=0;i<3;++i)reading.gyro[i]=sensorGyro_[i]-gyroBias_[i];
                if(state.motionSampleCount<state.motionSamples.size())state.motionSamples[state.motionSampleCount++]=reading;
                else{std::move(state.motionSamples.begin()+1,state.motionSamples.end(),state.motionSamples.begin());state.motionSamples.back()=reading;}
            }
        }
    }
    if(!focused||!gamepad_){touchQueue_.clear();touchState_={};touchIds_={-1,-1};haveGyro_=haveAccel_=false;triggerFilter_.reset();releaseEffects();return state;}
    state.inputActive=true;
    struct Button{SDL_GamepadButton input;ControllerState::Button output;};
    constexpr Button buttons[]={
        {SDL_GAMEPAD_BUTTON_SOUTH,ControllerState::Cross},{SDL_GAMEPAD_BUTTON_EAST,ControllerState::Circle},
        {SDL_GAMEPAD_BUTTON_WEST,ControllerState::Square},{SDL_GAMEPAD_BUTTON_NORTH,ControllerState::Triangle},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,ControllerState::Left},{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,ControllerState::Right},
        {SDL_GAMEPAD_BUTTON_DPAD_UP,ControllerState::Up},{SDL_GAMEPAD_BUTTON_DPAD_DOWN,ControllerState::Down},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,ControllerState::L1},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,ControllerState::R1},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,ControllerState::L3},{SDL_GAMEPAD_BUTTON_RIGHT_STICK,ControllerState::R3},
        {SDL_GAMEPAD_BUTTON_START,ControllerState::Options},{SDL_GAMEPAD_BUTTON_BACK,ControllerState::Share},
        {SDL_GAMEPAD_BUTTON_GUIDE,ControllerState::PS},{SDL_GAMEPAD_BUTTON_TOUCHPAD,ControllerState::Touchpad}};
    for(auto b:buttons)if(SDL_GetGamepadButton(gamepad_,b.input))state.buttons|=b.output;
    state.leftX=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFTX);state.leftY=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFTY);
    state.rightX=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHTX);state.rightY=SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHTY);
    state.l2=uint8_t(std::max(0,int(SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)))*255/32767);
    state.r2=uint8_t(std::max(0,int(SDL_GetGamepadAxis(gamepad_,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)))*255/32767);
    // Jitter must not push a trigger release out of the transport history window (TriggerFilter.h).
    {const auto t=triggerFilter_.apply(state.l2,state.r2,now);state.l2=t.l2;state.r2=t.r2;}
    if(!touchQueue_.empty()){state.touches=touchQueue_.front();touchQueue_.pop_front();}
    else{
        for(int finger=0;finger<2;++finger){bool down=false;float x=0,y=0,pressure=0;
            if(SDL_GetNumGamepadTouchpads(gamepad_)>0&&SDL_GetGamepadTouchpadFinger(gamepad_,0,finger,&down,&x,&y,&pressure)&&down&&std::isfinite(x)&&std::isfinite(y)){
                auto& touch=touchState_[finger];if(touch.id<0)touch.id=int8_t(nextTouchId_++&0x7f);touch.x=uint16_t(std::clamp(x,0.f,1.f)*1919);touch.y=uint16_t(std::clamp(y,0.f,1.f)*1079);
            }else touchState_[finger]={};
        }
        state.touches=touchState_;
    }
    if(gyro_&&accel_&&haveGyro_&&haveAccel_){state.motionValid=true;state.motionTimestampUs=sensorStamp_;state.accel=sensorAccel_;for(int i=0;i<3;++i)state.gyro[i]=sensorGyro_[i]-gyroBias_[i];}
    return state;
}
void ControllerInput::releaseEffects(){
    if(!effectsActive_)return;
    if(gamepad_){
        if(!SDL_RumbleGamepad(gamepad_,0,0,0))veyra::log::warn("input-feedback",SDL_GetError());
        if(SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5){
            std::array<uint8_t,47> effect{};effect[0]=0x0c;effect[10]=5;effect[21]=5;
            if(!SDL_SendGamepadEffect(gamepad_,effect.data(),int(effect.size())))veyra::log::warn("input-feedback",SDL_GetError());
        }
    }
    if(hapticStream_)SDL_ClearAudioStream(hapticStream_);
    effectsActive_=false;
}
void ControllerInput::applyLights(){
    lightsDirty_=false;
    if(!gamepad_)return;
    const auto props=SDL_GetGamepadProperties(gamepad_);
    if(haveLed_&&SDL_GetBooleanProperty(props,SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN,false)&&!SDL_SetGamepadLED(gamepad_,ledColor_[0],ledColor_[1],ledColor_[2]))
        veyra::log::warn("input-feedback",SDL_GetError());
    // SDL maps the index to the DualSense player LEDs.
    if(havePlayer_&&!SDL_SetGamepadPlayerIndex(gamepad_,int(player_)))
        veyra::log::warn("input-feedback",SDL_GetError());
}
void ControllerInput::feedback(ControllerFeedback f,bool focused){
    // Light bar and player indicator are console state rather than effects: the latest
    // values are kept while unfocused and applied once the pad is read again.
    if(f.led){ledColor_=f.ledColor;haveLed_=true;lightsDirty_=true;}
    if(f.playerIndex){player_=f.player;havePlayer_=true;lightsDirty_=true;}
    if(!focused||!gamepad_){releaseEffects();return;}
    if(lightsDirty_)applyLights();
    if(f.rumble){effectsActive_=true;if(!SDL_RumbleGamepad(gamepad_,uint16_t(f.left)*257,uint16_t(f.right)*257,5000))veyra::log::warn("input-feedback",SDL_GetError());}
    if(f.triggers&&SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5){
        // SDL DualSense effects report payload (47 bytes): right at10, left at21.
        std::array<uint8_t,47> effect{};effect[0]=0x0c;
        std::copy(f.rightTrigger.begin(),f.rightTrigger.end(),effect.begin()+10);
        std::copy(f.leftTrigger.begin(),f.leftTrigger.end(),effect.begin()+21);
        effectsActive_=true;
        if(!SDL_SendGamepadEffect(gamepad_,effect.data(),int(effect.size())))veyra::log::warn("input-feedback",SDL_GetError());
    }
    if(f.haptics.empty()||SDL_GetGamepadType(gamepad_)!=SDL_GAMEPAD_TYPE_PS5)return;
    if(!hapticAttempted_){
        hapticAttempted_=true;
        if(!audioInitialized_)audioInitialized_=SDL_InitSubSystem(SDL_INIT_AUDIO);
        if(!audioInitialized_){veyra::log::error("input-haptics",SDL_GetError());return;}
        // Never use the default speakers or a stereo endpoint. Ambiguity fails closed.
        int count=0;auto* devices=SDL_GetAudioPlaybackDevices(&count);SDL_AudioDeviceID selected=0;int matches=0;
        for(int i=0;i<count;++i){const auto* name=SDL_GetAudioDeviceName(devices[i]);SDL_AudioSpec spec{};
            if(name&&(std::strstr(name,"Wireless Controller")||std::strstr(name,"DualSense"))&&SDL_GetAudioDeviceFormat(devices[i],&spec,nullptr)&&spec.channels==4){selected=devices[i];++matches;}
        }
        SDL_free(devices);
        int gamepadCount=0;auto* gamepads=SDL_GetGamepads(&gamepadCount);SDL_free(gamepads);
        if(matches!=1||gamepadCount!=1){veyra::log::warn("input-haptics","No unique four-channel DualSense endpoint/controller pair; haptics not routed");return;}
        SDL_AudioSpec input{SDL_AUDIO_S16,4,3000};
        hapticStream_=SDL_OpenAudioDeviceStream(selected,&input,nullptr,nullptr);
        if(!hapticStream_||!SDL_ResumeAudioStreamDevice(hapticStream_)){veyra::log::error("input-haptics",SDL_GetError());if(hapticStream_)SDL_DestroyAudioStream(hapticStream_);hapticStream_=nullptr;return;}
        veyra::log::info("input-haptics","Dedicated four-channel endpoint opened; speaker channels silent");
    }
    if(!hapticStream_)return;
    std::vector<int16_t> quad;quad.reserve(f.haptics.size()*2);
    for(size_t i=0;i+1<f.haptics.size();i+=2){quad.push_back(0);quad.push_back(0);quad.push_back(f.haptics[i]);quad.push_back(f.haptics[i+1]);}
    if(SDL_GetAudioStreamQueued(hapticStream_)>2400)SDL_ClearAudioStream(hapticStream_);
    effectsActive_=true;
    if(!SDL_PutAudioStreamData(hapticStream_,quad.data(),int(quad.size()*sizeof(int16_t))))veyra::log::warn("input-haptics",SDL_GetError());
}

bool ControllerInput::calibrate(){
    if(!gamepad_||!gyro_||!accel_)return false;
    biasSum_={};calibrationSamples_=0;calibrationStart_=0;calibrating_=true;return true;
}
ControllerInput::Capabilities ControllerInput::capabilities()const{
    if(!gamepad_)return {};
    const auto d=describe(SDL_GetGamepadID(gamepad_));
    return {true,gyro_,accel_,SDL_GetNumGamepadTouchpads(gamepad_)>0,
        SDL_GetGamepadType(gamepad_)==SDL_GAMEPAD_TYPE_PS5,hapticStream_!=nullptr,calibrating_,d.name,d.guid,d.path};
}

}
