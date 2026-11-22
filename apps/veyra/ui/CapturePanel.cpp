#include "CapturePanel.h"
#include "Theme.h"
#include "SettingHelp.h"
#include "CapturePreferenceStore.h"
#include "veyra/RuntimePaths.h"
#include "veyra/Log.h"
#include "veyra/source/CaptureCardSource.h"
#include "veyra/source/CaptureFormatRank.h"
#include <future>
#include <thread>
#include <format>
namespace veyra::ui {
namespace {
HWND window=nullptr;HFONT font=nullptr;std::function<void(const std::wstring&)> start;
struct Query {int device=-1;std::vector<source::CaptureDevice> video,audio;std::vector<source::CaptureFormat> formats;};
std::future<Query> pending;bool busy=false,refreshAfterQuery=false;int queriedDevice=-1;ULONGLONG queryStarted=0;
std::vector<source::CaptureFormat> formats;
std::vector<source::CaptureDevice> videoDevices,audioDevices;
CapturePreferences remembered;
unsigned selectedColor(HWND h){return source::captureColorOverride(unsigned(SendDlgItemMessageW(h,10,CB_GETCURSEL,0,0)),unsigned(SendDlgItemMessageW(h,20,CB_GETCURSEL,0,0)));}
void saveDeviceColor(HWND h){
    const int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));
    if(device<0||size_t(device)>=videoDevices.size())return;
    remembered.deviceColors[videoDevices[size_t(device)].path]=selectedColor(h);
    if(!CapturePreferenceStore(runtime::localDataDirectory()).save(remembered))log::warn("capture","Failed to save capture color preference");
}
std::function<bool()> readSdr;std::function<bool(bool)> setSdr;std::function<int()> readAudioIngress;std::function<bool(int)> setAudioIngress;
std::function<bool()> readFlip;std::function<bool(bool)> setFlip;
std::function<int()> readBuffer;std::function<bool(int)> setBuffer;
void rebuildAudioList(HWND h,int device){
    SendDlgItemMessageW(h,3,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(L"Do not monitor audio"));
    const bool videoSelected=device>=0&&size_t(device)<videoDevices.size();
    if(videoSelected)SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(videoDevices[size_t(device)].hasEmbeddedAudio?L"Use video device built-in audio (detected)":L"Try video device built-in audio"));
    for(auto& audio:audioDevices){const auto label=std::format(L"[{}] {}",audio.wasapi?L"WASAPI":L"DirectShow",audio.name);SendDlgItemMessageW(h,3,CB_ADDSTRING,0,LPARAM(label.c_str()));}
    int restore=0;
    if(videoSelected&&videoDevices[size_t(device)].path==remembered.videoPath){
        if(remembered.audioMode==source::kCaptureAudioFromVideoDevice)restore=1;
        else if(!remembered.audioPath.empty()){
            restore=-1;
            for(size_t i=0;i<audioDevices.size();++i)
                if(audioDevices[i].path==remembered.audioPath&&audioDevices[i].wasapi==(remembered.audioMode==source::kCaptureAudioWasapi))restore=int(i)+2;
        }
    }
    SendDlgItemMessageW(h,3,CB_SETCURSEL,restore,0);
}
int selectedAudio(HWND h,int device){
    const int selection=int(SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0));if(selection<=0)return source::kCaptureAudioDisabled;
    const bool videoSelected=device>=0&&size_t(device)<videoDevices.size();
    if(videoSelected&&selection==1)return source::kCaptureAudioFromVideoDevice;
    return selection-1-(videoSelected?1:0);
}
void maybeShowFormatHint(HWND h){
    const int index=int(SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0));
    if(index<0||size_t(index)>=formats.size()||remembered.formatHintDismissed)return;
    const auto& format=formats[size_t(index)];
    const auto tier=static_cast<source::CaptureFormatTier>(format.tier);
    if(!source::captureFormatNeedsCostHint(tier))return;
    SetDlgItemTextW(h,8,std::format(L"Tip: {} at high resolution / frame rate has one more processing stage than low-latency formats ({}). Prefer low-latency formats; this tip appears only once.",format.label,source::captureFormatTierLabel(tier)).c_str());
    remembered.formatHintDismissed=true;
    CapturePreferenceStore(runtime::localDataDirectory()).save(remembered);
}
void query(int device){if(busy)return;busy=true;queriedDevice=device;queryStarted=GetTickCount64();SetDlgItemTextW(window,8,L"Querying device capabilities… playback continues");EnableWindow(GetDlgItem(window,4),FALSE);EnableWindow(GetDlgItem(window,5),FALSE);EnableWindow(GetDlgItem(window,1),FALSE);
    const std::wstring videoPath=device>=0&&size_t(device)<videoDevices.size()?videoDevices[size_t(device)].path:L"";
    EnableWindow(GetDlgItem(window,10),FALSE);EnableWindow(GetDlgItem(window,20),FALSE);
    pending=std::async(std::launch::async,[device,videoPath]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);Query result;result.device=device;try{if(device<0){result.video=source::CaptureCardSource::deviceDetails();result.audio=source::CaptureCardSource::deviceDetails(true);}else result.formats=videoPath.empty()?source::CaptureCardSource::formats(unsigned(device)):source::CaptureCardSource::formatsByPath(videoPath);}catch(...){}CoUninitialize();return result;});
}
void arrange(){RECT r{};GetClientRect(window,&r);const int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window));const int ys[]={0,44,114,184,448,448,14,84,380,154,254,224,294,514,484,602,566,542,338,342,254,224};for(int id=1;id<=21;++id){int x=id==5?width-152:id==18?width-156:(id==20||id==21)?width-180:16;int w=id==4?width-184:id==5||id==18?136:id==19?width-184:(id==10||id==11)?width-212:(id==20||id==21)?164:width-32;MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,ys[id]),dip(window,w),dip(window,(id<=3||id==10||id==13||id==16||id==20)?180:id==8?56:id==4||id==5?36:id==18?32:24),TRUE);}}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;font=makeFont(h);titleTheme(h);auto add=[&](const wchar_t* cls,const wchar_t* label,int id,DWORD style){auto c=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,HMENU(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,WPARAM(font),TRUE);themeControl(c);};
    remembered=CapturePreferenceStore(runtime::localDataDirectory()).load();
    add(L"EDIT",std::format(L"{:g}",remembered.requestedFps).c_str(),18,ES_AUTOHSCROLL|WS_TABSTOP);
    SendDlgItemMessageW(h,18,EM_SETLIMITTEXT,16,0);
    add(L"STATIC",L"Device frame rate FPS (0 = default, reconnect needed)",19,0);
    refreshAfterQuery=busy;
    for(int i=1;i<=3;++i)add(L"COMBOBOX",L"",i,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"COMBOBOX",L"",10,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Input color (change requires reconnect)",11,0);
    for(auto name:{L"Auto · device metadata",L"Rec.2100 PQ · HDR10",L"Rec.2100 HLG",L"Rec.709 · SDR"})SendDlgItemMessageW(h,10,CB_ADDSTRING,0,LPARAM(name));SendDlgItemMessageW(h,10,CB_SETCURSEL,0,0);
    add(L"COMBOBOX",L"",20,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Input range",21,0);
    for(auto name:{L"Auto",L"Limited / Limited",L"Full / Full"})SendDlgItemMessageW(h,20,CB_ADDSTRING,0,LPARAM(name));SendDlgItemMessageW(h,20,CB_SETCURSEL,0,0);
    add(L"BUTTON",L"Convert to SDR display (all previews, immediate effect)",12,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",13,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Capture audio (change requires reconnect)",14,0);
    for(auto name:{L"Auto: prefer linear PCM, decode Dolby/DTS bitstream when needed",L"Force linear PCM (no Dolby/DTS bitstream)",L"Bitstream first: pass straight to the amplifier (decode to PCM when no pass-through)"})SendDlgItemMessageW(h,13,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(h,13,CB_SETCURSEL,WPARAM(std::clamp(readAudioIngress?readAudioIngress():0,0,2)),0);
    add(L"BUTTON",L"Flip picture vertically (check when the capture is upside down, immediate effect)",15,BS_AUTOCHECKBOX|WS_TABSTOP);SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip&&readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    add(L"COMBOBOX",L"",16,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);add(L"STATIC",L"Device buffer (change requires reconnect)",17,0);
    for(auto name:{L"Auto: 2 frames for 1080p and below, 3 frames for higher resolutions",L"Minimum: 1-frame buffer (lowest latency, high load may drop frames)",L"Driver default: no suggestion, keep the device's own setting"})SendDlgItemMessageW(h,16,CB_ADDSTRING,0,LPARAM(name));
    SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer?readBuffer():0,0,2)),0);
    add(L"BUTTON",L"Connect and start watching",4,BS_PUSHBUTTON|WS_TABSTOP);marked(GetDlgItem(h,4));add(L"BUTTON",L"Refresh device",5,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"STATIC",L"Video input device",6,0);add(L"STATIC",L"Formats actually supported by the device",7,0);add(L"STATIC",L"",8,0);add(L"STATIC",L"Audio monitoring (selected capture input only, off by default)",9,0);
    installDialogHelp(h,{{16,L"The number of driver buffers the capture card uses to send frames to the software. More buffers let the card queue ahead, but every extra frame is a frame more of waiting; too few and the driver cannot keep up and frames are dropped. Auto: 2 frames for 1080p and below, 3 frames for higher resolutions; minimum: 1 frame (lowest latency but may drop frames under load); driver default: leave it alone. Changing this requires reconnecting the capture card; the connection log writes the suggested value and the driver's actual value (honestly marked when negotiation is not effective)."},{15,L"For capture cards whose orientation declaration disagrees with the actual picture: common with some RGB24 formats — the device writes bottom row first in the media type, but actually sends top row first, so the picture comes out upside down. Checking flips the capture picture once vertically, immediately; it only affects the local capture preview, screenshots and frames exported here, not the device or other formats. Do not check when the picture is fine; remember to uncheck when switching to a non-flipped format."},{13,L"When the capture card sends Dolby Atmos / Dolby Audio / DTS as a bitstream, this decides the route. The device's own audio format is unaffected; changing this requires reconnecting the capture card. Auto prefers PCM and falls back to bitstream decoding only when PCM is unavailable; bitstream-first does the reverse: first try to pass the bitstream through unchanged to the amplifier/backboard (exclusive output, the card does not decode in software, only the receiver can decode the object sound field; needs output device support, probe seen in the log bitstream-out), and fall back to decoding when no supporting endpoint exists. Suitable for devices already set to Dolby output that also provide PCM."},{12,L"Convert HDR to SDR display without changing device output or Windows. Enhancement still applies; only the preview is changed, video export is unaffected. Switching pauses briefly, screenshots follow the current picture. When off, follow the display."},{10,L"When the device reports no HDR info, specify manually; choose P010/P016. P010 can also carry SDR; do not force an ordinary picture into an HDR hat."},{1,L"Select the capture card's video device. Do not accidentally call the webcam to live-stream a game."},{2,L"Select the resolution, frame rate and pixel format the device actually provides. Clarity, bandwidth and latency all depend on it."},{3,L"If the device has built-in HDMI audio, it shows “Use video device built-in audio”; otherwise select a separate audio device or take no sound."},{4,L"Connect the capture card at the current format and apply the current enhancement settings."},{5,L"Rescan devices and formats. When a device is held by another app, refresh may not take it back."}});
    installDialogHelp(h,{{18,L"Directly request the capture card to send frames at this frame rate, decimals supported. 0 uses the selected format; requires reconnect after change. If the device does not support it or returns a different rate, an error is reported instead of silently dropping frames in software. The capture card cannot tell which pictures are duplicated game frames, so lowering the rate does not guarantee removal of duplicates."}});
    EnableWindow(GetDlgItem(h,4),FALSE);arrange();SetTimer(h,1,100,nullptr);if(!busy)query(-1);return 0;}
