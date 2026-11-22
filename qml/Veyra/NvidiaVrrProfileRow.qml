import QtQuick
import QtQuick.Layouts

VRow {
    id: row
    property var backend: null
    objectName: "presentation-gsync-profile"
    visible: backend !== null
    label: qsTr("NVIDIA G-SYNC setup")
    hintBelow: true
    hint: backend ? backend.summary + "\n" + backend.detail : ""
    Flow {
        id: actions
        width: Math.min(row.width, checkButton.implicitWidth
            + (repairButton.visible ? repairButton.implicitWidth + spacing : 0)
            + (restoreButton.visible ? restoreButton.implicitWidth + spacing : 0))
        spacing: 6
        VButton {
            id: checkButton
            objectName: "presentation-gsync-check"
            width: Math.min(implicitWidth, row.width)
            text: qsTr("Check")
            enabled: row.backend !== null && !row.backend.busy
            onClicked: row.backend.check()
        }
        VButton {
            id: repairButton
            objectName: "presentation-gsync-repair"
            width: Math.min(implicitWidth, row.width)
            text: qsTr("Repair")
            visible: row.backend !== null && row.backend.canRepair
            onClicked: row.backend.repair()
        }
        VButton {
            id: restoreButton
            objectName: "presentation-gsync-restore"
            width: Math.min(implicitWidth, row.width)
            text: qsTr("Restore original")
            visible: row.backend !== null && row.backend.canRestore
            ghost: true
            onClicked: row.backend.restore()
        }
    }
}
