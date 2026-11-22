// subtitle dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: subtitleDialog
    objectName: "subtitle-dialog"
    key: "subtitle"
    glyph: "type"
    title: qsTr("Subtitle settings")
    sub: qsTr("Live preview; settings apply to every file")
    dialogWidth: 620
    actions: [{ label: qsTr("Reset to default") }, { label: qsTr("Done"), primary: true }]
    onActionTriggered: label => {
        if (label !== qsTr("Reset to default")) { dialogHost.close(); return }
        // The look only: tracks and the timing offset belong to the file.
        const defaults = { subtitleEnabled: true, subtitleSize: 22, subtitleFont: 0, subtitleOutline: 2,
                           subtitleBackground: false, subtitleMargin: 0, subtitleFit: false, subtitleLines: 2 }
        for (const key in defaults) veyra.setPreference(key, defaults[key])
        veyra.logUi("ui-subtitle", "style reset to defaults")
    }

    readonly property var prefs: veyra.preferences
    function pref(key, fallback) { return prefs[key] !== undefined ? prefs[key] : fallback }
    readonly property var trackOptions: [{ id: "-1", label: qsTr("Off", "off") }]
        .concat(veyra.subtitleTracks.map(t => ({ id: String(t.index), label: t.label + " · " + t.note, disabled: !t.usable })))
    function trackLabel(index) {
        const t = veyra.subtitleTracks.find(x => x.index === index)
        return t ? t.label : qsTr("Off", "off")
    }
    VRow {
        visible: (veyra.liveTranslation || null) !== null
        label: qsTr("Live translation")
        VButton {
            objectName: "subtitle-live-translation"
            text: qsTr("Live translation settings…")
            onClicked: dialogHost.open("translation")
        }
    }

    // dialogs.js .sub-prev: a sample line over the picture, drawn with the
    // current style so every change shows at once.
    Rectangle {
        id: subPreview
        objectName: "subtitle-preview"
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: 180
        radius: 10
        border.width: 1
        border.color: Theme.stroke
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: "#1B2230" }
            GradientStop { position: 1; color: "#0B0C10" }
        }
        readonly property real scaleFactor: height / 540
        Image {
            anchors.fill: parent
            anchors.margins: 1
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: status === Image.Ready
            source: subtitleDialog.shown && veyra.hasSource && !veyra.isCapture && veyra.duration > 0
                    ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + Math.floor(veyra.position / 2) * 2000 : ""
        }
        Column {
            objectName: "subtitle-preview-lines"
            visible: subtitleDialog.pref("subtitleEnabled", true)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12 + subtitleDialog.pref("subtitleMargin", 0) * subPreview.scaleFactor
            spacing: 2
            Repeater {
                model: veyra.subtitleText.length > 0 ? veyra.subtitleText.split("\n").slice(0, 2)
                                                     : [qsTr("This is a subtitle preview."), qsTr("Changes show here live.")]
                delegate: Rectangle {
                    required property string modelData
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: line.implicitWidth + 16
                    height: line.implicitHeight + 2
                    radius: 4
                    color: subtitleDialog.pref("subtitleBackground", false) ? Qt.rgba(0, 0, 0, 0.6) : "transparent"
                    Text {
                        id: line
                        anchors.centerIn: parent
                        text: modelData
                        color: "#FFFFFF"
                        readonly property var families: ["", "SimHei", "SimSun", "DengXian", "Arial", "Segoe UI"]
                        font.family: families[subtitleDialog.pref("subtitleFont", 0)] || Theme.fontUi
                        font.pixelSize: Math.max(12, subtitleDialog.pref("subtitleSize", 22) * 0.8)
                        font.weight: Font.DemiBold
                        style: subtitleDialog.pref("subtitleOutline", 2) > 0 ? Text.Outline : Text.Normal
                        styleColor: Qt.rgba(0, 0, 0, subtitleDialog.pref("subtitleOutline", 2) >= 3 ? 1 : 0.85)
                    }
                }
            }
        }
    }

    DialogSection { text: qsTr("Subtitle tracks") }
    DialogGroup {
        VRow {
            label: qsTr("Main subtitles")
            hint: veyra.subtitleTracks.length === 0 ? (veyra.subtitleStatus.length > 0 ? veyra.subtitleStatus : (veyra.hasSource ? qsTr("This source has no subtitles") : qsTr("Available subtitles appear once a video is open"))) : ""
            VSelect {
                objectName: "subtitle-primary"
                implicitWidth: 220
                value: subtitleDialog.trackLabel(veyra.subtitlePrimary)
                options: subtitleDialog.trackOptions
                onPicked: id => veyra.subtitlePrimary = Number(id)
            }
        }
        VRow {
            label: qsTr("Secondary subtitles (bilingual)")
            hint: qsTr("Shown above the main subtitles")
            VSelect {
                objectName: "subtitle-secondary"
                implicitWidth: 220
                value: subtitleDialog.trackLabel(veyra.subtitleSecondary)
                options: subtitleDialog.trackOptions
                onPicked: id => veyra.subtitleSecondary = Number(id)
            }
        }
        VRow {
            label: qsTr("External subtitles")
            hint: qsTr("Text · PGS/SUP · VobSub (IDX+SUB)")
            VButton { objectName: "subtitle-load"; text: qsTr("Load file…"); onClicked: veyra.loadSubtitleDialog() }
        }
        VRow {
            label: qsTr("Show secondary subtitles by default")
            hint: qsTr("Pick the second available subtitle track automatically when opening a new file")
            VSwitch {
                checked: subtitleDialog.pref("subtitleSecondLanguage", false)
                onToggled: checked => veyra.setPreference("subtitleSecondLanguage", checked)
            }
        }
    }
    DialogSection { text: qsTr("Time") }
    DialogGroup {
        VRow {
            label: qsTr("Main subtitle delay")
            hint: qsTr("Z / X nudge by 50 ms, with Shift 1 s; positive = subtitles later")
            // .stepper: − value +, 50 ms a step; the value can be typed.
            RowLayout {
                objectName: "subtitle-delay-stepper"
                spacing: 4
                VButton { objectName: "subtitle-delay-minus"; icon: true; iconName: "minus"; implicitWidth: 28; implicitHeight: 28; onClicked: veyra.nudgeSubtitle(-50) }
                DialogTextField {
                    objectName: "subtitle-delay-value"
                    implicitWidth: 86
                    text: veyra.subtitleOffsetMs + " ms"
                    onEdited: text => { const v = parseInt(text); if (isFinite(v)) veyra.subtitleOffsetMs = v }
                }
                VButton { objectName: "subtitle-delay-plus"; icon: true; iconName: "plus"; implicitWidth: 28; implicitHeight: 28; onClicked: veyra.nudgeSubtitle(50) }
            }
        }
        VRow {
            label: qsTr("Auto-align by audio track")
            hint: veyra.subtitleStatus.length > 0 ? veyra.subtitleStatus : qsTr("Analyzes speech and subtitle timing in the first 30 minutes")
            VButton {
                text: veyra.subtitleAligning ? qsTr("Analyzing…") : qsTr("Auto-align")
                enabled: !veyra.subtitleAligning && veyra.subtitlePrimary >= 0
                onClicked: veyra.autoAlignSubtitle()
            }
        }
    }
    DialogSection { text: qsTr("Style") }
    DialogGroup {
        VRow {
            label: qsTr("Show subtitles")
            hint: qsTr("Shortcut B")
            VSwitch {
                objectName: "subtitle-enabled"
                checked: subtitleDialog.pref("subtitleEnabled", true)
                onToggled: checked => veyra.setPreference("subtitleEnabled", checked)
            }
        }
        VRow {
            label: qsTr("Font size")
            value: subtitleDialog.pref("subtitleSize", 22) + " px"
            VSlider {
                objectName: "subtitle-size"
                implicitWidth: 170
                from: 16; to: 56; value: subtitleDialog.pref("subtitleSize", 22)
                onMoved: veyra.setPreference("subtitleSize", Math.round(value))
            }
        }
        VRow {
            label: qsTr("Font")
            VSelect {
                objectName: "subtitle-font"
                implicitWidth: 170
                readonly property var names: [qsTr("Subtitle's own font"), qsTr("SimHei"), qsTr("SimSun"), qsTr("DengXian"), "Arial", "Segoe UI"]
                value: names[subtitleDialog.pref("subtitleFont", 0)]
                options: names.map((n, i) => ({ id: String(i), label: n }))
                onPicked: id => veyra.setPreference("subtitleFont", Number(id))
            }
        }
        VRow {
            label: qsTr("Outline")
            VSeg {
                objectName: "subtitle-outline"
                options: [{ id: "0", label: qsTr("None") }, { id: "1", label: qsTr("Thin") }, { id: "2", label: qsTr("Standard") }, { id: "3", label: qsTr("Coarse") }]
                current: String(subtitleDialog.pref("subtitleOutline", 2))
                onPicked: id => veyra.setPreference("subtitleOutline", Number(id))
            }
        }
        VRow {
            label: qsTr("Background bar")
            VSwitch {
                checked: subtitleDialog.pref("subtitleBackground", false)
                onToggled: checked => veyra.setPreference("subtitleBackground", checked)
            }
        }
        VRow {
            label: qsTr("Distance from bottom")
            value: subtitleDialog.pref("subtitleMargin", 0) + " px"
            VSlider {
                objectName: "subtitle-margin"
                implicitWidth: 170
                from: 0; to: 240; value: subtitleDialog.pref("subtitleMargin", 0)
                onMoved: veyra.setPreference("subtitleMargin", Math.round(value))
            }
        }
        VRow {
            label: qsTr("Shrink long lines automatically")
            hint: qsTr("Keeps the font size by default; when on, shrinks only beyond the target number of lines")
            VSwitch {
                checked: subtitleDialog.pref("subtitleFit", false)
                onToggled: checked => veyra.setPreference("subtitleFit", checked)
            }
        }
        VRow {
            visible: subtitleDialog.pref("subtitleFit", false)
            label: qsTr("Target lines")
            value: String(subtitleDialog.pref("subtitleLines", 2))
            VSlider {
                implicitWidth: 170
                from: 1; to: 8; value: subtitleDialog.pref("subtitleLines", 2)
                onMoved: veyra.setPreference("subtitleLines", Math.round(value))
            }
        }
    }
}
