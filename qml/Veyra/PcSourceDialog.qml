// moonlight dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: mlDialog
    objectName: "moonlight-dialog"
    key: "moonlight"
    glyph: "cast"
    title: qsTr("PC streaming")
    sub: qsTr("Stream another PC (Sunshine host) · enhancement and color use this app's own pipeline")
    dialogWidth: 760
    readonly property var ml: veyra.moonlight
    readonly property var st: ml ? ml.state : ({})
    readonly property var hostList: ml ? ml.hosts : []
    readonly property var appList: ml ? ml.apps : []
    readonly property var cfg: st.settings || ({})
    property int pickedApp: -1
    readonly property int effectiveApp: pickedApp >= 0 ? pickedApp
        : (st.runningApp > 0 ? st.runningApp : (appList.length > 0 ? appList[0].id : -1))
    readonly property bool canStart: !!st.paired && !!st.online && !st.busy && effectiveApp >= 0 && !st.streaming
    actions: st.streaming ? [
        { label: qsTr("Close") },
        { label: qsTr("Disconnect"), primary: true }
    ] : [
        { label: st.busy ? qsTr("Cancel") : qsTr("Close") },
        { label: qsTr("Start streaming"), primary: true, icon: "play" }
    ]
    onActionTriggered: label => {
        if (label === qsTr("Disconnect")) { veyra.moonlightDisconnect(); dialogHost.close(); return }
        if (label === qsTr("Cancel")) { ml.cancel(); return }
        if (label === qsTr("Close")) { dialogHost.close(); return }
        if (canStart) ml.connectStream(effectiveApp)
    }
    Connections {
        target: mlDialog
        function onShownChanged() {
            if (!mlDialog.ml) return
            if (mlDialog.shown) { mlDialog.pickedApp = -1; mlDialog.ml.load() }
            else mlDialog.ml.unload()
        }
    }
    Connections {
        target: mlDialog.ml
        function onStarted() { dialogHost.close(); dialogHost.startMoonlight() }
    }
    readonly property var resolutions: [
        { id: "720p", label: "720p · 1280×720" }, { id: "1080p", label: "1080p · 1920×1080" },
        { id: "1440p", label: "1440p · 2560×1440" }, { id: "4k", label: "4K · 3840×2160" },
        { id: "native", label: qsTr("This PC's screen resolution") }]
    readonly property var rates: [30, 60, 90, 120, 144].map(v => ({ id: String(v), label: v + " fps" }))
    function labelOf(list, id, fallback) { const f = list.find(x => String(x.id) === String(id)); return f ? f.label : fallback }
    function stateText(s) {
        return s === "online" ? qsTr("Online") : s === "unpaired" ? qsTr("Not paired") : s === "busy" ? qsTr("Online · a game is running")
             : s === "checking" ? qsTr("Checking…") : qsTr("Offline")
    }

    DialogSection { text: qsTr("Host") }
    DialogNote {
        visible: mlDialog.hostList.length === 0
        text: qsTr("No hosts yet. Install Sunshine on the PC you want to stream (official site app.lizardbyte.dev/Sunshine; not bundled with this app),")
            + qsTr("With both PCs on the same LAN, the host appears here automatically; you can also enter its IP below. Pairing is needed the first time.")
    }
    Repeater {
        model: mlDialog.hostList
        delegate: Rectangle {
            id: hostRow
            required property var modelData
            Layout.fillWidth: true
            Layout.topMargin: 6
            implicitHeight: 54
            radius: 11
            color: modelData.selected ? Theme.accentSoft : (hostHover.hovered ? Theme.card3 : Theme.card2)
            border.width: 1
            border.color: modelData.selected ? Theme.accent : Theme.stroke
            objectName: "moonlight-host-" + modelData.id
            HoverHandler { id: hostHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: mlDialog.ml.select(hostRow.modelData.id) }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 12
                VDot {
                    warn: hostRow.modelData.state === "unpaired"
                    off: hostRow.modelData.state === "offline" || hostRow.modelData.state === "checking"
                    Layout.alignment: Qt.AlignVCenter
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: hostRow.modelData.name
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: hostRow.modelData.address + " · " + mlDialog.stateText(hostRow.modelData.state)
                            + (hostRow.modelData.gpu.length > 0 ? " · " + hostRow.modelData.gpu : "")
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSmall
                        elide: Text.ElideRight
                    }
                }
                VTag { visible: hostRow.modelData.paired; text: qsTr("Paired"); kind: "ok" }
                VButton {
                    visible: !hostRow.modelData.paired && hostRow.modelData.state !== "offline"
                    objectName: "moonlight-pair-" + hostRow.modelData.id
                    text: qsTr("Pair")
                    primary: true
                    enabled: !mlDialog.st.busy
                    onClicked: mlDialog.ml.pair(hostRow.modelData.id)
                }
                VButton {
                    text: qsTr("Delete")
                    ghost: true
                    enabled: !mlDialog.st.busy
                    onClicked: mlDialog.ml.forget(hostRow.modelData.id)
                }
            }
        }
    }
    DialogGroup {
        VRow {
            label: qsTr("Add manually")
            hint: qsTr("IP or host name; if the port is not 47989, write it as 192.168.1.20:47989")
            RowLayout {
                spacing: 6
                DialogTextField { id: mlAddress; objectName: "moonlight-address"; implicitWidth: 190; placeholder: "192.168.1.x"; onEdited: text => { if (text.length > 0) { mlDialog.ml.addHost(text); mlAddress.text = "" } } }
                VSpinner { visible: mlDialog.st.busy === true; Layout.alignment: Qt.AlignVCenter }
                VButton { text: qsTr("Add"); ghost: true; enabled: !mlDialog.st.busy && mlAddress.text.length > 0; onClicked: { mlDialog.ml.addHost(mlAddress.text); mlAddress.text = "" } }
                VButton { text: qsTr("Refresh"); ghost: true; enabled: !mlDialog.st.busy; onClicked: mlDialog.ml.refresh() }
            }
        }
    }
    // The PIN the user types into the host's Sunshine page.
    Rectangle {
        visible: mlDialog.st.pairing === true
        objectName: "moonlight-pin-box"
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: pinCol.implicitHeight + 28
        radius: 11
        color: Theme.accentSoft
        border.width: 1
        border.color: Theme.accent
        ColumnLayout {
            id: pinCol
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: qsTr("On the host, open the Sunshine web page (https://host-IP:47990), go to the PIN page, enter the number below and submit:")
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "moonlight-pin"
                Layout.alignment: Qt.AlignHCenter
                text: mlDialog.st.pin || ""
                color: Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: 34
                font.letterSpacing: 10
            }
        }
    }

    DialogSection { visible: !!mlDialog.st.paired; text: qsTr("Games and programs · ") + (mlDialog.st.hostName || "") }
    DialogNote {
        visible: !!mlDialog.st.selected && !mlDialog.st.paired && !mlDialog.st.pairing
        text: qsTr("This host is not paired with this PC yet. Click Pair and enter the PIN the app shows in the host's Sunshine web page.")
    }
    DialogNote {
        visible: !!mlDialog.st.paired && !mlDialog.st.online
        text: qsTr("The host cannot be reached right now (offline, asleep, or its address changed). Turn it on and click Refresh.")
    }
    Flow {
        visible: !!mlDialog.st.paired && !!mlDialog.st.online
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: mlDialog.appList
            delegate: Rectangle {
                id: appCard
                required property var modelData
                readonly property bool picked: mlDialog.effectiveApp === modelData.id
                objectName: "moonlight-app-" + modelData.id
                width: 168
                height: 64
                radius: 11
                color: picked ? Theme.accentSoft : (appHover.hovered ? Theme.card3 : Theme.card2)
                border.width: 1
                border.color: picked ? Theme.accent : Theme.stroke
                HoverHandler { id: appHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: mlDialog.pickedApp = appCard.modelData.id
                    onDoubleTapped: { mlDialog.pickedApp = appCard.modelData.id; if (mlDialog.canStart) mlDialog.ml.connectStream(appCard.modelData.id) }
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: appCard.modelData.name
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 6
                        VTag { visible: appCard.modelData.running; text: qsTr("Running"); kind: "acc" }
                        VTag { visible: appCard.modelData.hdr; text: "HDR" }
                    }
                }
            }
        }
    }
    RowLayout {
        visible: !!mlDialog.st.paired && !!mlDialog.st.online && mlDialog.st.runningApp > 0
        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: qsTr("A game is running on the host. Disconnecting does not end it; select it and start to continue.")
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
        VButton { objectName: "moonlight-quit-app"; text: qsTr("Quit game"); ghost: true; enabled: !mlDialog.st.busy; onClicked: mlDialog.ml.quitApp() }
    }

    DialogSection { text: qsTr("Streaming settings (affect only the stream itself; saved per host)") }
    DialogGroup {
        VRow {
            label: qsTr("Resolution")
            hint: qsTr("The host encodes at this size; the app's super resolution and enhancement run after the stream arrives")
            VSelect { objectName: "moonlight-res"; implicitWidth: 190; value: mlDialog.labelOf(mlDialog.resolutions, mlDialog.cfg.res, "1080p · 1920×1080"); options: mlDialog.resolutions; onPicked: id => mlDialog.ml.set("res", id) }
        }
        VRow {
            label: qsTr("Frame rate")
            VSelect { objectName: "moonlight-fps"; implicitWidth: 130; value: (mlDialog.cfg.fps || 60) + " fps"; options: mlDialog.rates; onPicked: id => mlDialog.ml.set("fps", Number(id)) }
        }
        VRow {
            label: qsTr("Bitrate")
            hint: qsTr("Default 150 Mbps; wired gigabit can go to 300–500, lower it if wireless stutters. When the host picture is simple, the actual bitrate stays below this cap")
            RowLayout {
                spacing: 10
                VSlider {
                    id: bitrateSlider
                    objectName: "moonlight-bitrate"
                    implicitWidth: 170
                    from: 5; to: 500
                    live: false
                    value: mlDialog.cfg.bitrate || 150
                    onMoved: v => mlDialog.ml.set("bitrate", Math.max(5, Math.round(v / 5) * 5))
                }
                VSliderValue {
                    Layout.preferredWidth: 72
                    slider: bitrateSlider
                    horizontalAlignment: Text.AlignLeft
                    text: Math.max(5, Math.round((bitrateSlider.dragging ? bitrateSlider.dragValue : bitrateSlider.value) / 5) * 5) + " Mbps"
                }
            }
        }
        VRow {
            label: qsTr("Codec")
            hint: mlDialog.cfg.codec === 3 ? qsTr("AV1 needs a host GPU that can encode it and a local GPU that can decode it") : qsTr("Auto: HEVC first, H.264 when the host does not support it")
            VSeg {
                objectName: "moonlight-codec"
                options: [{ id: "0", label: qsTr("Auto") }, { id: "1", label: "H.264" }, { id: "2", label: "HEVC" }, { id: "3", label: "AV1" }]
                current: String(mlDialog.cfg.codec || 0)
                onPicked: id => mlDialog.ml.set("codec", Number(id))
            }
        }
        VRow {
            label: "HDR"
            hint: mlDialog.st.hdrHost ? qsTr("Needs HDR on for the host display and HEVC or AV1 as the codec; this PC needs hardware decoding") : qsTr("This host did not report 10-bit encoding support")
            VSwitch { objectName: "moonlight-hdr"; enabled: !!mlDialog.st.hdrHost || mlDialog.cfg.hdr === true; checked: mlDialog.cfg.hdr === true; onToggled: checked => mlDialog.ml.set("hdr", checked) }
        }
        VRow {
            label: qsTr("Channels")
            VSeg {
                objectName: "moonlight-audio"
                options: [{ id: "2", label: qsTr("Stereo") }, { id: "6", label: "5.1" }, { id: "8", label: "7.1" }]
                current: String(mlDialog.cfg.audio || 2)
                onPicked: id => mlDialog.ml.set("audio", Number(id))
            }
        }
        VRow {
            label: qsTr("Controller")
            hint: qsTr("Use this PC's controller as Xbox controller 1 on the console")
            VSwitch { objectName: "moonlight-gamepad"; checked: mlDialog.cfg.gamepad !== false; onToggled: checked => mlDialog.ml.set("gamepad", checked) }
        }
        VRow {
            label: qsTr("Capture keyboard and mouse automatically")
            hint: qsTr("Once started, keyboard and mouse go to the host; Ctrl+Alt+Shift+Z releases, +Q disconnects, +S shows stats")
            VSwitch { objectName: "moonlight-capture"; checked: mlDialog.cfg.captureInput !== false; onToggled: checked => mlDialog.ml.set("captureInput", checked) }
        }
        VRow {
            label: qsTr("Have the host switch to the stream resolution")
            hint: qsTr("When off, the host keeps its own resolution and scales the picture itself")
            VSwitch { objectName: "moonlight-sops"; checked: mlDialog.cfg.sops === true; onToggled: checked => mlDialog.ml.set("sops", checked) }
        }
        VRow {
            label: qsTr("Also play sound on the host")
            hint: qsTr("Sound plays only on this PC by default")
            VSwitch { objectName: "moonlight-hostaudio"; checked: mlDialog.cfg.hostAudio === true; onToggled: checked => mlDialog.ml.set("hostAudio", checked) }
        }
    }
    DialogNote { objectName: "moonlight-status"; visible: (mlDialog.st.status || "").length > 0; text: (mlDialog.st.busy ? qsTr("Processing · ") : "") + (mlDialog.st.status || "") }
}