case WM_TIMER:
    SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    if(readFlip)SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    if(readBuffer)SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer(),0,2)),0);
    if(busy&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        auto result=pending.get();busy=false;EnableWindow(GetDlgItem(h,5),TRUE);EnableWindow(GetDlgItem(h,1),TRUE);
        if(refreshAfterQuery){refreshAfterQuery=false;query(-1);return 0;}
        if(result.device<0){
            videoDevices=std::move(result.video);audioDevices=std::move(result.audio);
            SendDlgItemMessageW(h,1,CB_RESETCONTENT,0,0);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);formats.clear();
            for(auto& video:videoDevices)SendDlgItemMessageW(h,1,CB_ADDSTRING,0,LPARAM(video.name.c_str()));
            if(!videoDevices.empty()){
                int restore=remembered.videoPath.empty()?0:-1;
                for(size_t i=0;i<videoDevices.size();++i)if(videoDevices[i].path==remembered.videoPath)restore=int(i);
                SendDlgItemMessageW(h,1,CB_SETCURSEL,restore,0);rebuildAudioList(h,restore);
                if(restore>=0)query(restore);else SetDlgItemTextW(h,8,L"Last capture device not connected. Reconnect it and refresh, or select another device manually.");
            }else{
                rebuildAudioList(h,-1);SetDlgItemTextW(h,8,L"No capture device found. Connect one and click refresh.");
            }
        }else{
            formats=std::move(result.formats);SendDlgItemMessageW(h,2,CB_RESETCONTENT,0,0);
            for(auto& format:formats)SendDlgItemMessageW(h,2,CB_ADDSTRING,0,LPARAM(format.label.c_str()));
            int restore=formats.empty()?-1:0;
            const bool sameDevice=size_t(result.device)<videoDevices.size()&&videoDevices[size_t(result.device)].path==remembered.videoPath;
            if(sameDevice&&!remembered.formatKey.empty()){
                restore=-1;for(size_t i=0;i<formats.size();++i)if(formats[i].key==remembered.formatKey)restore=int(i);
            }
            const auto color=size_t(result.device)<videoDevices.size()?remembered.colorForDevice(videoDevices[size_t(result.device)].path):0;
            SendDlgItemMessageW(h,10,CB_SETCURSEL,source::captureColorSpace(color),0);
            SendDlgItemMessageW(h,20,CB_SETCURSEL,source::captureColorRange(color),0);
            EnableWindow(GetDlgItem(h,10),TRUE);EnableWindow(GetDlgItem(h,20),TRUE);
            SetDlgItemTextW(h,18,std::format(L"{:g}",sameDevice?remembered.requestedFps:0).c_str());
            SendDlgItemMessageW(h,2,CB_SETCURSEL,restore,0);EnableWindow(GetDlgItem(h,4),restore>=0);
            SetDlgItemTextW(h,8,formats.empty()?L"No valid capture format up to 4K read, or the device is held by another application.":L"Uses the current enhancement settings after connection. Changing format or audio requires reconnect.");
            if(!formats.empty()&&restore<0)SetDlgItemTextW(h,8,L"Last format is no longer available; reselect a format.");
            if(SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)==CB_ERR){EnableWindow(GetDlgItem(h,4),FALSE);SetDlgItemTextW(h,8,L"Last audio device not connected. Select an audio device or explicitly choose not to monitor audio.");}
        }
    }else if(busy&&GetTickCount64()-queryStarted>5000)SetDlgItemTextW(h,8,L"Device query is slow. You can close this panel; current playback is unaffected.");
    return 0;
