import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    property bool allEffects: false
    property string namePrefix: allEffects ? "effect-exclusion" : "protection"
    readonly property var maskState: allEffects ? veyra.exclusionState : veyra.protectionState
    readonly property var regions: maskState.regions || []
    readonly property string drawShape: maskState.drawShape || ""
    Layout.fillWidth: true
    spacing: 8
    Text {
        Layout.fillWidth: true
        text: editor.drawShape.length>0
              ? qsTr("Drag a region on the picture. Press Esc to cancel.")
              : editor.allEffects
                ? qsTr("Restore the source picture inside each region, updating only at the source frame cadence. Up to 4 regions.")
                : qsTr("Exclude NR processing inside the regions. Up to 4 regions.")
        wrapMode: Text.WordWrap
        color: editor.drawShape.length>0?Theme.accent:Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        VButton {
            objectName: editor.namePrefix+"-draw-rect"
            text: qsTr("Draw rectangle")
            primary: editor.drawShape==="rect"
            onClicked: veyra.beginMaskDraw(editor.allEffects,editor.drawShape==="rect"?"":"rect")
        }
        VButton {
            objectName: editor.namePrefix+"-draw-ellipse"
            text: qsTr("Draw circle")
            primary: editor.drawShape==="ellipse"
            onClicked: veyra.beginMaskDraw(editor.allEffects,editor.drawShape==="ellipse"?"":"ellipse")
        }
        Item { Layout.fillWidth: true }
        VButton {
            text: qsTr("Clear")
            ghost: true
            visible: editor.regions.length>0
            onClicked: veyra.clearMaskRegions(editor.allEffects)
        }
    }
    VRow {
        visible: editor.allEffects
        label: qsTr("Show region outlines")
        hint: qsTr("Select a region to move it. Outlines are excluded from screenshots and exports.")
        VSwitch {
            objectName: editor.namePrefix+"-show-regions"
            checked: veyra.exclusionOverlayVisible
            onToggled: checked => veyra.exclusionOverlayVisible = checked
        }
    }
    Repeater {
        model: editor.regions
        delegate: VRow {
            required property var modelData
            required property int index
            label: (modelData.ellipse?qsTr("Circle "):qsTr("Rectangle "))+(index+1)
            value: Math.round(modelData.left*100)+","+Math.round(modelData.top*100)+" → "
                   +Math.round(modelData.right*100)+","+Math.round(modelData.bottom*100)+" %"
            Row {
                spacing: 6
                VButton {
                    visible: editor.allEffects
                    objectName: editor.namePrefix+"-select-"+index
                    text: editor.maskState.selected === modelData.index ? qsTr("Selected") : qsTr("Select")
                    primary: editor.maskState.selected === modelData.index
                    onClicked: veyra.selectExclusionRegion(modelData.index)
                }
                VButton {
                    objectName: editor.namePrefix+"-remove-"+index
                    icon: true; ghost: true; iconName: "x"
                    onClicked: veyra.removeMaskRegion(editor.allEffects,modelData.index)
                }
            }
        }
    }
    VRow {
        label: qsTr("Feather (pixels)")
        value: Number(editor.maskState.feather || 0).toFixed(0)+" px"
        VSlider {
            objectName: editor.namePrefix+"-feather"
            implicitWidth: 110
            valueFromModel: true
            from: 0; to: 64
            value: editor.maskState.feather || 0
            onMoved: value=>veyra.setMaskFeather(editor.allEffects,Math.round(value))
        }
    }
}
