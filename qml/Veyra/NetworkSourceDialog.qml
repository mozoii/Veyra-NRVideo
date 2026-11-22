// network dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: networkDialog
    objectName: "network-dialog"
    key: "network"
    glyph: "cast"
    title: qsTr("Open media URL")
    sub: qsTr("Open a video stream over HTTP, HTTPS, RTSP, RTP, or UDP.")
    dialogWidth: 640
    actions: [{label:qsTr("Cancel")},{label:qsTr("Open"),primary:true,icon:"play"}]
    onActionTriggered: label => {
        if(label!==qsTr("Open")){dialogHost.close();return}
        if(veyra.openNetworkAddress(networkAddress.text))dialogHost.close()
    }
    DialogTextField {
        id: networkAddress
        objectName: "network-address"
        Layout.fillWidth: true
        placeholder: "https://… / rtsp://… / rtp://… / udp://…"
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Use a direct media URL. Include a port in RTP and UDP MPEG-TS URLs.")
        wrapMode: Text.Wrap
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 12
    }
    VRow {
        label: qsTr("Live timeshift")
        hint: qsTr("Keep recent live content in up to 256 MiB of memory. Higher bitrates shorten the available window.")
        VSeg {
            objectName: "network-timeshift"
            options: [{id:"0",label:qsTr("Off", "off")},{id:"60",label:qsTr("1 min")},
                      {id:"120",label:qsTr("2 min")},{id:"300",label:qsTr("5 min")}]
            current: String(veyra.preferences.networkTimeshift || 0)
            onPicked: id => veyra.setPreference("networkTimeshift", Number(id))
        }
    }
}
