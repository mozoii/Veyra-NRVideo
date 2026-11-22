import QtQuick
import QtQuick.Layouts
import "EffectSupport.js" as Support

ColumnLayout {
    id: editor
    property string namePrefix: "list"
    spacing: 6
    // Tiers and the supersample shrink belong to the RTX Video SR families
    // (standard / high bitrate / streaming); double SR takes every SR algorithm.
    readonly property bool srAvailable: Support.available(veyra,"sr")
    readonly property bool supported: veyra.srAlgorithmOf(veyra.videoSrQuality).startsWith("rtx") && srAvailable
    readonly property bool secondRtx: veyra.srAlgorithmOf(veyra.dualSrQuality).startsWith("rtx")
    readonly property bool manual: veyra.srPrescale >= 150 && veyra.srPrescale <= 190
    readonly property var targets: [{id:"1",label:"2K"},{id:"2",label:"4K"},{id:"4",label:"5K"},{id:"5",label:"6K"},{id:"6",label:"7K"},{id:"3",label:"8K"}]
    VRow {
        label: qsTr("Target size")
        hint: qsTr("Base SR size; with a shrink set, this is the intermediate size.")
        VSeg { objectName: editor.namePrefix+"-sr-target"; options: editor.targets; current: String(veyra.srTargetIndex); onPicked: id => veyra.srTargetIndex=Number(id) }
    }
    VRow {
        label: qsTr("Super resolution motion source")
        hint: qsTr("Zero motion limits temporal reconstruction; RTX Video Super Resolution does not use this input")
        VSeg { options:[{id:"0",label:qsTr("Zero motion")},{id:"1",label:qsTr("Optical flow")}]; current:String(veyra.srMotionSource); onPicked:id=>veyra.srMotionSource=Number(id) }
    }
    VRow {
        label: qsTr("Tier")
        hint: qsTr("Higher tiers are sharper and take longer; Streaming has only Medium and Ultra.")
        enabled: editor.supported; opacity: enabled ? 1 : 0.4
        VSeg {
            objectName: editor.namePrefix+"-sr-quality"
            options: veyra.srTierChoices(veyra.videoSrQuality)
            current: String(veyra.srTierOf(veyra.videoSrQuality))
            onPicked: id => veyra.videoSrQuality = veyra.srQualityFor(veyra.srAlgorithmOf(veyra.videoSrQuality), Number(id))
        }
    }
    // Supersampling: a single RTX Video SR pass at the target, shrunk into this size.
    VRow {
        label: qsTr("Shrink after SR to")
        hint: !editor.supported ? qsTr("Shrink after SR currently uses RTX Video SR")
            : veyra.dualSrEnabled ? qsTr("Not used while double SR is on; it returns when double SR is turned off.")
            : qsTr("Supersampling: SR to the target, then an exact shrink. More natural, slightly softer; more video memory. NR and frame generation run at the shrunk size.")
        enabled: editor.supported && !veyra.dualSrEnabled
        opacity: enabled ? 1 : 0.4
        VSelect {
            objectName: editor.namePrefix+"-sr-output"
            options: [{id:"0",label:qsTr("No shrink")},{id:"1",label:qsTr("Source size")},{id:"2",label:"2K"},{id:"3",label:"4K"},{id:"4",label:"5K"},{id:"5",label:"6K"},{id:"6",label:"7K"}]
            value: options.find(o=>o.id===String(veyra.srOutput))?.label||qsTr("No shrink")
            onPicked: id => veyra.srOutput=Number(id)
        }
    }
    VRow {
        label: qsTr("Double super resolution")
        hint: qsTr("Finish base SR, reduce its result, then run SR again. The two evaluations increase GPU time and memory.")
        enabled: editor.srAvailable
        opacity: enabled ? 1 : 0.4
        VSwitch { objectName: editor.namePrefix+"-dual-sr-toggle"; checked: veyra.dualSrEnabled; onToggled: checked => veyra.dualSrEnabled=checked }
    }
    ColumnLayout {
        objectName: editor.namePrefix+"-dual-sr-panel"
        visible: veyra.dualSrEnabled
        enabled: editor.srAvailable
        Layout.fillWidth: true
        spacing: 6
        VRow {
            label: qsTr("Reduce the first result to")
            hint: qsTr("This reduces the base SR result, not the original input. Select a size below both the base result and the second SR target.")
            VSelect {
                objectName: editor.namePrefix+"-sr-prescale"
                options: [{id:"1",label:"720p"},{id:"2",label:"900p"},{id:"3",label:"1080p"},{id:"4",label:"1440p"},{id:"manual",label:qsTr("Manual")}]
                value: editor.manual ? qsTr("Manual") : (options.find(o=>o.id===String(veyra.srPrescale))?.label||"1080p")
                onPicked: id => veyra.srPrescale=id==="manual"?175:Number(id)
            }
        }
        VRow {
            visible: editor.manual
            label: qsTr("Second-pass scale")
            value: Math.round(percent.displayedValue)+"%"
            VSlider {
                id: percent; objectName: editor.namePrefix+"-sr-prescale-percent"
                valueFromModel: true; from: 50; to: 90; stepSize: 5; live: false
                value: editor.manual?veyra.srPrescale-100:75
                onMoved: value => veyra.srPrescale=100+Math.round(value)
            }
        }
        VRow {
            label: qsTr("Second SR target")
            VSeg { objectName: editor.namePrefix+"-dual-sr-target"; options: editor.targets; current: String(veyra.dualSrTargetIndex); onPicked: id=>veyra.dualSrTargetIndex=Number(id) }
        }
        VRow {
            label: qsTr("Second SR algorithm")
            hint: qsTr("Independent of the base SR; any SR algorithm works. Both DLSS SR passes use the DLSS model above.")
            VSelect {
                objectName: editor.namePrefix+"-dual-sr-algorithm"
                implicitWidth: 210
                options: veyra.srBackendChoices || []
                value: options.find(o => o.id === veyra.srAlgorithmOf(veyra.dualSrQuality))?.label || ""
                // Switching between RTX families keeps the tier; DLSS SR and FSR have none.
                onPicked: id => veyra.dualSrQuality = veyra.srQualityFor(id, veyra.srTierOf(veyra.dualSrQuality))
            }
        }
        VRow {
            visible: editor.secondRtx
            label: qsTr("Second SR tier")
            VSeg {
                objectName: editor.namePrefix+"-dual-sr-quality"
                options: veyra.srTierChoices(veyra.dualSrQuality)
                current: String(veyra.srTierOf(veyra.dualSrQuality))
                onPicked: id => veyra.dualSrQuality = veyra.srQualityFor(veyra.srAlgorithmOf(veyra.dualSrQuality), Number(id))
            }
        }
    }
    // Running sizes of double SR or supersampling, and why a choice did not apply.
    Text { text: veyra.srStageSummary; visible:text.length>0; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize:11; wrapMode:Text.WordWrap; Layout.fillWidth:true }
    Text { text: veyra.srStageNote; visible:text.length>0; color:Theme.t2; font.pixelSize:11; wrapMode:Text.WordWrap; Layout.fillWidth:true }
}
