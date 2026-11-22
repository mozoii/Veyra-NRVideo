// Settings, rebuilt from the design (pages-b.js PAGES.set + pages.css .set).
//
// Design: a 220px nav column (Appearance / Startup and window / Playback / Audio / Performance / Compatibility /
// Shortcuts / Components and licences / About / Sponsors; split 2026-10-05 because one page held
// almost every setting) and a large body.
//
// Rows that would need engine support this build does not have say so, rather than
// being drawn as switches that would do nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)

    property string section: "look"
    property bool hdrAdvancedExpanded: false
    // The shortcut row waiting for a new key ("" = none).
    property string capturingKey: ""
    onSectionChanged: {
        capturingKey = ""
        if (settingsBodyScroll) settingsBodyScroll.contentY = settingsBodyScroll.originY
        // M32: the section's groups redraw one after another (pages-b.js
        // render(true): 480 ms, i*40 ms, y 12).
        Qt.callLater(() => stagger(sectionColumns[section] || null))
    }
    readonly property var sectionColumns: ({ look: secLook, window: secWindow, play: secPlay, audio: secAudio, perf: secPerf, compat: secCompat, keys: secKeys, comp: secComp, about: secAbout, sponsor: secSponsor })
    readonly property var portableUpdate: veyra.updates.portable
    Component { id: staggerComp; VRise.Stagger {} }
    function stagger(col) {
        if (Theme.reduced || !col) return
        let i = 0
        for (let k = 0; k < col.children.length; ++k) {
            const c = col.children[k]
            if (!c.visible || c.motionDy === undefined) continue
            staggerComp.createObject(root, { target: c, delay: i * 40, span: 480, dy: 12 }).start()
            ++i
        }
    }

    readonly property var sections: [
        { id: "look",   label: qsTr("Appearance"), icon: "palette" },
        { id: "window", label: qsTr("Startup & window"), icon: "appwin" },
        { id: "play",   label: qsTr("Playback", "section"), icon: "play" },
        { id: "audio",  label: qsTr("Audio"), icon: "vol" },
        { id: "perf",   label: qsTr("Performance"), icon: "gauge" },
        { id: "compat", label: qsTr("Compatible"), icon: "monitor" },
        { id: "keys",  label: qsTr("Shortcuts"), icon: "zap" },
        { id: "comp",  label: qsTr("Components and licenses"), icon: "box" },
        { id: "about", label: qsTr("About"), icon: "info" },
        // The one entry with a coloured icon, so it can be found.
        { id: "sponsor", label: qsTr("Support the author"), icon: "heart", tint: "#FF6B81" }
    ]

    // --- nav ---------------------------------------------------------------
    Rectangle {
        id: navCard
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 220
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke

        Flickable {
            id: navScroll
            objectName: "settings-navigation"
            anchors.fill: parent
            anchors.margins: 10
            contentWidth: width
            contentHeight: navColumn.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: VScrollBar { }
        }
        ColumnLayout {
            id: navColumn
            parent: navScroll.contentItem
            width: navScroll.width - (navScroll.contentHeight > navScroll.height ? 8 : 0)
            spacing: 2
            VH2 { text: qsTr("Settings"); Layout.margins: 8 }
            Item { implicitHeight: 6 }
            Repeater {
                model: root.sections
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 36
                    radius: 9
                    color: root.section === modelData.id ? Theme.card3
                         : navHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                    // .setnav button: icon, 10px, label.
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        VIcon {
                            name: modelData.icon
                            size: 15
                            color: modelData.tint !== undefined ? modelData.tint : navLabel.color
                        }
                        Text {
                            id: navLabel
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            text: modelData.label
                            color: root.section === modelData.id ? Theme.t1 : Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                    ToolTip.visible: navHover.hovered && navLabel.truncated
                    ToolTip.text: navLabel.text
                    ToolTip.delay: 500
                    HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.section = modelData.id }
                }
            }
        }
    }

    // --- body --------------------------------------------------------------
    Rectangle {
        id: bodyCard
        anchors.left: parent.left
        anchors.leftMargin: 14 + 220 + 10
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        clip: true

        Flickable {
            id: settingsBodyScroll
            anchors.fill: parent
            contentHeight: body.implicitHeight + 44
            clip: true
            ScrollBar.vertical: VScrollBar { }

            ColumnLayout {
                id: body
                x: 26
                y: 22
                // .setbody section { max-width: 720px }
                width: Math.min(parent.width - 52, 720)
                spacing: 12

                // --- Appearance ---
                ColumnLayout {
                    id: secLook
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "look"
                    VH1 { text: qsTr("Appearance") }
                    Text {
                        text: qsTr("The interface can be adjusted at any time without affecting playback or enhancement settings.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        // Dusk (the default) or the original black-grey Classic palette.
                        VRow {
                            label: qsTr("Color scheme")
                            hint: qsTr("Dusk: deep indigo-violet with a sunset gradient accent; Classic: the original black and grey")
                            VSeg {
                                objectName: "set-palette"
                                options: [{ id: "dusk", label: qsTr("Dusk") }, { id: "classic", label: qsTr("Classic") }]
                                current: veyra.preferences.palette === "classic" ? "classic" : "dusk"
                                onPicked: id => veyra.setPreference("palette", id)
                            }
                        }
                        // The dusk palette's intro (DuskIntro.qml); classic has none.
                        VRow {
                            visible: Theme.dusk
                            label: qsTr("Startup animation")
                            hint: qsTr("Plays when Veyra starts in the Dusk color scheme; click or press any key to skip")
                            VSwitch {
                                objectName: "set-intro"
                                checked: veyra.preferences.introAnimation !== false
                                onToggled: checked => veyra.setPreference("introAnimation", checked)
                            }
                        }
                        // This saved switch controls future launches. The corner button
                        // stops only the current launch and leaves this preference intact.
                        VRow {
                            label: qsTr("Startup music")
                            hint: typeof vyMusic !== "undefined" && vyMusic.available
                                  ? qsTr("Plays once per launch, continuing after the intro. This setting is saved; the corner button stops only this launch.")
                                  : qsTr("Music file not found: media/startup-music.mp3 in the program folder")
                            VSwitch {
                                objectName: "set-intro-music"
                                checked: veyra.preferences.introMusic !== false
                                onToggled: checked => {
                                    veyra.setPreference("introMusic", checked)
                                    if (!checked && typeof vyMusic !== "undefined") vyMusic.pause()
                                }
                            }
                        }
                        VRow {
                            label: qsTr("Page switcher")
                            hint: qsTr("Hidden normally; drops down when the pointer touches the top of the window")
                            VSeg {
                                objectName: "set-dock"
                                options: [{ id: "auto", label: qsTr("Auto-hide") }, { id: "always", label: qsTr("Always show") }]
                                current: veyra.preferences.dockPinned === true ? "always" : "auto"
                                onPicked: id => veyra.setPreference("dockPinned", id === "always")
                            }
                        }
                        VRow {
                            label: qsTr("Accent color")
                            hint: qsTr("Switches, progress, selected state")
                            // .swatch: two colour dots, the chosen one ringed and 1.12x.
                            Row {
                                objectName: "set-accent"
                                spacing: 10
                                Repeater {
                                    model: [{ id: "orange", c: "#FF8A3D", tip: qsTr("Orange"), duskTip: qsTr("Sunset gradient") },
                                            { id: "white", c: "#F3F3F5", tip: qsTr("White glow"), duskTip: qsTr("White glow") }]
                                    delegate: Rectangle {
                                        id: swatch
                                        required property var modelData
                                        readonly property bool on: (veyra.preferences.accent || "orange") === modelData.id
                                        objectName: "set-accent-" + modelData.id
                                        width: 24; height: 24; radius: 12
                                        // The dusk palette shows what each choice paints: the sunset
                                        // gradient, or the neon white (the ring stays on top).
                                        color: Theme.dusk ? "#FFF6FB" : modelData.c
                                        gradient: Theme.dusk && modelData.id === "orange" ? sunset : undefined
                                        Gradient {
                                            id: sunset
                                            orientation: Gradient.Horizontal
                                            GradientStop { position: 0.0; color: Theme.accentFrom }
                                            GradientStop { position: 1.0; color: Theme.accentTo }
                                        }
                                        border.width: 2
                                        border.color: on ? "#FFFFFF" : "transparent"
                                        scale: on ? 1.12 : 1
                                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                                        HoverHandler { id: swHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: veyra.setPreference("accent", modelData.id) }
                                        ToolTip.visible: swHover.hovered
                                        ToolTip.text: Theme.dusk ? modelData.duskTip : modelData.tip
                                    }
                                }
                            }
                        }
                        VRow {
                            label: qsTr("Reduce motion")
                            hint: qsTr("Turn off springs and transitions")
                            VSwitch {
                                checked: veyra.reducedMotion
                                onToggled: checked => veyra.reducedMotion = checked
                            }
                        }
                        VRow {
                            label: qsTr("Slider keyboard step")
                            hint: qsTr("Click a slider and use ← → to adjust by this step; Esc or clicking the picture gives the arrow keys back to seeking")
                            VSeg {
                                objectName: "set-slider-step"
                                options: [{ id: "1", label: "1" }, { id: "0.1", label: "0.1" }, { id: "0.01", label: "0.01" }]
                                current: String(veyra.preferences.sliderKeyStep !== undefined ? veyra.preferences.sliderKeyStep : 0.1)
                                onPicked: id => veyra.setPreference("sliderKeyStep", Number(id))
                            }
                        }
                        VRow {
                            label: qsTr("Background gradient strength")
                            hint: Theme.dusk ? qsTr("Strength of the sunset sky gradient, dithered against banding") : qsTr("A very light gradient and dither over the deep black (prevents banding)")
                            VSeg {
                                objectName: "set-backdrop"
                                options: [{ id: "0", label: qsTr("None") }, { id: "1", label: qsTr("Light") }, { id: "2", label: qsTr("Medium") }]
                                current: String(veyra.preferences.backdrop !== undefined ? veyra.preferences.backdrop : 1)
                                onPicked: id => veyra.setPreference("backdrop", Number(id))
                            }
                        }
                        VRow {
                            label: qsTr("Interface scale")
                            hint: (veyra.preferences.uiScale || 0) !== veyra.uiScaleActive ? qsTr("Takes effect after restart") : qsTr("Auto = follow the Windows display scale")
                            VSeg {
                                objectName: "set-scale"
                                options: [{ id: "0", label: qsTr("Auto") }, { id: "100", label: "100%" }, { id: "125", label: "125%" }, { id: "150", label: "150%" }]
                                current: String(veyra.preferences.uiScale || 0)
                                onPicked: id => veyra.setPreference("uiScale", Number(id))
                            }
                        }
                        VRow {
                            label: qsTr("Interface language")
                            hint: qsTr("Takes effect immediately; logs stay in English so problem reports stay readable")
                            VSelect {
                                objectName: "set-language"
                                implicitWidth: 170
                                // Each language is named in itself, so it can be found from any of them.
                                options: [
                                    { id: "auto", label: qsTr("Follow system") },
                                    { id: "zh-CN", label: qsTr("简体中文") },
                                    { id: "zh-TW", label: qsTr("繁體中文") },
                                    { id: "en", label: "English" },
                                    { id: "ja", label: qsTr("日本語") },
                                    { id: "ko", label: "한국어" },
                                    { id: "es", label: "Español" },
                                    { id: "fr", label: "Français" },
                                    { id: "de", label: "Deutsch" },
                                    { id: "pt-BR", label: "Português (Brasil)" },
                                    { id: "ru", label: "Русский" }
                                ]
                                value: (options.find(o => o.id === (veyra.preferences.language || "auto")) || options[0]).label
                                onPicked: id => veyra.setPreference("language", id)
                            }
                        }
                    }
                }

                // --- Startup and window ---
                ColumnLayout {
                    id: secWindow
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "window"
                    VH1 { text: qsTr("Startup & window") }
                    Text {
                        text: qsTr("Page, window and resume behaviour at startup.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        VRow {
                            label: qsTr("Page shown at startup")
                            hint: qsTr("Home = the page for choosing a source (clicking the logo at the top also returns here)")
                            VSeg {
                                objectName: "set-default-page"
                                options: [
                                    { id: "home", label: qsTr("Home") },
                                    { id: "min", label: qsTr("Minimal mode") },
                                    { id: "pro", label: qsTr("Pro mode") },
                                    { id: "last", label: qsTr("Last") }
                                ]
                                current: veyra.defaultPage
                                onPicked: id => veyra.defaultPage = id
                            }
                        }
                        VRow {
                            label: qsTr("Window size at startup")
                            hint: qsTr("Takes effect at next launch; shrunk and centered if larger than the screen")
                            VSelect {
                                objectName: "set-window-size"
                                readonly property var sizes: [
                                    { id: "1280x800", label: "1280 × 800" },
                                    { id: "1600x1000", label: "1600 × 1000" },
                                    { id: "1920x1200", label: "1920 × 1200" },
                                    { id: "last", label: qsTr("Remember last size") }
                                ]
                                options: sizes
                                value: (sizes.find(o => o.id === (veyra.preferences.windowSize || "1280x800")) || sizes[0]).label
                                onPicked: id => veyra.setPreference("windowSize", id)
                            }
                        }
                        VRow {
                            label: qsTr("Automatically resume on startup")
                            hint: qsTr("Resume the video position or reopen the last streaming session")
                            VSwitch {
                                objectName: "set-auto-resume"
                                checked: !!veyra.preferences.autoResume
                                onToggled: v => veyra.setPreference("autoResume", v)
                            }
                        }
                        VRow {
                            label: qsTr("Remember playback position")
                            hint: qsTr("Continue from the last position when the same file is reopened")
                            VSwitch {
                                objectName: "set-resume"
                                checked: veyra.preferences.rememberPosition !== false
                                onToggled: checked => veyra.setPreference("rememberPosition", checked)
                            }
                        }
                    }
                }

                // --- Playback ---
                ColumnLayout {
                    id: secPlay
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "play"
                    VH1 { text: qsTr("Playback", "section") }
                    Text {
                        text: qsTr("Defaults for file playback, subtitles and screenshots.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        VRow {
                            label: qsTr("Decode")
                            hint: (veyra.preferences.decode || "auto") === "hardware"
                                  ? qsTr("Hardware decoding only = use hardware decoding, report an error instead of falling back; applies to the next file opened")
                                  : qsTr("Auto = hardware decoding first, software if it fails; applies to the next file opened")
                            VSeg {
                                objectName: "set-decode"
                                options: [{ id: "auto", label: qsTr("Auto") }, { id: "hardware", label: qsTr("Hardware decoding only") }, { id: "software", label: qsTr("Software") }]
                                current: veyra.preferences.decode || "auto"
                                onPicked: id => veyra.setPreference("decode", id)
                            }
                        }
                        VRow {
                            label: qsTr("HDR output format")
                            hint: qsTr("HDR10 by default; frame generation always uses HDR10 and needs Windows HDR on")
                            VSeg { options: [{id:"0",label:"HDR10"},{id:"1",label:qsTr("scRGB float")}]; current: String(veyra.hdrOutputMode); onPicked: id => veyra.hdrOutputMode = Number(id) }
                        }
                        VRow {
                            label: qsTr("Automatic deinterlacing")
                            hint: veyra.deinterlaceAvailable ? qsTr("Processes only video marked as interlaced, using the GPU when decoding on hardware. Software decoding uses a compatible filter. Applies when you next open a video.") : qsTr("This package does not include the deinterlacing module")
                            VSwitch { enabled: veyra.deinterlaceAvailable; checked: veyra.preferences.deinterlace !== false; onToggled: checked => veyra.setPreference("deinterlace",checked) }
                        }
                        // Network / server playback (#48). Read when a source opens.
                        VRow {
                            label: qsTr("Network buffer")
                            hint: qsTr("Network addresses, live streams and server videos collect a short buffer before playing. When the server is slower than the video's bitrate, playback pauses to buffer and says why instead of failing to a black screen. Takes effect the next time a network video is opened")
                            VSeg {
                                objectName: "set-network-buffer"
                                options: [{ id: "auto", label: qsTr("Auto") }, { id: "off", label: qsTr("Off", "off") },
                                          { id: "small", label: qsTr("Small") }, { id: "medium", label: qsTr("Medium") }, { id: "large", label: qsTr("Large") }]
                                current: veyra.preferences.networkBuffer || "auto"
                                onPicked: id => veyra.setPreference("networkBuffer", id)
                            }
                        }
                        VRow {
                            label: qsTr("Startup buffer")
                            hint: qsTr("Seconds to collect before playback starts; “Follow buffer level” uses the level chosen above. Takes effect the next time a network video is opened")
                            VSelect {
                                objectName: "set-network-prebuffer"
                                implicitWidth: 170
                                options: [{ id: "-1", label: qsTr("Follow buffer level") }, { id: "0", label: qsTr("Don't wait") },
                                          { id: "3", label: qsTr("%1 s").arg(3) }, { id: "5", label: qsTr("%1 s").arg(5) },
                                          { id: "10", label: qsTr("%1 s").arg(10) }, { id: "20", label: qsTr("%1 s").arg(20) },
                                          { id: "30", label: qsTr("%1 s").arg(30) }]
                                value: (options.find(o => o.id === (veyra.preferences.networkPrebuffer === undefined ? "-1" : String(veyra.preferences.networkPrebuffer))) || options[0]).label
                                onPicked: id => veyra.setPreference("networkPrebuffer", Number(id))
                            }
                        }
                        VRow {
                            label: qsTr("Auto-reconnect")
                            hint: qsTr("Reconnect automatically after a network drop: on-demand video resumes where it stopped and live streams reconnect; when off, a drop stops playback. Takes effect the next time a network video is opened")
                            VSwitch {
                                objectName: "set-network-reconnect"
                                checked: veyra.preferences.networkAutoReconnect !== false
                                onToggled: checked => veyra.setPreference("networkAutoReconnect", checked)
                            }
                        }
                        VRow {
                            label: qsTr("Damaged picture handling")
                            hint: (veyra.preferences.networkCorrupt || "conceal") === "keyframe"
                                  ? qsTr("Wait for keyframe = drop damaged pictures and hold the last one until the next clean keyframe; only applies to network video and MPEG-TS")
                                  : qsTr("Keep picture = show the picture the decoder patched up, so motion carries on; only applies to network video and MPEG-TS")
                            VSeg {
                                objectName: "set-network-corrupt"
                                options: [{ id: "conceal", label: qsTr("Keep picture") }, { id: "keyframe", label: qsTr("Wait for keyframe") }]
                                current: veyra.preferences.networkCorrupt || "conceal"
                                onPicked: id => veyra.setPreference("networkCorrupt", id)
                            }
                        }
                        VRow {
                            label: qsTr("HDR brightness curve") + " / " + qsTr("HDR metadata")
                            VButton {
                                objectName: "set-hdr-advanced-toggle"
                                text: root.hdrAdvancedExpanded ? qsTr("Collapse") : qsTr("Expand")
                                trailingIcon: root.hdrAdvancedExpanded ? "up" : "down"
                                onClicked: root.hdrAdvancedExpanded = !root.hdrAdvancedExpanded
                            }
                        }
                        ColumnLayout {
                            objectName: "set-hdr-advanced-body"
                            Layout.fillWidth: true
                            spacing: 0
                            visible: root.hdrAdvancedExpanded
                            // HDR brightness curve: static, so the highlights are protected.
                            // One-click presets (same ids as the list page).
                            Flow {
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                spacing: 6
                                Repeater {
                                    model: [{id:"standard",label:qsTr("Standard")},
                                            {id:"darkLift",label:qsTr("Lift dark scenes")},
                                            {id:"highlightGuard",label:qsTr("Highlight protection")},
                                            {id:"darkAndBright",label:qsTr("Shadows and highlights")},
                                            {id:"brightRoom",label:qsTr("Bright room")},
                                            {id:"darkRoom",label:qsTr("Dark room")},
                                            {id:"punch",label:qsTr("Punch")}]
                                    delegate: VButton {
                                        required property var modelData
                                        text: modelData.label
                                        ghost: true
                                        primary: veyra.hdrTuningPreset === modelData.id
                                        onClicked: veyra.applyHdrTuningPreset(modelData.id)
                                    }
                                }
                            }
                            VRow {
                                label: qsTr("HDR brightness curve")
                                hint: qsTr("Static curve: shadows, midtones and highlights")
                                value: veyra.hdrCurve ? qsTr("On") : qsTr("Off")
                                VSwitch {
                                    objectName: "hdrcurve-enabled"
                                    checked: veyra.hdrCurve
                                    onToggled: checked => veyra.hdrCurve = checked
                                }
                            }
                            VRow {
                                label: qsTr("Strength")
                                hint: qsTr("0% disables the curve; peak protection and metadata are independent")
                                value: veyra.hdrCurveStrength + "%"
                                VSlider {
                                    objectName: "hdrcurve-strength"
                                    implicitWidth: 120
                                    valueFromModel: true
                                    resettable: true; defaultValue: 100
                                    from: 0; to: 200
                                    value: veyra.hdrCurveStrength
                                    onMoved: value => veyra.hdrCurveStrength = Math.round(value)
                                }
                            }
                            VRow {
                                label: qsTr("Display peak (nit)")
                                hint: veyra.hdrDisplayInfo.valid
                                    ? qsTr("System report: peak %1, full frame %2, black %3 nit")
                                        .arg(veyra.hdrDisplayInfo.peakNits)
                                        .arg(veyra.hdrDisplayInfo.fullFrameNits)
                                        .arg(Number(veyra.hdrDisplayInfo.minNits).toFixed(3))
                                    : qsTr("0 = use the value reported by the system")
                                value: veyra.hdrDisplayPeakNits > 0
                                    ? (veyra.hdrDisplayPeakNits + " nit") : qsTr("Auto")
                                VSlider {
                                    objectName: "hdrcurve-displaypeak"
                                    implicitWidth: 120
                                    valueFromModel: true
                                    resettable: true; defaultValue: 0
                                    from: 0; to: 4000
                                    value: veyra.hdrDisplayPeakNits
                                    onMoved: value => veyra.hdrDisplayPeakNits = Math.round(value)
                                }
                            }
                            Repeater {
                                model: [{key:"shadowLiftEv100",label:qsTr("Shadow lift (0.01 EV)"),from:-200,to:200,def:0},
                                        {key:"shadowRangeNits",label:qsTr("Lift range (nit)"),from:1,to:100,def:10,hint:qsTr("Only shadows below this level are lifted")},
                                        {key:"midGrayNits",label:qsTr("Mid-gray (nit)"),from:100,to:500,def:203,hint:qsTr("203 = neutral; higher brightens the picture with the peak unchanged")},
                                        {key:"highlightStartNits",label:qsTr("Highlight start (nit)"),from:0,to:2000,def:0,hint:qsTr("0 = no highlight roll-off")},
                                        {key:"rollOffPercent",label:qsTr("Roll-off strength (%)"),from:10,to:200,def:100},
                                        {key:"peakCapNits",label:qsTr("Peak cap (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = no limit")}]
                                delegate: VRow {
                                    required property var modelData
                                    label: modelData.label
                                    hint: modelData.hint || ""
                                    value: String(veyra.hdrCurveParams[modelData.key] ?? "—")
                                    VSlider {
                                        objectName: "hdrcurve-" + modelData.key
                                        implicitWidth: 150
                                        valueFromModel: true
                                        resettable: true; defaultValue: modelData.def
                                        from: modelData.from; to: modelData.to
                                        value: veyra.hdrCurveParams[modelData.key] ?? modelData.def
                                        onMoved: value => veyra.setHdrCurveParameter(modelData.key, Math.round(value))
                                    }
                                }
                            }
                            // HDR metadata: what the display's own tone mapping works from.
                            VRow {
                                label: qsTr("HDR metadata")
                                hint: qsTr("HDR10 hint; Windows or the display may ignore it")
                                value: veyra.hdrMetadataEnabled ? qsTr("Enabled (HDR10)") : qsTr("Off")
                                VSwitch {
                                    objectName: "hdrmeta-enabled"
                                    checked: veyra.hdrMetadataEnabled
                                    onToggled: checked => veyra.hdrMetadataEnabled = checked
                                }
                            }
                            Repeater {
                                model: [{key:"masteringPeakNits",label:qsTr("Mastering peak (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = follow the source; results depend on the display")},
                                        {key:"maxCllNits",label:qsTr("MaxCLL (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = follow the source")},
                                        {key:"maxFallNits",label:qsTr("MaxFALL (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = follow the source")}]
                                delegate: VRow {
                                    required property var modelData
                                    label: modelData.label
                                    hint: modelData.hint || ""
                                    value: String(veyra.hdrMetadataParams[modelData.key] ?? "—")
                                    VSlider {
                                        objectName: "hdrmeta-" + modelData.key
                                        implicitWidth: 150
                                        valueFromModel: true
                                        resettable: true; defaultValue: modelData.def
                                        from: modelData.from; to: modelData.to
                                        value: veyra.hdrMetadataParams[modelData.key] ?? modelData.def
                                        onMoved: value => veyra.setHdrMetadataParameter(modelData.key, Math.round(value))
                                    }
                                }
                            }

                        }
                        VRow {
                            label: qsTr("Default subtitle size")
                            hint: qsTr("Two lines are not shrunk automatically; more styles in subtitle settings")
                            VSeg {
                                objectName: "set-subsize"
                                readonly property int size: veyra.preferences.subtitleSize || 22
                                options: [{ id: "18", label: qsTr("Small") }, { id: "22", label: qsTr("Medium") }, { id: "28", label: qsTr("Large") }]
                                current: size <= 19 ? "18" : size >= 26 ? "28" : "22"
                                onPicked: id => veyra.setPreference("subtitleSize", Number(id))
                            }
                        }
                        VRow {
                            visible: (veyra.liveTranslation || null) !== null
                            label: qsTr("Live translation")
                            VButton {
                                objectName: "set-live-translation"
                                text: qsTr("Live translation settings…")
                                onClicked: root.requestPage("translation")
                            }
                        }
                        VRow {
                            label: qsTr("Screenshot folder")
                            hint: veyra.screenshotDirectory + qsTr(" (SDR saves PNG, HDR saves JPEG XR)")
                            RowLayout {
                                spacing: 6
                                VButton { text: qsTr("Open"); ghost: true; onClicked: veyra.openScreenshotDirectory() }
                                VButton { objectName: "set-shotdir"; text: qsTr("Change…"); onClicked: veyra.chooseScreenshotDirectory() }
                            }
                        }
                    }
                    VGroup {
                        objectName: "set-smooth-motion"
                        VRow {
                            label: qsTr("How to turn on Smooth Motion")
                            hint: qsTr("NVIDIA driver-level AI frame interpolation; use it or the in-app frame generation, not both")
                            VButton {
                                text: smoothHelp.visible ? qsTr("Collapse") : qsTr("View")
                                ghost: true
                                onClicked: smoothHelp.visible = !smoothHelp.visible
                            }
                        }
                        Text {
                            id: smoothHelp
                            visible: false
                            Layout.fillWidth: true
                            Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 10
                            text: qsTr("Using only driver frame generation: turn frame generation off on the Pro mode frame generation tab, then turn on Smooth Motion (AI frame interpolation) in the NVIDIA App.") + "\n"
                                  + qsTr("Using only DLSS / XeSS frame generation: turn Smooth Motion off in the NVIDIA App.") + "\n"
                                  + qsTr("The app does not detect or block the two being stacked, and does not change driver settings; AMD FSR frame generation is not offered yet.")
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            lineHeight: 1.3
                        }
                    }
                }

                // --- Audio ---
                ColumnLayout {
                    id: secAudio
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "audio"
                    VH1 { text: qsTr("Audio") }
                    Text {
                        text: qsTr("Audio sync, volume and output device.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        VRow {
                            label: qsTr("Audio sync")
                            hint: veyra.audioSyncMode === 0 ? (veyra.audioSyncLive ? qsTr("Estimated compensation ") + veyra.audioCompensationMs.toFixed(0) + " ms" : qsTr("Auto-estimated; applies to live streaming input"))
                                  : veyra.audioSyncMode === 1 ? qsTr("Manual offset · applies to live input") : qsTr("No compensation")
                            VSeg {
                                options: [{ id: "0", label: qsTr("Auto estimate") }, { id: "1", label: qsTr("Manual") }, { id: "2", label: qsTr("Off", "off") }]
                                current: String(veyra.audioSyncMode)
                                onPicked: id => veyra.audioSyncMode = Number(id)
                            }
                        }
                        VRow {
                            label: qsTr("Audio offset")
                            hint: qsTr("Positive values delay the audio")
                            value: veyra.audioOffsetMs + " ms"
                            VSlider {
                                implicitWidth: 150
                                center: true
                                from: -250; to: 250; value: veyra.audioOffsetMs; enabledControl: veyra.audioSyncMode === 1
                                onMoved: veyra.audioOffsetMs = Math.round(value)
                            }
                        }
                        VRow {
                            label: qsTr("Volume")
                            value: Math.round(veyra.volume * 100) + "%"
                            VSlider {
                                objectName: "settings-volume"
                                implicitWidth: 150
                                from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                                valueFromModel: true; defaultValue: 1
                                onMoved: veyra.volume = value
                            }
                        }
                    }
                    VGroup {
                        VRow {
                            label: qsTr("Output device and downmix")
                            hint: veyra.audioOutputStatus
                            VButton { text: qsTr("Audio settings…"); onClicked: veyra.openAudioDialog() }
                        }
                    }
                }

                // --- Performance ---
                ColumnLayout {
                    id: secPerf
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "perf"
                    VH1 { text: qsTr("Performance") }
                    Text {
                        text: qsTr("GPU scheduling, prewarming and resource monitoring. The defaults suit most PCs.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        VRow {
                            label: qsTr("GPU priority")
                            hint: veyra.gpuPriorityStatus + qsTr("; High by default. If a game or other heavy program running alongside slows down, choose Normal")
                            VSeg {
                                objectName: "set-gpu-priority"
                                options: [{ id: "normal", label: qsTr("Normal") }, { id: "high", label: qsTr("High") }, { id: "realtime", label: qsTr("Realtime") }]
                                current: veyra.preferences.gpuPriority || "high"
                                onPicked: id => veyra.setPreference("gpuPriority", id)
                            }
                        }
                        VRow {
                            label: qsTr("Prewarm enhancement components at startup")
                            hint: !veyra.enhancementPrewarmAvailable
                                  ? qsTr("Prewarming is not available on this GPU")
                                  : qsTr("Off by default. When on, the enabled single NR layer or upscaler is prepared while the home page is idle, so the first open is about 2 s faster; once ready it keeps about 0.8–1.8 GB of video memory until a video opens")
                            VSwitch {
                                objectName: "set-prewarm-enhancement"
                                enabled: veyra.enhancementPrewarmAvailable
                                checked: veyra.preferences.prewarmEnhancement === true
                                onToggled: v => veyra.setPreference("prewarmEnhancement", v)
                            }
                        }
                        VRow {
                            label: qsTr("GPU usage monitor")
                            hint: qsTr("The Pro page's GPU usage counts only this GPU") + (veyra.gpuMonitorName ? qsTr("; current: ") + veyra.gpuMonitorName : "")
                            VSelect {
                                objectName: "set-monitor-gpu"
                                implicitWidth: 230
                                options: veyra.gpuMonitorChoices
                                value: (options.find(o => o.id === (veyra.preferences.monitorGpu || "")) || options[0] || { label: "—" }).label
                                onPicked: id => veyra.setPreference("monitorGpu", id)
                            }
                        }
                        VRow {
                            label: qsTr("Also compute on this GPU")
                            hint: veyra.computeGpuHint
                            VSwitch {
                                objectName: "set-compute-gpu"
                                checked: veyra.preferences.computeOnMonitorGpu === true
                                onToggled: v => veyra.setPreference("computeOnMonitorGpu", v)
                            }
                        }
                        VRow {
                            label: qsTr("Fullscreen VRAM growth protection")
                            hint: qsTr("Exit fullscreen if abnormal growth is detected; off by default")
                            VSwitch {
                                objectName: "set-fullscreen-memory-protection"
                                checked: !!veyra.preferences.fullscreenMemoryProtection
                                onToggled: v => veyra.setPreference("fullscreenMemoryProtection", v)
                            }
                        }
                    }
                }

                // --- Compatibility ---
                ColumnLayout {
                    id: secCompat
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "compat"
                    VH1 { text: qsTr("Compatible") }
                    Text {
                        text: qsTr("Interface drawing, OBS and monitoring compatibility require a restart; HDR frame generation compatibility can be switched during playback.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        VRow {
                            // Interface drawing (veyra/ui/InterfaceRenderer.h, read by main.cpp at start).
                            // Software by default: no swapchain or D3D12 device of its own for
                            // overlays to hook, and no video memory. Monitoring software compatibility
                            // still draws in software when GPU is chosen.
                            readonly property bool gpuChosen: veyra.preferences.uiRenderer === "gpu"
                            label: qsTr("Interface drawing")
                            hint: gpuChosen
                                  ? (!veyra.softwareUiActive
                                     ? qsTr("GPU drawing: smoother animation with shadows and blur; uses video memory and GPU time, and may conflict with RTSS and other monitoring overlays")
                                     : veyra.overlayCompatActive
                                       ? qsTr("A monitoring overlay was detected at this start, so the interface is still drawn in software (monitoring overlay compatibility: Auto)")
                                       : qsTr("Takes effect after restart"))
                                  : (veyra.softwareUiActive
                                     ? qsTr("Software drawing: best compatibility, no video memory, no competition with the video for the GPU; some shadows and blur are simplified")
                                     : qsTr("Takes effect after restart"))
                            VSeg {
                                objectName: "set-ui-renderer"
                                options: [{ id: "software", label: qsTr("Software") }, { id: "gpu", label: "GPU" }]
                                current: veyra.preferences.uiRenderer === "gpu" ? "gpu" : "software"
                                onPicked: id => {
                                    // A restart changes the renderer unless a compatibility rule
                                    // keeps the interface in software anyway.
                                    const changes = id === "gpu"
                                        ? veyra.softwareUiActive
                                          && !((veyra.preferences.overlayCompat || "auto") === "auto" && veyra.presentationOverlayRunning())
                                        : !veyra.softwareUiActive
                                    if (veyra.setPreference("uiRenderer", id) && changes)
                                        obsRestart.open()
                                }
                            }
                        }
                        VRow {
                            label: qsTr("HDR frame generation flash compatibility")
                            hint: qsTr("Off by default. Applies to NVIDIA HDR10 frame generation; uses about 32 MiB of extra VRAM at 4K and may increase GPU work. Switching may briefly pause the picture.")
                            VSwitch {
                                objectName: "set-hdr-fg-compatibility"
                                checked: !!veyra.preferences.hdrFgCompatibility
                                onToggled: v => veyra.setPreference("hdrFgCompatibility", v)
                            }
                        }
                        VRow {
                            // RivaTuner (MSI Afterburner's OSD), NVIDIA's in-game overlay and GamePP: see
                            // main.cpp. Auto draws the interface in software when one runs at start, so
                            // they only hook the video. With interface drawing = software (the default) the interface
                            // is drawn in software anyway, and this choice changes nothing.
                            id: overlayCompatRow
                            readonly property bool automatic: (veyra.preferences.overlayCompat || "auto") === "auto"
                            readonly property bool gpuChosen: veyra.preferences.uiRenderer === "gpu"
                            label: qsTr("Monitoring overlay compatibility")
                            hint: !gpuChosen
                                  ? qsTr("Only matters when Interface drawing is set to GPU; with software drawing the RTSS OSD shows on the video only")
                                  : (automatic !== veyra.overlayCompatActive && (veyra.overlayCompatActive || veyra.presentationOverlayRunning()))
                                    ? qsTr("Takes effect after restart")
                                    : veyra.overlayCompatActive
                                      ? qsTr("Monitoring compatibility rendering is enabled: the OSD shows on the video only; some shadows and blur are simplified")
                                      : automatic
                                        ? qsTr("When RivaTuner (RTSS), NVIDIA's in-game overlay or GamePP is running at start, the interface is drawn in software and they show on the video only")
                                        : qsTr("The interface always draws on the GPU; the RTSS OSD is not shown in Veyra; with NVIDIA's in-game overlay or GamePP running, Veyra may freeze")
                            VSeg {
                                objectName: "set-overlay-compat"
                                options: [{ id: "auto", label: qsTr("Auto") }, { id: "off", label: qsTr("Close") }]
                                current: veyra.preferences.overlayCompat || "auto"
                                onPicked: id => {
                                    if (veyra.setPreference("overlayCompat", id)
                                            && overlayCompatRow.gpuChosen
                                            && (id === "auto") !== veyra.overlayCompatActive
                                            && (veyra.overlayCompatActive || veyra.presentationOverlayRunning()))
                                        obsRestart.open()
                                }
                            }
                        }
                    }
                }

                // --- Shortcuts --------------------------------------------------
                ColumnLayout {
                    id: secKeys
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "keys"
                    VH1 { text: qsTr("Shortcuts") }
                    Text {
                        text: qsTr("Click the key on the right, then press the new key combination to rebind it; Esc cancels.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VGroup {
                        Repeater {
                            model: [
                                { id: "playPause", a: qsTr("Play / pause") },
                                { id: "fullscreen", a: qsTr("Fullscreen"), note: qsTr("F11 / Alt+Enter work too") },
                                { id: "lock", a: qsTr("Lock the fullscreen control bar") },
                                { id: "hold", a: qsTr("Hold to see the original") },
                                { id: "screenshot", a: qsTr("Screenshot") },
                                { id: "toggleMode", a: qsTr("Toggle Minimal / Pro") }
                            ]
                            delegate: VRow {
                                id: keyRow
                                required property var modelData
                                label: modelData.a
                                hint: modelData.note || ""
                                Rectangle {
                                    objectName: "key-" + keyRow.modelData.id
                                    readonly property bool capturing: root.capturingKey === keyRow.modelData.id
                                    implicitWidth: Math.max(64, keyText.implicitWidth + 20)
                                    implicitHeight: 26
                                    radius: 7
                                    color: capturing ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.06)
                                    border.width: 1
                                    border.color: capturing ? Theme.accent : Theme.stroke
                                    Text {
                                        id: keyText
                                        anchors.centerIn: parent
                                        text: parent.capturing ? qsTr("Press the new key…") : (veyra.shortcuts[keyRow.modelData.id] || qsTr("Not set"))
                                        color: Theme.t1
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fsSmall
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: { root.capturingKey = keyRow.modelData.id; keyCatcher.forceActiveFocus() } }
                                }
                            }
                        }
                        VRow {
                            label: qsTr("Reset to default")
                            VButton { text: qsTr("Reset all"); ghost: true; onClicked: veyra.resetShortcuts() }
                        }
                    }
                    VGroup {
                        Repeater {
                            model: [
                                { a: qsTr("Back / forward 5 seconds"), k: "← / →" },
                                { a: qsTr("Play / pause (when playing a file)"), k: qsTr("Click the picture") },
                                { a: qsTr("Volume"), k: "↑ / ↓" },
                                { a: qsTr("Open file / Export page"), k: "Ctrl+O / Ctrl+E" },
                                { a: qsTr("Subtitles on/off / main track / secondary track"), k: "B / T / Y" },
                                { a: qsTr("Subtitle delay 50 ms (Shift = 1 s)"), k: "Z / X" },
                            ]
                            delegate: VRow {
                                required property var modelData
                                label: modelData.a
                                hint: qsTr("Fixed")
                                VTag { text: modelData.k }
                            }
                        }
                    }
                    // Captures the next key combination for the row being rebound.
                    Item {
                        id: keyCatcher
                        focus: false
                        Keys.onPressed: event => {
                            if (root.capturingKey === "") return
                            event.accepted = true
                            if (event.key === Qt.Key_Escape) { root.capturingKey = ""; return }
                            const modifierOnly = [Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta].indexOf(event.key) >= 0
                            if (modifierOnly) return
                            const parts = []
                            if (event.modifiers & Qt.MetaModifier) parts.push("Meta")
                            if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
                            if (event.modifiers & Qt.AltModifier) parts.push("Alt")
                            if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
                            const names = { [Qt.Key_Space]: "Space", [Qt.Key_Tab]: "Tab", [Qt.Key_Return]: "Return", [Qt.Key_Enter]: "Enter",
                                            [Qt.Key_Backspace]: "Backspace", [Qt.Key_Delete]: "Del", [Qt.Key_Home]: "Home", [Qt.Key_End]: "End",
                                            [Qt.Key_PageUp]: "PgUp", [Qt.Key_PageDown]: "PgDown", [Qt.Key_Insert]: "Ins" }
                            let name = names[event.key]
                            if (!name && event.key >= Qt.Key_F1 && event.key <= Qt.Key_F24) name = "F" + (event.key - Qt.Key_F1 + 1)
                            if (!name && event.text && event.text.trim().length === 1) name = event.text.toUpperCase()
                            if (!name && event.key >= 0x20 && event.key < 0x7F) name = String.fromCharCode(event.key)
                            if (!name) return
                            parts.push(name)
                            if (veyra.setShortcut(root.capturingKey, parts.join("+"))) root.capturingKey = ""
                        }
                    }
                }

                // --- Components and licences ------------------------------------------
                ColumnLayout {
                    id: secComp
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "comp"
                    onVisibleChanged: if (visible) veyra.refreshComponentList()
                    VH1 { text: qsTr("Components and licenses") }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("All runtime components are experimental runtimes, not an official NVIDIA partnership or certification. Detailed hashes in release-runtime-manifest.json.")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        // Reported by the engine's component state rather than a
                        // hand-written list that could drift from the package.
                        Repeater {
                            model: veyra.componentList
                            delegate: VRow {
                                required property var modelData
                                label: modelData.name
                                hint: modelData.detail
                                VTag {
                                    text: modelData.experimental ? qsTr("Experimental") : (modelData.loaded ? qsTr("Loaded") : qsTr("Not loaded"))
                                    kind: modelData.experimental ? "exp" : (modelData.loaded ? "ok" : "")
                                }
                            }
                        }
                    }
                }

                // --- About ----------------------------------------------------
                ColumnLayout {
                    id: secAbout
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "about"
                    RowLayout {
                        spacing: 18
                        Image {
                            source: Theme.logoImage
                            sourceSize.width: 88
                            fillMode: Image.PreserveAspectFit
                        }
                        ColumnLayout {
                            spacing: 4
                            VH1 { text: "Veyra" }
                            Text {
                                text: qsTr("Version ") + veyra.version
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                            }
                        }
                    }
                    VGroup {
                        VRow {
                            label: qsTr("Download route")
                            hint: qsTr("Veyra connects directly to GitHub if the mirror fails.")
                            VSelect {
                                objectName: "set-update-download-route"
                                options: [
                                    { id: "github", label: qsTr("GitHub direct") },
                                    { id: "gh-proxy", label: "GH-Proxy · gh-proxy.com" },
                                    { id: "ghfast", label: "GHFast · ghfast.top" }
                                ]
                                value: (options.find(option => option.id === veyra.preferences.updateDownloadRoute) || options[0]).label
                                enabled: !veyra.updates.checking && !root.portableUpdate.downloading
                                onPicked: id => veyra.setPreference("updateDownloadRoute", id)
                            }
                        }
                        VRow {
                            label: qsTr("Check for updates")
                            hint: veyra.updates.latestVersion.length > 0
                                  ? qsTr("New version %1 available").arg(veyra.updates.latestVersion)
                                  : qsTr("Checks on startup; notifies when an update is available")
                            Row {
                                spacing: 6
                                VButton {
                                    text: veyra.updates.checking ? qsTr("Checking…") : qsTr("Check")
                                    enabled: !veyra.updates.checking
                                    onClicked: veyra.updates.check(true)
                                }
                                VButton {
                                    visible: veyra.updates.latestVersion.length > 0
                                    text: qsTr("View update")
                                    onClicked: veyra.updates.openRelease()
                                }
                            }
                        }
                        VRow {
                            visible: root.portableUpdate.available || root.portableUpdate.downloading || root.portableUpdate.scheduled
                            label: root.portableUpdate.scheduled ? qsTr("Update ready. It will be installed after exit.")
                                  : root.portableUpdate.downloading ? qsTr("Downloading update · %1%").arg(root.portableUpdate.progress)
                                  : qsTr("Portable update · %1").arg(root.portableUpdate.sizeText)
                            hint: root.portableUpdate.available ? root.portableUpdate.releaseNotes : ""
                            hintBelow: true
                            VButton {
                                objectName: "portable-download"
                                text: root.portableUpdate.available ? qsTr("Download and update on exit") : qsTr("Cancel update")
                                onClicked: {
                                    if (root.portableUpdate.available) root.portableUpdate.downloadAndSchedule()
                                    else root.portableUpdate.cancel()
                                }
                            }
                        }
                        VRow {
                            label: qsTr("Report a problem")
                            hint: qsTr("Opens GitHub Issues; diagnostics have been copied for you")
                            VButton { text: qsTr("Open"); onClicked: veyra.openFeedbackPage() }
                        }
                        VRow {
                            label: qsTr("Support the author")
                            hint: qsTr("Patreon, WeChat and Alipay")
                            VButton { objectName: "about-sponsor"; text: qsTr("Go"); onClicked: root.section = "sponsor" }
                        }
                        VRow {
                            label: qsTr("Community groups")
                            hint: qsTr("The group QR codes are in the project README")
                            VButton { text: qsTr("View"); onClicked: veyra.openProjectPage() }
                        }
                        VRow {
                            label: qsTr("Diagnostics")
                            hint: qsTr("Copy, then attach to your report")
                            VButton { text: qsTr("Copy"); onClicked: veyra.copyDiagnostics() }
                        }
                        VRow {
                            label: qsTr("Log")
                            hint: veyra.logFile
                            VButton { text: qsTr("Open folder"); ghost: true; onClicked: veyra.openLogFolder() }
                        }
                        VRow {
                            label: qsTr("User data")
                            hint: veyra.dataDirectory
                            VButton { text: qsTr("Open folder"); ghost: true; onClicked: veyra.openDataFolder() }
                        }
                    }
                    // The diagnostics the engine actually reports, shown verbatim.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 220
                        radius: 9
                        color: Theme.card2
                        clip: true
                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 10
                            contentHeight: diagText.implicitHeight
                            clip: true
                            ScrollBar.vertical: VScrollBar { }
                            Text {
                                id: diagText
                                width: parent.width
                                text: veyra.diagnosticsReport()
                                color: Theme.t2
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsSmall
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                // --- Sponsors ----------------------------------------------------
                SponsorPanel {
                    id: secSponsor
                    objectName: "set-sponsor"
                    Layout.fillWidth: true
                    visible: root.section === "sponsor"
                    onOpenLink: id => veyra.openSponsorLink(id)
                }

                Item { Layout.preferredHeight: 12 }
            }
        }
    }

    VConfirm {
        id: obsRestart
        objectName: "obs-restart-confirm"
        title: qsTr("Restart required")
        text: qsTr("Settings saved. Restart to apply them. Restart now? Current playback or streaming will stop.")
        acceptText: qsTr("Yes")
        rejectText: qsTr("No")
        onAccepted: veyra.restartApplication()
    }

    // [data-in] entrance order from pages-b.js PAGES.set.
    VRise { target: navCard; d: 0 }
    VRise { target: bodyCard; d: 1 }
}
