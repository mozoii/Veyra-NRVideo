// save dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: saveDialog
    objectName: "preset-save-dialog"
    key: "save"
    glyph: "plus"
    readonly property string kindLabel: veyra.nodeMode === 1 ? qsTr("Nodes") : qsTr("List")
    title: qsTr("Save as") + kindLabel + qsTr("Presets")
    sub: kindLabel + qsTr("Presets and") + (veyra.nodeMode === 1 ? qsTr("List") : qsTr("Nodes")) + qsTr("Presets are saved separately and do not affect each other; applying one only overwrites the checked parts")
    dialogWidth: 580
    // presets.js currentItems: what the chain holds right now, one row per
    // node, with the parameters that matter.
    function nodeSummary(n) {
        switch (n.type) {
        case "sr": return veyra.srTargetLabel + (veyra.srQualityLabel(veyra.videoSrQuality, true) ? " · " + veyra.srQualityLabel(veyra.videoSrQuality, true) : "")
        case "nr": { const l = veyra.nrLayers.find(x => x.index === n.index); return l ? qsTr("Strength ") + l.intensity.toFixed(2) : "NR" }
        case "video-hdr": return qsTr("Peak ") + (veyra.videoHdrParams.peakNits || 1000) + " nits"
        case "protection": return veyra.protectionRegions.length + qsTr(" / 4 areas")
        case "color": return qsTr("Color settings")
        case "frame-generation": return veyra.fgMultiplier + "X · " + (veyra.fgBackendChoices.find(x => x.id === veyra.fgBackendName) || {label: "FG"}).label
        }
        return ""
    }
    function nodeLabel(n) {
        if (n.type !== "nr") return n.label
        let k = 0
        for (const m of veyra.chain) { if (m.type === "nr") ++k; if (m.index === n.index) break }
        return qsTr("NR layer ") + k
    }
    readonly property bool nameTaken: presetNameField.text.trim().length > 0 &&
        veyra.presets.some(p => p.name === presetNameField.text.trim())
    // The name column fits the longest node name in the interface language: a fixed
    // 110px cut "NR protected areas" and "DLSS Frame Generation".
    FontMetrics { id: nodeNameMetrics; font.family: Theme.fontUi; font.pixelSize: 12; font.weight: Font.DemiBold }
    readonly property real nameColumn: {
        let w = 110
        if (shown) for (const n of veyra.chain) w = Math.max(w, Math.ceil(nodeNameMetrics.advanceWidth(nodeLabel(n))) + 4)
        return Math.min(w, 200)
    }
    property var chosen: ({})
    property var parts: []
    function resetForOpen() {
        const current = veyra.presetSaveParts
        const selected = {}
        for (const part of current) selected[part.id] = part.meaningful === true
        parts = current
        chosen = selected
        presetNameField.text = ""
        saveNote.text = ""
        saveAsDefault.checked = false
    }
    actions: [
        { label: qsTr("Cancel") },
        { label: qsTr("Save preset"), primary: true, icon: "check" }
    ]
    onActionTriggered: label => {
        if (label !== qsTr("Save preset")) { dialogHost.close(); return }
        const name = presetNameField.text.trim()
        if (name.length === 0) { saveNote.text = qsTr("The preset needs a name"); return }
        let mask = 0
        const ids = ["chain", "color", "fg", "audio", "flow"]
        for (const part of parts) {
            const bit = ids.indexOf(part.id)
            if (bit >= 0 && chosen[part.id] === true) mask |= (1 << bit)
        }
        if (mask === 0) { saveNote.text = qsTr("Choose at least one item"); return }
        if (veyra.savePresetAs(name, mask, veyra.nodeMode === 1)) {
            if (saveAsDefault.checked) {
                const saved = veyra.presets.find(p => p.name === name && p.nodeMode === (veyra.nodeMode === 1))
                if (saved) veyra.setDefaultPreset(saved.index)
            }
            dialogHost.close()
        }
        else saveNote.text = qsTr("Save failed: the name may already exist")
    }

    // .namebox: the name first, with a live note under it.
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 6
        Text { text: qsTr("Preset name"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12 }
        DialogTextField {
            id: presetNameField
            objectName: "preset-name"
            Layout.fillWidth: true
            placeholder: qsTr("e.g. Night gaming")
        }
        Text {
            objectName: "preset-name-hint"
            Layout.fillWidth: true
            text: saveDialog.nameTaken ? qsTr("A preset with this name already exists; choose another name to save")
                  : presetNameField.text.trim().length > 0 ? qsTr("Can be saved")
                  : qsTr("The name appears in the preset menus of Minimal mode and the Export page")
            color: saveDialog.nameTaken ? Theme.warn : Theme.ok
            font.family: Theme.fontUi
            font.pixelSize: 11
        }
    }
    DialogSection { text: qsTr("What will be saved · what is in use now") + saveDialog.kindLabel + qsTr("Settings") }
    // .savelist: every node, a green dot when it runs; the list dims when
    // the effect chain itself is not part of the preset.
    ColumnLayout {
        objectName: "preset-save-list"
        Layout.fillWidth: true
        spacing: 4
        opacity: saveDialog.chosen["chain"] === true ? 1 : 0.45
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        Repeater {
            model: saveDialog.shown ? veyra.chain : []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 34
                radius: 8
                color: Qt.rgba(1, 1, 1, 0.03)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Rectangle {
                        implicitWidth: 7; implicitHeight: 7; radius: 3.5
                        color: modelData.enabled ? Theme.ok : Qt.rgba(1, 1, 1, 0.25)
                    }
                    Text {
                        Layout.preferredWidth: saveDialog.nameColumn
                        text: saveDialog.nodeLabel(modelData)
                        color: modelData.enabled ? Theme.t1 : Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 12
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: saveDialog.nodeSummary(modelData)
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    VTag { visible: !modelData.enabled; text: qsTr("Off") }
                }
            }
        }
    }
    DialogSection { text: qsTr("Options") }
    DialogGroup {
        Repeater {
            model: saveDialog.shown ? saveDialog.parts : []
            delegate: VRow {
                required property var modelData
                label: qsTr("Includes") + modelData.label
                hint: modelData.summary
                VSwitch {
                    objectName: "preset-part-" + modelData.id
                    // A part at its default is still offered, but starts off: a
                    // preset that stored nothing would be a surprise.
                    checked: saveDialog.chosen[modelData.id] === true
                    onToggled: checked => {
                        const c = Object.assign({}, saveDialog.chosen)
                        c[modelData.id] = checked
                        saveDialog.chosen = c
                    }
                }
            }
        }
        VRow {
            label: qsTr("Make it the startup default after saving")
            hint: qsTr("Apply this preset automatically the next time the app starts")
            VSwitch { id: saveAsDefault; objectName: "preset-save-default"; checked: false; onToggled: v => checked = v }
        }
    }
    Text {
        id: saveNote
        objectName: "preset-save-error"
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: ""
        color: Theme.warn
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.WordWrap
    }
}
