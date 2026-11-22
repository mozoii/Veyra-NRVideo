// audio dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    key: "audio"
    glyph: "music"
    title: qsTr("Audio settings")
    dialogWidth: 560
    actions: [{ label: qsTr("Done"), primary: true }]
    onActionTriggered: dialogHost.close()

    DialogSection { text: qsTr("Audio tracks") }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6
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
                implicitHeight: 52
                radius: 12
                color: Theme.card2
                border.width: 1
                border.color: veyra.selectedAudioTrack === modelData.index ? Theme.accent : Theme.stroke
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    // .trk .bars: the playing track's bars move (M39).
                    VEqBars {
                        objectName: "audio-track-bars-" + modelData.index
                        active: veyra.selectedAudioTrack === modelData.index && veyra.running && !veyra.paused
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
                    VIcon {
                        visible: veyra.selectedAudioTrack === modelData.index
                        name: "check"
                        color: Theme.accent
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: veyra.selectedAudioTrack = modelData.index }
            }
        }
    }
    DialogSection { text: qsTr("Output") }
    DialogGroup {
        VRow {
            label: qsTr("Volume")
            value: Math.round(veyra.volume * 100) + "%"
            VSlider {
                implicitWidth: 170
                from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                defaultValue: 1
                onMoved: veyra.volume = value
            }
        }
        VRow {
            label: qsTr("Mute")
            VSwitch {
                checked: veyra.muted
                onToggled: checked => veyra.muted = checked
            }
        }
    }
    DialogSection { text: qsTr("Audio/video sync") }
    DialogGroup {
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
            label: qsTr("Manual offset")
            hint: qsTr("Negative = sound earlier")
            value: veyra.audioOffsetMs + " ms"
            VSlider {
                implicitWidth: 170
                center: true
                from: -250; to: 250; value: veyra.audioOffsetMs; enabledControl: veyra.audioSyncMode === 1
                onMoved: veyra.audioOffsetMs = Math.round(value / 10) * 10
            }
        }
        VRow {
            label: qsTr("Current offset")
            hint: qsTr("Software-measured audio/video skew, not speaker-tested; only exists for live streaming input")
            value: veyra.audioSkewKnown ? (veyra.audioSkewMs >= 0 ? "+" : "") + veyra.audioSkewMs.toFixed(0) + " ms" : qsTr("N/A")
        }
    }
    DialogSection { text: qsTr("Output device") }
    DialogGroup {
        VRow {
            label: qsTr("Output device")
            hint: veyra.audioOutputStatus
            VSelect {
                objectName: "audio-device"
                implicitWidth: 240
                readonly property string chosen: veyra.preferences.audioDevice || ""
                value: { const d = veyra.audioDevices.find(x => x.id === chosen); return d ? d.label : qsTr("Follow the system default") }
                options: veyra.audioDevices
                onPicked: id => veyra.setPreference("audioDevice", id)
            }
        }
        VRow {
            label: qsTr("Stereo downmix")
            hint: qsTr("Mix multichannel sources down to stereo (headphones); when off, channels follow the device layout")
            VSwitch {
                objectName: "audio-stereo"
                checked: veyra.preferences.audioForceStereo === true
                onToggled: checked => veyra.setPreference("audioForceStereo", checked)
            }
        }
    }
    // The list is read when the dialog opens: devices come and go.
    Connections {
        target: dialogHost
        function onDialogChanged() { if (dialogHost.dialog === "audio") veyra.refreshAudioDevices() }
    }
}
