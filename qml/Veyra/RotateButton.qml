import QtQuick

VButton {
    icon: true
    ghost: true
    iconName: "rotatecw"
    implicitWidth: 28
    implicitHeight: 28
    enabled: veyra.hasSource
    Accessible.role: Accessible.Button
    Accessible.name: tip
    tip: qsTr("Rotate 90° clockwise (rotated %1°)").arg(veyra.previewRotation)
    onClicked: veyra.rotatePictureClockwise()
}
