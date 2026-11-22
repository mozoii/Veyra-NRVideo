#pragma once

namespace veyra::ui {
// How the interface draws, decided in main.cpp before Qt starts (2026-10-26,
// docs/SOFTWARE_UI_DEFAULT_PLAN_2026-10-26.md). Software is the default: the
// interface then avoids a GPU scene-graph swapchain/device. Video rendering
// still uses the GPU; software drawing is not a guarantee against video-driver
// faults or all overlay interactions. Settings → interface rendering = GPU
// draws it with the GPU again. The compatibility switch keeps its meaning:
// an overlay seen at start (overlay compatibility = auto) still draws in
// software when GPU is chosen, and VEYRA_UI_RHI still picks a GPU renderer.
struct InterfaceRendererInputs {
    bool gpuChosen=false;        // Settings → interface rendering = GPU
    bool overlayCompatAuto=true; // Settings → overlay compatibility = auto
    bool overlayRunning=false;   // RTSS / NVIDIA overlay / GamePP / TikTok LIVE companion at start
    bool compatRestart=false;    // --overlay-compat-restart: an accepted compatibility restart
    bool rendererForced=false;   // VEYRA_UI_RHI
};
enum class InterfaceRendererReason { Gpu, Default, OverlayCompat };
struct InterfaceRendererDecision {
    bool software=false;
    bool overlayCompat=false;    // the overlay compatibility rule applied (logged, shown in Settings)
    InterfaceRendererReason reason=InterfaceRendererReason::Gpu;
};
constexpr InterfaceRendererDecision decideInterfaceRenderer(const InterfaceRendererInputs& in) {
    InterfaceRendererDecision d;
    d.overlayCompat=in.overlayCompatAuto&&(in.overlayRunning||in.compatRestart)&&!in.rendererForced;
    if(d.overlayCompat){d.software=true;d.reason=InterfaceRendererReason::OverlayCompat;}
    else if(!in.gpuChosen&&!in.rendererForced){d.software=true;d.reason=InterfaceRendererReason::Default;}
    return d;
}
}
