// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace veyra::input {

// Local steady-clock timestamp, 100 ns units.
using HostTime = std::int64_t;

struct ControllerFeedback {
    bool rumble=false,triggers=false,motionReset=false,led=false,playerIndex=false;
    uint8_t left=0,right=0;
    std::array<uint8_t,11> leftTrigger{},rightTrigger{};
    std::array<uint8_t,3> ledColor{}; // Light bar RGB sent by the console.
    uint8_t player=0; // Player indicator sent by the console.
    std::vector<int16_t> haptics; // Stereo PCM 3kHz; hard cap 600 samples (100ms).
    void merge(ControllerFeedback next){
        if(next.rumble){rumble=true;left=next.left;right=next.right;}
        if(next.triggers){triggers=true;leftTrigger=next.leftTrigger;rightTrigger=next.rightTrigger;}
        motionReset|=next.motionReset;
        if(next.led){led=true;ledColor=next.ledColor;}
        if(next.playerIndex){playerIndex=true;player=next.player;}
        if(next.haptics.size()>600)next.haptics.resize(600);
        if(haptics.size()+next.haptics.size()>600)haptics.clear();
        haptics.insert(haptics.end(),next.haptics.begin(),next.haptics.end());
    }
};
struct ControllerState {
    bool inputActive=false; // Distinguishes a released button/touch from focus/device loss.
    // Semantic buttons; translated explicitly per transport in the source layer.
    enum Button : std::uint32_t { Cross=1u<<0, Circle=1u<<1, Square=1u<<2, Triangle=1u<<3,
        Left=1u<<4, Right=1u<<5, Up=1u<<6, Down=1u<<7, L1=1u<<8, R1=1u<<9,
        L3=1u<<10, R3=1u<<11, Options=1u<<12, Share=1u<<13, Touchpad=1u<<14, PS=1u<<15 };
    std::uint32_t buttons = 0;
    std::int16_t leftX=0, leftY=0, rightX=0, rightY=0;
    std::uint8_t l2=0, r2=0;
    struct Touch {int8_t id=-1;uint16_t x=0,y=0;bool operator==(const Touch&)const=default;};
    std::array<Touch,2> touches{};
    // SDL coordinate frame: angular velocity rad/s, acceleration in g.
    std::array<float,3> gyro{},accel{0,1,0};
    uint64_t motionTimestampUs=0;bool motionValid=false;
    struct MotionSample {std::array<float,3> gyro{},accel{0,1,0};uint64_t timestampUs=0;bool operator==(const MotionSample&)const=default;};
    std::array<MotionSample,16> motionSamples{};uint8_t motionSampleCount=0;
    bool operator==(const ControllerState&) const = default;
};
} // namespace veyra::input
