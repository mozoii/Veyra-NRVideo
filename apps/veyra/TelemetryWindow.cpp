#include "TelemetryWindow.h"
#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include <shellapi.h>
#include <filesystem>
#include "ui/Theme.h"
#include <sstream>
#include <array>
#include <iomanip>
namespace veyra::ui {
namespace {
unsigned windowDpi=96;HWND window=nullptr;engine::EngineController* engine=nullptr;HFONT font=nullptr;std::wstring preview;
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
std::wstring value(std::optional<double> v){if(!v)return L"Could not be measured";std::wostringstream o;o<<std::fixed<<std::setprecision(3)<<*v;return o.str();}
void refresh(){auto s=engine->snapshot();static constexpr const wchar_t* names[]={L"Upload / color conversion",L"SR",L"Optical flow GPU queue interval (including sync)",L"NR",L"NR change compositing",L"FG subframe 1",L"FG subframe 2",L"FG subframe 3",L"FG subframe 4",L"FG subframe 5",L"FG subframe 6",L"FG subframe 7",L"FG batch",L"Final blit",L"RTX Video HDR",L"NR node 1",L"NR node 2",L"NR node 3",L"NR node 4",L"Color node 1",L"Color node 2",L"Color node 3",L"Color node 4",L"Color node 5",L"Color node 6",L"Anti-aliasing node 1",L"Anti-aliasing node 2",L"Beauty node 1",L"Beauty node 2",L"Beauty node 3",L"Beauty node 4",L"Beauty node 5",L"Beauty node 6",L"Beauty face detection"};static_assert(std::size(names)==size_t(diagnostics::GpuStage::Count),"one name per GPU stage");std::wostringstream o;
    o<<L"GPU timestamp (ms); not executed counts as 0. Sample frames "<<s.metrics.identity.sourceFrameId<<L" / epoch "<<s.metrics.identity.epoch<<L" / settings version "<<s.metrics.identity.settingsRevision<<L"\r\n";
    for(size_t i=0;i<s.metrics.gpu.size();++i){const auto& g=s.metrics.gpu[i];o<<names[i]<<L":"<<(g.state==diagnostics::SampleState::NotExecuted?L"Not executed":g.state==diagnostics::SampleState::Pending?L"Waiting for GPU":value(g.milliseconds))<<L"\r\n";}
    o<<L"r\nCPU and scheduling (ms, independent of GPU): frame pickup "<<value(s.metrics.decodeCpuMs)<<L" / thumbnail submit "<<value(s.metrics.submitCpuMs)<<L" / GPU-ready wait "<<value(s.metrics.gpuWaitCpuMs)<<L"r\nDeadline wait "<<value(s.metrics.deadlineWaitCpuMs)<<L" / Present call "<<value(s.metrics.presentCpuMs)<<L"r\nSource frames "<<s.metrics.sourceFrames<<L" / valid generated "<<s.metrics.validGenerated<<L" / actual submit "<<s.metrics.submitted<<L" / expired frame generation "<<s.metrics.expired<<L"r\nCurrent batch capacity "<<s.metrics.queueWatermark<<L"(max 4); capture entry capacity 1; capture dropped "<<s.captureDropped<<L"r\nActual submission rate (last second observed):"<<value(s.submissionFps)<<L"fps; actual display scan rate / photon delay: not measured\n";
    const auto flowName=s.applied.flow==engine::FlowQuality::Performance?L"Performance":s.applied.flow==engine::FlowQuality::Balanced?L"Balance":L"Quality";
    const auto& f=s.metrics.flow;const auto& c=f.counters;
    o<<L"r\nLast-second link stats: mean / P95 ms, n = sample count\n";
    for(size_t i=0;i<f.gpuTiming.size();++i){const auto& a=f.gpuTiming[i];o<<names[i]<<L":"<<value(a.mean)<<L" / "<<value(a.p95)<<L" n="<<a.samples<<L"\r\n";}
    o<<L"r\nCurrent stats window: session "<<f.latest.sessionId<<L" / settings "<<f.latest.frame.settingsRevision<<L" / epoch "<<f.latest.frame.epoch
     <<L"r\nValid generated "<<value(f.validGeneratedFps)<<L" fps / Present submit "<<value(f.presentSubmitFps)<<L" fps"
     <<L"r\nActual source frame completion "<<value(f.sourceCompletedFps)<<L" fps / total processed output "<<value(f.outputCompletedFps)<<L" fps"
     <<L"r\nXeSS SDK presented "<<value(f.xessSdkSubmitFps)<<L" fps (must not be mixed with processed output)"
     <<L"r\nSoftware front-to-back latency mean "<<value(f.softwareLatencyMs)<<L" / P95 "<<value(f.softwareLatencyP95Ms)<<L" ms; valid samples "<<f.latencySamples
     <<L"r\nXeSS SDK generated submit "<<c.xessSdkGenerated<<L"(not equal to display scan frames)"
     <<L"r\nFrame generation candidates "<<c.fgCandidate<<L" / skipped before submit "<<c.fgSkippedBeforeEval<<L" / executed "<<c.fgEvaluated<<L" / warmup "<<c.fgWarmup
     <<L"r\nValid generated "<<c.fgReadyValid<<L" / invalid "<<c.fgInvalid<<L" / presented "<<c.generatedPresented<<L" / expired "<<c.generatedExpiredAfterEval
     <<L"r\nReal-frame present "<<c.realPresented<<L" / cancelled present "<<c.cancelledBeforePresent<<L" / sample skipped "<<c.sourceSkippedBeforeGraph
     <<L"r\nCommand slot usage "<<c.commandSlotsInFlight<<L" / 6; observed peak "<<c.commandSlotHighWater<<L"; presentation batch peak "<<c.presentationBatchHighWater<<L" / 2"
     <<L"r\nSlot reuse wait "<<f.slotReuseWaitCount<<L" times / "<<value(f.slotReuseWaitMs)<<L" ms\r\n";
    o<<L"Optical flow requested: "<<flowName<<L"; actual SDK perf="<<s.flowPerf<<L"; grid=4 (SDK capability verified)r\nContent cadence: "<<(s.contentFps?std::to_wstring(s.contentFps)+L"fps":L"Unconfirmed (static or insufficient evidence)")<<L"; source timestamps kept, duplicate content does not count as valid\n"<<s.status;
    // Preserve the reader's scroll position and selection across the
    // 250 ms refresh (the text differs almost every tick).
    HWND edit=GetDlgItem(window,1);
    const auto firstLine=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);
    DWORD selStart=0,selEnd=0;SendMessageW(edit,EM_GETSEL,WPARAM(&selStart),LPARAM(&selEnd));
    const auto text=o.str();
    wchar_t old[8]{};GetWindowTextW(edit,old,8);
    SetWindowTextW(edit,text.c_str());
    if(selEnd>selStart)SendMessageW(edit,EM_SETSEL,selStart,selEnd);
    const auto nowFirst=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);
    if(firstLine!=nowFirst)SendMessageW(edit,EM_LINESCROLL,0,firstLine-nowFirst);
}
void arrange(){RECT r{};GetClientRect(window,&r);int width=MulDiv(r.right,96,veyra::ui::layoutDpi(window)),height=MulDiv(r.bottom,96,veyra::ui::layoutDpi(window));auto pos=[&](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(window,id),dip(window,x),dip(window,y),dip(window,std::max(1,w)),dip(window,std::max(1,h)),TRUE);};bool wide=width>=700;pos(6,width-56,8,40,32);pos(7,16,10,width-80,32);pos(1,16,52,wide?width/2-24:width-32,wide?height-116:height/2-68);pos(2,wide?width/2+8:16,wide?96:height/2+36,wide?width/2-24:width-32,wide?height-160:height/2-100);pos(3,wide?width/2+8:16,wide?52:height/2-8,wide?width/2-24:width-32,36);pos(4,16,height-52,width/2-24,36);pos(5,width/2+8,height-52,width/2-24,36);}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){case WM_CREATE:{preview.clear();window=h;font=makeFont(h,13);auto add=[&](const wchar_t* c,const wchar_t* t,int id,DWORD style){auto child=CreateWindowExW(0,c,t,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);themeControl(child);};
    add(L"EDIT",L"",1,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);add(L"BUTTON",L"View / refresh sanitized preview",3,BS_PUSHBUTTON|WS_TABSTOP);add(L"EDIT",L"Click the button above to view diagnostics. Copy only copies the sanitized text previewed here.",2,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);add(L"BUTTON",L"Copy previewed content",4,BS_PUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"Open the app log directory",5,BS_PUSHBUTTON|WS_TABSTOP);add(L"BUTTON",L"Close diagnostics",6,BS_PUSHBUTTON|WS_TABSTOP);icon(GetDlgItem(h,6),Icon::Close);add(L"STATIC",L"Performance and diagnostics  /  playback session",7,0);SetTimer(h,1,250,nullptr);arrange();refresh();return 0;}
case WM_ERASEBKGND:return 1;
case WM_PAINT:{PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);return 0;}
case WM_SIZE:arrange();return 0;
case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:return colors(m,w,l);

