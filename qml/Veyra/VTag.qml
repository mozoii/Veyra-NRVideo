import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: tag
    property string text: ""
    property string kind: ""   // "" | "exp" | "acc" | "warn" | "ok"
    // Long labels: fill the width the layout gives the tag and wrap inside it.
    // The default keeps the one-line pill metrics every other tag relies on.
    property bool wrap: false
    implicitWidth: wrap ? 0 : tagText.implicitWidth + 14
    implicitHeight: wrap ? tagText.implicitHeight + 8 : Theme.tagHeight
    radius: 6
    color: kind === "exp" ? Theme.expSoft
         : kind === "acc" ? Theme.accentSoft
         : kind === "warn" ? Qt.rgba(0.961, 0.784, 0.294, 0.13)
         : kind === "ok" ? Qt.rgba(0.239, 0.863, 0.518, 0.12)
         : Qt.rgba(1, 1, 1, 0.07)
    Text {
        id: tagText
        anchors.centerIn: parent
        width: tag.wrap ? Math.max(0, tag.width - 14) : implicitWidth
        wrapMode: tag.wrap ? Text.WordWrap : Text.NoWrap
        horizontalAlignment: Text.AlignHCenter
        text: tag.text
        color: tag.kind === "exp" ? Theme.exp
             : tag.kind === "acc" ? Theme.accent
             : tag.kind === "warn" ? Theme.warn
             : tag.kind === "ok" ? Theme.ok
             : Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: 11
        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
    }
}
