// The cinema control pill (pages-a.js .cine-bar, pages.css .cine-bar), shared by the
// minimal page and the fullscreen control window (G2.5). Only the pill: where it
// sits and how it enters belong to the owner.
//
// .cine-bar: 3 columns (230px | 1fr | 230px), 92px tall, radius 30. The design's
// frosted glass cannot sample a native video window, so the fill is the opaque
// approximation rgba(22,22,26,.86) (D5).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Rectangle {
    id: bar
    signal requestPage(string page)
    // Opens one of the main window's dialogs (subtitle settings / audio settings).
    signal requestDialog(string key)
    signal requestFullscreen()
    signal requestLock()
    // Windowed Minimal: a drag on the pill's empty space moves the window, like a title bar.
    signal requestMove()
    signal requestHide()
    signal requestPlaylist()
    // Set on the fullscreen bar: shows the lock button.
    property bool fullscreen: false
    // Set by FullscreenBar so upward menus stop above the playback pill.
    property real menuBottomLimit: -1
    // Any popover needs the owner window's full mask, not just the preset menu.
    readonly property bool menuOpen: presetMenu.visible || ccMenu.visible || audioMenu.visible || loadMenu.visible || playbackRate.menuOpen
    // The pointer is over the pill.
    readonly property bool hovered: barHover.hovered
    readonly property bool seekPreviewOpen: seekMouse.enabled && (seekMouse.containsMouse || seekArea.scrubbing)
    readonly property bool editing: volumeSlider.editing
    // Keep the control window active until a seek releases its mouse grab, too.
    // Returning focus to the owner during scrubbing cancels the MouseArea press.
    readonly property bool interacting: seekArea.scrubbing || volumeSlider.pressed || editing
    function finishEditing() { volumeSlider.closeEditor(true) }
    HoverHandler { id: barHover }
    // Buttons, the seek rail and the volume slider take their own presses first; only
    // a drag that starts on empty pill reaches this one.
    DragHandler {
        objectName: "cine-move"
        target: null
        enabled: !bar.fullscreen && !bar.interacting
        // Never take a drag over from a control: by default a DragHandler steals the grab
        // from an item once the pointer passes the drag threshold, so scrubbing the seek
        // rail moved the window instead (field report 2026-10-02).
        grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything
        onActiveChanged: if (active) bar.requestMove()
    }
    implicitHeight: 92
    radius: 30
    color: Theme.shade(22, 22, 26, 0.86)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.1)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 18

        // --- left: artwork + title (230px) ---------------------------
        RowLayout {
            Layout.preferredWidth: 230; Layout.minimumWidth: 140; Layout.maximumWidth: 230
            Layout.fillHeight: true
            spacing: 10
            // The file's own cover art when it carries one (MP4 covr, MKV image
            // attachment); otherwise the plate stays black - never a stand-in.
            Rectangle {
                id: coverPlate
                implicitWidth: 46; implicitHeight: 46; radius: 12
                color: Theme.videoBlack
                border.width: 1
                border.color: Theme.stroke
                clip: true
                Image {
                    id: coverArt
                    objectName: "cine-cover"
                    anchors.fill: parent
                    anchors.margins: 1
                    // Drawn through the masking effect below, never directly.
                    visible: false
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    source: veyra.hasSource && !veyra.isCapture ? "image://veyra-thumb/cover/" + veyra.thumbnailGeneration : ""
                }
                // A sibling MultiEffect, not layer.effect: Qt recreates a layer's effect item on
                // a screen DPI change while it walks the parent's children, and the walk then touched
                // the deleted item (crash moving the window to a 200 % monitor, field 2026-10-01).
                MultiEffect {
                    source: coverArt
                    anchors.fill: coverArt
                    visible: coverArt.status === Image.Ready
                    maskEnabled: true
                    maskSource: coverMask
                }
                Rectangle { id: coverMask; anchors.fill: coverArt; radius: 11; visible: false; layer.enabled: true }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: veyra.sourceName.length > 0 ? veyra.sourceName : qsTr("Not open")
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: 13
                    font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: veyra.sourceSummary
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            VButton {
                objectName: "cine-playlist"
                icon: true; iconName: "list"; tip: qsTr("Playlist")
                implicitWidth: 32; implicitHeight: 32
                onClicked: bar.requestPlaylist()
            }
        }

        // --- middle: transport + seek --------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 4

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                // .cbtn: 32px round, muted, brightens on hover.
                component CBtn: Item {
                    id: cbtn
                    property string glyph: ""
                    property string tip: ""
                    // The signal has to be declared: an inline component does not
                    // inherit the TapHandler's signal, so callers cannot assign
                    // onTapped without it.
                    signal tapped()
                    implicitWidth: 32
                    implicitHeight: 32
                    Rectangle {
                        anchors.fill: parent
                        radius: 16
                        color: cHover.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: glyph
                        color: cHover.hovered ? "#FFFFFF" : Theme.t2
                    }
                    scale: cTap.pressed ? 0.86 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    HoverHandler { id: cHover; cursorShape: Qt.PointingHandCursor }
                    ToolTip.visible: cHover.hovered && cbtn.tip.length > 0
                    ToolTip.text: cbtn.tip
                    TapHandler { id: cTap; gesturePolicy: TapHandler.WithinBounds; onTapped: cbtn.tapped() }
                }

                // Load (field request 2026-10-02): the home page's sources without leaving Minimal.
                CBtn { objectName: "cine-load"; glyph: "folder"; tip: qsTr("Load source"); onTapped: loadMenu.openAt(this, "up") }
                CBtn { glyph: "cc"; onTapped: ccMenu.openAt(this, "up") }
                CBtn { glyph: "back10"; onTapped: veyra.seekBy(-10) }

                // .play: 40px white circle with the play/pause glyph.
                Item {
                    implicitWidth: 40
                    implicitHeight: 40
                    Rectangle {
                        anchors.fill: parent
                        radius: 20
                        color: "#FFFFFF"
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: (veyra.running && !veyra.paused) ? "pausefill" : "playfill"
                        filled: true
                        color: "#0A0A0C"
                    }
                    scale: playTap.pressed ? 0.88 : (playHover.hovered ? 1.06 : 1.0)
                    Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                    HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { id: playTap; gesturePolicy: TapHandler.WithinBounds; onTapped: veyra.togglePlayPause() }
                }

                CBtn { glyph: "fwd10"; onTapped: veyra.seekBy(10) }
                CBtn { glyph: "music"; onTapped: audioMenu.openAt(this, "up") }
                PlaybackRateButton { id: playbackRate; objectName: "cine-playback-rate"; menuBottomLimit: bar.menuBottomLimit }
            }

            // .seekrow: mono times either side of the rail.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    text: veyra.positionText
                    color: Theme.t3
                    font.family: Theme.fontMono
                    font.pixelSize: 10
                }
                Item {
                    id: seekArea
                    objectName: "cine-seek"
                    Layout.fillWidth: true
                    implicitHeight: 14
                    property bool scrubbing: false
                    property real scrubFrac: 0
                    // .seek:hover .thumb { left: 44% } - the thumb tracks the real
                    // progress, not the design's hard-coded 44%.
                    readonly property real frac: scrubbing ? scrubFrac : Math.max(0, Math.min(1, shownProgress))
                    // Twice a second, not every snapshot: the pill is its own window and a rail
                    // that moved every tick made it present ~50 times a second, more often than
                    // a film plays, so OBS game capture could lock onto the pill.
                    property real shownProgress: veyra.progress
                    // Where the last scrub seek was sent, so a release that lands on
                    // the same spot does not interrupt it with a duplicate.
                    property real lastSeekTarget: -1
                    Timer {
                        interval: 500; repeat: true; running: bar.visible
                        onTriggered: seekArea.shownProgress = veyra.progress
                    }
                    readonly property real hoverFrac: scrubbing ? scrubFrac
                                                      : seekMouse.containsMouse && seekArea.width > 0
                                                        ? Math.max(0, Math.min(1, seekMouse.mouseX / seekArea.width))
                                                        : frac
                    property int thumbnailBucket: -1
                    function fractionAt(x) {
                        return seekArea.width > 0 ? Math.max(0, Math.min(1, x / seekArea.width)) : 0
                    }
                    Timer {
                        id: scrubSeek
                        // Restarted on every pointer move (see the MouseArea below), so it
                        // fires only once the drag settles. Seeking mid-drag makes two
                        // seeks overlap and they interrupt each other: a network source
                        // re-opens its read on each one, which is what made a drag look
                        // like a freeze. newBV defers the same way (1 s there).
                        interval: 250
                        onTriggered: {
                            if (!seekArea.scrubbing) return
                            const target = veyra.timelineSeconds(seekArea.scrubFrac)
                            seekArea.lastSeekTarget = target
                            veyra.seekTo(target)
                        }
                    }
                    Timer {
                        id: thumbnailDelay
                        interval: 120
                        onTriggered: if (!veyra.liveTimeshift.enabled) seekArea.thumbnailBucket = Math.floor(seekArea.hoverFrac * veyra.duration / 2)
                    }
                    Rectangle {
                        id: rail
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: seekMouse.containsMouse ? 6 : 3
                        radius: 9
                        color: Qt.rgba(1, 1, 1, 0.14)
                        Behavior on height { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } }
                        // White; the sunset gradient along the rail in the dusk palette.
                        Rectangle {
                            width: parent.width * seekArea.frac
                            height: parent.height
                            radius: 9
                            color: "#FFFFFF"
                            visible: !Theme.accentGradient
                        }
                        VAccentFill {
                            width: parent.width * seekArea.frac
                            height: parent.height
                            radius: 9
                            farEnd: true
                            span: parent.width
                            visible: Theme.accentGradient
                        }
                    }
                    // .seek .thumb: 11px dot, scale 0 -> 1 over .4s --spring on hover.
                    Rectangle {
                        width: 11; height: 11; radius: 5.5
                        color: "#FFFFFF"
                        x: seekArea.frac * seekArea.width - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        scale: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    }
                    // .seek .peek: 144x104 above the rail, opacity .15s, scale .85 -> 1
                    // over .4s --spring from its bottom centre.
                    Rectangle {
                        visible: opacity > 0.01
                        width: veyra.liveTimeshift.available ? 84 : 144
                        height: veyra.liveTimeshift.available ? 26 : 104
                        radius: 8
                        color: "#000000"
                        border.width: 1
                        border.color: Theme.stroke2
                        x: Math.max(0, Math.min(parent.width - width,
                                                seekArea.hoverFrac * seekArea.width - width / 2))
                        y: -height - 18
                        opacity: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0
                        scale: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0.85
                        transformOrigin: Item.Bottom
                        Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                        Image {
                            id: previewFrame
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 81
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            source: !veyra.liveTimeshift.enabled &&
                                    (seekMouse.containsMouse || seekArea.scrubbing) && seekArea.thumbnailBucket >= 0
                                    ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + (seekArea.thumbnailBucket * 2000)
                                    : ""
                        }
                        Text {
                            anchors.centerIn: previewFrame
                            visible: previewFrame.status === Image.Loading || previewFrame.status === Image.Error
                            text: previewFrame.status === Image.Loading ? qsTr("Loading") : qsTr("Preview unavailable")
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 23
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: veyra.liveTimeshift.enabled
                                  ? "−" + veyra.formatTime(veyra.liveTimeshift.end - veyra.timelineSeconds(seekArea.hoverFrac))
                                  : veyra.formatTime(veyra.timelineSeconds(seekArea.hoverFrac))
                            color: Theme.t1
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                            font.weight: Font.Medium; font.variableAxes: Theme.axesMedium
                            Rectangle {
                                anchors.fill: parent
                                z: -1
                                color: "#111111"
                            }
                        }
                    }
                    MouseArea {
                        id: seekMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        enabled: veyra.timelineSeekable
                        onEnabledChanged: if (!enabled && seekArea.scrubbing) {
                            scrubSeek.stop()
                            seekArea.scrubbing = false
                        }
                        cursorShape: Qt.PointingHandCursor
                        onEntered: thumbnailDelay.restart()
                        onExited: {
                            thumbnailDelay.stop()
                            seekArea.thumbnailBucket = -1
                        }
                        onPressed: mouse => {
                            seekArea.lastSeekTarget = -1
                            seekArea.scrubFrac = seekArea.fractionAt(mouse.x)
                            seekArea.scrubbing = true
                        }
                        onPositionChanged: mouse => {
                            if (seekArea.scrubbing) {
                                seekArea.scrubFrac = seekArea.fractionAt(mouse.x)
                                // restart(), not start(): each move pushes the deadline
                                // back, so the seek waits for the pointer to settle.
                                scrubSeek.restart()
                            }
                            thumbnailDelay.restart()
                        }
                        onReleased: mouse => {
                            const target = veyra.timelineSeconds(seekArea.fractionAt(mouse.x))
                            scrubSeek.stop()
                            seekArea.shownProgress = seekArea.fractionAt(mouse.x)
                            seekArea.scrubbing = false
                            veyra.logUi("ui-seek", "targetSeconds=" + target.toFixed(3))
                            // The timer may have fired on the last move and already
                            // started a seek to (very nearly) this position. A second
                            // request would interrupt that one and cost another full
                            // re-position, so a near-identical target is dropped.
                            if (Math.abs(target - seekArea.lastSeekTarget) > 0.5) {
                                seekArea.lastSeekTarget = target
                                veyra.seekTo(target)
                            }
                        }
                        onCanceled: {
                            scrubSeek.stop()
                            seekArea.scrubbing = false
                        }
                    }
                }
                Text {
                    text: veyra.durationText
                    visible: !veyra.liveTimeshift.available
                    color: Theme.t3
                    font.family: Theme.fontMono
                    font.pixelSize: 10
                }
                VButton {
                    objectName: "cine-return-live"
                    visible: veyra.liveTimeshift.available
                    ghost: true
                    text: qsTr("Go live")
                    onClicked: veyra.returnToLive()
                }
            }
        }

        // --- right: preset, volume, fullscreen (230px) ---------------
        RowLayout {
            Layout.preferredWidth: 308; Layout.minimumWidth: 308; Layout.maximumWidth: 308
            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            spacing: 6

            // .pill with a status dot: the dot reports the engine's state, and
            // the label is the preset actually in use.
            VPill {
                key: ""
                value: veyra.currentPresetName
                onClicked: presetMenu.openAt(this, "up")
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    color: veyra.failed ? Theme.err : veyra.captureRecovering ? Theme.warn : Theme.ok
                }
            }

            Item {
                implicitWidth: 146
                implicitHeight: 32
                Layout.minimumWidth: 146
                RowLayout {
                    anchors.fill: parent
                    spacing: 6
                    VIcon { name: "vol"; color: Theme.t2 }
                    VSlider {
                        id: volumeSlider
                        objectName: "cine-volume"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 72
                        from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                        defaultValue: 1
                        valueFromModel: true
                        fullKnobHitArea: true
                        onMoved: moved => veyra.volume = moved
                    }
                    VSliderValue {
                        Layout.preferredWidth: 36
                        slider: volumeSlider
                        text: Math.round(veyra.volume * 100) + "%"
                        color: Theme.t2; pixelSize: 10
                    }
                }
            }

            RotateButton { objectName: "cine-rotate" }

            Item {
                implicitWidth: 32; implicitHeight: 32
                VIcon {
                    anchors.centerIn: parent
                    name: "max"
                    color: Theme.t2
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                // The design's "Fullscreen" button (title="Fullscreen"). It used to call an
                // undefined root.requestPage("pro").
                TapHandler { onTapped: bar.requestFullscreen() }
            }
            // Hide the pill (field request 2026-10-01: it covers the game when playing
            // through a capture card or a stream). The dock's eye button brings it back.
            Item {
                objectName: "cine-hide"
                implicitWidth: 32; implicitHeight: 32
                VIcon { anchors.centerIn: parent; name: "eyeoff"; color: hideHover.hovered ? "#FFFFFF" : Theme.t2 }
                HoverHandler { id: hideHover; cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: hideHover.hovered
                ToolTip.text: qsTr("Hide the playback bar (it can be shown again from the dock at the top)")
                TapHandler { onTapped: bar.requestHide() }
            }
        }
    }

    // The preset menu: list presets and node presets in one menu, as designed.
    // pages-a.js presetMenu: list presets, node presets, then the way to manage them.
    VMenu {
        id: presetMenu
        aboveLimit: bar.menuBottomLimit
        title: qsTr("Presets")
        readonly property var mk: p => ({ label: p.name, note: p.note, checked: p.name === veyra.currentPresetName,
                                          tag: p.nodeMode ? qsTr("Nodes") : "", preset: p.index })
        items: [{ head: qsTr("List presets") }].concat(veyra.presets.filter(p => !p.nodeMode).map(mk))
            .concat([{ head: qsTr("Node presets") }]).concat(veyra.presets.filter(p => p.nodeMode).map(mk))
            .concat([{ sep: true }, { label: qsTr("Manage presets in Pro mode…"), icon: "sliders", act: "pro" }])
        onPicked: (i, o) => {
            if (o.act === "pro") bar.requestPage("pro")
            else veyra.applyPresetIndex(o.preset)
        }
    }

    // Subtitles (design: app.menu(anchor, 'Subtitles', [...])): the file's own tracks
    // (embedded and same-name external) from the subtitle loader, the primary
    // one checked, then loading a file and the settings dialog.
    VMenu {
        id: ccMenu
        aboveLimit: bar.menuBottomLimit
        title: qsTr("Subtitles")
        items: [{ label: qsTr("Off", "off"), checked: veyra.subtitlePrimary < 0, track: -1 }]
            .concat(veyra.subtitleTracks.map(t => ({ label: t.label, note: t.note, track: t.index,
                                                     disabled: !t.usable, checked: t.index === veyra.subtitlePrimary })))
            .concat([{ label: qsTr("Load external subtitles…"), icon: "import", act: "load" },
                     { sep: true },
                     { label: qsTr("Subtitle settings…"), note: qsTr("Font, size, outline, position, delay"), icon: "type", act: "dlg" }])
        onPicked: (i, o) => {
            if (o.act === "load") veyra.loadSubtitleDialog()
            else if (o.act === "dlg") bar.requestDialog("subtitle")
            else if (o.track !== undefined) veyra.subtitlePrimary = o.track
        }
    }

    // Load: the same six sources as the home page's cards, each opening its own dialog
    // (or the file picker) in the main window; the pill steps aside while one is open.
    VMenu {
        id: loadMenu
        objectName: "cine-load-menu"
        aboveLimit: bar.menuBottomLimit
        title: qsTr("Load source")
        items: [
            { label: qsTr("Open video"), note: qsTr("MP4 · MKV · images"), icon: "folder", act: "file" },
            { label: qsTr("Open media URL…"), note: "HTTP · HTTPS · RTSP", icon: "cast", act: "network" },
            { label: qsTr("Capture card"), note: qsTr("HDMI capture device"), icon: "video", act: "capture" },
            { label: qsTr("PC streaming"), note: qsTr("Sunshine host"), icon: "cast", act: "moonlight" },
            { label: qsTr("Xbox streaming"), note: qsTr("Account sign-in · experimental"), icon: "gamepad", act: "xbox" },
            { label: qsTr("Screen capture"), note: qsTr("Window or display"), icon: "monitor", act: "screen" }
        ]
        onPicked: (i, o) => {
            switch (o.act) {
            case "file": veyra.openFileDialog(); break
            case "network": veyra.openNetworkDialog(); break
            case "capture": veyra.openCaptureDialog(); break
            case "moonlight": veyra.openMoonlightDialog(); break
            case "xbox": veyra.openXboxDialog(); break
            case "screen": veyra.openScreenCaptureDialog(); break
            }
        }
    }

    // Audio: this one is real - the bridge exposes the file's audio streams and the
    // selected index, so the list and the check mark both come from the engine.
    VMenu {
        id: audioMenu
        aboveLimit: bar.menuBottomLimit
        title: qsTr("Audio tracks")
        // The bridge's label already carries language · title · codec; only the
        // channel count is separate.
        readonly property var mk: t => ({ label: t.label, index: t.index,
                                          note: t.channels > 0 ? (t.channels + qsTr(" ch")) : "",
                                          checked: t.index === veyra.selectedAudioTrack })
        items: veyra.audioTracks.length > 0
               ? veyra.audioTracks.map(mk).concat([{ sep: true },
                     { label: qsTr("Audio settings…"), note: qsTr("Output device, audio/video sync, offset"), icon: "music", act: "dlg" }])
               : [{ label: qsTr("The source has no audio tracks or is not open yet"), disabled: true }]
        onPicked: (i, o) => {
            if (o.act === "dlg") return bar.requestDialog("audio")
            if (o.index !== undefined) veyra.selectedAudioTrack = o.index
        }
    }
}
