import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: guide
    objectName: "capture-guide-dialog"
    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(760, Math.max(1, parent ? parent.width - 32 : 640))
    height: Math.min(680, Math.max(1, parent ? parent.height - 32 : 480))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        reader.contentY = 0
        reader.forceActiveFocus()
    }

    background: Rectangle {
        readonly property bool videoCover: guide.visible
        readonly property real coverRadius: 14
        color: Theme.bgOuter
        radius: coverRadius
        border.color: Theme.stroke
    }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Capture format and frame generation guide")
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH2
                font.bold: true
                wrapMode: Text.Wrap
            }
            VButton {
                objectName: "capture-guide-close"
                text: qsTr("Close")
                ghost: true
                onClicked: guide.close()
            }
        }

        Flickable {
            id: reader
            objectName: "capture-guide-scroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: article.height + 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            ScrollBar.vertical: VScrollBar { wide: true }

            function scrollBy(delta) {
                contentY = Math.max(0, Math.min(contentHeight - height, contentY + delta))
            }
            Keys.onUpPressed: scrollBy(-38)
            Keys.onDownPressed: scrollBy(38)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    scrollBy((event.key === Qt.Key_PageUp ? -1 : 1) * height * 0.85)
                    event.accepted = true
                }
            }

            Text {
                id: article
                objectName: "capture-guide-article"
                width: Math.max(1, reader.width - 14)
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: 14
                text: guide.visible ? [
                    "## " + qsTr("Pick your capture format first"),
                    qsTr("For a regular SDR game, I start with **1440p60 · NV12 · SDR · 709**. On a 1080p display, start at 1080p60. Check that the raw picture and capture rate are stable, then turn on frame generation and super resolution."),
                    qsTr("**Use P010 with HDR · PQ for native HDR10 capture.** Enable HDR on your console, choose **P010** under Pixel format and **HDR · PQ** under Input color (SDR / HDR), then reconnect. **For NV12, YUY2 and RGB, use SDR · 709 in this setup.**"),
                    [
                        "- **NV12** · " + qsTr("8-bit 4:2:0. It's light on bandwidth and my first choice for high resolution and high frame rates. If your card offers 4K60 NV12, give it a try."),
                        "- **YUY2** · " + qsTr("8-bit 4:2:2, with more vertical color detail. Try it if there's no suitable NV12 mode or you need 4:2:2 for fine colored text."),
                        "- **RGB** · " + qsTr("Usually RGB24, with a higher bandwidth cost. Pick it when the device offers only RGB for that mode or when you specifically need RGB. If automatic removal keeps reporting 60 fps, compare it with NV12."),
                        "- **P010** · " + qsTr("10-bit 4:2:0. Pair it with **HDR · PQ** when the console is sending HDR10. If the P010 signal is SDR, read it as SDR. I'd try 1440p60 HDR first on a card that can't do 4K60 HDR.")
                    ].join("\n"),
                    "## " + qsTr("Why I still capture 30 fps games at 60"),
                    qsTr("I develop Veyra. Here's what I'd use for GTA 6 or another console game running at 30 fps. Thanks for using it and telling me how it runs on your gear. If previewing in OBS or PotPlayer felt laggy, try the same card and capture mode in Veyra."),
                    qsTr("Why capture a 30 fps game at 60? At 60 fps there's a capture opportunity about every 16.7 ms, compared with 33.3 ms at 30 fps. New frames can reach the PC sooner. The actual delay depends on your card and driver."),
                    qsTr("Say the game outputs `ABCDEFG` and the card sends `AABBCCDDEEFFGG`. Two identical pictures give the interpolator no new motion to work with. In Frame Gen, choose **60→30** or **Automatic removal** so it handles distinct frames such as `A→B→C`. Start with 60→30 on a locked 30 fps game. If the rate moves around, compare Automatic removal with the game's actual frame rate."),
                    "## " + qsTr("Picking a frame generation model and quality"),
                    qsTr("For a 30 fps source, I prefer **VFG**. In the scenes I tried, DLSS and XeSS showed more ghosting and wobble at low input rates. Start at **VFG Medium 2X**. If that runs smoothly, try High. If VFG is unavailable, start at 2X with a backend your GPU supports."),
                    qsTr("VFG High can look incredibly smooth and it's demanding. At 4K on my RTX 5070, a generated frame took about **20 ms** in High. Medium took around **one-third to one-quarter** as much time. Pan the camera a few times and check the timings and pacing to see which setting works on your GPU."),
                    "## " + qsTr("The super resolution setting I use"),
                    qsTr("Veyra works with a finished image, which limits what DLSS SR can do here. On an NVIDIA GPU, I reach for **RTX Video Super Resolution · Standard · Medium**. It sharpens up the picture without taking a big bite out of the GPU budget."),
                    "## " + qsTr("Capture latency from my tests"),
                    qsTr("These are approximate readings from the original post. Your card and capture mode will affect the numbers."),
                    "- 1440p60 NV12 · 10–20 ms\n- 4K60 NV12 · 30–40 ms\n- 4K30 P010 HDR · 80–100 ms",
                    qsTr("If you care most about responsiveness, I'd capture at **1440p60 NV12 and upscale**. For an HDR look on a budget, try **RTX Video HDR** after SDR capture. For native 4K60 HDR capture, I'd look at PCIe cards."),
                    "## " + qsTr("Picking a capture card"),
                    qsTr("Check what the card can deliver to the PC. HDMI passthrough and capture have separate specifications."),
                    [
                        "- **5 Gbps USB** · " + qsTr("I mainly look at 1440p60 and 4K30 SDR modes. Examples from the post") + " · MICHENSEN T800/T900, KUHAIMI 27P/26P, ACASIS 007 Pro/009 Pro, AVerMedia GC553 Pro/GC551G2, Elgato HD60 X.",
                        "- **10 Gbps USB** · " + qsTr("Look for 4K60 NV12, 1440p60 HDR or 4K30 HDR. Examples from the post") + " · MICHENSEN T911, KUHAIMI G50, AVerMedia GC553G2, Elgato 4K X.",
                        "- **PCIe** · " + qsTr("For 4K60 HDR, I'd look at these cards") + " · AVerMedia GC573, Elgato 4K Pro."
                    ].join("\n"),
                    qsTr("Comparable cards in my tests were usually within about 5 ms of each other. Pick one that handles your resolution, frame rate and color mode reliably. Enjoy your games!")
                ].join("\n\n") : ""
            }
        }

        VButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Read the Reddit post")
            ghost: true
            onClicked: Qt.openUrlExternally("https://www.reddit.com/r/GTA6unmoderated/comments/1xy9xff/how_to_properly_use_frame_generation_for_gta_6/")
        }
    }
}
