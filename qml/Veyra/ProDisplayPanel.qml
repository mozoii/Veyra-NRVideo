// Professional-view displayCol controls; settings remain in the shared bridge.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    id: displayCol
    readonly property bool hdrMeterObserved: visible && hdrCurveAccordion.open && hdrReadAccordion.open && veyra.hdrCurve
    Layout.fillWidth: true
    spacing: 6
    VGroup {
        ProPresentationRows { }
        VRow {
            label: qsTr("Original / enhanced comparison")
            hint: (veyra.compareMode === 2 ? qsTr("Hold the left button on the picture and drag the divider: original on the left, enhanced on the right.") : "") + (veyra.compareMode !== 0 ? qsTr("Frame generation pauses while comparing and resumes when comparison is off") : "")
            VSeg {
                objectName: "display-compare"
                options: [{ id: "0", label: qsTr("Off", "off") }, { id: "2", label: qsTr("Split") }, { id: "1", label: qsTr("Original only") }]
                current: String(veyra.compareMode)
                onPicked: id => veyra.compareMode = Number(id)
            }
        }
        VRow {
            label: qsTr("Reference base")
            hint: qsTr("With low-latency NR first, \"before enhancement\" is the original scaled up, not a separate super-resolution reference")
            visible: veyra.compareMode !== 0
            VSeg {
                objectName: "display-compare-base"
                options: [{ id: "0", label: qsTr("Input original") }, { id: "1", label: qsTr("Before enhancement") }]
                current: veyra.compareBase ? "1" : "0"
                onPicked: id => veyra.compareBase = id === "1"
            }
        }
        VRow {
            label: qsTr("Hold V to see the original")
            hint: qsTr("Keys can be changed under Settings → Shortcuts")
            VSwitch {
                objectName: "display-hold-compare"
                checked: veyra.preferences.holdCompare !== false
                onToggled: checked => veyra.setPreference("holdCompare", checked)
            }
        }
        VRow {
            label: qsTr("Aspect ratio")
            hint: veyra.aspectMode === 1 ? qsTr("Native = one output pixel per screen pixel") : veyra.aspectMode === 2 ? qsTr("Fill the window and crop what overflows") : ""
            VSelect {
                objectName: "display-aspect"
                options: [{ id: "0", label: qsTr("Fit") }, { id: "1", label: qsTr("Native pixels") }, { id: "2", label: qsTr("Fill and crop") }, { id: "3", label: qsTr("Stretch to fill") }, { id: "4", label: "16:9" }, { id: "5", label: "4:3" }, { id: "6", label: "21:9" }]
                value: options[veyra.aspectMode].label
                onPicked: id => veyra.aspectMode = Number(id)
            }
        }
        VRow {
            label: qsTr("Force SDR preview")
            hint: qsTr("HDR sources are shown as SDR too; the same setting as the capture card's \"Show as SDR\"")
            VSwitch {
                objectName: "display-force-sdr"
                checked: veyra.forceSdrPreview
                onToggled: checked => veyra.forceSdrPreview = checked
            }
        }
        VRow {
            label: qsTr("HDR output format")
            hint: qsTr("The frame generation path is fixed to HDR10; Windows HDR must be on")
            VSeg { objectName: "display-hdr-output"; options: [{id:"0",label:"HDR10"},{id:"1",label:qsTr("scRGB float")}]; current: String(veyra.hdrOutputMode); onPicked: id => veyra.hdrOutputMode = Number(id) }
        }

                // HDR brightness curve: a static curve. Unlike a plain exposure the
                // highlights are protected by construction.
                VAccordion {
                    id: hdrCurveAccordion
                    objectName: "list-hdr-curve"
                    Layout.fillWidth: true
                    glyph: "sun"
                    hue: "#F5C84B"
                    title: qsTr("HDR brightness curve")
                    summary: veyra.hdrCurve ? qsTr("On") : qsTr("Off")
                    on: veyra.hdrCurve
                    onToggled: on => veyra.hdrCurve = on
                    // The curve graph (custom): C++ samples it with the same formula the
                    // shader uses. The dashed line is the "no processing" reference; orange is the current curve.

                        VAccordion {
                            id: hdrReadAccordion
                            objectName: "list-hdr-readgroup"
                            Layout.fillWidth: true
                            glyph: "activity"
                            hue: "#F5C84B"
                            Layout.topMargin: 8
                            enabledSwitch: false
                            open: true
                            title: qsTr("Curve, readings and guidance")
                            summary: qsTr("2 items")
                    // GPU sample statistics: at most four per second, only while this card is visible and the curve is in effect.
                    VRow {
                        hintBelow: true
                        objectName: "list-hdrcurve-live"
                        label: qsTr("Image sample statistics")
                        hint: !veyra.hdrCurveCoverage.valid
                            ? qsTr("Expand this card during HDR playback to sample, up to 4 times per second")
                            : veyra.hdrPeakWarning.capNits === 0
                                ? qsTr("No peak cap; no hard clipping. Highlight compression %1% · shadow lift %2%")
                                    .arg(Number(veyra.hdrCurveCoverage.rollOff).toFixed(1))
                                    .arg(Number(veyra.hdrCurveCoverage.lift).toFixed(1))
                            : veyra.hdrCurveCoverage.aboveCeiling <= 0.0
                                ? qsTr("Cap %1 nit not reached (highlight compression %2% · shadow lift %3%)")
                                    .arg(veyra.hdrPeakWarning.capNits)
                                    .arg(Number(veyra.hdrCurveCoverage.rollOff).toFixed(1))
                                    .arg(Number(veyra.hdrCurveCoverage.lift).toFixed(1))
                            : qsTr("At hard cap %1% · highlight compression %2% · shadow lift %3%")
                                .arg(Number(veyra.hdrCurveCoverage.aboveCeiling).toFixed(1))
                                .arg(Number(veyra.hdrCurveCoverage.rollOff).toFixed(1))
                                .arg(Number(veyra.hdrCurveCoverage.lift).toFixed(1))
                        value: !veyra.hdrCurveCoverage.valid ? qsTr("—")
                            : veyra.hdrPeakWarning.capNits === 0 ? qsTr("No cap")
                            : veyra.hdrCurveCoverage.aboveCeiling > 5 ? qsTr("Slightly high")
                            : veyra.hdrCurveCoverage.aboveCeiling > 0 ? qsTr("Slight") : qsTr("Not reached")
                    }
                    // Highlight warning (custom): source peak versus the post-curve ceiling.
                    VRow {
                        objectName: "list-hdrcurve-peakwarning"
                        label: qsTr("Highlight guidance")
                        hint: !veyra.hdrSourceInfo.valid ? qsTr("No luminance metadata in the source")
                            : !veyra.hdrPeakWarning.valid ? qsTr("Curve is off")
                            : veyra.hdrPeakWarning.capNits === 0 ? qsTr("No hard cap is set on the curve")
                            : qsTr("Declared source peak %1 nit, curve cap %2 nit: %3")
                                .arg(veyra.hdrPeakWarning.sourcePeakNits)
                                .arg(veyra.hdrPeakWarning.capNits)
                                .arg(veyra.hdrPeakWarning.clipped
                                     ? qsTr("Brightness beyond the cap needs compression; use the sample reading to check hard clipping")
                                     : qsTr("The cap covers the declared source peak; check the sample reading for the processed image"))
                        value: !veyra.hdrSourceInfo.valid || !veyra.hdrPeakWarning.valid ? qsTr("—")
                            : veyra.hdrPeakWarning.clipped ? qsTr("Compression needed") : qsTr("Within range")
                    }
                    Canvas {
                        id: hdrCurveCanvas
                        Layout.fillWidth: true
                        Layout.preferredHeight: 92
                        onWidthChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                        Connections {
                            target: veyra
                            function onSettingsChanged() { hdrCurveCanvas.requestPaint() }
                        }
                        onPaint: {
                            var ctx = getContext("2d");
                            var w = width, h = height;
                            ctx.clearRect(0, 0, w, h);
                            var g = veyra.hdrCurveGraph;
                            if (!g || !g.inputs || g.inputs.length < 2) return;
                            var minN = g.minNits, maxN = g.maxNits;
                            function px(n) {
                                var v = Math.max(n, minN) / minN;
                                return Math.log(v) / Math.log(maxN / minN);
                            }
                            // The reference line: when nothing is adjusted the curve overlaps it
                            ctx.strokeStyle = Theme.t3;
                            ctx.lineWidth = 1;
                            ctx.beginPath();
                            ctx.moveTo(0, h); ctx.lineTo(w, 0); ctx.stroke();
                            // The current curve
                            ctx.strokeStyle = g.enabled ? "#F5C84B" : Theme.t3;
                            ctx.lineWidth = 2;
                            ctx.beginPath();
                            for (var i = 0; i < g.inputs.length; ++i) {
                                var X = px(g.inputs[i]) * w;
                                var Y = h - px(g.outputs[i]) * h;
                                if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y);
                            }
                            ctx.stroke();
                        }
                    }
                    // One-click preset from the source (custom): the source's own peak / average
                    // brightness picks the step and strength; the interface only explains the reason.
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Applies to HDR10 or scRGB output, including RTX Video HDR conversion. Dim images may not reach the highlight thresholds.")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                        }
                    VAccordion {
                        objectName: "list-hdr-pickgroup"
                        Layout.fillWidth: true
                        glyph: "wand"
                        hue: "#A78BFA"
                        Layout.topMargin: 8
                        enabledSwitch: false
                        open: true
                        title: qsTr("Quick presets")
                        summary: qsTr("4 items")
                    // How the preset is chosen (custom): whole preset vs. free combination. Both write the same curve,
                    // so switching changes the interface only, never the saved data or the picture.
                    VRow {
                        hintBelow: true
                        label: qsTr("Select from source")
                        hint: {
                            if (!veyra.hdrRecommendation.available)
                                return veyra.sourceIsSdr
                                    ? qsTr("SDR sources have no HDR luminance metadata. Choose a preset manually; it applies to HDR output.")
                                    : qsTr("No luminance metadata (capture/stream); automatic selection is unavailable");
                            var r = veyra.hdrRecommendation.reason;
                            var why = r === "dark-content" ? qsTr("Mostly dark with a few bright highlights")
                                : r === "bright-content" ? qsTr("Many strong highlights")
                                : r === "narrow-range" ? qsTr("Low mastering peak (older or converted title)")
                                : r === "beyond-display" ? qsTr("Content brightness far exceeds the display capability")
                                : r === "dark-and-bright" ? qsTr("Mostly dark with highlights beyond the display capability")
                                : qsTr("Balanced brightness");
                            var axes = veyra.hdrPresetMode === "axes";
                            var pick = axes
                                ? (r === "dark-content" ? qsTr("Content: mostly dark")
                                    : r === "bright-content" ? qsTr("Content: strong highlights")
                                    : r === "beyond-display" ? qsTr("Content: strong highlights")
                                    : r === "dark-and-bright" ? qsTr("Content: dark and bright")
                                    : r === "narrow-range" ? qsTr("Content: flat older transfer")
                                    : qsTr("Content: standard"))
                                : (r === "dark-content" ? qsTr("Lift dark scenes")
                                    : r === "bright-content" ? qsTr("Highlight protection")
                                    : r === "beyond-display" ? qsTr("Highlight protection")
                                    : r === "dark-and-bright" ? qsTr("Shadows and highlights")
                                    : r === "narrow-range" ? qsTr("Punch") : qsTr("Standard"));
                            return why + qsTr(" → Suggested: ") + pick
                                + qsTr("(Source peak %1 nit, maximum frame average %2 nit)")
                                    .arg(veyra.hdrRecommendation.cllNits)
                                    .arg(veyra.hdrRecommendation.fallNits);
                        }
                        VButton {


                            objectName: "list-hdrcurve-auto"
                            text: qsTr("Recommend")
                            ghost: true
                            enabled: veyra.hdrRecommendation.available
                            onClicked: veyra.applyRecommendedHdrTuning()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: true
                        text: qsTr("Both selection modes edit the same curve; switching changes only how it is shown")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    Flow {
                        objectName: "list-hdrcurve-mode"
                        Layout.fillWidth: true
                        visible: true
                        Layout.preferredHeight: implicitHeight
                        spacing: 6
                        Repeater {
                            model: [{id:"legacy",label:qsTr("Full presets")}, {id:"axes",label:qsTr("Combine content and room")}]
                            delegate: VButton {
                                required property var modelData
                                text: modelData.label
                                ghost: true
                                primary: veyra.hdrPresetMode === modelData.id
                                onClicked: veyra.hdrPresetMode = modelData.id
                            }
                        }
                    }

                    // One-click presets: pick the one that matches the film.
                    Flow {
                        visible: veyra.hdrPresetMode === "legacy"
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        spacing: 6
                        Repeater {
                            model: [{id:"standard",label:qsTr("Standard")},
                                    {id:"darkLift",label:qsTr("Lift dark scenes")},
                                    {id:"highlightGuard",label:qsTr("Highlight protection")},
                                    {id:"darkAndBright",label:qsTr("Shadows and highlights")},
                                    {id:"brightRoom",label:qsTr("Bright room")},
                                    {id:"darkRoom",label:qsTr("Dark room")},
                                    {id:"punch",label:qsTr("Punch")}]
                            delegate: VButton {
                                required property var modelData
                                text: modelData.label
                                ghost: true
                                // Both presets write the same curve, so the values can both match (with "no compensation" the free combination equals the preset).
                                // Only the current mode's row is lit, so it cannot look like two modes at once.
                                primary: veyra.hdrAppliedFrom !== "axes" && veyra.hdrTuningPreset === modelData.id
                                onClicked: { veyra.hdrPresetMode = "legacy"; veyra.applyHdrTuningPreset(modelData.id) }
                            }
                        }
                    }
                    Text {
                        visible: veyra.hdrPresetMode === "legacy"
                        Layout.fillWidth: true
                        text: veyra.hdrTuningPreset === "darkLift" ? qsTr("Lifts shadows and midtones while keeping the highlight peak.")
                            : veyra.hdrTuningPreset === "highlightGuard" ? qsTr("Films with very bright highlights (snow, explosions, concerts): rolls off the top end and leaves the shadows alone.")
                            : veyra.hdrTuningPreset === "darkAndBright" ? qsTr("For content with deep shadows and bright highlights.")
                            : veyra.hdrTuningPreset === "brightRoom" ? qsTr("Raises overall brightness for a bright viewing environment.")
                            : veyra.hdrTuningPreset === "darkRoom" ? qsTr("Softer viewing in a dark room while retaining shadow detail.")
                            : veyra.hdrTuningPreset === "punch" ? qsTr("Adds contrast to flat or faded transfers.")
                            : veyra.hdrTuningPreset === "standard" ? qsTr("Keeps the original curve and follows source HDR10 metadata.")
                            : qsTr("Custom: adjusted with the sliders below.")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: veyra.hdrPresetMode === "axes"
                        text: qsTr("Content: what kind of film is this?")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    Flow {
                        objectName: "list-hdrcurve-content"
                        Layout.fillWidth: true
                        visible: veyra.hdrPresetMode === "axes"
                        Layout.preferredHeight: implicitHeight
                        spacing: 6
                        Repeater {
                            model: [{id:"standard",label:qsTr("Standard")}, {id:"dark",label:qsTr("Mostly dark")}, {id:"highlight",label:qsTr("Strong highlights")}, {id:"darkAndBright",label:qsTr("Dark and bright")}, {id:"punch",label:qsTr("Flat older transfer")}]
                            delegate: VButton {
                                required property var modelData
                                text: modelData.label
                                ghost: true
                                // Lit only by the combination the curve actually equals: a whole preset (or a
    // hand-tuned curve) lights nothing, matching the whole-preset "custom" behaviour.
    primary: veyra.hdrAppliedFrom === "axes" && veyra.hdrAxesMatch.content === modelData.id
                                onClicked: { veyra.hdrPresetMode = "axes"; veyra.applyHdrAxesPreset(modelData.id, veyra.hdrAxes.environment !== "" ? veyra.hdrAxes.environment : "compensate"); }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: veyra.hdrPresetMode === "axes"
                        text: qsTr("Ambient compensation: add some in a bright room; use none in a dark room")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    Flow {
                        objectName: "list-hdrcurve-environment"
                        Layout.fillWidth: true
                        visible: veyra.hdrPresetMode === "axes"
                        Layout.preferredHeight: implicitHeight
                        spacing: 6
                        Repeater {
                            model: [{id:"compensate",label:qsTr("Bright-room compensation")}, {id:"plain",label:qsTr("No compensation")}]
                            delegate: VButton {
                                required property var modelData
                                text: modelData.label
                                ghost: true
                                primary: veyra.hdrAppliedFrom === "axes" && veyra.hdrAxesMatch.environment === modelData.id
                                onClicked: { veyra.hdrPresetMode = "axes"; veyra.applyHdrAxesPreset(veyra.hdrAxesMatch.content !== "" ? veyra.hdrAxesMatch.content : (veyra.hdrAxes.content !== "" ? veyra.hdrAxes.content : "standard"), modelData.id); }
                            }
                        }
                    }
                    // Current status (custom): in the whole-preset mode it names the current step; after a hand
                    // tune (matches no whole preset) it says "custom" explicitly.
                    Text {
                        Layout.fillWidth: true
                        visible: veyra.hdrPresetMode !== "axes"
                        objectName: "list-hdrcurve-active-preset"
                        text: veyra.hdrTuningPreset !== "custom"
                            ? qsTr("Active preset: %1").arg(
                                veyra.hdrTuningPreset === "darkLift" ? qsTr("Lift dark scenes")
                                : veyra.hdrTuningPreset === "highlightGuard" ? qsTr("Highlight protection")
                                : veyra.hdrTuningPreset === "darkAndBright" ? qsTr("Shadows and highlights")
                                : veyra.hdrTuningPreset === "brightRoom" ? qsTr("Bright room")
                                : veyra.hdrTuningPreset === "darkRoom" ? qsTr("Dark room")
                                : veyra.hdrTuningPreset === "punch" ? qsTr("Punch")
                                : qsTr("Standard"))
                            : qsTr("Active: custom adjustments (no matching preset)")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    // Current combination (custom): in the free-combination mode it names the two items now
                    // selected. After a hand tune matches nothing, this line stays visible.
                    Text {
                        Layout.fillWidth: true
                        visible: veyra.hdrPresetMode === "axes"
                        text: qsTr("Current combination: content %1 · environment %2").arg(
                                veyra.hdrAxes.content === "dark" ? qsTr("Mostly dark")
                                : veyra.hdrAxes.content === "highlight" ? qsTr("Strong highlights")
                                : veyra.hdrAxes.content === "darkAndBright" ? qsTr("Dark and bright")
                                : veyra.hdrAxes.content === "punch" ? qsTr("Flat older transfer")
                                : qsTr("Standard")).arg(
                                veyra.hdrAxes.environment === "compensate" ? qsTr("Bright-room compensation")
                                : qsTr("No compensation"))
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    // Equivalence note (custom): both presets write the same curve, and "no compensation" makes the free
                    // combination numerically identical to the whole preset, so the equivalence is stated directly.
                    // A hand-tuned curve matches nothing, and this sentence disappears on its own.
                    Text {
                        Layout.fillWidth: true
                        objectName: "list-hdrcurve-equivalent"
                        visible: veyra.hdrPresetMode === "axes" && veyra.hdrTuningPreset !== "custom"
                        text: qsTr("This curve also matches the full preset %1").arg(
                            veyra.hdrTuningPreset === "darkLift" ? qsTr("Lift dark scenes")
                            : veyra.hdrTuningPreset === "highlightGuard" ? qsTr("Highlight protection")
                            : veyra.hdrTuningPreset === "darkAndBright" ? qsTr("Shadows and highlights")
                            : veyra.hdrTuningPreset === "brightRoom" ? qsTr("Bright room")
                            : veyra.hdrTuningPreset === "darkRoom" ? qsTr("Dark room")
                            : veyra.hdrTuningPreset === "punch" ? qsTr("Punch")
                            : qsTr("Standard"))
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    }
                            VAccordion {
                                objectName: "list-hdr-basegroup"
                                Layout.fillWidth: true
                                glyph: "sliders"
                                hue: "#4F7BFF"
                                Layout.topMargin: 8
                                enabledSwitch: false
                                open: true
                                title: qsTr("Basic controls")
                                summary: qsTr("5 items")
                    VRow {
                        hintBelow: true
                        label: qsTr("Overall strength")
                        hint: qsTr("Adjustment amount; 0% disables the curve")
                        value: veyra.hdrCurveStrength + "%"
                        VSlider {
                            objectName: "list-hdrcurve-strength"
                            implicitWidth: 120
                            valueFromModel: true
                            resettable: true; defaultValue: 100
                            from: 0; to: 200
                            value: veyra.hdrCurveStrength
                            onMoved: value => veyra.hdrCurveStrength = Math.round(value)
                        }
                    }

                    // Plain sliders (custom): the technical parameters translated into three understandable sliders;
                    // the exact parameters stay in the "Advanced" section below.
                    VRow {
                        hintBelow: true
                        label: qsTr("Shadow brightness")
                        hint: qsTr("Lift shadows")
                        // The steps are only shortcuts: real parameters are given alongside, avoiding "a touch jumps the value" confusion.
                        value: (Number(veyra.hdrCurveParams.shadowLiftEv100 ?? 0) / 100).toFixed(2) + " EV"
                        VSlider {
                            objectName: "list-hdrcurve-dial-lift"
                            implicitWidth: 120
                            valueFromModel: true
                            resettable: true; defaultValue: 0
                            from: 0; to: 10
                            value: veyra.hdrDials.shadowLift
                            onMoved: value => veyra.setHdrDial("shadowLift", Math.round(value))
                        }
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Brightness balance")
                        hint: qsTr("Overall brightness")
                        value: (veyra.hdrCurveParams.midGrayNits ?? "—") + " nit"
                        VSlider {
                            objectName: "list-hdrcurve-dial-brightness"
                            implicitWidth: 120
                            valueFromModel: true
                            resettable: true; defaultValue: 3
                            from: 0; to: 10
                            value: veyra.hdrDials.brightness
                            onMoved: value => veyra.setHdrDial("brightness", Math.round(value))
                        }
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Highlight protection")
                        hint: qsTr("Protect highlights")
                        value: [qsTr("Off"),qsTr("Weak"),qsTr("Medium"),qsTr("Strong")][Math.max(0,Math.min(3,veyra.hdrDials.highlight))]
                        VSlider {
                            objectName: "list-hdrcurve-dial-highlight"
                            implicitWidth: 120
                            valueFromModel: true
                            resettable: true; defaultValue: 0
                            from: 0; to: 3
                            value: veyra.hdrDials.highlight
                            onMoved: value => veyra.setHdrDial("highlight", Math.round(value))
                        }
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Display peak")
                        hint: qsTr("Display peak in nits; 0 = automatic")
                        value: veyra.hdrDisplayPeakNits > 0
                            ? (veyra.hdrDisplayPeakNits + " nit") : qsTr("Auto")
                        VSlider {
                            objectName: "list-hdrcurve-displaypeak"
                            implicitWidth: 120
                            valueFromModel: true
                            resettable: true; defaultValue: 0
                            from: 0; to: 4000
                            value: veyra.hdrDisplayPeakNits
                            onMoved: value => veyra.hdrDisplayPeakNits = Math.round(value)
                        }
                    }

                                // The display's own report, kept at the card's foot like the author's section notes.
                                Text {
                                    Layout.fillWidth: true
                                    visible: veyra.hdrDisplayInfo.valid
                                    text: qsTr("System report: peak %1, full frame %2, black %3 nit")
                                            .arg(veyra.hdrDisplayInfo.peakNits)
                                            .arg(veyra.hdrDisplayInfo.fullFrameNits)
                                            .arg(Number(veyra.hdrDisplayInfo.minNits).toFixed(3))
                                        + " · " + qsTr("System values are not measurements; enter a peak manually if needed")
                                    wrapMode: Text.WordWrap
                                    color: Theme.t3
                                    font.family: Theme.fontUi; font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: veyra.hdrDisplayInfo.valid && !veyra.hdrDisplayInfo.plausible
                                    text: qsTr("This figure is the display's ceiling declaration, not a measurement, so the curve is not scaled by it.")
                                    wrapMode: Text.WordWrap
                                    color: Theme.t3
                                    font.family: Theme.fontUi; font.pixelSize: 11
                                }
                            }

                    // The two dials. Both scale the baseline the sliders below
                    // edit, which is why moving either of them keeps the preset
                    // above selected instead of falling back to "custom".
                    // Advanced technical parameters (custom): the exact values behind the three
                    // dials above. Collapsed by default so the panel stays readable.
                    // Advanced parameters (custom): the exact values behind the three dials above,
                    // in the author's own collapsible-group style (label + chevron, no switch).
                    VAccordion {
                        objectName: "list-hdr-advanced"
                        Layout.fillWidth: true
                        glyph: "settings"
                        hue: "#8A93A6"
                        Layout.topMargin: 8
                        enabledSwitch: false
                        open: false
                        title: qsTr("Advanced: precise parameters")
                        summary: qsTr("6 items")
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Exact values for the three basic controls: shadows, brightness balance and highlight protection. Overall strength and display peak are already direct controls.")
                            wrapMode: Text.WordWrap
                            color: Theme.t3
                            font.family: Theme.fontUi; font.pixelSize: 11
                        }
                                                    Repeater {
                                                        model: [{key:"shadowLiftEv100",label:qsTr("Shadow lift"),from:-200,to:200,def:0,hint:qsTr("Units of 0.01 EV: positive lifts shadows, negative darkens them")},
                                                                {key:"shadowRangeNits",label:qsTr("Lift range (nit)"),from:1,to:100,def:10,hint:qsTr("Only shadows below this level are lifted")},
                                                                {key:"midGrayNits",label:qsTr("Mid-gray (nit)"),from:100,to:500,def:203,hint:qsTr("203 = neutral; higher brightens the picture with the peak unchanged")},
                                                                {key:"highlightStartNits",label:qsTr("Highlight start (nit)"),from:0,to:2000,def:0,hint:qsTr("0 = no highlight roll-off")},
                                                                {key:"rollOffPercent",label:qsTr("Roll-off strength (%)"),from:10,to:200,def:100,hint:qsTr("Higher values give a gentler highlight transition")},
                                                                {key:"peakCapNits",label:qsTr("Peak cap (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = no limit")}]
                                                        delegate: VRow {
                                                            hintBelow: true
                                                            required property var modelData
                                                            label: modelData.label
                                                            hint: modelData.hint || ""
                                                            value: String(veyra.hdrCurveParams[modelData.key] ?? "—")
                                                            VSlider {
                                                                objectName: "list-hdrcurve-" + modelData.key
                                                                implicitWidth: 120
                                                                valueFromModel: true
                                                                resettable: true; defaultValue: modelData.def
                                                                from: modelData.from; to: modelData.to
                                                                value: veyra.hdrCurveParams[modelData.key] ?? modelData.def
                                                                onMoved: value => veyra.setHdrCurveParameter(modelData.key, Math.round(value))
                                                            }
                                                        }
                                                    }
                    }
                    VAccordion {
                        objectName: "list-hdr-memory-group"
                        Layout.fillWidth: true
                        glyph: "clock"
                        hue: "#4FD1A5"
                        Layout.topMargin: 8
                        enabledSwitch: false
                        open: false
                        title: qsTr("Memory and automation")
                        summary: qsTr("4 items")
                    // Memory and automation (custom)
                    // One configuration per display device: the curve is remembered by display identity,
                    // and applied automatically to that device (projector / TV) on swap.
                    VRow {
                        hintBelow: true
                        label: qsTr("Remember per display")
                        hint: veyra.hdrDisplayProfileCount > 0
                            ? qsTr("Curves saved for %1 displays; restored when switching displays")
                                .arg(veyra.hdrDisplayProfileCount)
                            : qsTr("Curve edits are saved for this display and restored when you switch back")
                        value: veyra.hdrDisplayProfileCount > 0 ? qsTr("Enabled") : qsTr("—")
                        VButton {
                            objectName: "list-hdrcurve-forget"
                            text: qsTr("Clear memory")
                            ghost: true
                            enabled: veyra.hdrDisplayProfileCount > 0
                            onClicked: veyra.forgetHdrDisplayProfiles()
                        }
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Remember each film")
                        hint: veyra.hdrTitleCount > 0
                            ? qsTr("Curves saved for %1 films; restored on next playback").arg(veyra.hdrTitleCount)
                            : qsTr("Curve edits are saved for the film and restored next time")
                        value: veyra.hdrTitleMemory ? qsTr("On") : qsTr("Off")
                        VSwitch {
                            objectName: "list-hdrcurve-titlememory"
                            checked: veyra.hdrTitleMemory
                            onToggled: checked => veyra.hdrTitleMemory = checked
                        }
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Auto-select on playback")
                        hint: qsTr("Select once when a film opens; remembered film settings take priority")
                        value: veyra.hdrAutoRecommend ? qsTr("On") : qsTr("Off")
                        VSwitch {
                            objectName: "list-hdrcurve-autorecommend"
                            checked: veyra.hdrAutoRecommend
                            onToggled: checked => veyra.hdrAutoRecommend = checked
                        }
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Clear film memory")
                        hint: qsTr("Clear film memory; keep display memory")
                        VButton {
                            objectName: "list-hdrcurve-forget-titles"
                            text: qsTr("Clear")
                            ghost: true
                            enabled: veyra.hdrTitleCount > 0
                            onClicked: veyra.forgetHdrTitles()
                        }
                    }
                    }
                }

                // Dolby Vision: its own card, not mixed into the HDR curve's basic settings.
                VAccordion {
                    objectName: "list-hdr-dovi"
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    glyph: "sun"
                    hue: "#A88BF0"
                    title: qsTr("Dolby Vision / HDR10+ settings")
                    summary: {
                        const d = veyra.hdrSourceInfo;
                        // The follow switch below serves HDR10+ as well, so a
                        // Dolby-only summary said "no Dolby Vision source" on a
                        // stream that does have dynamic metadata.
                        if(d && d.hdr10Plus) return qsTr("HDR10+ source (per-frame following available)");
                        if(!d || !d.doviProfile) return qsTr("No source with dynamic metadata detected");
                        const kind = d.doviProfile === 5 ? qsTr("Single layer")
                            : d.doviProfile === 7 ? qsTr("Dual layer")
                            : d.doviProfile === 8 ? qsTr("Compatible layer") : qsTr("Other");
                        const peak = d.doviMaxNits > 0
                            ? qsTr(" · peak %1 nit").arg(d.doviMaxNits) : "";
                        return qsTr("Profile %1 (%2) %3").arg(d.doviProfile).arg(kind).arg(peak);
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Recommendation for this source")
                        hint: {
                            if (!(veyra.hdrSourceInfo && veyra.hdrSourceInfo.doviProfile > 0))
                                return qsTr("this source is not Dolby Vision; use the recommendation button under the curve settings");
                            if (!veyra.hdrRecommendation.available) {
                                // Profile 5 keeps its brightness inside the RPU, so the
                                // HDR10 static-metadata recommendation cannot run even
                                // though the follow switch still works. Say which case
                                // this is instead of implying the source has no data.
                                const prof = veyra.hdrSourceInfo
                                    ? veyra.hdrSourceInfo.doviProfile : 0;
                                if (veyra.sourceIsSdr)
                                    return qsTr("SDR sources carry no HDR brightness metadata; pick a preset manually");
                                if (prof === 5)
                                    return qsTr("Profile 5 brightness is in the RPU, not HDR10 metadata; automatic preset recommendations are unavailable. Dynamic metadata following below still works");
                                return qsTr("no brightness metadata in this source; cannot pick automatically");
                            }
                            var r = veyra.hdrRecommendation.reason;
                            var why = r === "dark-content" ? qsTr("the film is mostly dark")
                                : r === "bright-content" ? qsTr("Many strong highlights")
                                : r === "narrow-range" ? qsTr("low mastering peak (older transfer)")
                                : r === "beyond-display" ? qsTr("Source brightness exceeds display capability")
                                : r === "dark-and-bright" ? qsTr("dim, and brighter than the screen can show")
                                : qsTr("Balanced brightness");
                            var pick = r === "dark-content" ? qsTr("Lift dark scenes")
                                : r === "bright-content" ? qsTr("Highlight protection")
                                : r === "beyond-display" ? qsTr("Highlight protection")
                                : r === "dark-and-bright" ? qsTr("Shadows and highlights")
                                : r === "narrow-range" ? qsTr("Punch") : qsTr("Standard");
                            return why + qsTr(" → Suggested: ") + pick
                                + qsTr("(source peak %1 nit)").arg(veyra.hdrRecommendation.cllNits);
                        }
                        VButton {
                            objectName: "list-hdr-dovi-recommend"
                            text: qsTr("Apply recommendation")
                            ghost: true
                            enabled: veyra.hdrRecommendation.available
                                && veyra.hdrSourceInfo
                                && veyra.hdrSourceInfo.doviProfile > 0
                            onClicked: veyra.applyRecommendedHdrTuning()
                        }
                    }
                    VRow {
                        hintBelow: true
                        // The follow only moves where the curve's roll-off starts,
                        // so with the curve off there is nothing for it to move.
                        enabled: veyra.hdrCurve
                        opacity: enabled ? 1 : 0.5
                        label: qsTr("Follow dynamic metadata")
                        hint: veyra.hdrCurve
                            ? qsTr("Dolby Vision L1 and HDR10+ scene metadata both use this path: highlights follow each frame's brightness; shadows and midtones retain your settings")
                            : qsTr("Turn the HDR curve on first; without it there is no roll-off to move")
                        value: veyra.doviFollow ? qsTr("On") : qsTr("Off")
                        VSwitch {
                            objectName: "list-hdr-dovi-follow"
                            checked: veyra.doviFollow
                            onToggled: checked => veyra.doviFollow = checked
                        }
                    }
                    VRow {
                        hintBelow: true
                        visible: veyra.doviFollow
                        enabled: veyra.hdrCurve
                        opacity: enabled ? 1 : 0.5
                        label: qsTr("Use average brightness")
                        hint: qsTr("When on, the highlight start also considers each frame’s average brightness: dark films compress earlier, bright ones keep more highlights. Off uses the peak only.")
                        VSwitch {
                            objectName: "list-hdr-dovi-follow-average"
                            checked: veyra.doviFollowAverage
                            onToggled: checked => veyra.doviFollowAverage = checked
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: veyra.doviFollow && !veyra.hdrCurve
                        text: qsTr("The curve is off, so following does not change the picture yet.")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                    VRow {
                        hintBelow: true
                        label: qsTr("Merge enhancement layer (experimental)")
                        hint: qsTr("Experimental: applies to Profile 7 dual-layer sources only, and most frames fall back to the base layer because the enhancement layer fails to decode. Turning it off forces base-layer-only playback (applies after reopening the file).")
                        value: {
                            const d = veyra.hdrSourceInfo;
                            if(!d || !d.doviEnhancement) return qsTr("No enhancement layer");
                            return veyra.doviP7Compose ? qsTr("On") : qsTr("Off");
                        }
                        VSwitch {
                            objectName: "list-hdr-dovi-p7"
                            checked: veyra.doviP7Compose
                            onToggled: checked => veyra.doviP7Compose = checked
                        }
                    }
                }
                }
    VGroup {
        VRow {
            label: qsTr("Submit rate")
            value: veyra.submitFpsKnown ? veyra.submitFps.toFixed(2) : qsTr("Not measured")
        }
        VRow {
            label: qsTr("1% low")
            hint: qsTr("The frame rate of the slowest 1% of submit intervals in the last 10 s, computed the same way as overlays such as RTSS")
            value: veyra.submitLowFpsKnown ? veyra.submitLowFps.toFixed(2) : "—"
        }
        VRow {
            label: qsTr("Late P95")
            value: veyra.lateP95Ms.toFixed(2) + " ms"
        }
        VRow {
            label: qsTr("Scheduling wait P95")
            value: veyra.scheduleP95Ms.toFixed(2) + " ms"
        }
    }
}
