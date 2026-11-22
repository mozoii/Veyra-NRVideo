// screen dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: screenDialog
    objectName: "screen-dialog"
    key: "screen"
    glyph: "monitor"
    title: qsTr("Screen capture")
    sub: qsTr("Use a window or a whole display as the source")
    dialogWidth: 700
    actions: [
        { label: qsTr("Cancel") },
        { label: qsTr("Start capture"), primary: true }
    ]
    onActionTriggered: label => {
        if (label !== qsTr("Start capture")) { dialogHost.close(); return }
        if (veyra.startScreenCapture()) { dialogHost.close(); dialogHost.startScreen() }
    }
    Connections {
        target: dialogHost
        function onDialogChanged() { if (dialogHost.dialog === "screen") veyra.refreshCaptureTargets() }
    }
    readonly property var o: veyra.screenOptions
    // The chosen window is on another Windows virtual desktop (Win+Ctrl+D).
    readonly property bool otherDesktopTarget: {
        const targets = veyra.screenTargets, id = veyra.screenTargetId
        for (let i = 0; i < targets.length; ++i)
            if (targets[i].id === id) return targets[i].otherDesktop === true
        return false
    }
    // The chosen target's display is connected to another GPU than the one Veyra
    // processes on, so Windows copies every frame between the two (issue #107).
    readonly property var otherGpuTarget: {
        const targets = veyra.screenTargets, id = veyra.screenTargetId
        for (let i = 0; i < targets.length; ++i)
            if (targets[i].id === id) return targets[i].otherGpu === true ? targets[i] : null
        return null
    }

    DialogSection { text: qsTr("Target") }
    DialogGroup {
        VRow {
            label: qsTr("Type")
            VSeg {
                objectName: "screen-kind"
                options: [{ id: "0", label: qsTr("Window") }, { id: "1", label: qsTr("Display") }]
                current: String(screenDialog.o.kind)
                onPicked: id => veyra.setScreenOption("kind", Number(id))
            }
        }
        VRow {
            label: qsTr("Capture target")
            hint: {
                if (veyra.screenTargets.length === 0) return qsTr("No window or display found to capture")
                const lines = []
                if (screenDialog.otherDesktopTarget) lines.push(qsTr("This window is on another virtual desktop and can be captured without switching to it; some programs stop drawing while their desktop is not shown"))
                const gpu = screenDialog.otherGpuTarget
                if (gpu) lines.push(qsTr("The display this target is on is connected to %1, while Veyra processes on %2: Windows copies every frame between the two GPUs, which adds latency. When two GPUs share the work, connect the display to the processing GPU (on a laptop, turn on the discrete-GPU-only (MUX) mode)").arg(gpu.displayGpu).arg(gpu.processingGpu))
                return lines.join("\n")
            }
            RowLayout {
                spacing: 6
                VSelect {
                    objectName: "screen-target"
                    implicitWidth: 300
                    value: veyra.screenTargetLabel
                    options: veyra.screenTargets
                    onPicked: id => veyra.screenTargetId = id
                }
                VButton { text: qsTr("Refresh"); ghost: true; onClicked: veyra.refreshCaptureTargets() }
            }
        }
    }
    DialogSection { text: qsTr("Game") }
    DialogGroup {
        VRow {
            label: qsTr("Game mode")
            hint: screenDialog.o.kind === 0 && screenDialog.otherDesktopTarget && screenDialog.o.passthrough === true
                  ? qsTr("Game mode needs the window on the same virtual desktop as Veyra: move the window to the current desktop, or turn game mode off and capture it directly")
                  : screenDialog.o.kind === 1
                  ? qsTr("Veyra opens fullscreen on this display. Keyboard, mouse and controller operate its programs; %1 adjusts the picture and %2 exits.").arg(veyra.shortcuts.gamePanel).arg(veyra.shortcuts.gameExit)
                  : qsTr("Original-window alignment supports precise mouse passthrough. Fill/stretch supports keyboard and gamepad; mouse clicks are blocked to prevent misalignment. %1 adjusts the picture; %2 exits. Use borderless instead of exclusive fullscreen.").arg(veyra.shortcuts.gamePanel).arg(veyra.shortcuts.gameExit)
            VSwitch {
                objectName: "screen-game-mode"
                checked: screenDialog.o.passthrough === true
                onToggled: checked => veyra.setScreenOption("passthrough", checked)
            }
        }
    }
    DialogSection { text: qsTr("Capture") }
    DialogGroup {
        VRow {
            label: qsTr("Capture method")
            hint: screenDialog.o.kind === 1 ? qsTr("DXGI is the display-compatible mode, without the mouse pointer. A display capture shows no yellow border, and Veyra itself stays out of screenshots and recordings") : qsTr("Windows can only be captured with Windows Graphics Capture")
            VSeg {
                enabled: screenDialog.o.kind === 1
                opacity: enabled ? 1 : 0.5
                options: [{ id: "0", label: "WGC" }, { id: "1", label: "DXGI" }]
                current: String(screenDialog.o.method)
                onPicked: id => veyra.setScreenOption("method", Number(id))
            }
        }
        VRow {
            label: qsTr("Frame rate cap")
            hint: qsTr("If you turn on DLSS5 (NR), set the frame rate limit to 60: DLSS5 then has fewer frames to process and less load")
            hintColor: Theme.err
            hintBelow: true
            VSeg {
                options: [{ id: "0", label: qsTr("Follow") }, { id: "1", label: "30" }, { id: "2", label: "60" }, { id: "3", label: "120" }, { id: "4", label: "144" }, { id: "5", label: "240" }]
                current: String(screenDialog.o.fps)
                onPicked: id => veyra.setScreenOption("fps", Number(id))
            }
        }
        VRow {
            label: qsTr("Show mouse pointer")
            hint: screenDialog.o.passthrough === true ? qsTr("Game mode uses the real pointer, so the picture does not draw a second one") : ""
            VSwitch {
                enabled: screenDialog.o.method === 0 && screenDialog.o.passthrough !== true
                opacity: enabled ? 1 : 0.5
                checked: screenDialog.o.cursor === true && screenDialog.o.passthrough !== true
                onToggled: checked => veyra.setScreenOption("cursor", checked)
            }
        }
        VRow {
            label: qsTr("Crop (pixels)")
            hint: screenDialog.o.passthrough === true ? qsTr("Game mode does not crop the source. Original-window alignment preserves exact mouse positions.") : qsTr("Left / top / right / bottom, in source pixels")
            RowLayout {
                spacing: 4
                enabled: screenDialog.o.passthrough !== true
                opacity: enabled ? 1 : 0.5
                Repeater {
                    model: ["left", "top", "right", "bottom"]
                    delegate: DialogTextField {
                        required property string modelData
                        implicitWidth: 58
                        text: String(screenDialog.o[modelData])
                        onEdited: text => veyra.setScreenOption(modelData, Number(text))
                    }
                }
            }
        }
        VRow {
            label: qsTr("Visual crop")
            VButton {
                objectName: "screen-crop-preview"
                text: qsTr("Preview and drag")
                enabled: screenDialog.o.passthrough !== true && veyra.screenTargetId.length > 0
                onClicked: {
                    if (!veyra.startScreenCapture(true)) return
                    dialogHost.close()
                    dialogHost.startScreenCrop()
                }
            }
        }
        VRow {
            label: qsTr("Picture")
            hint: screenDialog.o.passthrough === true ? qsTr("Fill screen supports keyboard and gamepad. Choose original-window alignment for precise mouse passthrough.") : ""
            VSeg {
                options: [{ id: "fit", label: screenDialog.o.passthrough === true ? qsTr("Align to source window") : qsTr("Fit to window") },
                          { id: "fill", label: screenDialog.o.passthrough === true ? qsTr("Fill screen") : qsTr("Fill the window") }]
                current: screenDialog.o.fill ? "fill" : "fit"
                onPicked: id => veyra.setScreenOption("fill", id === "fill")
            }
        }
    }
}
