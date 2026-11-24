// One NR editor; each parameter group independently chooses web or list.
// Both views use the same specs, bridge parameters and persistence.
import QtQuick
import QtQuick.Layouts
import "EffectSupport.js" as Support

ColumnLayout {
    id: editor
    required property var layerData
    property int layerCount: 1
    property bool nrFirst: false
    // The list page owns copy/delete and the list-wide NR->SR order; the node
    // page uses its node menu and wiring instead.
    property bool listControls: true
    // Read the previous global choice only as a migration fallback.
    function usesWeb(key) {
        const prefs = veyra.preferences
        return (prefs[key] ?? (prefs.nrWebEditor === true ? "web" : "list")) === "web"
    }
    readonly property bool residualWeb: usesWeb("nrResidualView")
    readonly property bool correctionWeb: usesWeb("nrCorrectionView")
    readonly property var residualSpecs: [{key:"total",label:qsTr("Overall change strength"),hint:qsTr("Overall multiplier · Affects all four change directions"),min:0,max:5,defaultValue:1},
               {key:"darken",label:qsTr("Darkening change"),hint:qsTr("Increase darkening introduced by NR, rather than lowering overall brightness."),min:0,max:2,defaultValue:1},
               {key:"brighten",label:qsTr("Brightness change"),hint:qsTr("Increase brightening introduced by NR; 0 removes this change."),min:0,max:2,defaultValue:1},
               {key:"color",label:qsTr("Color change"),hint:qsTr("Increase color changes introduced by NR; 0 removes this change."),min:0,max:2,defaultValue:1},
               {key:"luminance",label:qsTr("Lightness change"),hint:qsTr("Increase luminance changes introduced by NR; 0 removes this change."),min:0,max:2,defaultValue:1}]
    readonly property var correctionSpecs: [{key:"hueProtection",label:qsTr("Hue protection"),hint:qsTr("Retain source hue; little change when hue already matches"),min:0,max:1,defaultValue:1},
                    {key:"chromaProtection",label:qsTr("Chroma protection"),hint:qsTr("Soften excessive chroma changes; retain small changes"),min:0,max:1,defaultValue:0.75},
                    {key:"neutralProtection",label:qsTr("Neutral color protection"),hint:qsTr("Suppress added tint on white walls, clouds and gray areas"),min:0,max:1,defaultValue:0},
                    {key:"colorRetention",label:qsTr("Source color retention"),hint:qsTr("Retain source hue and saturation; 1 fully retains color"),min:0,max:1,defaultValue:0},
                    {key:"luminanceRetention",label:qsTr("Lightness retention"),hint:qsTr("Reduce NR lightness changes; 1 retains source lightness"),min:0,max:1,defaultValue:0},
                    {key:"shadowProtection",label:qsTr("Shadow protection"),hint:qsTr("Reduce further shadow darkening while retaining brighter detail"),min:0,max:1,defaultValue:0},
                    {key:"highlightProtection",label:qsTr("Highlight protection"),hint:qsTr("Compress changes toward blown whites; retain highlight darkening"),min:0,max:1,defaultValue:1},
                    {key:"localCompression",label:qsTr("Local compression"),hint:qsTr("Soften excessive lightness and color changes together"),min:0,max:1,defaultValue:0.75},
                    {key:"temporalStability",label:qsTr("Temporal stability"),hint:qsTr("Reduce flicker during playback; a paused frame does not show temporal effects"),min:0,max:1,defaultValue:0.8}]
    signal edited(int nodeIndex, string key, double amount)
    signal duplicateRequested(int nodeIndex)
    signal removeRequested(int nodeIndex)
    signal orderEdited(bool enabled)
    readonly property bool amdNr: layerData.runtime === 4
    readonly property string amdRuntimeLabel: {
        const choices = veyra.nrRuntimeChoices || []
        const choice = choices.find(entry => String(entry.id) === "4")
        return choice ? choice.label : qsTr("RX 7000 / 9000 · AMD NR (experimental)")
    }
    property string autoReason: ""
    readonly property bool manualSize: layerData.manualSize ?? false
    property int lastManualPercent: 100
    function updateAutoReason() {
        autoReason = veyra.nrAutoSelectionReason(layerData.index)
        if (layerData.manualSize) lastManualPercent = layerData.sizePercent ?? 100
    }
    Component.onCompleted: updateAutoReason()
    onLayerDataChanged: updateAutoReason()
    // The snapshot arrives about 60 times a second and both editor pages stay
    // loaded; a hidden editor catches up when it is shown.
    onVisibleChanged: if (visible) updateAutoReason()
    Connections {
        target: veyra
        function onSettingsChanged() { editor.updateAutoReason() }
        function onChainChanged() { editor.updateAutoReason() }
        function onSnapshotChanged() { if (editor.visible) editor.updateAutoReason() }
        function onEffectCapabilitiesChanged() { editor.updateAutoReason() }
    }
    readonly property var sizeChoices: [
        {id:"2",label:"480p"}, {id:"3",label:"720p"}, {id:"4",label:"900p"},
        {id:"0",label:qsTr("1080p · default")}, {id:"5",label:"1440p",disabled:amdNr,note:amdNr ? qsTr("AMD NR has a maximum 1080p pixel budget") : ""}, {id:"1",label:qsTr("Native"),disabled:amdNr,note:amdNr ? qsTr("AMD NR has a maximum 1080p pixel budget") : ""},
        {id:"6",label:qsTr("Auto (by load)"),disabled:autoReason.length>0,note:autoReason},
        {id:"manual",label:qsTr("Manual")}
    ]
    readonly property string sizeLabel: manualSize ? qsTr("Manual") : sizeChoices.find(o => Number(o.id) === layerData.sizePolicy)?.label ?? qsTr("Unknown")
    // Source-faithful cleanup: NR keeps temporal detail, its colour delta is
    // removed and lightness change scaled down so the source look is preserved.
    readonly property bool sourceFaithful: layerData.color === 0 && Math.abs((layerData.luminance ?? 1) - 0.7) < 0.01 && layerData.temporal === true
    function setSourceFaithful(on) {
        if (on) {
            editor.edited(editor.layerData.index, "color", 0)
            editor.edited(editor.layerData.index, "luminance", 0.7)
            editor.edited(editor.layerData.index, "temporal", 1)
        } else {
            editor.edited(editor.layerData.index, "color", 1)
            editor.edited(editor.layerData.index, "luminance", 1)
        }
    }
    spacing: 6

    component NrValueRow: VRow {
        id: valueRow
        required property var spec
        required property double amount
        signal edited(double amount)
        label: spec.label
        hint: spec.hint ?? ""
        value: amount.toFixed(2)
        VSlider {
            objectName: "nr-" + valueRow.spec.key
            valueFromModel: true
            resettable: true; defaultValue: valueRow.spec.defaultValue ?? (valueRow.spec.key === "skin" ? -1 : 1)
            implicitWidth: 110
            from: valueRow.spec.min; to: valueRow.spec.max
            value: valueRow.amount
            onMoved: value => { if (valueRow.visible && valueRow.enabled) valueRow.edited(value) }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: editor.listControls
        VButton {
            objectName: "nr-copy"
            text: qsTr("Copy this layer"); enabled: editor.layerCount < 4
            onClicked: editor.duplicateRequested(editor.layerData.index)
        }
        VButton {
            objectName: "nr-reset"
            text: qsTr("Reset to default"); ghost: true
            onClicked: if (editor.enabled) veyra.resetNrLayer(editor.layerData.index)
        }
        VButton {
            objectName: "nr-remove"
            text: qsTr("Delete this layer")
            onClicked: editor.removeRequested(editor.layerData.index)
        }
    }
    VRow {
        label: qsTr("NR version")
        hint: qsTr("Shared by the whole chain · switching rebuilds the NR pipeline")
        VSelect {
            objectName: "nr-runtime"
            value: editor.layerData.runtime === 4 ? editor.amdRuntimeLabel
                   : editor.layerData.runtime === 3 ? qsTr("RTX 50 · NVIDIA original")
                   : "RTX 20–50 · SF 310.9.16"
            options: veyra.nrRuntimeChoices || [{id:"2",label:"RTX 20–50 · SF 310.9.16"},{id:"3",label:qsTr("RTX 50 · NVIDIA original")},{id:"4",label:qsTr("RX 7000 / 9000 · AMD NR (experimental)")}]
            onPicked: id => editor.edited(editor.layerData.index, "runtime", Number(id))
        }
    }
    VRow {
        objectName: "nr-performance-row"
        label: qsTr("Use performance model")
        hint: editor.amdNr ? qsTr("On: Small model. Off: Original model. Each layer selects independently. Switching rebuilds NR and may change the picture.")
                           : qsTr("On: new model B. Off: legacy model A. Switching rebuilds NR and may change the picture.")
        enabled: (editor.layerData.runtime === 2 || editor.amdNr) && Support.available(veyra, "nr" + editor.layerData.runtime)
        opacity: enabled ? 1 : 0.4
        VSwitch {
            objectName: "nr-performance"
            checked: editor.layerData.performance ?? true
            onToggled: checked => editor.edited(editor.layerData.index, "performance", checked ? 1 : 0)
        }
    }
    VRow {
        label: qsTr("NR motion source")
        enabled: Support.available(veyra, "nr" + editor.layerData.runtime)
        opacity: enabled ? 1 : 0.4
        hint: qsTr("Shared by the whole chain; anti-flicker still needs optical flow when on")
        VSeg {
            objectName: "nr-motion-source"
            options: [{id:"0",label:qsTr("Zero motion")},{id:"1",label:qsTr("Optical flow")}]
            current: String(veyra.nrMotionSource)
            onPicked: id => { if (editor.enabled) veyra.nrMotionSource = Number(id) }
        }
    }
    VRow {
        label: qsTr("Internal processing resolution")
        enabled: Support.available(veyra, "nr" + editor.layerData.runtime)
        opacity: enabled ? 1 : 0.4
        hint: qsTr("Internal downsampling · output size unchanged")
        VSelect {
            objectName: "nr-resolution"
            value: editor.sizeLabel; options: editor.sizeChoices
            onPicked: id => id === "manual" ? editor.edited(editor.layerData.index, "sizePercent", editor.lastManualPercent)
                                             : editor.edited(editor.layerData.index, "sizePolicy", Number(id))
        }
    }
    VRow {
        objectName: "nr-manual-size-row"
        visible: editor.manualSize
        enabled: Support.available(veyra, "nr" + editor.layerData.runtime)
        opacity: enabled ? 1 : 0.4
        label: qsTr("Processing scale")
        hint: qsTr("Apply on release · scale both dimensions")
        value: Math.round(percentSlider.displayedValue) + "%"
        VSlider {
            id: percentSlider
            objectName: "nr-size-percent"
            valueFromModel: true
            resettable: true; defaultValue: 100
            implicitWidth: 110
            from: 25; to: 100; stepSize: 1
            live: false // NR dimensions rebuild GPU features: commit once on release.
            value: editor.layerData.sizePercent ?? 100
            onMoved: value => editor.edited(editor.layerData.index, "sizePercent", Math.round(value))
        }
    }
    Text {
        Layout.fillWidth: true
        visible: editor.manualSize
        text: editor.amdNr ? qsTr("Manual scale is relative to the input within the 1080p budget. It applies to preview and video export; the output size is unchanged.")
                          : qsTr("Manual scale changes the width and height of this layer’s input; 50% is about one quarter of the pixels. It applies to preview and video export; the output size is unchanged.")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: editor.layerData.sizePolicy === 6
        text: qsTr("Auto adjusts preview processing from 100% to 40% using enhancement time. Prebuilt instances add about 1.7GB of VRAM and half a second of startup time. Video export uses a fixed maximum of 1440p; the output size is unchanged.") +
              (veyra.nrAutoStatus.length > 0 ? "\n" + veyra.nrAutoStatus : "")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        text: editor.amdNr ? qsTr("AMD NR preview and video export use up to internal 1080p processing while keeping the video output size. Image export must still fit the model size budget.")
                          : qsTr("Preview and video export both process NR at the selected internal resolution, keeping the aspect ratio without upscaling small inputs; the output size is unchanged. Image export is still fully processed.")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: !editor.amdNr && editor.layerCount > 1 &&
                 (!editor.listControls || (veyra.nrLayers.length > 0 && editor.layerData.index === veyra.nrLayers[0].index))
        text: qsTr("For multiple NR layers, try 720p first and 1080p last; for three layers, try 480p / 720p / 1080p. Lowering the earlier layers changes the image. Video export also uses each layer's selected resolution.")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
    NrValueRow {
        spec: ({key:"intensity",label:qsTr("Model strength"),min:0,max:1})
        enabled: Support.available(veyra, "nr" + editor.layerData.runtime)
        opacity: enabled ? 1 : 0.4
        amount: editor.layerData.intensity
        onEdited: amount => editor.edited(editor.layerData.index, "intensity", amount)
    }
    Text {
        Layout.fillWidth: true
        visible: editor.amdNr || !Support.available(veyra, "nr" + editor.layerData.runtime)
        text: !Support.available(veyra, "nr" + editor.layerData.runtime) ? Support.reason(veyra, "nr" + editor.layerData.runtime) : qsTr("AMD NR (experimental): RX 7000 / 9000 use separate runtimes and require HIP 7. Performance mode selects Small; off selects Original. Internal processing supports percentages within a 1080p pixel budget. RX 7000 uses the default style; RX 9000 retains runtime configuration. Native HDR preserves its base and reconstructs the SDR-proxy enhancement. RX 7000 hardware validation is still required.")
        color: Theme.t3; font.pixelSize: 11; wrapMode: Text.WordWrap
    }
    VRow {
        objectName: "nr-source-faithful-row"
        label: qsTr("Preserve source look")
        hint: qsTr("Keep the source colours and limit lightness drift while NR reduces flicker and smear")
        VSwitch {
            objectName: "nr-source-faithful"
            checked: editor.sourceFaithful
            onToggled: checked => editor.setSourceFaithful(checked)
        }
    }
    VSubGroup {
        objectName: "nr-model-group"
        enabled: !editor.amdNr && Support.available(veyra, "nr" + editor.layerData.runtime)
        opacity: enabled ? 1 : 0.4
        Layout.fillWidth: true
        label: qsTr("Model parameters"); count: 6
        Repeater {
            model: [{key:"tone",label:qsTr("Local tone"),min:0,max:1},
                    {key:"structure",label:qsTr("Local structure"),min:0,max:1}]
            delegate: NrValueRow {
                required property var modelData
                spec: modelData; amount: editor.layerData[modelData.key]
                onEdited: amount => editor.edited(editor.layerData.index, modelData.key, amount)
            }
        }
        VRow {
            label: qsTr("Skin · unverified")
            VSelect {
                objectName: "nr-skin-mode"
                value: editor.layerData.skin < 0 ? qsTr("Not specified") : qsTr("Custom")
                options: [{id:"-1",label:qsTr("Not specified")},{id:"0",label:qsTr("Custom")}]
                onPicked: id => editor.edited(editor.layerData.index,"skin",Number(id))
            }
        }
        NrValueRow {
            visible: editor.layerData.skin >= 0
            spec: ({key:"skin",label:qsTr("Skin value"),min:0,max:2})
            amount: Math.max(0,editor.layerData.skin)
            onEdited: amount => editor.edited(editor.layerData.index,"skin",amount)
        }
        VRow {
            label: qsTr("Style · experimental")
            VSeg {
                objectName: "nr-style"
                options: [{id:"0",label:"0"},{id:"1",label:"1"},{id:"2",label:"2"}]
                current: String(editor.layerData.style)
                onPicked: id => editor.edited(editor.layerData.index,"style",Number(id))
            }
        }
        VRow {
            label: qsTr("Auto mask · experimental")
            VSwitch {
                objectName: "nr-autoMask"
                checked: editor.layerData.autoMask
                onToggled: checked => editor.edited(editor.layerData.index,"autoMask",checked?1:0)
            }
        }
        VRow {
            label: qsTr("UI correction · unverified")
            VSwitch {
                objectName: "nr-uiCorrection"
                checked: editor.layerData.uiCorrection
                onToggled: checked => editor.edited(editor.layerData.index,"uiCorrection",checked?1:0)
            }
        }
    }
    VSubGroup {
        objectName: "nr-residual-group"
        Layout.fillWidth: true
        label: qsTr("Enhancement change"); count: 5
        VRow {
            label: qsTr("Adjustment method")
            VSeg {
                objectName: "nr-residual-view"
                options: [{id:"web",label:qsTr("Web")},{id:"list",label:qsTr("List")}]
                current: editor.residualWeb ? "web" : "list"
                onPicked: id => veyra.setPreference("nrResidualView", id)
            }
        }
        Repeater {
            model: editor.residualSpecs
            delegate: NrValueRow {
                required property var modelData
                visible: !editor.residualWeb || modelData.key === "total"
                spec: modelData; amount: editor.layerData[modelData.key]
                onEdited: amount => editor.edited(editor.layerData.index, modelData.key, amount)
            }
        }
        NrWebPad {
            id: residualPad
            objectName: "nr-residual-web"
            Layout.fillWidth: true
            visible: editor.residualWeb
            identity: String(editor.layerData.index) + ":" + editor.layerData.runtime
            axes: editor.residualSpecs.slice(1)
            values: editor.layerData
            onEdited: (key, value) => { if (residualPad.visible && residualPad.enabled) editor.edited(editor.layerData.index, key, value) }
        }
    }
    VSubGroup {
        objectName: "nr-correction-group"
        Layout.fillWidth: true
        label: qsTr("Image correction"); count: editor.layerData.correctionEnabled ? (editor.layerData.correctionAuto ? 1 : 9) : 0
        VRow {
            label: qsTr("Enable correction")
            hint: qsTr("Off preserves the original residual gain up to 5")
            VSwitch {
                objectName: "nr-correctionEnabled"
                checked: editor.layerData.correctionEnabled ?? false
                onToggled: checked => editor.edited(editor.layerData.index,"correctionEnabled",checked?1:0)
            }
        }
        VRow {
            visible: editor.layerData.correctionEnabled ?? false
            label: qsTr("Correction mode")
            VSeg {
                objectName: "nr-correctionAuto"
                options: [{id:"1",label:qsTr("Auto")},{id:"0",label:qsTr("Manual")}]
                current: (editor.layerData.correctionAuto ?? true) ? "1" : "0"
                onPicked: id => editor.edited(editor.layerData.index,"correctionAuto",Number(id))
            }
        }
        VRow {
            visible: (editor.layerData.correctionEnabled ?? false) && !(editor.layerData.correctionAuto ?? true)
            label: qsTr("Adjustment method")
            VSeg {
                objectName: "nr-correction-view"
                options: [{id:"web",label:qsTr("Web")},{id:"list",label:qsTr("List")}]
                current: editor.correctionWeb ? "web" : "list"
                onPicked: id => veyra.setPreference("nrCorrectionView", id)
            }
        }
        NrWebPad {
            id: correctionPad
            objectName: "nr-correction-web"
            Layout.fillWidth: true
            visible: (editor.layerData.correctionEnabled ?? false) && !(editor.layerData.correctionAuto ?? true) && editor.correctionWeb
            identity: String(editor.layerData.index) + ":" + editor.layerData.runtime
            axes: editor.correctionSpecs
            values: editor.layerData
            onEdited: (key, value) => { if (correctionPad.visible && correctionPad.enabled) editor.edited(editor.layerData.index, key, value) }
        }
        Repeater {
            model: editor.correctionSpecs
            delegate: NrValueRow {
                required property var modelData
                visible: (editor.layerData.correctionEnabled ?? false) && !(editor.layerData.correctionAuto ?? true) &&
                         !editor.correctionWeb
                spec: modelData; amount: editor.layerData[modelData.key] ?? modelData.defaultValue
                onEdited: amount => editor.edited(editor.layerData.index,modelData.key,amount)
            }
        }
        NrValueRow {
            visible: (editor.layerData.correctionEnabled ?? false) && (editor.layerData.correctionAuto ?? true)
            spec: ({key:"correctionAmount",label:qsTr("Automatic control amount"),hint:qsTr("0 retains raw changes; 1 applies this style’s full correction"),min:0,max:1,defaultValue:1})
            amount: editor.layerData.correctionAmount ?? 1
            onEdited: amount => editor.edited(editor.layerData.index,"correctionAmount",amount)
        }
        VButton {
            objectName: "nr-use-auto-defaults"
            visible: (editor.layerData.correctionEnabled ?? false) && !(editor.layerData.correctionAuto ?? true)
            text: qsTr("Use this style’s automatic values"); ghost: true
            onClicked: editor.edited(editor.layerData.index,"correctionUseAuto",1)
        }
        Text {
            Layout.fillWidth: true
            visible: editor.layerData.correctionEnabled ?? false
            text: qsTr("Automatic values are tuned for styles 0/1/2. Manual controls independently retain color, lightness and shadows, reducing the corresponding changes. Temporal stability uses optical flow; switching modes preserves manual values.")
            color: Theme.t3; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
    }
    VSubGroup {
        objectName: "nr-experimental-group"
        Layout.fillWidth: true
        label: qsTr("Experimental"); count: editor.layerData.temporal ? 3 : 2
        VRow {
            label: qsTr("Temporal anti-flicker")
            hint: editor.layerData.correctionEnabled ? qsTr("Image correction controls temporal stability; this setting returns when correction is off") : qsTr("Needs optical flow; when off there is no temporal accumulation")
            VSwitch {
                objectName: "nr-temporal"
                checked: editor.layerData.temporal
                enabled: !(editor.layerData.correctionEnabled ?? false)
                onToggled: checked => editor.edited(editor.layerData.index,"temporal",checked?1:0)
            }
        }
        VRow {
            objectName: "nr-antiflicker-row"
            // Only meaningful when temporal accumulation is on; the tier picks
            // how the accumulation behaves, not whether it runs.
            visible: editor.layerData.temporal
            label: qsTr("Anti-flicker level")
            hint: qsTr("Static is the steadiest, optical flow is the default, low-frequency keeps high-frequency detail")
            VSelect {
                objectName: "nr-antiflicker"
                enabled: !(editor.layerData.correctionEnabled ?? false)
                options: [
                    {id:"1",label:qsTr("Static accumulation")},
                    {id:"2",label:qsTr("Accumulated flow · default")},
                    {id:"3",label:qsTr("Accumulated flow+")},
                    {id:"4",label:qsTr("Low-frequency temporal rebuild")}
                ]
                value: options.find(o => Number(o.id) === editor.layerData.antiFlicker)?.label ?? qsTr("Accumulated flow · default")
                onPicked: id => editor.edited(editor.layerData.index,"antiFlicker",Number(id))
            }
        }
        VRow {
            visible: editor.listControls
            label: qsTr("NR → SR · all layers")
            hint: qsTr("Low-latency order, preview only, all layers in sync")
            VSwitch {
                checked: editor.nrFirst
                onToggled: checked => editor.orderEdited(checked)
            }
        }
    }
}