case WM_TIMER:if(!IsWindowVisible(h))return 0;refresh();return 0;
case WM_COMMAND:if(LOWORD(w)==6){SendMessageW(GetParent(h),WM_APP+43,0,0);}else if(LOWORD(w)==3){preview=wide(Logger::instance().diagnosticReport());SetDlgItemTextW(h,2,preview.c_str());}else if(LOWORD(w)==4&&!preview.empty()){
    if(OpenClipboard(h)){HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(preview.size()+1)*sizeof(wchar_t));if(data){if(void* p=GlobalLock(data)){memcpy(p,preview.c_str(),(preview.size()+1)*sizeof(wchar_t));GlobalUnlock(data);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data))GlobalFree(data);}else GlobalFree(data);}CloseClipboard();}
}else if(LOWORD(w)==5)ShellExecuteW(h,L"open",runtime::logsDirectory().c_str(),nullptr,nullptr,SW_SHOW);return 0;

case WM_CLOSE:DestroyWindow(h);return 0;case WM_DESTROY:KillTimer(h,1);DeleteObject(font);window=nullptr;return 0;}return DefWindowProcW(h,m,w,l);}
}
HWND createTelemetryPanel(HWND parent,engine::EngineController& controller){engine=&controller;WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraTelemetry";wc.hbrBackground=panelBrush();wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);return CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Performance and diagnostics center",WS_CHILD|WS_CLIPCHILDREN,0,0,800,620,parent,nullptr,wc.hInstance,nullptr);}
void telemetryDpi(){if(!window)return;auto old=font;font=makeFont(window,13);EnumChildWindows(window,[](HWND child,LPARAM f)->BOOL{SendMessageW(child,WM_SETFONT,WPARAM(f),TRUE);return TRUE;},LPARAM(font));DeleteObject(old);arrange();}
}