case WM_COMMAND:
    if((LOWORD(wp)==10||LOWORD(wp)==20)&&HIWORD(wp)==CBN_SELCHANGE){saveDeviceColor(h);return 0;}
    if(LOWORD(wp)==12&&HIWORD(wp)==BN_CLICKED){
        const bool enabled=SendDlgItemMessageW(h,12,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!setSdr(enabled))SetDlgItemTextW(h,8,L"Enhancement is switching right now; retry later.");
        SendDlgItemMessageW(h,12,BM_SETCHECK,readSdr()?BST_CHECKED:BST_UNCHECKED,0);
    }else if(LOWORD(wp)==15&&HIWORD(wp)==BN_CLICKED){
        const bool enabled=SendDlgItemMessageW(h,15,BM_GETCHECK,0,0)==BST_CHECKED;
        if(!setFlip||!setFlip(enabled))SetDlgItemTextW(h,8,L"Enhancement is switching right now; retry later.");
        if(readFlip)SendDlgItemMessageW(h,15,BM_SETCHECK,readFlip()?BST_CHECKED:BST_UNCHECKED,0);
    }else if(LOWORD(wp)==13&&HIWORD(wp)==CBN_SELCHANGE){
        const int mode=int(SendDlgItemMessageW(h,13,CB_GETCURSEL,0,0));
        if(mode>=0&&setAudioIngress&&!setAudioIngress(mode))SetDlgItemTextW(h,8,L"Enhancement or capture is switching right now; retry later.");
        if(readAudioIngress)SendDlgItemMessageW(h,13,CB_SETCURSEL,WPARAM(std::clamp(readAudioIngress(),0,2)),0);
    }else if(LOWORD(wp)==16&&HIWORD(wp)==CBN_SELCHANGE){
        const int mode=int(SendDlgItemMessageW(h,16,CB_GETCURSEL,0,0));
        if(mode>=0&&setBuffer&&!setBuffer(mode))SetDlgItemTextW(h,8,L"Enhancement or capture is switching right now; retry later.");
        if(readBuffer)SendDlgItemMessageW(h,16,CB_SETCURSEL,WPARAM(std::clamp(readBuffer(),0,2)),0);
    }else if(LOWORD(wp)==1&&HIWORD(wp)==CBN_SELCHANGE){
        const int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));rebuildAudioList(h,device);query(device);
    }else if((LOWORD(wp)==2||LOWORD(wp)==3)&&HIWORD(wp)==CBN_SELCHANGE){
        EnableWindow(GetDlgItem(h,4),!busy&&SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0)!=CB_ERR&&SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)!=CB_ERR);
        if(LOWORD(wp)==2)maybeShowFormatHint(h);
    }else if(LOWORD(wp)==5){
        query(-1);
    }else if(LOWORD(wp)==4){
        const int device=int(SendDlgItemMessageW(h,1,CB_GETCURSEL,0,0));
        const int format=int(SendDlgItemMessageW(h,2,CB_GETCURSEL,0,0));
        const int audio=selectedAudio(h,device);
        wchar_t rateText[32]{};GetDlgItemTextW(h,18,rateText,32);double requestedFps=0;
        if(!source::parseCaptureFrameRate(rateText,requestedFps)){SetDlgItemTextW(h,8,L"Enter a number from 1–1000 for the capture frame rate (decimals allowed), or 0 to follow the device default. ");SetFocus(GetDlgItem(h,18));return 0;}
        const bool audioIndexValid=SendDlgItemMessageW(h,3,CB_GETCURSEL,0,0)!=CB_ERR&&(audio<0||size_t(audio)<audioDevices.size());
        if(!busy&&device==queriedDevice&&format>=0&&size_t(format)<formats.size()&&device>=0&&size_t(device)<videoDevices.size()&&audioIndexValid){
            const auto* audioDevice=audio>=0?&audioDevices[size_t(audio)]:nullptr;
            const auto path=source::CaptureCardSource::makeCapturePath(unsigned(device),videoDevices[size_t(device)],formats[size_t(format)].index,audio,audioDevice,selectedColor(h),requestedFps,formats[size_t(format)].key);
            if(!path.empty()){
                auto colors=remembered.deviceColors;colors[videoDevices[size_t(device)].path]=selectedColor(h);
                remembered={videoDevices[size_t(device)].path,formats[size_t(format)].key,audioDevice?audioDevice->path:L"",audioDevice?(audioDevice->wasapi?source::kCaptureAudioWasapi:0):audio,selectedColor(h),remembered.formatHintDismissed,requestedFps,std::move(colors)};
                if(!CapturePreferenceStore(runtime::localDataDirectory()).save(remembered))log::warn("capture","Failed to save capture selection");
                start(path);DestroyWindow(h);
            }
        }
    }
    return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(msg,wp,lp);
