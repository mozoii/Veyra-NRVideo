// One beauty node's parameters, shared by the list page's beauty layers and the
// node page's beauty node, so both edit the same values through the same bridge
// calls (as AntiAliasEditor does for anti-aliasing). Two independent sub-functions:
// Original face restore blends the frame's original back in inside the faces, skin smoothing smooths
// skin while keeping eyes, lashes and lips. The face detection is the chain's,
// shared by every beauty node.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    // veyra.beautyLayers[i] (list) or veyra.beautyParamsAt(index) (node).
    required property var params
    // veyra.beautyDetection and veyra.beautyStatus. The list page draws them once
    // for the whole group (its layers set showDetection false and one editor sets
    // detectionOnly); the node editor shows them in every node.
    property var detection: ({quality: 1, maxFaces: 4, stability: 0.5})
    property string status: ""
    property bool showDetection: true
    property bool detectionOnly: false
    property string namePrefix: "beauty"
    signal edited(string key, double amount)
    signal presetPicked(string preset)
    signal detectionEdited(string key, double amount)
    spacing: 6
    readonly property bool restoreOn: params.restore === true
    readonly property bool smoothOn: params.smooth === true
    readonly property var presets: [
        {id: "natural", label: qsTr("Natural smoothing")}, {id: "light", label: qsTr("Light smoothing")},
        {id: "restore", label: qsTr("Original face restore")}, {id: "restoreNatural", label: qsTr("Original restore + natural smoothing")}
    ]

    // A 0..1 value shown and typed as a percentage.
    component PercentRow: VRow {
        id: row
        required property string key
        property real from: 0
        property real to: 1
        property real defaultValue: 0
        property real step: 0.01
        value: Math.round(Number(editor.params[key] ?? defaultValue) * 100) + "%"
        VSlider {
            objectName: editor.namePrefix + "-" + row.key
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: row.defaultValue
            from: row.from; to: row.to; inputScale: 100
            value: editor.params[row.key] ?? row.defaultValue
            onMoved: value => editor.edited(row.key, Math.round(value / row.step) * row.step)
        }
    }

    // Every layer row sits in this column, so the shared panel hides them at once.
    ColumnLayout {
        Layout.fillWidth: true
        visible: !editor.detectionOnly
        spacing: 6
        Text {
            Layout.fillWidth: true
            text: qsTr("Original face restore: blends this frame's original (before any effect) back in along the face outline. Skin smoothing: softens skin and leaves the eyes, brows and lips out. Each can be switched on its own.")
            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
        VRow {
            label: qsTr("Presets")
            hint: qsTr("Sets the switches and values in one go; the skin range and face detection stay as they are")
            VSelect {
                objectName: editor.namePrefix + "-preset"
                title: qsTr("Beauty presets")
                options: editor.presets
                value: qsTr("Choose…")
                onPicked: id => editor.presetPicked(id)
            }
        }

        // --- Original face restore ----------------------------------------
        VRow {
            label: qsTr("Original face restore")
            hint: qsTr("For example when grading or enhancement makes faces look wrong: they go back to the original")
            VSwitch {
                objectName: editor.namePrefix + "-restore"
                checked: editor.restoreOn
                onToggled: checked => editor.edited("restore", checked ? 1 : 0)
            }
        }
        PercentRow {
            key: "restoreStrength"; label: qsTr("Restore strength"); defaultValue: 1
            visible: editor.restoreOn
        }
        VRow {
            label: qsTr("Restore area")
            hint: qsTr("Skin only: hair and background keep the effects")
            visible: editor.restoreOn
            VSeg {
                objectName: editor.namePrefix + "-restoreScope"
                options: [{id: "0", label: qsTr("Whole face")}, {id: "1", label: qsTr("Skin only")}]
                current: String(editor.params.restoreScope ?? 0)
                onPicked: id => editor.edited("restoreScope", Number(id))
            }
        }
        PercentRow {
            key: "regionExpand"; label: qsTr("Area expansion"); from: -0.10; to: 0.30; defaultValue: 0.05
            hint: qsTr("Relative to the face size; negative shrinks it")
            visible: editor.restoreOn
        }
        PercentRow {
            key: "feather"; label: qsTr("Edge feather"); from: 0; to: 0.20; defaultValue: 0.08
            visible: editor.restoreOn
        }

        // --- Skin smoothing -----------------------------------------------
        VRow {
            label: qsTr("Skin smoothing")
            hint: qsTr("Runs after the original restore")
            VSwitch {
                objectName: editor.namePrefix + "-smooth"
                checked: editor.smoothOn
                onToggled: checked => editor.edited("smooth", checked ? 1 : 0)
            }
        }
        PercentRow { key: "strength"; label: qsTr("Overall strength"); defaultValue: 1; visible: editor.smoothOn }
        PercentRow { key: "smoothAmount"; label: qsTr("Smoothing amount"); defaultValue: 0.5; visible: editor.smoothOn }
        VRow {
            label: qsTr("Smoothing radius")
            hint: qsTr("In 1080p pixels, scaled with the picture size; larger is softer")
            visible: editor.smoothOn
            value: Number(editor.params.radius ?? 4).toFixed(1)
            VSlider {
                objectName: editor.namePrefix + "-radius"
                implicitWidth: 110
                valueFromModel: true
                resettable: true; defaultValue: 4
                from: 0; to: 12
                value: editor.params.radius ?? 4
                onMoved: value => editor.edited("radius", Math.round(value * 2) / 2)
            }
        }
        PercentRow {
            key: "detail"; label: qsTr("Detail protection"); defaultValue: 0.65; visible: editor.smoothOn
            hint: qsTr("Higher keeps more of the facial features and hair")
        }
        PercentRow {
            key: "texture"; label: qsTr("Texture kept"); defaultValue: 0.3; visible: editor.smoothOn
            hint: qsTr("Brings part of the skin texture back to avoid a plastic look")
        }
        PercentRow { key: "brighten"; label: qsTr("Skin brightening"); defaultValue: 0; visible: editor.smoothOn }
        VRow {
            label: qsTr("Smoothing area")
            hint: qsTr("All skin: works without face detection; every skin-coloured area of the picture is smoothed")
            visible: editor.smoothOn
            VSeg {
                objectName: editor.namePrefix + "-scope"
                options: [{id: "0", label: qsTr("Faces only")}, {id: "1", label: qsTr("All skin")}]
                current: String(editor.params.scope ?? 0)
                onPicked: id => editor.edited("scope", Number(id))
            }
        }
        VSubGroup {
            objectName: editor.namePrefix + "-skin-group"
            Layout.fillWidth: true
            visible: editor.restoreOn || editor.smoothOn
            label: qsTr("Skin range · advanced"); count: 7
            PercentRow { key: "hueCenter"; label: qsTr("Hue centre"); defaultValue: 0.08 }
            PercentRow { key: "hueRange"; label: qsTr("Hue range"); from: 0.01; to: 0.5; defaultValue: 0.10 }
            PercentRow { key: "satMin"; label: qsTr("Saturation min"); defaultValue: 0.08; hint: qsTr("Must stay below the maximum") }
            PercentRow { key: "satMax"; label: qsTr("Saturation max"); defaultValue: 0.80 }
            PercentRow { key: "lumaMin"; label: qsTr("Brightness min"); defaultValue: 0.04; hint: qsTr("Lower it for faces at night or in shadow") }
            PercentRow { key: "lumaMax"; label: qsTr("Brightness max"); defaultValue: 0.95 }
            PercentRow { key: "softness"; label: qsTr("Edge softness"); defaultValue: 0.35 }
        }
        PercentRow {
            key: "minConfidence"; label: qsTr("Face confidence"); from: 0.3; to: 0.95; defaultValue: 0.6
            hint: qsTr("Faces below it don't count; raise it when other things are taken for faces")
            visible: editor.restoreOn || editor.smoothOn
        }
        VRow {
            label: qsTr("Show mask")
            hint: qsTr("Blue: original restore area; magenta: smoothing area. Turned off automatically for export")
            visible: editor.restoreOn || editor.smoothOn
            VSwitch {
                objectName: editor.namePrefix + "-showMask"
                checked: editor.params.showMask === true
                onToggled: checked => editor.edited("showMask", checked ? 1 : 0)
            }
        }
    }

    // --- Face detection (shared by every beauty node) ----------------------
    VSubGroup {
        objectName: editor.namePrefix + "-detection-group"
        Layout.fillWidth: true
        visible: editor.showDetection
        expanded: editor.detectionOnly
        label: qsTr("Face detection · shared by all beauty nodes"); count: 3
        VRow {
            label: qsTr("Detection level")
            hint: editor.detection.quality === 0 ? qsTr("Every frame · 320 px, lightest; small distant faces may be missed")
                : editor.detection.quality === 2 ? qsTr("Every frame · 640 px, finds small faces too")
                : qsTr("Every frame · 512 px")
            VSeg {
                objectName: editor.namePrefix + "-quality"
                options: [{id: "0", label: qsTr("Performance")}, {id: "1", label: qsTr("Balanced")}, {id: "2", label: qsTr("Fine")}]
                current: String(editor.detection.quality ?? 1)
                onPicked: id => editor.detectionEdited("quality", Number(id))
            }
        }
        VRow {
            label: qsTr("Max faces")
            value: String(editor.detection.maxFaces ?? 4)
            VSlider {
                objectName: editor.namePrefix + "-maxFaces"
                implicitWidth: 110
                valueFromModel: true
                resettable: true; defaultValue: 4
                from: 1; to: 8; inputDecimals: 0
                value: editor.detection.maxFaces ?? 4
                onMoved: value => editor.detectionEdited("maxFaces", Math.round(value))
            }
        }
        VRow {
            label: qsTr("Tracking smoothing")
            hint: qsTr("Higher is steadier at rest; moving faces are followed closely either way")
            value: Math.round(Number(editor.detection.stability ?? 0.5) * 100) + "%"
            VSlider {
                objectName: editor.namePrefix + "-stability"
                implicitWidth: 110
                valueFromModel: true
                resettable: true; defaultValue: 0.5
                from: 0; to: 1; inputScale: 100
                value: editor.detection.stability ?? 0.5
                onMoved: value => editor.detectionEdited("stability", Math.round(value * 100) / 100)
            }
        }
    }
    Text {
        objectName: editor.namePrefix + "-status"
        Layout.fillWidth: true
        visible: editor.showDetection && editor.status.length > 0
        text: editor.status
        color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
    }
}
