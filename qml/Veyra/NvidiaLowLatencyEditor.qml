import QtQuick
import QtQuick.Layouts
ColumnLayout {
    id: root
    property string namePrefix: "list"
    property var backend: veyra
    readonly property bool available: root.backend.nvidiaLowLatencyAvailable === true
    Layout.fillWidth: true
    spacing: 4
    VRow {
        objectName: root.namePrefix+"-nvidia-low-latency-row"
        label: qsTr("NVIDIA Low Latency")
        hintBelow: true
        hint: !root.available ? qsTr("Requires an NVIDIA GPU") : ""
        VSeg {
            objectName: root.namePrefix+"-nvidia-low-latency"
            enabled: root.available
            opacity: enabled ? 1 : 0.4
            options: [{id:"0",label:qsTr("Off")},{id:"1",label:qsTr("On")}]
            // Another adapter shows Off: the NVIDIA choice is kept for the next
            // NVIDIA session but does nothing there.
            current: root.available ? (root.backend.nvidiaLowLatency > 0 ? "1" : "0") : "0"
            onPicked: id => root.backend.nvidiaLowLatency=Number(id)
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.backend.running ? root.backend.presentationStatus : qsTr("Applies when a video is opened")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
