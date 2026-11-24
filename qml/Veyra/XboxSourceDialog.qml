// xbox dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: xbDialog
    objectName: "xbox-dialog"
    key: "xbox"
    glyph: "gamepad"
    title: qsTr("Xbox streaming")
    sub: qsTr("Stream your own Xbox console · unofficial, experimental · enhancement and color use this app's own pipeline")
    dialogWidth: 680
    readonly property var xb: veyra.xbox
    readonly property var st: xb ? xb.state : ({})
    readonly property var list: xb ? xb.consoles : []
    actions: st.streaming ? [
        { label: qsTr("Close") },
        { label: qsTr("Disconnect"), primary: true }
    ] : !st.signedIn ? [
        { label: st.busy ? qsTr("Cancel") : qsTr("Close") }
    ] : [
        { label: st.busy ? qsTr("Cancel") : qsTr("Close") },
        { label: qsTr("Start streaming"), primary: true, icon: "play" }
    ]
    onActionTriggered: label => {
        if (label === qsTr("Disconnect")) { veyra.xboxDisconnect(); dialogHost.close(); return }
        if (label === qsTr("Cancel")) { xb.cancel(); return }
        if (label === qsTr("Close")) { dialogHost.close(); return }
        if (st.signedIn && !st.busy && (st.selected || "").length > 0) xb.connectStream()
    }
    Connections {
        target: xbDialog
        function onShownChanged() { if (xbDialog.shown && xbDialog.xb) { xbDialog.xb.load(); veyra.refreshControllers() } }
    }
    Connections {
        target: xbDialog.xb
        function onStarted() { dialogHost.close(); dialogHost.startXbox() }
    }

    DialogNote {
        text: qsTr("Unofficial: connects to Microsoft's streaming service the same way open-source clients such as Greenlight do. Not affiliated with Microsoft; it may stop working whenever Microsoft changes something. Only for your own account and console.")
    }
    DialogSection { text: qsTr("Account") }
    DialogGroup {
        VRow {
            label: xbDialog.st.signedIn ? qsTr("Signed in") : qsTr("Not signed in")
            hint: xbDialog.st.signedIn ? qsTr("Sign-in data is stored encrypted on this PC; signing out deletes it") : qsTr("Sign in on Microsoft's page with your Xbox (Microsoft) account; the app never sees the password")
            RowLayout {
                spacing: 6
                VSpinner { visible: xbDialog.st.busy === true; Layout.alignment: Qt.AlignVCenter }
                VButton { objectName: "xbox-signin"; visible: !xbDialog.st.signedIn; text: qsTr("Sign in"); primary: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signIn() }
                VButton { visible: !!xbDialog.st.signedIn; text: qsTr("Sign out"); ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signOut() }
            }
        }
    }
    // The device code, large, with the page to type it into.
    Rectangle {
        visible: (xbDialog.st.code || "").length > 0
        objectName: "xbox-code-box"
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: codeCol.implicitHeight + 28
        radius: 11
        color: Theme.accentSoft
        border.width: 1
        border.color: Theme.accent
        ColumnLayout {
            id: codeCol
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: qsTr("On your phone or browser, open ") + (xbDialog.st.verificationUri || "https://www.microsoft.com/link") + qsTr(", enter the sign-in code below:")
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "xbox-code"
                Layout.alignment: Qt.AlignHCenter
                text: xbDialog.st.code || ""
                color: Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: 30
                font.letterSpacing: 4
            }
            VButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("Open the sign-in page in the browser"); ghost: true; onClicked: xbDialog.xb.openSignInPage() }
        }
    }

    DialogSection { visible: !!xbDialog.st.signedIn; text: qsTr("Host") }
    Repeater {
        model: xbDialog.st.signedIn ? xbDialog.list : []
        delegate: Rectangle {
            id: consoleRow
            required property var modelData
            readonly property bool picked: xbDialog.st.selected === modelData.id
            objectName: "xbox-console-" + modelData.id
            Layout.fillWidth: true
            Layout.topMargin: 6
            implicitHeight: 54
            radius: 11
            color: picked ? Theme.accentSoft : (consoleHover.hovered ? Theme.card3 : Theme.card2)
            border.width: 1
            border.color: picked ? Theme.accent : Theme.stroke
            HoverHandler { id: consoleHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: xbDialog.xb.select(consoleRow.modelData.id)
                onDoubleTapped: { xbDialog.xb.select(consoleRow.modelData.id); if (!xbDialog.st.busy) xbDialog.xb.connectStream() }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 12
                VDot { off: consoleRow.modelData.power === qsTr("Power off"); warn: consoleRow.modelData.power === qsTr("Updating"); Layout.alignment: Qt.AlignVCenter }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { Layout.fillWidth: true; text: consoleRow.modelData.name; color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: consoleRow.modelData.type + " · " + consoleRow.modelData.power; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall; elide: Text.ElideRight }
                }
            }
        }
    }
    DialogGroup {
        visible: !!xbDialog.st.signedIn
        VRow {
            label: qsTr("Hosts")
            hint: qsTr("The console must be signed in with the same account, with remote features enabled under Settings → Devices & connections → Remote features")
            VButton { text: qsTr("Refresh"); ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.refresh() }
        }
        VRow {
            label: qsTr("Controller")
            hint: qsTr("Use this PC's controller as a controller on the console")
            VSwitch { objectName: "xbox-gamepad"; checked: xbDialog.st.gamepad !== false; onToggled: checked => xbDialog.xb.set("gamepad", checked) }
        }
        VRow {
            objectName: "xbox-controller-picker"
            label: qsTr("Controller device")
            hint: qsTr("Which connected gamepad sends input to the console; Auto picks the first one")
            VSelect {
                objectName: "xbox-controller"
                implicitWidth: 170
                options: veyra.controllerChoices || []
                value: {
                    const cur = xbDialog.st.controllerGuid || ""
                    const opt = (veyra.controllerChoices || []).find(o => o.id === cur)
                    return opt ? opt.label : qsTr("Auto")
                }
                onPicked: guid => veyra.setController(guid)
            }
        }
        VRow {
            objectName: "xbox-controller-status"
            label: qsTr("Active controller")
            hint: veyra.controllerName ? qsTr("This pad sends input while streaming")
                                       : ((veyra.controllerChoices || []).length > 1 ? qsTr("No pad is open yet; start a stream to use the selection") : qsTr("No controller detected"))
        }
        VRow {
            label: qsTr("Resolution")
            hint: qsTr("Requested quality tier; Auto lets the service pick the highest tier the device and network support")
            VSelect {
                objectName: "xbox-resolution"
                implicitWidth: 170
                options: xbDialog.st.resolutionLabels || []
                value: {
                    const cur = xbDialog.st.resolutionMode || "auto"
                    const opt = (xbDialog.st.resolutionLabels || []).find(o => o.id === cur)
                    return opt ? opt.label : "Auto"
                }
                onPicked: id => xbDialog.xb.set("resolutionMode", id)
            }
        }
        VRow {
            label: qsTr("Software video decoding")
            hint: qsTr("Compatibility option: try this if picture corruption persists, then disconnect and reconnect. May increase CPU usage and latency; super resolution and frame generation still use the GPU.")
            VSwitch { objectName: "xbox-software-decode"; checked: xbDialog.st.softwareDecode === true; onToggled: checked => xbDialog.xb.set("softwareDecode", checked) }
        }
        VRow {
            label: qsTr("Transport engine")
            hint: qsTr("V1 native is the default and the tested path. V2 enhanced runs the Chromium reference player in WebView2; its 1440p support is not verified on a physical console yet, so a 1440p selection uses V1.")
            hintBelow: true
            VSelect {
                objectName: "xbox-transport-engine"
                implicitWidth: 170
                title: qsTr("Transport engine")
                options: [{id:"1",label:qsTr("V1 native")},{id:"2",label:qsTr("V2 enhanced (WebView2)")}]
                value: xbDialog.st.transportEngine === 2 ? qsTr("V2 enhanced (WebView2)") : qsTr("V1 native")
                onPicked: id => xbDialog.xb.set("transportEngine", Number(id))
            }
        }
        VRow {
            visible: xbDialog.st.transportEngine === 2 && (xbDialog.st.resolutionMode || "") === "p1440"
            label: qsTr("1440p on V2")
            hint: qsTr("V2 1440p is not verified on a physical console. This session will use V1; the decoded size in the HUD is the proof.")
            hintBelow: true
        }
        VRow {
            label: qsTr("Hardware decode API")
            hint: qsTr("Auto uses D3D12VA. Pick D3D11VA (DXVA) if Intel integrated graphics corrupts the picture; both are hardware decode. Reconnect to apply.")
            hintBelow: true
            VSelect {
                objectName: "xbox-decode-backend"
                implicitWidth: 170
                title: qsTr("Hardware decode API")
                options: [{id:"0",label:qsTr("Auto (D3D12VA)")},{id:"1",label:qsTr("D3D11VA (DXVA)")}]
                value: xbDialog.st.decodeBackend === 1 ? qsTr("D3D11VA (DXVA)") : qsTr("Auto (D3D12VA)")
                onPicked: id => xbDialog.xb.set("decodeBackend", Number(id))
            }
        }
        VRow {
            label: qsTr("Bitrate cap (Mbps)")
            hint: qsTr("0 lets the console decide. Do not set this below the rate the console already sends; a lower b=AS can cap the stream.")
            hintBelow: true
            DialogTextField {
                objectName: "xbox-bitrate"
                implicitWidth: 110
                placeholder: "0"
                text: String(Math.round((xbDialog.st.videoBitrateKbps || 0) / 1000))
                onTyped: text => xbDialog.xb.set("videoBitrateKbps", Math.max(0, Math.min(40, parseInt(text) || 0)) * 1000)
            }
        }
        VRow {
            label: qsTr("Public STUN")
            hint: qsTr("Adds the reference clients' public STUN servers for NAT traversal. Leave off on a local network.")
            VSwitch { objectName: "xbox-use-stun"; checked: xbDialog.st.useStun === true; onToggled: checked => xbDialog.xb.set("useStun", checked) }
        }
        VRow {
            label: qsTr("Full Teredo")
            hint: qsTr("Adds the extra IPv4 candidates the reference clients try. Leave off unless connecting across NATs fails.")
            VSwitch { objectName: "xbox-full-teredo"; checked: xbDialog.st.fullTeredo === true; onToggled: checked => xbDialog.xb.set("fullTeredo", checked) }
        }
    }
    DialogNote { objectName: "xbox-status"; visible: (xbDialog.st.status || "").length > 0; text: (xbDialog.st.busy ? qsTr("Processing · ") : "") + (xbDialog.st.status || "") }
}
