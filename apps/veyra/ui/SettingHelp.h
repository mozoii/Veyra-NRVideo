#pragma once
#include "veyra/Log.h"
// V2 size also works without a Common Controls v6 activation context.
namespace veyra::ui {
inline void installDialogHelp(HWND root,std::initializer_list<std::pair<int,const wchar_t*>> entries){
    auto tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    SetWindowTheme(tip,L"",L"");SendMessageW(tip,TTM_SETTIPBKCOLOR,RGB(28,31,33),0);SendMessageW(tip,TTM_SETTIPTEXTCOLOR,RGB(225,230,228),0);
    SendMessageW(tip,TTM_SETMAXTIPWIDTH,0,360);SendMessageW(tip,TTM_SETDELAYTIME,TTDT_INITIAL,550);SendMessageW(tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);
    for(auto [id,text]:entries){auto child=GetDlgItem(root,id);if(!child)continue;TOOLINFOW info{TTTOOLINFOW_V2_SIZE};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=root;info.uId=UINT_PTR(child);info.lpszText=const_cast<wchar_t*>(text);if(!SendMessageW(tip,TTM_ADDTOOLW,0,LPARAM(&info)))veyra::log::error("ui-help","Tooltip registration failed");}
}
inline const wchar_t* settingHelp(int id){
    static const wchar_t* model[]={
        L"How hard NR works. 0–1, taste a little first; maxing it is not always better looking.",
        L"Tune NR's local brightness/darkness bias. 0–1, the lighting director's knob, not screen brightness.",
        L"Tune NR's local structure bias. 0–1, details may become clearer; do not invite the noise up on stage too.",
        L"Experimental skin parameter: -1 uses the default, otherwise 0–2. The effect is unverified; no one-click smoothing guarantee.",
        L"Switch between the three experimental style values 0/1/2. Try by taste; the number is not a quality ranking.",
        L"Automatic mask experimental switch. Whether it helps the current footage needs real measurement; not a universal cloth.",
        L"UI correction experimental switch, effect unverified. To keep subtitles safe, prioritize drawing NR exclusions.",
        L"Control how much the NR change finally blends in. 0 keeps the base image; adding goes up; range 0–2.",
        L"Control the part of the picture NR darkens. 0–2, do not press the shadows flat in one gulp.",
        L"Control the part of the picture NR brightens. 0–2, do not treat the bulb as the sun.",
        L"Control the color change from NR. 0–2, whatever the taste, keep an eye on skin tone.",
        L"Control the luminance change from NR. 0–2, find the balance between detail and look."};
    if(id>=100&&id<=111)return model[id-100];
    if(id>=600&&id<=611)return model[id-600];
    if(id>=700&&id<=702)return model[4];if(id==710||id==711)return model[5];if(id==720||id==721)return model[6];
    if(id>=730&&id<=732)return L"Choose the SR output cap: 2K / 4K / 8K, preserving aspect, never below 1x target. More pixels, more GPU to feed.";
    switch(id){
    case 200:return L"This is our experimental DLSS5 NR: it rebuilds picture detail with a neural network. Not game-native integration; the effect depends on the footage and the GPU.";
    case 201:return L"Scale the picture to the chosen target size. It does not magically bring back all details, and the GPU is not a wishes pool.";
    case 202:return L"FG multiplier: 2X inserts 1, 3X inserts 2, 4X inserts 3. Target frame rate goes up, the GPU may not keep up; ghosting may also show up.";
    case 203:return L"Choose the NR internal processing cap; it scales by picture ratio, then fills the output. Low tiers save compute, high tiers keep detail; the native tier makes the GPU work overtime. Export still uses the full size.";
    case 204:return L"Optical flow quality tier: estimates how objects move. Quality tiers cost more, and a wrong motion estimate can make frame generation drift too.";
    case 205:return L"Choose content cadence. Capture 60→30 suits repeated 30-frame game content in a 60Hz signal; real 60fps content is not halved by force. Timestamps are kept.";
    case 206:return L"NR changes inside exclusion areas are suppressed, keeping the original, up to 4 blocks. It only covers NR, not SR and FG.";
    case 207:return L"Choose DLSS SR or RTX Video SR, and the video SR quality. The two routes behave differently; try them to judge quality and time.";
    case 208:return L"Choose DLSS frame generation or experimental XeSS frame generation. AMD FSR frame generation was removed from the UI in 1.4.0 (switching back to DLSS tends to stick and the effect is average): the engine backend stays, but the panel no longer offers it; if an old setting or preset stores FSR, it switches back to DLSS on open and logs it. XeSS-generated frames exist only in the display exchange chain and are not available to the export pipeline; export switches to DLSS frame generation automatically, or turns frame generation off when the GPU does not support it.";
    case 508:return L"Export bitrate: only decides the size and quality of the export file, not the preview. Auto means the encoder's constant-quality tier (NVENC CONSTQP / system encoder quality mode). H.264 normally needs 10-40 Mbps, 4K or high dynamics suggest 60-150; HEVC can be lower for the same quality.";
    case 209:return L"Choose the motion estimation backend. NVOF, FidelityFX and GPU DIS are responsible for seeing how objects move. DIS is an experimental algorithm using general compute units, may compete with NR for work and is not guaranteed faster; choosing it does not change your frame generation method, nor unlock AMD NR.";
    case 210:return L"Frame generation pacing. Off (default) = the 1.4.0 rule: a frame group is generated if the whole group makes the last frame's deadline. On = stricter: if the first frame in the group misses its own deadline, the whole group is not generated; the rhythm is cleaner but fewer frames are generated.";
    case 211:return L"Restore enhancement parameters to built-in defaults. If you got lost, this is the way here; quality parameters immediately return to factory state.";
    case 213:return L"Drag a box on the picture to add an NR exclusion area, Esc cancels. Circle the parts you do not want NR to change.";
    case 214:return L"Clear the current NR exclusion boxes. This only removes the fence; it does not delete the video.";
    case 222:return L"Transition width at the exclusion edge, in working-resolution pixels: 0 is a hard edge, 12 is about 0.5% of 4K frame height, larger is softer. Takes effect immediately after change.";
    case 800:return L"Color grading master switch. When on, color grading runs before all effects (NR/SR/FG), with a little extra overhead; when off, this chain does not exist at all, zero overhead, and the picture returns to ungraded.";
    case 801:return L"Restore every color page parameter to neutral (does not change the master switch). The pre-restore values leave one undo opportunity.";
    case 802:return L"Recover the values from before the last “reset all”.";
    case 215:return L"Halve the AMD optical flow input width and height, computing fewer pixels. Speed may improve, but small motion may be lost.";
    case 216:return L"Auto-estimate audio compensation, or switch to a manual offset. The goal is that mouth and sound arrive together; do not let the character carry its own dubbing delay.";
    case 217:return L"-250 to 250 ms: positive delays the audio, negative reduces existing compensation; cannot bring future sound out.";
    case 218:return L"Choose the NR runtime file. The default is the community Lecram 310.8.3, for RTX50, with the same picture as the NVIDIA original and slightly lower power; the community-modified version tries to support RTX40/50; the RTX 30 compatible version is the community SF-v2 (its own FP16 route for RTX 20/30, with 30-series actual effect pending hardware verification). Switching rebuilds the chain; a brief pause is normal.";
    case 219:return L"Toggle the streaming compatibility display path. It is a stepping stone for capture software, not a way to make every capture method suddenly smart.";
    case 220:return L"Default: SR → NR (DLSS5) → FG. When opened: NR → SR → FG, possibly faster, possibly more ghosting or edge artifacts. Off by default, preview only; needs both NR and SR on. Export order unchanged.";
    case 500:return L"H.264 is widely compatible, HEVC usually saves more space. HDR video must choose HEVC, saved as Main10 / PQ; do not force HDR into ordinary H.264. The encoder is chosen automatically per GPU: NVIDIA uses NVENC (zero-copy, fastest), AMD/Intel use system hardware encoding (the driver's own H.264/HEVC encoder), and HDR export still needs an NVIDIA card.";
    case 501:return L"Choose a save location and export video. Parameters are frozen at start, NR runs native; the low-latency preview order does not carry into export.";
    case 502:return L"Save the current image or video frame. A great moment, kept.";
    case 503:return L"Pause or resume the export task, not the video you are watching.";
    case 504:return L"Cancel the current export. Unfinished output is not disguised as a finished film.";
    case 505:return L"During export, prioritize watching; export a bit slower. Both sides competing for the GPU at the same time, someone has to be polite.";
    default:return nullptr;
    }
}
}
