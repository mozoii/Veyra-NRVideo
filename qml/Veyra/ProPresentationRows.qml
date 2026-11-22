// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    id: presentationRows
    readonly property var profileTools: ("nvidiaVrrProfile" in veyra) ? veyra.nvidiaVrrProfile : null
    Layout.fillWidth: true
    spacing: 0
    VRow {
        label: qsTr("Display sync")
        // The explanation is long; keep it under the row so a short option strip
        // does not squeeze it into a narrow column beside the control.
        hintBelow: true
        hint: qsTr("Auto (recommended): no tearing and no VSync wait; each refresh shows the newest frame for the lowest latency · VSync: every frame waits for a refresh, at most one refresh longer than Auto · Allow tearing: tear lines may appear")
        VSeg {
            objectName: "presentation-display-sync"
            options: [{ id: "0", label: qsTr("Allow tearing") }, { id: "1", label: qsTr("VSync") }, { id: "2", label: qsTr("Auto") }]
            current: String(veyra.displaySync)
            onPicked: id => veyra.displaySync = Number(id)
        }
    }
    // Variable refresh (G-SYNC / FreeSync): the switch greys when the monitor cannot
    // do it. The state is read on events only; refresh display state in the signal
    // card asks the driver again.
    VRow {
        label: qsTr("Variable refresh")
        hint: veyra.vrrHint
        VSwitch {
            objectName: "presentation-vrr"
            enabled: veyra.vrrState >= 2
            opacity: enabled ? 1 : 0.5
            checked: veyra.variableRefresh
            onToggled: checked => veyra.variableRefresh = checked
        }
    }
    NvidiaVrrProfileRow { backend: presentationRows.profileTools }
    VRow {
        label: qsTr("Output cap")
        enabled: !veyra.presentationOwned
        hint: veyra.presentationStatus
        VSeg {
            objectName: "presentation-output-rate"
            options: [{ id: "0", label: qsTr("Off", "off") }, { id: "1", label: qsTr("Follow the display") }, { id: "2", label: qsTr("Custom") }]
            current: String(veyra.outputRateMode)
            onPicked: id => veyra.outputRateMode = Number(id)
        }
    }
    VRow {
        label: qsTr("Custom cap")
        enabled: !veyra.presentationOwned
        visible: veyra.outputRateMode === 2
        RowLayout {
            spacing: 6
            DialogHost.VTextField {
                objectName: "presentation-custom-fps"
                implicitWidth: 90
                text: veyra.outputCustomFps.toFixed(3)
                onEdited: text => { const v = Number(text); if (isFinite(v)) veyra.outputCustomFps = v }
            }
            Text { text: "FPS"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
        }
    }
}
