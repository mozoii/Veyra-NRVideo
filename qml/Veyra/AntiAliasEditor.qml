// One anti-aliasing instance's parameters (#53), shared by the list page's
// Anti-aliasing card and the node page's anti-aliasing node, so both edit the same
// values through the same bridge calls (as NrLayerEditor does for NR).
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    // veyra.antiAliasParams (list card) or veyra.antiAliasParamsAt(index) (node).
    required property var params
    property string namePrefix: "aa"
    signal edited(string key, double amount)
    spacing: 6
    readonly property int method: params.method ?? 1
    readonly property int quality: params.quality ?? 2
    readonly property int temporal: params.temporal ?? 0
    // DLAA runs where DLSS SR does (same runtime and adapters).
    readonly property var dlssCapability: (veyra.effectCapabilities || {}).sr0 || {}
    readonly property bool dlaaAvailable: dlssCapability.available === true
    readonly property var qualityChoices: method === 0
        ? [{id: "0", label: qsTr("Medium")}, {id: "1", label: qsTr("High")}, {id: "2", label: qsTr("Ultra")}]
        : [{id: "0", label: qsTr("Low")}, {id: "1", label: qsTr("Medium")}, {id: "2", label: qsTr("High")}, {id: "3", label: qsTr("Ultra")}]

    Text {
        Layout.fillWidth: true
        text: qsTr("Removes jagged edges, for game footage, text and interface lines. In list mode with NR on it runs after NR, so the edges NR redraws are smoothed too; without NR it runs at source resolution before super resolution, so the jaggies are not enlarged with the picture.")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
    }
    VRow {
        label: qsTr("Method")
        hint: editor.method === 0 ? qsTr("FXAA: fastest, slightly softer overall")
            : editor.method === 1 ? qsTr("SMAA: more accurate edges, keeps more detail")
            : editor.method === 3 ? qsTr("DLAA: NVIDIA DLSS at the native resolution, rebuilding edges across frames along the optical flow; it uses the DLSS SR model and costs more than the other methods. Captured pictures have no jittered samples like a game's, so the effect is weaker than in-game DLAA")
            : qsTr("MLAA: morphological anti-aliasing with adjustable threshold and strength")
        VSeg {
            objectName: editor.namePrefix + "-method"
            options: [{id: "0", label: "FXAA"}, {id: "1", label: "SMAA"}, {id: "2", label: "MLAA"},
                      {id: "3", label: "DLAA", disabled: !editor.dlaaAvailable}]
            current: String(editor.method)
            onPicked: id => editor.edited("method", Number(id))
        }
    }
    VRow {
        label: qsTr("Quality")
        visible: editor.method !== 2 && editor.method !== 3
        VSeg {
            objectName: editor.namePrefix + "-quality"
            options: editor.qualityChoices
            current: String(editor.quality)
            onPicked: id => editor.edited("quality", Number(id))
        }
    }
    VRow {
        label: qsTr("Edge threshold")
        hint: qsTr("A brightness step above this counts as an edge; lower values treat more edges")
        visible: editor.method === 2
        value: Number(editor.params.mlaaThreshold ?? 0.08).toFixed(2)
        VSlider {
            objectName: editor.namePrefix + "-mlaa-threshold"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: 0.08
            from: 0.02; to: 0.30
            value: editor.params.mlaaThreshold ?? 0.08
            onMoved: value => editor.edited("mlaaThreshold", Math.round(value * 100) / 100)
        }
    }
    VRow {
        label: qsTr("Strength")
        visible: editor.method === 2
        value: Number(editor.params.mlaaStrength ?? 1).toFixed(2)
        VSlider {
            objectName: editor.namePrefix + "-mlaa-strength"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: 1
            from: 0; to: 1
            value: editor.params.mlaaStrength ?? 1
            onMoved: value => editor.edited("mlaaStrength", Math.round(value * 20) / 20)
        }
    }
    VRow {
        label: qsTr("Temporal accumulation")
        hint: qsTr("Experimental: captured frames cannot be jittered, so this approximates SMAA T2x / 4x without jitter; steadier on still scenes, may ghost on motion")
        visible: editor.method === 1
        VSeg {
            objectName: editor.namePrefix + "-temporal"
            options: [{id: "0", label: qsTr("Close")}, {id: "1", label: "T2x"}, {id: "2", label: "4x"}]
            current: String(editor.temporal)
            onPicked: id => editor.edited("temporal", Number(id))
        }
    }
    VRow {
        label: qsTr("History weight")
        hint: qsTr("Higher is smoother but ghosts more on motion")
        visible: editor.method === 1 && editor.temporal > 0
        value: Number(editor.params.historyWeight ?? 0.5).toFixed(2)
        VSlider {
            objectName: editor.namePrefix + "-history-weight"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: editor.temporal === 2 ? 0.75 : 0.5
            from: 0; to: 0.95
            value: editor.params.historyWeight ?? 0.5
            onMoved: value => editor.edited("historyWeight", Math.round(value * 20) / 20)
        }
    }
    VRow {
        label: qsTr("History rejection")
        hint: qsTr("Drops the history where the picture changes by more; higher means less ghosting")
        visible: editor.method === 1 && editor.temporal > 0
        value: Number(editor.params.historyRejection ?? 8).toFixed(1)
        VSlider {
            objectName: editor.namePrefix + "-history-rejection"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: 8
            from: 0; to: 32
            value: editor.params.historyRejection ?? 8
            onMoved: value => editor.edited("historyRejection", Math.round(value * 2) / 2)
        }
    }
}
