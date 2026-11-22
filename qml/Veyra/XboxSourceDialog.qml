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
        function onShownChanged() { if (xbDialog.shown && xbDialog.xb) xbDialog.xb.load() }
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
    }
    DialogNote { objectName: "xbox-status"; visible: (xbDialog.st.status || "").length > 0; text: (xbDialog.st.busy ? qsTr("Processing · ") : "") + (xbDialog.st.status || "") }
}