case WM_SIZE:arrange();return 0;
case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);auto old=font;font=makeFont(h);EnumChildWindows(h,[](HWND c,LPARAM f)->BOOL{SendMessageW(c,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();return 0;}
case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(h);return 0;}break;
case WM_CLOSE:DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;
    // Do not block window destruction on a device enumeration still running:
    // park the future so its destructor waits on a detached helper instead.
    if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready){std::thread([f=std::move(pending)]()mutable{try{(void)f.get();}catch(...){}}).detach();}
    return 0;
}return DefWindowProcW(h,msg,wp,lp);}
}
void showCapturePanel(HWND parent,std::function<void(const std::wstring&)> callback,std::function<bool()> read,std::function<bool(bool)> write,std::function<int()> readIngress,std::function<bool(int)> writeIngress,std::function<bool()> readFlipped,std::function<bool(bool)> writeFlipped,std::function<int()> readBuffered,std::function<bool(int)> writeBuffered){start=std::move(callback);readSdr=std::move(read);setSdr=std::move(write);readAudioIngress=std::move(readIngress);setAudioIngress=std::move(writeIngress);readFlip=std::move(readFlipped);setFlip=std::move(writeFlipped);readBuffer=std::move(readBuffered);setBuffer=std::move(writeBuffered);if(window){SetForegroundWindow(window);return;}WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraCaptureSetup";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Capture card · connection settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,dip(parent,560),dip(parent,680),parent,nullptr,wc.hInstance,nullptr);}
}
