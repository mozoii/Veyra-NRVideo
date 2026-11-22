// Professional-view audioCol controls; settings remain in the shared bridge.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    id: audioCol
    Layout.fillWidth: true
    spacing: 6
    VGroup {
        VRow {
            label: qsTr("Volume")
            value: Math.round(veyra.volume * 100) + "%"
            VSlider {
                implicitWidth: 150
                from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                defaultValue: 1
                onMoved: value => veyra.volume = value
            }
        }
        VRow {
            label: qsTr("Mute")
            VSwitch {
                checked: veyra.muted
                onToggled: checked => veyra.muted = checked
            }
        }
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
                onMoved: value => veyra.audioOffsetMs = Math.round(value)
            }
        }
    }
    VEyebrow { text: qsTr("Audio tracks") }
    Text {
        visible: veyra.audioTracks.length === 0
        text: qsTr("This source has no selectable audio tracks.")
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
    }
    Repeater {
        model: veyra.audioTracks
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 44
            radius: 12
            color: Theme.card2
            border.width: 1
            border.color: veyra.selectedAudioTrack === modelData.index ? Theme.accent : Theme.stroke
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12
                VEqBars {
                    active: veyra.selectedAudioTrack === modelData.index && veyra.running && !veyra.paused
                    opacity: veyra.selectedAudioTrack === modelData.index ? 1 : 0.6
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: modelData.label
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    Text {
                        text: modelData.channels > 0 ? modelData.channels + qsTr(" ch") : ""
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                    }
                }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: veyra.selectedAudioTrack = modelData.index }
        }
    }
}
