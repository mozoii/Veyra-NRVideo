import QtQuick
import QtQuick.Layouts

VRow {
    id: row
    required property var backend
    property string namePrefix: "list"
    property bool showHelp: true
    property int lastMode: 1
    readonly property int currentMode: backend && backend.contentRate!==undefined
        ? (Number(backend.contentRate)===5?5:Number(backend.contentRate)>0?1:0) : 0
    readonly property var modes: [
        {id:"5",label:"60→30"},
        {id:"1",label:qsTr("Automatic removal")}
    ]
    onCurrentModeChanged: if(currentMode!==0)lastMode=currentMode
    Component.onCompleted: if(currentMode!==0)lastMode=currentMode
    label: qsTr("Remove duplicate frames")
    hint: showHelp?qsTr("60→30 keeps every other frame and skips repeated images when the game runs below 30 fps. Automatic removal detects and skips duplicate images."):""
    hintBelow: true
    RowLayout {
        spacing: 8
        VSwitch {
            objectName: row.namePrefix+"-dedup-enabled"
            checked: row.currentMode!==0
            onToggled: checked=>row.backend.contentRate=checked?row.lastMode:0
        }
        VSelect {
            objectName: row.namePrefix+"-content-rate"
            implicitWidth: 136
            title: qsTr("Remove duplicate frames")
            enabled: row.currentMode!==0
            options: row.modes
            value: (row.modes.find(option=>Number(option.id)===(row.currentMode||row.lastMode))||row.modes[1]).label
            onPicked: id=>{
                const mode=Number(id)
                if(mode===1||mode===5)row.backend.contentRate=mode
            }
        }
    }
}
