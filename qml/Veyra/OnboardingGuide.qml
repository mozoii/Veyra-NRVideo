import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: guide
    objectName: "onboarding-guide"
    required property var targetFor
    signal stepRequested(var step)

    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.NoAutoClose
    popupType: Popup.Item
    background: null

    property int stepIndex: -1
    readonly property var step: steps[stepIndex]
    property rect spotlight: Qt.rect(0, 0, 0, 0)
    readonly property var steps: [
        { target: "capture", title: qsTr("Start with your capture card"),
          detail: qsTr("On the home page, open Capture card to set up your HDMI input.") },
        { target: "device", title: qsTr("Choose your capture card"),
          detail: qsTr("Under Video input device, pick the capture card you connected. Veyra will list the capture modes it supports.") },
        { target: "resolution", title: qsTr("Choose a capture resolution"),
          detail: qsTr("Pick one of the resolutions your capture card reports. The list of frame rates updates with your selection.") },
        { target: "capture-rate", title: qsTr("Prefer 60 FPS for capture"),
          detail: qsTr("Use 60 FPS if your card offers it. 59.94 FPS is another common option close to 60. You can capture at 60 FPS even if the game runs at 30 FPS.") },
        { target: "format", title: qsTr("Choose a capture format"),
          detail: qsTr("Pixel format lists the options available at the selected resolution and frame rate. Here is what the common formats carry.") },
        { target: "input-color", title: qsTr("Set the input color"),
          detail: qsTr("When your console is outputting HDR10, choose P010 and set the input to HDR · PQ. Use SDR · 709 for NV12, YUY2, or RGB24. Reconnect to apply the change.") },
        { target: "connect", title: qsTr("Connect and start capture"),
          detail: qsTr("After you choose a device and format, connect here. Check that the preview has a picture and the capture rate looks right, then explore enhancements in Pro mode.") },
        { tab: "quality", target: "sr", title: qsTr("Find Super resolution here"),
          detail: qsTr("In Super resolution, pick an algorithm and a target size when you want to change the output resolution.") },
        { tab: "quality", target: "nr", title: qsTr("DLSS 5 neural rendering (NR)"),
          detail: qsTr("Add a layer under NR enhancement. Veyra applies experimental neural rendering to the captured picture to enhance lighting, materials, and visual details.") },
        { tab: "quality", target: "hdr", title: qsTr("Convert SDR to HDR here"),
          detail: qsTr("Try RTX Video HDR to convert SDR to HDR when you have a supported GPU and an HDR display.") },
        { tab: "fg", target: "fg", title: qsTr("Set up frame generation"),
          detail: qsTr("Go to Frame Gen to choose an algorithm, a multiplier, and a quality mode. The multiplier controls how many frames are generated between source frames.") },
        { tab: "fg", target: "dedup", title: qsTr("Turn on automatic deduplication"),
          detail: qsTr("Under Optical flow · motion estimation, turn on Remove duplicate frames and select Automatic removal. Veyra compares images and skips repeats so frame generation uses different source frames. It checks the picture even when the game's frame rate varies.") },
        { tab: "quality", target: "timing", title: qsTr("Check frame rates and timings"),
          detail: qsTr("Compare input FPS, output FPS, and GPU timings to check whether your settings are running steadily. In fullscreen, press Home for detailed timings.") }
    ]

    function start() {
        if (visible) return
        open()
        goTo(0)
    }
    function goTo(index) {
        if (index < 0) return
        if (index >= steps.length) { close(); return }
        if (index === 0 && previousButton.activeFocus) nextButton.forceActiveFocus()
        stepIndex = index
        copyScroll.cancelFlick()
        copyScroll.contentY = copyScroll.originY
        spotlight = Qt.rect(0, 0, 0, 0)
        stepRequested(step)
        Qt.callLater(updateSpotlight)
    }
    onOpened: nextButton.forceActiveFocus()
    onClosed: stepIndex = -1

    function intersect(a, b) {
        const left = Math.max(a.x, b.x)
        const top = Math.max(a.y, b.y)
        const right = Math.min(a.x + a.width, b.x + b.width)
        const bottom = Math.min(a.y + a.height, b.y + b.height)
        return Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top))
    }

    function updateSpotlight() {
        if (!visible || !step) return
        const anchor = targetFor(step.target)
        const item = anchor ? anchor.item : null
        if (!item || !item.visible || item.width <= 0 || item.height <= 0) {
            if (spotlight.width > 0) spotlight = Qt.rect(0, 0, 0, 0)
            return
        }

        const viewport = anchor.viewport
        if (viewport && viewport.height > 0) {
            const row = item.mapToItem(viewport, 0, 0, item.width, item.height)
            const margin = Math.min(8, Math.max(0, (viewport.height - row.height) / 2))
            if (row.y < margin - 0.5 || row.y + row.height > viewport.height - margin + 0.5) {
                const offset = row.y - Math.max(0, (viewport.height - row.height) / 2)
                const maxY = viewport.originY + Math.max(0, viewport.contentHeight - viewport.height)
                const desiredY = Math.max(viewport.originY, Math.min(maxY, viewport.contentY + offset))
                if (Math.abs(viewport.contentY - desiredY) > 0.5) viewport.contentY = desiredY
            }
        }

        const bounds = item.mapToItem(pane, -4, -4, item.width + 8, item.height + 8)
        let rect = intersect(bounds, Qt.rect(0, 0, width, height))
        for (let ancestor = item.parent; ancestor && rect.width > 0 && rect.height > 0; ancestor = ancestor.parent) {
            if (ancestor.clip)
                rect = intersect(rect, ancestor.mapToItem(pane, 0, 0, ancestor.width, ancestor.height))
        }
        if (rect.x !== spotlight.x || rect.y !== spotlight.y ||
            rect.width !== spotlight.width || rect.height !== spotlight.height)
            spotlight = rect
    }

    // The walkthrough also follows late layouts, animated accordion bodies,
    // and rows that move as an existing capture source reports new properties.
    Timer { interval: 33; running: guide.visible; repeat: true; onTriggered: guide.updateSpotlight() }

    function tipPlacement() {
        const edge = 10
        const gap = 12
        const idealWidth = Math.min(340, Math.max(1, width - 2 * edge))
        const wholeHeight = Math.max(1, height - 2 * edge)
        const hole = spotlight
        if (hole.width <= 0 || hole.height <= 0)
            return { x: (width - idealWidth) / 2, centerY: height / 2, width: idealWidth, limit: wholeHeight }

        const roomLeft = hole.x - edge - gap
        const roomRight = width - hole.x - hole.width - edge - gap
        const sideWidth = Math.max(roomLeft, roomRight)
        if (sideWidth >= Math.min(270, idealWidth)) {
            const panelWidth = Math.min(idealWidth, sideWidth)
            const x = roomRight >= roomLeft ? hole.x + hole.width + gap : hole.x - gap - panelWidth
            return { x: x, centerY: hole.y + hole.height / 2, width: panelWidth, limit: wholeHeight }
        }

        const stackedWidth = Math.min(440, width - 2 * edge)
        const x = Math.max(edge, Math.min(width - edge - stackedWidth, hole.x + (hole.width - stackedWidth) / 2))
        const above = hole.y - edge - gap
        const below = height - hole.y - hole.height - edge - gap
        if (Math.max(above, below) < 49)
            return { x: x, centerY: height / 2, width: idealWidth, limit: wholeHeight }
        if (below >= above)
            return { x: x, top: hole.y + hole.height + gap, width: stackedWidth, limit: below }
        return { x: x, bottom: hole.y - gap, width: stackedWidth, limit: above }
    }
    readonly property var placement: tipPlacement()

    component GuideButton: VButton {
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.onPressAction: if (enabled) clicked()
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onSpacePressed: clicked()
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.color: Theme.t1
            visible: parent.activeFocus
        }
    }
    component CopyText: Text {
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    component FrameRow: Row {
        id: frames
        property bool deduplicated: false
        spacing: 4
        Repeater {
            model: frames.deduplicated ? ["A", "B", "C"] : ["A", "A", "B", "B", "C", "C"]
            delegate: Rectangle {
                required property string modelData
                required property int index
                readonly property bool repeated: !frames.deduplicated && index % 2 === 1
                width: 22
                height: 22
                radius: 4
                color: Theme.card3
                opacity: repeated ? 0.5 : 1
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: Theme.accent
                    font.family: Theme.fontMono
                    font.pixelSize: 12
                    font.strikeout: parent.repeated
                }
            }
        }
    }

    contentItem: FocusScope {
        id: pane
        readonly property bool videoCover: true
        Keys.onShortcutOverride: event => event.accepted = true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) guide.close()
            else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_Right) guide.goTo(guide.stepIndex + 1)
            else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_Left) guide.goTo(guide.stepIndex - 1)
            else return
            event.accepted = true
        }

        readonly property color shade: Qt.rgba(0, 0, 0, 0.72)
        Rectangle {
            width: parent.width
            height: hotspot.visible ? hotspot.y : parent.height
            color: pane.shade
        }
        Rectangle {
            y: hotspot.y + hotspot.height
            width: parent.width
            height: hotspot.visible ? Math.max(0, parent.height - y) : 0
            color: pane.shade
        }
        Rectangle {
            y: hotspot.y
            width: hotspot.visible ? hotspot.x : 0
            height: hotspot.height
            color: pane.shade
        }
        Rectangle {
            x: hotspot.x + hotspot.width
            y: hotspot.y
            width: hotspot.visible ? Math.max(0, parent.width - x) : 0
            height: hotspot.height
            color: pane.shade
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            onWheel: event => event.accepted = true
        }
        MouseArea {
            id: hotspot
            objectName: "onboarding-spotlight"
            x: guide.spotlight.x
            y: guide.spotlight.y
            width: guide.spotlight.width
            height: guide.spotlight.height
            visible: width > 0 && height > 0
            cursorShape: Qt.PointingHandCursor
            preventStealing: true
            Accessible.role: Accessible.Button
            Accessible.name: nextButton.text
            Accessible.onPressAction: guide.goTo(guide.stepIndex + 1)
            onClicked: guide.goTo(guide.stepIndex + 1)
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: 7
                border.width: 1
                border.color: Theme.accent
            }
            Rectangle {
                objectName: "onboarding-pulse"
                anchors.fill: parent
                anchors.margins: -2
                color: "transparent"
                radius: 9
                border.width: 2
                border.color: Theme.accent
                opacity: Theme.reduced ? 0.72 : 0.34
                SequentialAnimation on opacity {
                    running: guide.visible && hotspot.visible && !Theme.reduced
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.34; to: 0.92; duration: 925; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.92; to: 0.34; duration: 925; easing.type: Easing.InOutSine }
                }
            }
        }

        Rectangle {
            id: tip
            objectName: "onboarding-tip"
            x: guide.placement.x
            y: guide.placement.top !== undefined ? guide.placement.top
               : guide.placement.bottom !== undefined ? guide.placement.bottom - height
               : Math.max(10, Math.min(pane.height - height - 10, guide.placement.centerY - height / 2))
            width: guide.placement.width
            readonly property bool compact: guide.placement.limit < 130
            height: Math.min(guide.placement.limit, card.implicitHeight + 2 * card.anchors.topMargin)
            radius: 12
            color: Theme.dialog
            border.width: 1
            border.color: Theme.stroke2

            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            GridLayout {
                id: card
                columns: 4
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: tip.compact ? 4 : 7
                anchors.bottomMargin: anchors.topMargin
                rowSpacing: tip.compact ? 3 : 5
                columnSpacing: 5

                Text {
                    Layout.row: 0
                    Layout.column: 0
                    Layout.columnSpan: tip.compact ? 1 : 3
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: qsTr("Step %1 of %2").arg(guide.stepIndex + 1).arg(guide.steps.length)
                    color: Theme.accent
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                }
                GuideButton {
                    id: closeButton
                    objectName: "onboarding-close"
                    Layout.row: 0
                    Layout.column: 3
                    Layout.alignment: Qt.AlignRight
                    Layout.preferredWidth: tip.compact ? 24 : implicitWidth
                    Layout.preferredHeight: tip.compact ? 22 : implicitHeight
                    icon: true
                    iconName: "x"
                    ghost: true
                    tip: qsTr("Exit tour")
                    Accessible.name: closeButton.tip
                    onClicked: guide.close()
                }

                Flickable {
                    id: copyScroll
                    objectName: "onboarding-copy"
                    Layout.row: 1
                    Layout.column: 0
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: copyColumn.implicitHeight
                    contentWidth: width
                    contentHeight: copyColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                    ScrollBar.vertical: VScrollBar {}

                    WheelHandler {
                        target: null
                        enabled: copyScroll.interactive
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => {
                            const delta = event.pixelDelta.y || event.angleDelta.y / 3
                            if (delta === 0) return
                            copyScroll.cancelFlick()
                            const first = copyScroll.originY
                            const last = first + copyScroll.contentHeight - copyScroll.height
                            copyScroll.contentY = Math.max(first, Math.min(last, copyScroll.contentY - delta))
                            event.accepted = true
                        }
                    }

                    Column {
                        id: copyColumn
                        width: Math.max(0, parent.width - 10)
                        spacing: 6
                        CopyText {
                            objectName: "onboarding-title"
                            width: parent.width
                            text: guide.step ? guide.step.title : ""
                            font.pixelSize: Theme.fsH2
                            font.weight: Font.DemiBold
                            font.variableAxes: Theme.axesDemiBold
                            color: Theme.t1
                            Accessible.role: Accessible.Heading
                        }
                        CopyText {
                            width: parent.width
                            text: guide.step ? guide.step.detail : ""
                        }
                        Column {
                            visible: guide.step?.target === "format"
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: [
                                    { name: "NV12", note: qsTr("8-bit 4:2:0 · SDR with lower bandwidth requirements.") },
                                    { name: "YUY2", note: qsTr("8-bit 4:2:2 · SDR with more vertical color detail.") },
                                    { name: "RGB24", note: qsTr("8-bit RGB · SDR with a full set of RGB components per pixel and higher bandwidth.") },
                                    { name: "P010", note: qsTr("10-bit 4:2:0 · Of these four formats, use P010 for native HDR10 capture.") }
                                ]
                                delegate: RowLayout {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 6
                                    CopyText {
                                        Layout.preferredWidth: 46
                                        text: modelData.name
                                        color: Theme.accent
                                        font.weight: Font.DemiBold
                                        font.variableAxes: Theme.axesDemiBold
                                    }
                                    CopyText { Layout.fillWidth: true; text: modelData.note }
                                }
                            }
                        }
                        Column {
                            visible: guide.step?.target === "dedup"
                            width: parent.width
                            spacing: 4
                            CopyText { text: qsTr("30 FPS game example"); color: Theme.t3 }
                            CopyText { text: qsTr("60 FPS capture"); width: parent.width }
                            FrameRow {}
                            CopyText { text: qsTr("After automatic removal"); width: parent.width }
                            FrameRow { deduplicated: true }
                        }
                    }
                }

                GuideButton {
                    id: previousButton
                    objectName: "onboarding-previous"
                    Layout.row: tip.compact ? 0 : 2
                    Layout.column: tip.compact ? 1 : 0
                    Layout.maximumWidth: (tip.width - 56) / 2
                    Layout.preferredHeight: tip.compact ? 24 : implicitHeight
                    text: qsTr("Previous")
                    enabled: guide.stepIndex > 0
                    onClicked: guide.goTo(guide.stepIndex - 1)
                }
                GuideButton {
                    id: nextButton
                    objectName: "onboarding-next"
                    Layout.row: tip.compact ? 0 : 2
                    Layout.column: tip.compact ? 2 : 3
                    Layout.alignment: Qt.AlignRight
                    Layout.maximumWidth: (tip.width - 56) / 2
                    Layout.preferredHeight: tip.compact ? 24 : implicitHeight
                    text: guide.stepIndex === guide.steps.length - 1 ? qsTr("Done") : qsTr("Next")
                    primary: true
                    onClicked: guide.goTo(guide.stepIndex + 1)
                }
            }
        }
    }
}
