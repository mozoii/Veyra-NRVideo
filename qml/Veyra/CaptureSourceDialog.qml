// capture dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    required property var legacyPicker
    signal guideRequested()
    id: captureDialog
    property bool onboardingActive: false
    instantTransition: onboardingActive
    objectName: "capture-dialog"
    key: "capture"
    glyph: "video"
    title: qsTr("Capture card")
    sub: qsTr("After connecting the device, turn enhancement off to check the base picture first, then turn on what you need")
    dialogWidth: 660
    foot: qsTr("Close other software using the same capture card")
    actions: [
        { label: captureDialog.live ? qsTr("Disconnect") : qsTr("Cancel") },
        { label: qsTr("Connect and start"), primary: true, icon: "play" }
    ]
    onActionTriggered: label => {
        if (onboardingActive) return
        if (label === qsTr("Disconnect")) { veyra.stopPlayback(); return }
        if (label !== qsTr("Connect and start")) { dialogHost.close(); return }
        if (veyra.startCaptureSession()) dialogHost.close()
    }
    Connections {
        target: dialogHost
        function onDialogChanged() {
            if (dialogHost.dialog !== "capture" || captureDialog.onboardingActive) return
            veyra.refreshCaptureDevices()
        }
    }
    function onboardingTarget(key) {
        const rows = {
            "device": videoDeviceRow,
            "resolution": resolutionRow,
            "capture-rate": captureRateRow,
            "format": pixelFormatRow,
            "input-color": inputColorRow
        }
        return key === "connect" ? { item: primaryAction }
                                 : { item: rows[key], viewport: bodyViewport }
    }
    function labelOf(list, id, fallback) { const f = list.find(x => String(x.id) === String(id)); return f ? f.label : fallback }
    readonly property bool live: veyra.captureSignalLevel !== "idle"
    VButton {
        objectName: "capture-guide-open"
        Layout.alignment: Qt.AlignLeft
        Layout.bottomMargin: 4
        text: qsTr("How do I choose a capture format?")
        onClicked: captureDialog.guideRequested()
    }

    // dialogs.js .cap-prev: the picture and a signal line under it. The
    // picture is the session's own poster still when the engine made one;
    // there is no periodic frame grab here, because that would sit on the
    // live capture path. The status line is live.
    Rectangle {
        objectName: "capture-preview"
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: 180 + 34
        radius: 10
        color: Theme.videoBlack
        border.width: 1
        border.color: Theme.stroke
        clip: true
        Image {
            id: capturePoster
            objectName: "capture-preview-image"
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 1
            height: 180
            fillMode: Image.PreserveAspectCrop
            source: captureDialog.live ? veyra.capturePreviewUrl : ""
            visible: status === Image.Ready
        }
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 90 - height / 2
            spacing: 8
            visible: !capturePoster.visible
            VIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "video"; size: 22; color: Theme.t3 }
            Text {
                text: captureDialog.live ? qsTr("The picture plays in the main window") : qsTr("Shows the picture in the main window once connected")
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 12
            }
        }
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 34
            anchors.leftMargin: 10; anchors.rightMargin: 10
            spacing: 8
            VTag {
                objectName: "capture-signal"
                text: veyra.captureSignalText
                kind: veyra.captureSignalLevel === "ok" ? "ok" : veyra.captureSignalLevel === "warn" ? "warn" : ""
            }
            Text {
                Layout.fillWidth: true
                text: captureDialog.live
                      ? veyra.sourceSummary.replace("x", "×") + " · " + veyra.captureFps.toFixed(2) + " fps"
                        + (veyra.sourceFormatText.split(" · ").length > 2 ? " · " + veyra.sourceFormatText.split(" · ")[1] : "")
                      : (veyra.captureFormatLabel.length ? veyra.captureFormatLabel : qsTr("Choose a device and format, then connect"))
                color: Theme.t2
                font.family: Theme.fontMono
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    DialogSection { text: qsTr("Device") }
    DialogGroup {
        VRow {
            id: videoDeviceRow
            label: qsTr("Video input device")
            hint: veyra.captureQueryBusy ? qsTr("Checking… playback continues") : (veyra.captureDevices.length === 0 ? qsTr("No capture device detected") : "")
            RowLayout {
                spacing: 6
                VSelect {
                    objectName: "capture-device"
                    implicitWidth: 220
                    value: veyra.captureDeviceLabel
                    options: veyra.captureDevices
                    onPicked: id => veyra.captureDeviceId = id
                }
                // M38: the refresh icon becomes a spinner while the query runs.
                Item {
                    implicitWidth: Theme.ctlHeight; implicitHeight: Theme.ctlHeight
                    VButton {
                        objectName: "capture-refresh"
                        anchors.fill: parent
                        visible: !veyra.captureQueryBusy
                        icon: true; ghost: true; iconName: "refresh"; tip: qsTr("Refresh devices")
                        onClicked: veyra.refreshCaptureDevices()
                    }
                    VSpinner { anchors.centerIn: parent; visible: veyra.captureQueryBusy }
                }
            }
        }
        VRow {
            objectName: "capture-legacy-entry"
            label: qsTr("Legacy format selection")
            hint: qsTr("View the full list, including modes hidden by the common frame rate filter")
            VButton {
                objectName: "capture-legacy-open"
                text: qsTr("Open legacy format list")
                enabled: !veyra.captureQueryBusy && veyra.captureDeviceId.length > 0
                onClicked: legacyPicker.open()
            }
        }
        VRow {
            id: resolutionRow
            label: qsTr("Resolution")
            hint: qsTr("Choose a resolution first, then a frame rate and pixel format supported at that resolution.")
            VSelect {
                objectName: "capture-resolution"
                enabled: !veyra.captureQueryBusy
                opacity: enabled ? 1 : 0.5
                implicitWidth: 180
                value: captureDialog.labelOf(veyra.captureResolutionOptions, veyra.captureResolutionId, veyra.captureResolutionOptions.length ? qsTr("Choose a resolution") : "—")
                options: veyra.captureResolutionOptions
                onPicked: id => veyra.captureResolutionId = id
            }
        }
        VRow {
            label: qsTr("Custom resolution")
            hint: qsTr("Only sizes within driver bounds and granularity are accepted; the actual format is checked on connection")
            RowLayout {
                spacing: 6
                enabled: !veyra.captureQueryBusy && veyra.captureDeviceId.length > 0
                DialogTextField { id: customCaptureResolution; objectName: "capture-custom-resolution"; implicitWidth: 140; text: "1920x1080" }
                VButton { text: qsTr("Check and select"); onClicked: veyra.captureResolutionId = customCaptureResolution.text }
            }
        }
        VRow {
            id: captureRateRow
            label: qsTr("Frame rate")
            hint: qsTr("Lists discrete rates and candidates within driver bounds; device FPS 0 uses this selection, verified on connection")
            VSelect {
                objectName: "capture-frame-rate"
                enabled: !veyra.captureQueryBusy && veyra.captureResolutionId.length > 0
                opacity: enabled ? 1 : 0.5
                implicitWidth: 280
                value: captureDialog.labelOf(veyra.captureFrameRateOptions, veyra.captureFrameRateId, veyra.captureFrameRateOptions.length ? qsTr("Choose a frame rate") : "—")
                options: veyra.captureFrameRateOptions
                onPicked: id => veyra.captureFrameRateId = id
            }
        }
        VRow {
            // Some cards list every rate from 1 to 120 fps (logs(18)). The frame-rate menu
            // shows the common rates the card offers; this lists the rest too.
            objectName: "capture-all-rates-row"
            visible: veyra.captureHiddenFrameRates > 0
            label: qsTr("Show all available frame rates")
            hint: qsTr("Only common frame rates are listed by default: 24, 25, 30, 50, 60, 100, 120, 144, 165, 240 (including non-integer rates such as 59.94). This card reports %1 more").arg(veyra.captureHiddenFrameRates)
            VSwitch {
                objectName: "capture-all-rates"
                enabled: !veyra.captureQueryBusy
                opacity: enabled ? 1 : 0.35
                checked: veyra.preferences.captureShowAllFrameRates === true
                onToggled: checked => veyra.setPreference("captureShowAllFrameRates", checked)
            }
        }
        VRow {
            id: pixelFormatRow
            label: qsTr("Pixel format")
            // Never search veyra.captureFormats here: a card can list 15000+ (logs(18)).
            hint: veyra.captureFormatCostHint ? qsTr("This format has one more processing step than the low-latency format; at high resolution / frame rate prefer the low-latency format") : qsTr("Only pixel formats available at the current resolution and frame rate")
            VSelect {
                objectName: "capture-format"
                enabled: !veyra.captureQueryBusy && veyra.captureResolutionId.length > 0 && veyra.captureFrameRateId.length > 0
                opacity: enabled ? 1 : 0.5
                implicitWidth: 220
                value: captureDialog.labelOf(veyra.capturePixelOptions, veyra.capturePixelId, veyra.captureResolutionId.length ? qsTr("Choose a pixel format") : qsTr("Choose a resolution first"))
                options: veyra.capturePixelOptions
                onPicked: id => veyra.capturePixelId = id
            }
        }
        VRow {
            objectName: "capture-driver-settings-row"
            visible: veyra.captureDeviceId.length > 0
            label: qsTr("Driver settings")
            hint: captureDialog.live ? qsTr("Disconnect capture before configuring the driver")
                  : veyra.captureBlackmagicDevice
                    ? (veyra.preferences.blackmagicNativeCapture === true
                       ? qsTr("Select the matching HDMI/SDI connector in the device list. Driver keeps the current driver input. Use Auto on cards supporting signal detection; an unlocked signal is reported explicitly.")
                       : qsTr("Both Blackmagic entries may refer to one card. If DirectShow is black, try native capture below. Manual formats must match the input signal."))
                    : qsTr("Configure the input and video properties exposed by the driver. Close other capture applications using the device first.")
            RowLayout {
                spacing: 6
                VButton {
                    objectName: "capture-driver-properties"
                    text: qsTr("Device properties"); ghost: true
                    enabled: !veyra.captureQueryBusy && !captureDialog.live && !veyra.liveOpeningText.length
                    onClicked: veyra.openCaptureDriverSettings(false)
                }
                VButton {
                    objectName: "capture-driver-input"
                    visible: veyra.captureBlackmagicDevice
                    text: qsTr("Select input"); ghost: true
                    enabled: !veyra.captureQueryBusy && !captureDialog.live && !veyra.liveOpeningText.length
                    onClicked: veyra.openCaptureDriverSettings(true)
                }
            }
        }
        VRow {
            objectName: "capture-blackmagic-native-row"
            label: qsTr("Blackmagic native capture")
            hint: qsTr("Off by default. On uses DeckLink; off uses DirectShow. Requires Desktop Video. Reselect the device and format, then reconnect. Driver preferences are not changed.")
            VSwitch {
                objectName: "capture-blackmagic-native"
                enabled: !veyra.captureQueryBusy && (veyra.captureBlackmagicDevice || veyra.preferences.blackmagicNativeCapture === true)
                opacity: enabled ? 1 : 0.35
                checked: veyra.preferences.blackmagicNativeCapture === true
                onToggled: checked => veyra.setPreference("blackmagicNativeCapture", checked)
            }
        }
        VRow {
            objectName: "capture-magewell-row"
            label: qsTr("Magewell low-latency mode")
            hint: !veyra.captureMagewellDevice
                  ? qsTr("Magewell Pro Capture (PCIe) only; the USB Capture series is not supported")
                  : (veyra.captureMagewellStatus.length > 0 ? veyra.captureMagewellStatus + " · " : "")
                    + qsTr("Transfer starts before the full frame arrives. Sustained drops switch back to normal capture in the same format. Reconnect to apply.")
            VSwitch {
                objectName: "capture-magewell"
                enabled: veyra.captureMagewellDevice
                opacity: enabled ? 1 : 0.35
                checked: veyra.captureMagewellDevice && veyra.preferences.magewellLowLatency === true
                onToggled: checked => veyra.setPreference("magewellLowLatency", checked)
            }
        }
        VRow {
            label: qsTr("Device frame rate (FPS)")
            hint: qsTr("0 = use the selected format; a device that returns another frame rate reports an error instead of frames being dropped silently")
            DialogTextField {
                objectName: "capture-fps"
                implicitWidth: 100
                text: String(veyra.captureRequestedFps)
                onEdited: text => veyra.captureRequestedFps = Number(text)
                // Take a valid rate as it is typed. Only a canonical number ("59.94", not
                // "59.") so the field's binding never rewrites what is still being typed.
                onTyped: text => {
                    const n = Number(text.trim())
                    if (text.trim().length > 0 && String(n) === text.trim() && (n === 0 || (n >= 1 && n <= 1000)))
                        veyra.captureRequestedFps = n
                }
            }
        }
    }
    DialogSection { text: qsTr("Audio") }
    DialogGroup {
        VRow {
            label: qsTr("Audio input")
            hint: qsTr("Select an audio input to record capture-card sound. Local monitoring can be turned off independently.")
            VSelect {
                objectName: "capture-audio"
                implicitWidth: 260
                value: captureDialog.labelOf(veyra.captureAudioInputs, veyra.captureAudioChoice, qsTr("No audio input"))
                options: veyra.captureAudioInputs
                onPicked: id => veyra.captureAudioChoice = Number(id)
            }
        }
        VRow {
            label: qsTr("Local audio monitoring")
            hint: qsTr("When off, recording and replay still receive audio; capture sound is not played locally.")
            VSwitch {
                objectName: "capture-audio-monitor"
                checked: veyra.preferences.captureAudioMonitor !== false
                onToggled: v => veyra.setPreference("captureAudioMonitor", v)
            }
        }
        VRow {
            label: qsTr("Veyra Virtual Speaker")
            hint: veyra.captureVirtualAudioBusy ? qsTr("Working with VB-CABLE Setup…")
                : veyra.captureVirtualAudioInstalled ? qsTr("VB-CABLE detected · Receives Windows monitoring audio")
                : qsTr("Optional · For capture cards with silent direct audio")
            RowLayout {
                spacing: 6
                VButton {
                    objectName: "capture-virtual-audio-setup"
                    text: veyra.captureVirtualAudioInstalled ? qsTr("Installed") : qsTr("Install…")
                    ghost: true
                    enabled: !veyra.captureVirtualAudioBusy && !veyra.captureVirtualAudioInstalled
                    onClicked: virtualAudioGuide.showFor(1)
                }
                VButton {
                    objectName: "capture-virtual-audio-instructions"
                    text: qsTr("Instructions")
                    ghost: true
                    enabled: !veyra.captureVirtualAudioBusy
                    onClicked: virtualAudioGuide.showFor(0)
                }
                VButton {
                    objectName: "capture-virtual-audio-remove"
                    visible: veyra.captureVirtualAudioInstalled
                    text: qsTr("Uninstall…")
                    ghost: true
                    enabled: !veyra.captureVirtualAudioBusy
                    onClicked: virtualAudioGuide.showFor(2)
                }
            }
        }
        VRow {
            label: qsTr("Dolby / DTS bitstream")
            VSeg {
                objectName: "capture-audio-ingress"
                options: [{ id: "0", label: qsTr("Auto") }, { id: "1", label: qsTr("Force PCM") }, { id: "2", label: qsTr("Bitstream first") }]
                current: String(veyra.captureAudioIngress)
                onPicked: id => veyra.captureAudioIngress = Number(id)
            }
        }
    }
    VConfirm {
        id: virtualAudioGuide
        parent: captureDialog.dialogHost
        objectName: "capture-virtual-audio-confirm"
        cardWidth: 510
        property int operation: 0
        property bool disconnectRequested: false
        readonly property bool removalBlocked: shown && operation === 2 && veyra.captureVirtualAudioRemovalBlocked
        function showFor(value) {
            operation = value
            disconnectRequested = false
            open()
        }
        title: operation === 2 ? qsTr("Uninstall Veyra Virtual Speaker")
            : operation === 1 ? qsTr("Install Veyra Virtual Speaker") : qsTr("Veyra Virtual Speaker instructions")
        text: operation === 2
            ? qsTr("First turn off Listen to this device for the capture card in Windows, or restore its previous playback target. After confirming, choose Remove Driver in VB-Audio Setup and restart if prompted. Other software using VB-CABLE will lose this audio route.")
            : qsTr("This feature uses VB-CABLE by VB-Audio (donationware). Install it offline from the bundled package. First-time installation requires administrator approval. Restart Windows if prompted.\n\n1. In Windows Recording settings, open the capture card’s audio properties, enable Listen to this device, and select CABLE Input. If you renamed it, select the new name.")
        acceptText: operation === 2 ? qsTr("Confirm uninstall") : operation === 1 ? qsTr("Confirm installation") : qsTr("Done")
        acceptEnabled: !veyra.captureVirtualAudioBusy && !removalBlocked
        onAccepted: { if (operation !== 0) veyra.setupCaptureVirtualAudio(operation === 2) }
        Connections {
            target: captureDialog.dialogHost
            function onDialogChanged() { if (!captureDialog.shown) virtualAudioGuide.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            VButton {
                objectName: "capture-virtual-audio-windows-listen"
                text: qsTr("Open Windows recording settings")
                ghost: true
                onClicked: veyra.openCaptureWindowsListen()
            }
            VButton {
                text: qsTr("VB-Audio website")
                ghost: true
                onClicked: veyra.openCaptureVirtualAudioVendor()
            }
        }
        RowLayout {
            visible: virtualAudioGuide.removalBlocked
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Disconnect capture and wait for the connection to close.")
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                wrapMode: Text.WordWrap
            }
            VButton {
                objectName: "capture-virtual-audio-disconnect"
                visible: captureDialog.live
                text: qsTr("Disconnect capture")
                ghost: true
                enabled: !virtualAudioGuide.disconnectRequested
                onClicked: {
                    virtualAudioGuide.disconnectRequested = true
                    veyra.stopPlayback()
                }
            }
        }
        Text {
            objectName: "capture-virtual-audio-follow-up"
            visible: virtualAudioGuide.operation !== 2
            Layout.fillWidth: true
            text: qsTr("2. In Veyra, select Veyra virtual speaker under Audio input, then connect capture. Select real headphones or speakers as the audio output to use A/V sync.\n\nOptional: rename CABLE Input to Veyra capture audio in Windows Sound → Playback → Properties → General.")
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }
    }
    DialogSection { text: qsTr("Format and picture") }
    DialogGroup {
        VRow {
            id: inputColorRow
            label: qsTr("Input color (SDR / HDR)")
            hint: qsTr("If unsure, choose Auto. To capture HDR: pick P010 as the capture format and HDR · PQ here (HDR10 from consoles and GPUs is PQ). HDR · HLG is for broadcast signals. SDR · 709 is a normal picture. Changes need a reconnect")
            VSeg {
                objectName: "capture-color-space"
                options: [{ id: "0", label: qsTr("Auto") }, { id: "1", label: "HDR · PQ" }, { id: "2", label: "HDR · HLG" }, { id: "3", label: "SDR · 709" }]
                current: String(veyra.captureColorSpace)
                onPicked: id => veyra.captureColorSpace = Number(id)
            }
        }
        VRow {
            label: qsTr("Input range")
            hint: qsTr("Limited = 16–235, Full = 0–255. If the picture looks washed out choose Limited; if shadows crush to black and highlights blow out choose Full")
            VSeg {
                objectName: "capture-color-range"
                options: [{ id: "0", label: qsTr("Auto") }, { id: "1", label: qsTr("Limited") }, { id: "2", label: qsTr("Full") }]
                current: String(veyra.captureColorRange)
                onPicked: id => veyra.captureColorRange = Number(id)
            }
        }
        VRow {
            label: qsTr("Device buffers")
            hint: qsTr("Auto: 2 frames at 1080p and below, 3 above; 1 frame has the lowest latency but may drop frames")
            VSeg {
                objectName: "capture-buffer-mode"
                options: [{ id: "0", label: qsTr("Auto") }, { id: "1", label: qsTr("Minimum") }, { id: "2", label: qsTr("Driver default") }]
                current: String(veyra.captureBufferMode)
                onPicked: id => veyra.captureBufferMode = Number(id)
            }
        }
        VRow {
            label: qsTr("Show as SDR")
            hint: qsTr("Preview HDR as SDR too; takes effect immediately")
            VSwitch {
                objectName: "capture-force-sdr"
                checked: veyra.captureForceSdr
                onToggled: checked => veyra.captureForceSdr = checked
            }
        }
        VRow {
            label: qsTr("Flip vertically")
            hint: qsTr("Turn on when the captured picture is upside down; takes effect immediately")
            VSwitch {
                objectName: "capture-flip-vertical"
                checked: veyra.captureFlipVertical
                onToggled: checked => veyra.captureFlipVertical = checked
            }
        }
    }
    DialogNote { visible: veyra.captureStatus.length > 0; text: veyra.captureStatus }
}
