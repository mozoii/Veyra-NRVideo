// Professional mode · node view, rebuilt from the design (pages-node.js + pages.css .nodeview).
//
// Design structure: padding 14; a fixed 330px video card on top with a stats bar
// at its foot; a 10px split handle; then an infinite canvas with the nodes. The
// stats bar carries input / output / display fps / submit fps / queue / total latency, and each
// node shows its own cost in its header. A timing strip along the bottom of the
// canvas colours each stage by its cost, with the remaining budget hatched.
//
// Pinned stages: frame generation is always last, RTX Video HDR always immediately before it.
// Both are drawn locked; everything else is freely arrangeable.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "EffectSupport.js" as Support
import QtQuick.Shapes

VPage {
    id: root
    signal requestPage(string page)
    signal requestDialog(string key)
    signal requestInspector(int nodeIndex, string nodeType)
    signal requestFullscreen()

    // Fullscreen quick-adjust (Home, Main.qml): only the canvas is shown, as a
    // panel along the bottom of the picture, cut out of the native video window.
    property bool overlay: false
    function overlayContains(x, y) {
        if (!overlay) return false
        const p = canvasCard.mapToItem(root, 0, 0)
        return x >= p.x && x <= p.x + canvasCard.width && y >= p.y && y <= p.y + canvasCard.height
    }
    property real panX: 220
    property real panY: 0
    property real zoom: 1.0
    property int selectedNode: -1
    property int selectedId: -1
    property int wireFrom: -1
    // Height of the video card; the splitter under it edits it (Main keeps the
    // native video window in step).
    property real videoHeight: 330
    signal videoHeightEdited(real height)
    // The status summary can wrap and the transport gains a row on narrow
    // windows. The native video host must use this same responsive geometry.
    readonly property bool shortLayout: height < 400
    readonly property real minimumVideoCardHeight: shortLayout ? statsBar.height + 44 : 180
    readonly property real maximumVideoCardHeight:
        Math.max(minimumVideoCardHeight, height - 28 - 10 - (shortLayout ? 110 : 200))
    readonly property real videoCardHeight: Math.max(statsBar.height + 1,
        Math.min(root.videoHeight, maximumVideoCardHeight))
    readonly property rect videoViewport: Qt.rect(14, 14, Math.max(1, root.width - 28),
        Math.max(1, root.videoCardHeight - statsBar.height))
    // Where a node picked from the add menu lands (world coordinates).
    property var addAt: null
    readonly property real cardWidth: 236
    // Fixed boxes (input / optical flow / output) move on the canvas like any card; their
    // positions are editor furniture saved by the bridge (node-anchors.json).
    readonly property var anchorDefaults: ({ input: Qt.point(-190, 30), flow: Qt.point(-190, 106), output: Qt.point(1120, 30) })
    function anchorPos(key) {
        const a = veyra.nodeAnchors[key]
        return a ? Qt.point(a.x, a.y) : anchorDefaults[key]
    }
    // Wire drag / click-to-connect preview and the link a dragged node would enter.
    property point pointerWorld: Qt.point(0, 0)
    property bool dragWire: false
    property int hotFrom: -1
    property int hotTo: -1

    // A pointer on a card, a port (ports stick out past the card edge), or an
    // anchor box belongs to that item, never to the empty canvas.
    function hitWorld(p) {
        const w = world.mapFromItem(canvasCard, p.x, p.y)
        for (const c of world.children) {
            if (!c.visible || c.width <= 0) continue
            if (w.x >= c.x - 14 && w.x <= c.x + c.width + 14 && w.y >= c.y - 6 && w.y <= c.y + c.height + 6) return true
        }
        return false
    }
    function clearSelection() { selectedId = -1; selectedNode = -1; wireFrom = -1; dragWire = false; marked = []; wires.requestPaint() }
    // Ids picked with the rubber band (left drag on empty canvas).
    property var marked: []
    // pages-node.js / data.js hue per node type: the card header is tinted
    // with it (N1).
    function hueOf(type) {
        switch (type) {
        case "color": return "#E0C341"
        case "sr": return "#4F7BFF"
        case "aa": return "#7FD4FF"
        case "beauty": return "#FF8FBF"
        case "nr": return "#FF8A3D"
        case "protection": return "#8A8A96"
        case "nr-stabilizer": return "#5DCCA1"
        case "exclusion": return "#E7B868"
        case "video-hdr": return "#FFB547"
        case "frame-generation": case "dlss-fg": case "xess-fg": case "fsr3-fg": case "fsr4-fg": case "vfg-fg": case "anr-fg": case "amdfrc-fg": return "#3DDC84"
        }
        return "#55555C"
    }
    // M33 .bnode.born / die: the node just added pops in (scale .6 over .6s
    // --spring); a removed one shrinks to .7 and fades over .22s first.
    property int bornId: -1
    Timer { id: bornReset; interval: 900; onTriggered: root.bornId = -1 }
    function markBorn(id) { bornId = id; bornReset.restart() }
    function removeNodeAnimated(id) {
        const card = nodeRepeater.itemAt(veyra.nodeIndexForId(id))
        const drop = () => { const i = veyra.nodeIndexForId(id); if (i >= 0) veyra.removeEffect(i) }
        if (card && card.die) card.die(drop)
        else drop()
    }
    // Centre the view on a node and select it (timing strip clicks).
    function focusNode(id) {
        const card = nodeRepeater.itemAt(veyra.nodeIndexForId(id))
        if (!card) return
        selectId(id)
        panX = canvasCard.width / 2 - (card.x + card.width / 2) * zoom
        panY = Math.min(40, canvasCard.height / 3 - (card.y + 40) * zoom)
    }
    // Delete (the key) removes the marked nodes, or the selected one. Frame
    // generation stays: it is pinned to the end and can only be switched off.
    function deleteSelected() {
        const ids = marked.length > 0 ? marked.slice() : (selectedId > 1 ? [selectedId] : [])
        let removed = 0, pinned = 0
        for (const id of ids) {
            const n = veyra.chain.find(x => x.id === id)
            if (!n) continue
            if (n.mustBeLast) { ++pinned; continue }
            if (veyra.nodeIndexForId(id) >= 0) { root.removeNodeAnimated(id); ++removed }
        }
        clearSelection()
        veyra.logUi("ui-node", "delete ids=" + ids.length + " removed=" + removed + " pinned=" + pinned)
    }
    Shortcut {
        sequence: "Delete"
        enabled: root.visible && (root.marked.length > 0 || root.selectedId > 1)
        onActivated: root.deleteSelected()
    }
    // One local Escape action: the old overlapping wire/selection shortcuts
    // became ambiguous. In fullscreen the shell arbitrates visible operations.
    Shortcut {
        sequence: "Escape"
        enabled: root.visible && (!root.shell || !root.shell.fullscreen) &&
                 (root.wireFrom >= 0 || root.marked.length > 0)
        onActivated: {
            if (root.wireFrom >= 0) root.clearSelection()
            else root.marked = []
        }
    }
    // Every box with its TARGET position (where the spring is heading), not the
    // animated on-screen one; a box being dragged uses where the pointer holds it.
    function boxes() {
        const anchor = (key, item) => {
            const p = item.animate ? anchorPos(key) : Qt.point(item.x, item.y)
            return { key: key, item: item, x: p.x, y: p.y, w: item.width, h: item.height }
        }
        const out = [anchor("input", inputBox), anchor("output", outputBox)]
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const c = nodeRepeater.itemAt(i)
            if (!c || !c.node || c.node.id === undefined) continue
            out.push({ key: "n" + c.node.id, item: c, index: c.node.index,
                       x: c.animate ? c.node.x : c.x, y: c.animate ? c.node.y : c.y, w: c.width, h: c.height })
        }
        return out
    }
    function overlapping(gap) {
        const r = boxes(), g = gap || 0
        for (let i = 0; i < r.length; ++i) for (let j = i + 1; j < r.length; ++j) {
            const a = r[i], b = r[j]
            if (Math.min(a.x + a.w, b.x + b.w) + g > Math.max(a.x, b.x) && Math.min(a.y + a.h, b.y + b.h) + g > Math.max(a.y, b.y)) return true
        }
        return false
    }
    // Fit view (pages-node.js fit): every box in view.
    function fitView() {
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
        for (const b of boxes()) {
            x0 = Math.min(x0, b.x); y0 = Math.min(y0, b.y)
            x1 = Math.max(x1, b.x + b.w); y1 = Math.max(y1, b.y + b.h)
        }
        x0 -= 20; y0 -= 20; x1 += 20; y1 += 20
        // The floating canvas overlays sit over the bottom of the canvas: the timing
        // strip (12px margin + 18px) and the tools bar (36px margin + about 40px).
        // 90px of bottom reserve let the tallest card's last row slide under the tools
        // bar once English text made it taller, so the reserve covers both.
        const floatBottom = 36 + 40 + 8
        const k = Math.max(0.25, Math.min(1.0, Math.min((canvasCard.width - 40) / (x1 - x0),
                                                        (canvasCard.height - 30 - floatBottom) / (y1 - y0))))
        zoom = k
        panX = (canvasCard.width - (x1 - x0) * k) / 2 - x0 * k
        panY = 30 - y0 * k
        veyra.logUi("ui-node", "fit view zoom=" + k.toFixed(3))
    }
    function pathIds() {
        const next = {}
        for (const e of veyra.nodeConnections) next[e.from] = e.to
        const path = []
        let id = 0, guard = 0
        while (next[id] !== undefined && next[id] > 1 && guard++ < 32) { id = next[id]; path.push(id) }
        return path
    }
    // Auto arrange (pages-node.js data-auto): the wired path left to right, detached
    // cards on a row below, output after the last card. Saved like a drag.
    function autoArrange() {
        const path = pathIds(), GAP = 40
        let x = 40, bottom = 30
        path.forEach((nid, i) => {
            const index = veyra.nodeIndexForId(nid), c = nodeRepeater.itemAt(index)
            const y = 30 + (i % 2) * 24
            veyra.setEffectPosition(index, x, y)
            x += (c ? c.width : cardWidth) + GAP
            bottom = Math.max(bottom, y + (c ? c.height : 200))
        })
        veyra.setNodeAnchor("input", -190, 30)
        veyra.setNodeAnchor("output", x, 30)
        // Detached cards on their own row below the tallest chained card.
        let fx = 40
        for (const n of veyra.chain)
            if (path.indexOf(n.id) < 0) {
                const c = nodeRepeater.itemAt(n.index)
                veyra.setEffectPosition(n.index, fx, bottom + 80); fx += (c ? c.width : cardWidth) + GAP
            }
        veyra.logUi("ui-node", "auto arrange path=" + path.length + " detached=" + (veyra.chain.length - path.length))
        Qt.callLater(() => { settle(""); fitView() })
    }
    // settle() from pages-node.js: boxes closer than GAP push apart, the held
    // box (and the input) stays put; linked cards are pushed sideways so the
    // chain reads left to right. Positions animate with the spring (Behavior).
    function settle(fixedKey) {
        const list = boxes(), GAP = 28
        const r = list.map(b => ({ key: b.key, index: b.index, x: b.x, y: b.y, w: b.w, h: b.h }))
        const linked = new Set(pathIds().map(id => "n" + id))
        const moved = new Set()
        // Wider English cards need more passes to separate on a small canvas.
        for (let pass = 0; pass < 200; ++pass) {
            let any = false
            for (let i = 0; i < r.length; ++i) for (let j = i + 1; j < r.length; ++j) {
                const a = r[i], b = r[j]
                const ox = Math.min(a.x + a.w, b.x + b.w) + GAP - Math.max(a.x, b.x)
                const oy = Math.min(a.y + a.h, b.y + b.h) + GAP - Math.max(a.y, b.y)
                if (ox <= 0 || oy <= 0) continue
                const fixed = k => k === fixedKey || k === "input"
                if (fixed(a.key) && fixed(b.key)) continue
                any = true
                // Chains read left to right: the right-hand box moves right (the
                // left one moves left only when the right one is held). Loose
                // cards stack downwards the same way.
                // A box squeezed between a held box and a neighbour would bounce
                // sideways forever; after 20 passes the rest resolve downwards.
                const horiz = pass < 20 && (ox < oy * 1.4 || linked.has(a.key) || linked.has(b.key) || a.key === "output" || b.key === "output")
                let mover, delta
                if (horiz) {
                    const right = (b.x + b.w / 2 >= a.x + a.w / 2) ? b : a, left = right === a ? b : a
                    if (!fixed(right.key)) { mover = right; mover.x += ox } else { mover = left; mover.x -= ox }
                } else {
                    const lower = (b.y + b.h / 2 >= a.y + a.h / 2) ? b : a, upper = lower === a ? b : a
                    if (!fixed(lower.key)) { mover = lower; mover.y += oy } else { mover = upper; mover.y -= oy }
                }
                moved.add(mover.key)
            }
            if (!any) break
        }
        for (const b of r) {
            if (!moved.has(b.key)) continue
            if (b.index !== undefined) veyra.setEffectPosition(b.index, b.x, b.y)
            else veyra.setNodeAnchor(b.key, b.x, b.y)
        }
        if (moved.size) {
            veyra.logUi("ui-node", "settle moved=" + Array.from(moved).join(","))
            // The cards are wider and taller than they were for Chinese names, so a chain
            // that fitted before can now run past the canvas and under the floating
            // tools bar and timing strip. Bring the view back to fit instead.
            let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
            for (const b of r) {
                x0 = Math.min(x0, b.x); y0 = Math.min(y0, b.y)
                x1 = Math.max(x1, b.x + b.w); y1 = Math.max(y1, b.y + b.h)
            }
            const fits = (x1 - x0) * zoom <= canvasCard.width - 40 &&
                         (y1 - y0) * zoom <= canvasCard.height - 30 - 84
            if (!fits) fitView()
        }
    }
    // Replace an output's or an input's existing wire rather than refusing; if
    // the engine rejects the new link, the old wiring is put back.
    function rewire(from, to) {
        if (from === to || from === 1 || to === 0) return false
        const edges = veyra.nodeConnections
        const oldOut = edges.find(e => e.from === from), oldIn = edges.find(e => e.to === to)
        if (oldOut && oldOut.to === to) return true
        if (oldOut) veyra.disconnectNode(from)
        if (oldIn && oldIn.from !== from) veyra.disconnectNode(oldIn.from)
        if (veyra.connectNodes(from, to)) { veyra.logUi("ui-node", "wire " + from + "->" + to); return true }
        if (oldIn && oldIn.from !== from) veyra.connectNodes(oldIn.from, to)
        if (oldOut) veyra.connectNodes(from, oldOut.to)
        return false
    }
    // Take a wired node out of the path and join its neighbours (design detach()).
    function detach(id, nudge) {
        const edges = veyra.nodeConnections
        const prev = edges.find(e => e.to === id), next = edges.find(e => e.from === id)
        if (!prev && !next) return
        if (prev) veyra.disconnectNode(prev.from)
        if (next) veyra.disconnectNode(id)
        if (prev && next) veyra.connectNodes(prev.from, next.to)
        const index = veyra.nodeIndexForId(id)
        const c = nodeRepeater.itemAt(index)
        if (nudge && c) veyra.setEffectPosition(index, c.x, c.y + 330)
        veyra.logUi("ui-node", "detach " + id)
        Qt.callLater(() => settle("n" + id))
    }
    // The link under a dragged card (pages-node.js nearestLink).
    function nearestLink(cx, cy, selfId) {
        let best = null, bd = 70 / zoom
        for (const e of veyra.nodeConnections) {
            if (e.from === selfId || e.to === selfId) continue
            const a = endpoint(e.from, true), b = endpoint(e.to, false)
            if (!a || !b || cx < a.x - 40 || cx > b.x + 40) continue
            const t = Math.max(0, Math.min(1, (cx - a.x) / Math.max(1, b.x - a.x)))
            const my = a.y + (b.y - a.y) * (3 * t * t - 2 * t * t * t)
            const d = Math.abs(cy - my)
            if (d < bd) { bd = d; best = e }
        }
        return best
    }
    // The input port under a world point (for drag-to-connect).
    function inputAt(w) {
        const o = outputBox
        if (Math.abs(w.x - o.x) < 18 && Math.abs(w.y - (o.y + 28)) < 18) return 1
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const c = nodeRepeater.itemAt(i)
            if (c && Math.abs(w.x - c.x) < 18 && Math.abs(w.y - (c.y + 16)) < 18) return c.node.id
        }
        return -1
    }

    function endpoint(id, output) {
        if (id === 0) return Qt.point(inputBox.x + inputBox.width, inputBox.y + 28)
        if (id === 1) return Qt.point(outputBox.x, outputBox.y + 28)
        const index = veyra.nodeIndexForId(id)
        const card = nodeRepeater.itemAt(index)
        return card ? Qt.point(card.x + (output ? card.width : 0), card.y + 16) : null
    }
    function selectId(id) {
        selectedId = id
        selectedNode = veyra.nodeIndexForId(id)
    }
    Connections {
        target: veyra
        function onChainChanged() {
            root.selectedNode = veyra.nodeIndexForId(root.selectedId)
            if (root.wireFrom > 1 && veyra.nodeIndexForId(root.wireFrom) < 0) root.wireFrom = -1
            wires.requestPaint()
        }
        function onNodeAnchorsChanged() { wires.requestPaint() }
    }
    // Card sizes are only known after their editors lay out (and change on
    // collapse / section switches): settle once the sizes stop changing.
    // A layout that overlaps once the cards have their real sizes after the page
    // opens (legacy grid positions, or cards that grew their full editors) is
    // laid out properly left to right once; later size changes only settle.
    property bool layoutChecked: false
    Timer {
        id: settleTimer
        interval: 120
        onTriggered: {
            // Hidden pages have no reliable sizes; the check runs when shown.
            if (!root.visible) return
            if (!root.layoutChecked) {
                root.layoutChecked = true
                if (root.overlapping(20)) { veyra.logUi("ui-node", "overlapping layout on show: auto arrange"); root.autoArrange(); return }
                // An empty graph (the node editor's starting point): bring input and
                // output into view without moving them, leaving the space between
                // them for the nodes the user adds.
                if (veyra.chain.length === 0) { root.fitView(); return }
            }
            root.settle("")
        }
    }
    onVisibleChanged: if (visible) { layoutChecked = false; settleTimer.restart() }
    Component.onCompleted: settleTimer.restart()

    onPanXChanged: { canvasGrid.requestPaint(); wires.requestPaint() }
    onPanYChanged: { canvasGrid.requestPaint(); wires.requestPaint() }
    onZoomChanged: { canvasGrid.requestPaint(); wires.requestPaint() }

    ColumnLayout {
        id: nodeLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 14
        spacing: 0
        states: State {
            name: "overlay"
            when: root.overlay
            AnchorChanges { target: nodeLayout; anchors.top: undefined }
            PropertyChanges { target: nodeLayout; height: Math.round(Math.max(260, root.height * 0.42)) }
        }

        // --- video card (fixed 330px) --------------------------------------
        Rectangle {
            id: nvTop
            visible: !root.overlay
            Layout.fillWidth: true
            Layout.preferredHeight: root.videoCardHeight
            Layout.minimumHeight: root.videoCardHeight
            Layout.maximumHeight: root.videoCardHeight
            radius: Theme.rCard
            color: Theme.videoBlack
            border.width: 1
            border.color: Theme.stroke
            clip: true

            // The native window sits over this area; the bar occupies its foot.
            Item {
                id: videoArea
                objectName: "videoArea"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: statsBar.top
                Text {
                    anchors.centerIn: parent
                    visible: !veyra.hasSource
                    text: veyra.statusText
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                }
            }

            // .nv-bar: transport, then the stats strip.
            Rectangle {
                id: statsBar
                readonly property bool tight: width < 800
                readonly property bool compact: width < nodeSourceBtn.implicitWidth + nodePresetBtn.implicitWidth
                    + nodeModeSwitch.implicitWidth + nodeOrbs.implicitWidth + 3 * 32 + 32 + 8 * 8 + 360
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.shortLayout ? 40 : (compact ? 72 : 40) + nodeStatus.implicitHeight + 8
                color: Theme.card

                GridLayout {
                    objectName: "node-transport-layout"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    columns: statsBar.compact ? 8 : 9
                    columnSpacing: 8
                    rowSpacing: 0

                    // pages-node.js .nv-bar: the source first, as on the list page.
                    VButton {
                        id: nodeSourceBtn
                        Layout.row: 0; Layout.column: 0
                        Layout.minimumWidth: 100
                        Layout.maximumWidth: implicitWidth
                        Layout.fillWidth: statsBar.compact
                        objectName: "node-source-button"
                        iconName: veyra.sourceKind === "moonlight" ? "cast" : veyra.sourceKind === "xbox" ? "gamepad" : veyra.sourceKind === "screen" ? "monitor"
                                : veyra.sourceKind === "image" ? "image" : veyra.sourceKind === "file" ? "film" : "video"
                        text: veyra.sourceTitle.length > 0 ? veyra.sourceTitle : qsTr("Source")
                        maxTextWidth: 180
                        trailingIcon: "down"
                        onClicked: nodeSourceMenu.openAt(this, "down")
                    }
                    VButton {
                        Layout.row: 0; Layout.column: 1
                        icon: true
                        iconName: (veyra.running && !veyra.paused) ? "pause" : "play"
                        onClicked: veyra.togglePlayPause()
                    }
                    VButton { Layout.row: 0; Layout.column: 2; icon: true; iconName: "back10"; onClicked: veyra.seekBy(-10) }
                    VButton { Layout.row: 0; Layout.column: 3; icon: true; iconName: "fwd10"; onClicked: veyra.seekBy(10) }

                    // .nv-stats: each entry separated by a hairline, mono numbers.
                    Flickable {
                        objectName: "node-stats-scroll"
                        visible: !root.shortLayout
                        Layout.row: statsBar.compact ? 1 : 0
                        Layout.column: statsBar.compact ? 0 : 4
                        Layout.columnSpan: statsBar.compact ? 8 : 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 30
                        clip: true
                        contentWidth: statItems.implicitWidth
                        contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                        HoverHandler { id: statsHover }
                        ToolTip.visible: statsHover.hovered && contentWidth > width
                        ToolTip.delay: 400
                        ToolTip.text: statRepeater.model.map(x => x.k + ": " + (x.v.length ? x.v : "—")).join("\n")
                        RowLayout {
                        id: statItems
                        height: parent.height
                        spacing: 0
                        Repeater {
                            id: statRepeater
                            model: [
                                { k: qsTr("Input"), v: veyra.sourceSummary, hi: false },
                                { k: qsTr("Output"), v: veyra.outputSummary.length > 0 ? veyra.outputSummary : qsTr("N/A"), hi: false },
                                { k: veyra.submitFpsLabel + " fps", v: veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) : qsTr("N/A"), hi: true },
                                { k: qsTr("1% low"), v: veyra.submitLowFpsKnown ? veyra.submitLowFps.toFixed(1) : "—", hi: false },
                                { k: qsTr("Waiting to present"), v: veyra.queuedFramesKnown ? veyra.queuedFrames.toFixed(0) + qsTr(" frames") : qsTr("N/A"), hi: false }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                required property int index
                                spacing: 5
                                Rectangle {
                                    implicitWidth: 1; implicitHeight: 12
                                    color: Theme.stroke
                                    visible: index > 0
                                }
                                Text {
                                    text: modelData.k
                                    color: Theme.t3
                                    font.family: Theme.fontUi
                                    font.pixelSize: 10
                                }
                                Text {
                                    text: modelData.v.length > 0 ? modelData.v : "—"
                                    color: modelData.hi && veyra.submitFpsKnown ? Theme.ok : Theme.t1
                                    font.family: Theme.fontMono
                                    font.pixelSize: modelData.hi ? 14 : 12
                                    font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                                }
                            }
                        }
                        }
                    }

                    // Total time / GPU / output-vs-target orbs (replace "measured enhancement P95").
                    ProPage.PerfOrbs {
                        id: nodeOrbs
                        objectName: "node-perf-orbs"
                        visible: !root.shortLayout && !statsBar.tight
                        Layout.row: 0
                        Layout.column: statsBar.compact ? 4 : 5
                        size: 32
                        showLabels: false
                        spacing: 6
                    }
                    VButton {
                        Layout.row: 0; Layout.column: statsBar.compact ? 5 : 6
                        objectName: "node-fullscreen"
                        icon: true; iconName: "max"
                        tip: qsTr("Fullscreen (double-click the picture / F11)")
                        onClicked: root.requestFullscreen()
                    }
                    // .viewtog: List asks first (the list configuration comes back).
                    VSeg {
                        id: nodeModeSwitch
                        Layout.row: 0; Layout.column: statsBar.compact ? 6 : 7
                        objectName: "node-mode-switch"
                        options: [{ id: "list", label: qsTr("List"), icon: "list" }, { id: "node", label: qsTr("Nodes"), icon: "nodes" }]
                        current: "node"
                        onPicked: id => { if (id === "list") returnDialog.open() }
                    }
                    VButton { objectName: "node-return-list"; visible: false; text: qsTr("Back to list"); onClicked: returnDialog.open() }
                    VButton {
                        id: nodePresetBtn
                        Layout.row: 0; Layout.column: statsBar.compact ? 7 : 8
                        Layout.minimumWidth: 120
                        Layout.maximumWidth: implicitWidth
                        Layout.fillWidth: statsBar.compact
                        objectName: "node-preset-button"
                        iconName: "nodes"
                        trailingIcon: "down"
                        maxTextWidth: statsBar.tight ? 90 : 150
                        text: qsTr("Node preset: ") + veyra.currentPresetName
                        tip: text
                        onClicked: nodePresetMenu.openAt(this, "down")
                    }
                    ProPage.StatusSummary {
                        id: nodeStatus
                        visible: !root.shortLayout
                        Layout.row: statsBar.compact ? 2 : 1
                        Layout.column: 0
                        Layout.columnSpan: statsBar.compact ? 8 : 9
                        Layout.fillWidth: true
                        Layout.bottomMargin: 4
                    }
                }
            }
        }

        // --- split handle (10px) -------------------------------------------
        Item {
            objectName: "node-splitter"
            visible: !root.overlay
            Layout.fillWidth: true
            Layout.preferredHeight: 10
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 3
                width: 40; height: 4; radius: 4
                color: splitHover.hovered ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
            }
            HoverHandler { id: splitHover; cursorShape: Qt.SizeVerCursor }
            // Drag to share the height between the picture and the canvas: the
            // picture keeps at least 180px, the canvas at least 200px.
            DragHandler {
                id: splitDrag
                target: null
                cursorShape: Qt.SizeVerCursor
                property real origin: 0
                onActiveChanged: if (active) origin = root.videoHeight
                onActiveTranslationChanged: if (active) {
                    const height = Math.min(root.maximumVideoCardHeight, origin + activeTranslation.y)
                    root.videoHeightEdited(Math.round(Math.max(root.minimumVideoCardHeight, height)))
                }
            }
        }

        // --- canvas --------------------------------------------------------
        Rectangle {
            id: canvasCard
            objectName: "node-canvas"
            // Over the fullscreen picture the canvas is a cover like any panel.
            readonly property bool videoCover: root.overlay
            property real coverRadius: radius
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.rCard
            color: Theme.shade(10, 10, 13)
            border.width: 1
            border.color: Theme.stroke
            clip: true

            Text {
                objectName: "node-quick-hint"
                z: 100
                visible: root.overlay
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                text: qsTr("%1 to close").arg("Home")
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
            }

            // .canvas dot grid: 22px spacing, drawn over the viewport only, since
            // an "infinite" canvas is a feel and infinite dots would cost frames.
            Canvas {
                id: canvasGrid
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const step = 22 * root.zoom
                    if (step < 5) return
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.08)
                    const ox = ((root.panX % step) + step) % step
                    const oy = ((root.panY % step) + step) % step
                    for (let x = ox; x < width; x += step)
                        for (let y = oy; y < height; y += step)
                            ctx.fillRect(x, y, 1.2, 1.2)
                }
            }

            // Hint and the right-click affordance.
            Text {
                objectName: "node-canvas-tip"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 10
                z: 4
                text: root.wireFrom >= 0 ? qsTr("Choose an input port to finish the link · Esc to cancel") :
                      (veyra.chainValid ? qsTr("Right-click empty space to add · drop onto a link to insert · drag far (more than a step up or down) or hold Alt when releasing to disconnect · drag/click ports to link · middle button pans · wheel zooms") : veyra.chainError)
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            Canvas {
                id: wires
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset(); ctx.clearRect(0, 0, width, height)
                    ctx.save(); ctx.translate(root.panX, root.panY); ctx.scale(root.zoom, root.zoom)
                    const curve = (a, b) => {
                        const bend = Math.max(60, Math.abs(b.x - a.x) / 2)
                        ctx.beginPath(); ctx.moveTo(a.x, a.y)
                        ctx.bezierCurveTo(a.x + bend, a.y, b.x - bend, b.y, b.x, b.y); ctx.stroke()
                    }
                    for (const edge of veyra.nodeConnections) {
                        const a = root.endpoint(edge.from, true), b = root.endpoint(edge.to, false)
                        if (!a || !b) continue
                        const hot = edge.from === root.hotFrom && edge.to === root.hotTo
                        ctx.strokeStyle = hot ? (Theme.dusk ? Theme.accent : "#FF8A3D") : "#8291AA"; ctx.lineWidth = (hot ? 4 : 2) / root.zoom
                        curve(a, b)
                    }
                    // Rubber band from the chosen output port to the pointer.
                    if (root.wireFrom >= 0) {
                        const a = root.endpoint(root.wireFrom, true)
                        if (a) {
                            ctx.setLineDash([6 / root.zoom, 4 / root.zoom])
                            ctx.strokeStyle = Theme.dusk ? Theme.accent : "#FF8A3D"; ctx.lineWidth = 2 / root.zoom
                            curve(a, root.pointerWorld)
                            ctx.setLineDash([])
                        }
                    }
                    ctx.restore()
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
            }

            Item {
                id: world
                x: root.panX
                y: root.panY
                transform: Scale { origin.x: 0; origin.y: 0; xScale: root.zoom; yScale: root.zoom }

                // Input, shared optical-flow and output boxes: fixed in the chain,
                // free on the canvas (user decision 2026-09-28).
                // Input: the source plus the shared optical-flow choice (moved in
                // from its own box, user request 2026-09-29). Port 0 starts the chain.
                AnchorBox {
                    id: inputBox
                    key: "input"; width: 250; height: inCol.implicitHeight + 20; handleHeight: 30
                    objectName: "node-input-anchor"
                    NodePort { nodeId: 0; output: true; x: parent.width - width / 2; y: 28 - height / 2 }
                    ColumnLayout {
                        id: inCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 10; spacing: 8
                        Text { text: qsTr("Input · optical flow"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium }
                        Text {
                            Layout.fillWidth: true
                            text: veyra.sourceSummary.length > 0 ? veyra.sourceSummary : qsTr("No source open")
                            color: Theme.t3; font.family: Theme.fontMono; font.pixelSize: 10; elide: Text.ElideRight
                        }
                        VSelect {
                            objectName: "node-flow-choice"
                            Layout.fillWidth: true
                            title: qsTr("Optical flow algorithm")
                            options: [Support.option(veyra,"0","NVIDIA NVOF","flow0"),Support.option(veyra,"1","AMD FidelityFX","flow1")]
                            value: options[veyra.opticalFlowChoice]?.label ?? (veyra.opticalFlowChoice === 2 ? qsTr("GPU DIS has been removed; choose again") : qsTr("Unknown configuration"))
                            onPicked: id => veyra.setOpticalFlowChoice(Number(id))
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            NodeLabel { Layout.fillWidth: true; text: qsTr("Edge Aegis (anti-jelly)") }
                            VSwitch { objectName: "node-edge-aegis"; checked: veyra.edgeAegis; onToggled: checked => veyra.edgeAegis = checked }
                        }
                        NodeLabel { text: qsTr("Motion estimation quality") }
                        VSeg {
                            objectName: "node-flow-quality"
                            Layout.fillWidth: true
                            options: [{ id: "0", label: qsTr("Performance") }, { id: "1", label: qsTr("Balance") }, { id: "2", label: qsTr("Quality") }]
                            current: String(veyra.flowQuality)
                            onPicked: id => veyra.flowQuality = Number(id)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: veyra.opticalFlowChoice === 1
                            NodeLabel { Layout.fillWidth: true; text: qsTr("AMD performance (half width and height)") }
                            VSwitch { objectName: "node-amd-half"; checked: veyra.amdFlowHalf; onToggled: checked => veyra.amdFlowHalf = checked }
                        }
                        DedupModeRow { backend: veyra; namePrefix: "node"; showHelp: false }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Shared by frame generation and NR, computed once after the input, not repeated per node.")
                            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Repeater {
                    id: nodeRepeater
                    // A count, not the array: delegates survive position updates so
                    // the settle spring can animate them.
                    model: veyra.chain.length
                    // New cards settle through settleTimer once their size is known.
                    onItemAdded: settleTimer.restart()
                    onItemRemoved: wires.requestPaint()
                    delegate: NodeCard {
                        required property int index
                        node: veyra.chain[index] || ({})
                    }
                }

                // Output: plus the sound settings. Audio is not a processing step, so
                // it lives on the output box rather than as a node in the chain.
                AnchorBox {
                    id: outputBox
                    key: "output"; width: 280; height: outCol.implicitHeight + 20; handleHeight: 30
                    objectName: "node-output-anchor"
                    NodePort { nodeId: 1; output: false; x: -width / 2; y: 28 - height / 2 }
                    ColumnLayout {
                        id: outCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 10; spacing: 6
                        Text { text: qsTr("Output · audio"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium }
                        Text {
                            Layout.fillWidth: true
                            text: veyra.outputSummary.length > 0 ? veyra.outputSummary : qsTr("No output")
                            color: Theme.t3; font.family: Theme.fontMono; font.pixelSize: 10; elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            VIcon { name: "vol"; size: 14; color: Theme.t2 }
                            VSlider {
                                id: nodeVolume
                                objectName: "node-out-volume"
                                Layout.fillWidth: true
                                from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                                valueFromModel: true; defaultValue: 1
                                onMoved: value => veyra.volume = value
                            }
                            VSliderValue { slider: nodeVolume; text: Math.round(veyra.volume * 100) + "%"; color: Theme.t2; pixelSize: 10 }
                            VSwitch { objectName: "node-out-mute"; checked: !veyra.muted; onToggled: checked => veyra.muted = !checked }
                        }
                        VSeg {
                            objectName: "node-out-sync"
                            Layout.fillWidth: true
                            options: [{ id: "0", label: qsTr("Auto estimate") }, { id: "1", label: qsTr("Manual") }, { id: "2", label: qsTr("Off", "off") }]
                            current: String(veyra.audioSyncMode)
                            onPicked: id => veyra.audioSyncMode = Number(id)
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: veyra.audioSyncMode === 1
                            Text { text: qsTr("Offset"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10 }
                            VSlider {
                                id: nodeOffset
                                Layout.fillWidth: true
                                center: true
                                from: -250; to: 250; value: veyra.audioOffsetMs
                                onMoved: value => veyra.audioOffsetMs = Math.round(value)
                            }
                            VSliderValue { slider: nodeOffset; text: veyra.audioOffsetMs + " ms"; color: Theme.t2; pixelSize: 10 }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: veyra.audioSyncMode === 0
                            text: veyra.audioSyncLive ? qsTr("Estimated compensation ") + veyra.audioCompensationMs.toFixed(0) + " ms" : qsTr("Auto-estimated; applies to live streaming input")
                            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10; wrapMode: Text.WordWrap
                        }
                        // Display sync / output cap: presentation belongs to the output.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            enabled: !veyra.presentationOwned
                            opacity: enabled ? 1 : 0.5
                            NodeLabel { text: veyra.presentationOwned ? qsTr("Display sync · Intel XeSS handles presentation") : qsTr("Display sync") }
                            VSeg {
                                objectName: "node-display-sync"
                                Layout.fillWidth: true
                                options: [{ id: "0", label: qsTr("Allow tearing") }, { id: "1", label: qsTr("VSync") }, { id: "2", label: qsTr("Auto") }]
                                current: String(veyra.displaySync)
                                onPicked: id => veyra.displaySync = Number(id)
                            }
                            // Variable refresh (G-SYNC / FreeSync): the switch greys when the monitor
                            // cannot do it. The node page has no Signal card, so the shared
                            // refresh display state (HDR, refresh rate, VRR; read on events only) sits here.
                            RowLayout {
                                Layout.fillWidth: true
                                NodeLabel { Layout.fillWidth: true; text: qsTr("Variable refresh"); wrapMode: Text.WordWrap }
                                Item {
                                    implicitWidth: Theme.ctlHeight; implicitHeight: Theme.ctlHeight
                                    VButton {
                                        objectName: "node-display-refresh"
                                        anchors.fill: parent
                                        visible: !veyra.displayChecking
                                        icon: true; ghost: true; iconName: "refresh"; tip: qsTr("Refresh display state")
                                        onClicked: veyra.recheckDisplay()
                                    }
                                    VSpinner { anchors.centerIn: parent; visible: veyra.displayChecking }
                                }
                                VSwitch {
                                    objectName: "node-vrr"
                                    enabled: veyra.vrrState >= 2
                                    opacity: enabled ? 1 : 0.5
                                    checked: veyra.variableRefresh
                                    onToggled: checked => veyra.variableRefresh = checked
                                }
                            }
                            NodeLabel { Layout.fillWidth: true; text: veyra.vrrHint; wrapMode: Text.WordWrap }
                            NodeLabel { text: qsTr("Output cap") }
                            VSeg {
                                objectName: "node-output-rate"
                                Layout.fillWidth: true
                                options: [{ id: "0", label: qsTr("Off", "off") }, { id: "1", label: qsTr("Follow the display") }, { id: "2", label: qsTr("Custom") }]
                                current: String(veyra.outputRateMode)
                                onPicked: id => veyra.outputRateMode = Number(id)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: veyra.outputRateMode === 2
                                NodeLabel { Layout.fillWidth: true; text: qsTr("Custom cap (FPS)") }
                                DialogHost.VTextField {
                                    objectName: "node-custom-fps"
                                    implicitWidth: 90
                                    text: veyra.outputCustomFps.toFixed(3)
                                    onEdited: text => { const v = Number(text); if (isFinite(v)) veyra.outputCustomFps = v }
                                }
                            }
                        }
                        NodeLabel { text: qsTr("Output device") }
                        VSelect {
                            objectName: "node-out-device"
                            Layout.fillWidth: true
                            title: qsTr("Output device")
                            readonly property string chosen: veyra.preferences.audioDevice || ""
                            value: { const d = veyra.audioDevices.find(x => x.id === chosen); return d ? d.label : qsTr("Follow the system default") }
                            options: veyra.audioDevices
                            onPicked: id => veyra.setPreference("audioDevice", id)
                        }

                    }
                }
            }

            // The pointer in world coordinates, for the rubber-band wire.
            HoverHandler {
                onPointChanged: {
                    if (root.wireFrom < 0) return
                    root.pointerWorld = world.mapFromItem(canvasCard, point.position.x, point.position.y)
                    wires.requestPaint()
                }
            }

            // Middle-button drag pans (user decision 2026-09-28): the left button
            // only selects. The wheel zooms (clamped so a layout cannot vanish).
            DragHandler {
                objectName: "node-canvas-pan"
                target: null
                acceptedButtons: Qt.MiddleButton
                property real originX: 0
                property real originY: 0
                onActiveChanged: if (active) { originX = root.panX; originY = root.panY }
                onActiveTranslationChanged: {
                    root.panX = originX + activeTranslation.x
                    root.panY = originY + activeTranslation.y
                }
            }
            WheelHandler {
                onWheel: event => {
                    root.zoom = Math.max(0.25, Math.min(2.0, root.zoom * (event.angleDelta.y > 0 ? 1.1 : 0.9)))
                }
            }
            // Left click on empty canvas clears the selection; right click on empty
            // canvas opens the add menu there and the new node lands at that point.
            Item { id: tapAnchor; width: 1; height: 1 }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: eventPoint => { if (!root.hitWorld(eventPoint.position)) root.clearSelection() }
            }
            TapHandler {
                objectName: "node-canvas-context"
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => {
                    const p = eventPoint.position
                    if (root.hitWorld(p)) return
                    tapAnchor.x = p.x
                    tapAnchor.y = p.y
                    root.addAt = Qt.point((p.x - root.panX) / root.zoom, (p.y - root.panY) / root.zoom)
                    addMenu.openAt(tapAnchor, "at")
                }
            }

            // Rubber band: a left drag that starts on empty canvas marks every
            // card it touches; Delete then removes them.
            DragHandler {
                id: marquee
                objectName: "node-canvas-marquee"
                target: null
                acceptedButtons: Qt.LeftButton
                // Never take a drag from a card, a port or a fixed box: those
                // handlers sit deeper and must win; the band only gets drags
                // nobody else wants (it was stealing the input box's drag).
                grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything
                property bool live: false
                property point from: Qt.point(0, 0)
                property point to: Qt.point(0, 0)
                onActiveChanged: {
                    if (active) {
                        live = root.wireFrom < 0 && !root.hitWorld(centroid.pressPosition)
                        from = centroid.pressPosition; to = from
                        return
                    }
                    if (!live) return
                    live = false
                    const a = world.mapFromItem(canvasCard, Math.min(from.x, to.x), Math.min(from.y, to.y))
                    const b = world.mapFromItem(canvasCard, Math.max(from.x, to.x), Math.max(from.y, to.y))
                    const picked = []
                    for (let i = 0; i < nodeRepeater.count; ++i) {
                        const c = nodeRepeater.itemAt(i)
                        if (!c || !c.node || c.node.id === undefined) continue
                        if (c.x < b.x && c.x + c.width > a.x && c.y < b.y && c.y + c.height > a.y) picked.push(c.node.id)
                    }
                    root.selectedId = -1; root.selectedNode = -1
                    root.marked = picked
                    veyra.logUi("ui-node", "marquee marked=" + picked.length)
                }
                onCentroidChanged: if (active && live) to = centroid.position
            }
            Rectangle {
                objectName: "node-canvas-band"
                visible: marquee.active && marquee.live
                x: Math.min(marquee.from.x, marquee.to.x); y: Math.min(marquee.from.y, marquee.to.y)
                width: Math.abs(marquee.to.x - marquee.from.x); height: Math.abs(marquee.to.y - marquee.from.y)
                z: 5
                color: Qt.rgba(1, 138 / 255, 61 / 255, 0.10)
                border.width: 1
                border.color: Theme.accent
            }

            // .canvas-tools: Fit view · Auto arrange | Add node | chain summary.
            Rectangle {
                id: canvasTools
                objectName: "node-canvas-tools"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.shortLayout && !root.overlay ? 8 : 36
                z: 4
                width: toolsRow.implicitWidth + 8
                height: toolsRow.implicitHeight + 8
                radius: 12
                color: Theme.shade(20, 20, 24, 0.94)
                border.width: 1
                border.color: Theme.stroke2
                RowLayout {
                    id: toolsRow
                    anchors.centerIn: parent
                    spacing: 2
                    VButton { objectName: "node-fit"; icon: true; ghost: true; iconName: "fit"; onClicked: root.fitView() }
                    VButton { objectName: "node-auto"; icon: true; ghost: true; iconName: "magnet"; onClicked: root.autoArrange() }
                    Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.stroke2 }
                    VButton {
                        id: toolsAdd
                        objectName: "node-add"
                        ghost: true; iconName: "plus"; text: qsTr("Add node")
                        onClicked: {
                            root.addAt = Qt.point((canvasCard.width / 2 - root.panX) / root.zoom - root.cardWidth / 2,
                                                  (canvasCard.height / 3 - root.panY) / root.zoom)
                            addMenu.openAt(toolsAdd, "up")
                        }
                    }
                    VButton {
                        id: nodePresetButton
                        objectName: "node-presets"
                        ghost: true; text: qsTr("Presets")
                        onClicked: nodePresetMenu.openAt(nodePresetButton, "up")
                    }
                    Rectangle { implicitWidth: 1; implicitHeight: 16; color: Theme.stroke2 }
                    Text {
                        Layout.leftMargin: 8; Layout.rightMargin: 8
                        text: veyra.chain.length + qsTr(" nodes · ") + (veyra.chainValid ? qsTr("Chain is valid") : qsTr("Draft not run"))
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                    }
                }
            }

            // The chain's cost, node by node (user request 2026-09-29): only the
            // enabled nodes on the running path, in processing order, each as
            // wide as its own GPU time; the rest of the source-frame budget is the
            // striped tail. A node still waiting for samples keeps a small
            // labelled slot. Click a segment to find its node.
            Rectangle {
                id: timingStrip
                objectName: "node-timing-strip"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                height: 18
                radius: 5
                color: Theme.shade(10, 10, 13, 0.85)
                visible: segments.length > 0 && (!root.shortLayout || root.overlay)
                readonly property var hues: ({ sr: "#6EA8FF", nr: "#FF8A3D", color: "#A8A8B8", "video-hdr": "#E58BD9", aa: "#7FD4FF",
                                               "frame-generation": "#3DDC84", protection: "#E7B868" })
                readonly property var segments: {
                    const timings = veyra.nodeTimings, chain = veyra.chain
                    const byId = {}
                    for (const n of chain) byId[n.id] = n
                    const out = []
                    for (const id of root.pathIds()) {
                        const n = byId[id]
                        if (!n || !n.enabled) continue
                        const t = timings[String(id)] || {}
                        out.push({ id: id, label: n.label, color: hues[n.type] || Theme.accent,
                                   measured: t.state === "measured", ms: t.state === "measured" ? t.ms : 0,
                                   state: t.state || "", reason: t.reason || "" })
                    }
                    return out
                }
                readonly property real measuredTotal: segments.reduce((a, g) => a + g.ms, 0)
                readonly property int waiting: segments.filter(g => !g.measured).length
                readonly property real scale: Math.max(measuredTotal, veyra.stageBudgetMs)
                readonly property real spare: veyra.stageBudgetMs > measuredTotal && measuredTotal > 0 ? veyra.stageBudgetMs - measuredTotal : 0
                // One label for both the strip and the width it needs: the English state
                // words ("Waiting for samples") are about twice the Chinese ones, so a
                // fixed 72px slot cut every waiting segment to "NR enhancemen…".
                FontMetrics { id: stripLabelMetrics; font.family: Theme.fontUi; font.pixelSize: 10; font.weight: Font.DemiBold }
                // A segment is as wide as its own GPU time, so its label can never be
                // guaranteed to fit. A waiting segment therefore shows the node name only
                // ("NR enhancement · layer 1" fits a sixth of the strip; the English
                // "… Waiting for samples" suffix did not, and every slot was cut to
                // "NR enhancemen…"). The state word is the tooltip's second line.
                function segmentText(g) {
                    return g.measured ? g.label + " " + g.ms.toFixed(1) + " ms" : g.label
                }
                function stateText(g) {
                    return g.state === "failed" ? qsTr("Failed to enable")
                        : g.state === "sdk" ? qsTr("SDK internal")
                        : g.state === "fused" ? qsTr("Merge into input")
                        : g.state === "pending" ? qsTr("Waiting for samples")
                        : g.state === "stopped" ? qsTr("Not playing")
                        : g.reason || qsTr("No timing for this node yet")
                }
                readonly property real waitingWidth: {
                    let w = 72
                    for (const g of segments)
                        if (!g.measured) w = Math.max(w, Math.ceil(stripLabelMetrics.advanceWidth(segmentText(g))) + 10)
                    return w
                }
                function widthOf(g) {
                    const gaps = Math.max(0, segments.length - 1)
                    const room = width - 2 - gaps
                    // A timed bar keeps 40px; waiting slots share the rest up to the width
                    // their own name needs, and give it back when the strip is that narrow.
                    const timedRoom = (segments.length - waiting) * 40
                    const waitingSlot = Math.max(40, Math.min(waitingWidth,
                        (room - timedRoom) / Math.max(1, waiting)))
                    if (!g.measured) return waitingSlot
                    const free = Math.max(0, room - waiting * waitingSlot)
                    return scale > 0 ? Math.max(40, free * g.ms / scale) : 40
                }
                Row {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 1
                    Repeater {
                        model: timingStrip.segments
                        delegate: Rectangle {
                            id: timingSegment
                            required property var modelData
                            height: parent.height
                            width: Math.round(telemetryWidth.value)
                            radius: 4
                            color: modelData.measured ? modelData.color : Qt.rgba(1, 1, 1, 0.12)
                            VTelemetryValue {
                                id: telemetryWidth
                                targetValue: timingStrip.widthOf(timingSegment.modelData)
                                animate: timingSegment.visible && !Theme.reduced
                                duration: 600
                            }
                            Text {
                                anchors.centerIn: parent
                                width: parent.width - 6
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: timingStrip.segmentText(modelData)
                                color: modelData.measured ? Qt.rgba(0, 0, 0, 0.8) : Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                            }
                            HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
                            ToolTip.visible: segHover.hovered
                            ToolTip.text: modelData.label + "：" + (modelData.measured ? modelData.ms.toFixed(2) + qsTr(" ms (GPU, last-second average)")
                                : timingStrip.stateText(modelData) + (modelData.reason.length > 0 && modelData.reason !== timingStrip.stateText(modelData) ? " · " + modelData.reason : ""))
                            TapHandler {
                                gesturePolicy: TapHandler.WithinBounds
                                onTapped: root.focusNode(modelData.id)
                            }
                        }
                    }
                    // Source-frame budget spare: striped, as in the design.
                    Rectangle {
                        visible: timingStrip.spare > 0
                        height: parent.height
                        width: Math.max(0, parent.width - x)
                        radius: 4
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.12)
                        clip: true
                        Text {
                            anchors.centerIn: parent
                            visible: parent.width > 90
                            text: qsTr("Headroom ") + timingStrip.spare.toFixed(1) + " ms"
                            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }

    // A small caption above a control inside the fixed boxes.
    component NodeLabel: Text {
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 10
    }
    // A draggable fixed box (input / optical flow / output). The top strip (or the whole box
    // when it has no controls) is the drag handle.
    component AnchorBox: Rectangle {
        id: box
        required property string key
        property real handleHeight: height
        property bool animate: true
        readonly property point saved: root.anchorPos(key)
        x: saved.x
        y: saved.y
        Behavior on x { enabled: box.animate; NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
        Behavior on y { enabled: box.animate; NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
        onXChanged: wires.requestPaint()
        onYChanged: wires.requestPaint()
        radius: 10
        color: Theme.shade(29, 29, 34)
        border.width: 1
        border.color: Theme.stroke2
        Item {
            width: parent.width; height: box.handleHeight
            z: -1
            HoverHandler { cursorShape: Qt.SizeAllCursor }
            DragHandler {
                target: null
                property real ox: 0
                property real oy: 0
                // Scene coordinates: the handle moves with the box, so its own
                // local translation would feed back into itself.
                property point start: Qt.point(0, 0)
                onActiveChanged: {
                    if (active) { box.animate = false; ox = box.x; oy = box.y; start = centroid.scenePressPosition; return }
                    veyra.setNodeAnchor(box.key, box.x, box.y)
                    box.x = Qt.binding(() => box.saved.x); box.y = Qt.binding(() => box.saved.y)
                    box.animate = true
                    root.settle(box.key)
                }
                onCentroidChanged: if (active) {
                    box.x = ox + (centroid.scenePosition.x - start.x) / root.zoom
                    box.y = oy + (centroid.scenePosition.y - start.y) / root.zoom
                }
            }
        }
    }

    // A node card: header (drag handle, collapse, cost, switch, detach) and the
    // node's FULL parameters on the card, the same editors and bridge calls as
    // the list page (user decision 2026-09-28).
    component NodeCard: Rectangle {
        id: card
        property var node: ({})
        property bool collapsed: false
        property bool animate: true
        property bool willDetach: false
        objectName: "node-card-" + node.index
        readonly property var nr: veyra.nrLayers.find(layer => layer.index === node.index) || null
        readonly property bool locked: node.mustBeLast === true
        readonly property bool linked: veyra.nodeConnections.some(e => e.to === node.id)

        x: node.x !== undefined ? node.x : 0
        y: node.y !== undefined ? node.y : 0
        Behavior on x { enabled: card.animate; NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
        Behavior on y { enabled: card.animate; NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
        onXChanged: wires.requestPaint()
        onYChanged: wires.requestPaint()
        onHeightChanged: { wires.requestPaint(); if (!headDrag.active) settleTimer.restart() }
        onWidthChanged: settleTimer.restart()
        z: root.selectedId === node.id ? 3 : (headDrag.active ? 4 : 1)
        width: Math.max(baseWidth, headerWidth)
        // 272 / 320 / 360 were measured for Chinese node names. English ones run about
        // twice as wide ("DLSS Frame Generation"), and the fixed widths cut them to
        // "DLSS Frame…". The header's own implicit width is the honest minimum.
        readonly property real baseWidth: node.type === "color" ? 360 : ["nr", "protection", "exclusion"].indexOf(node.type)>=0 ? 320 : 272
        // Capped so a very long custom name cannot make one card wider than the canvas.
        readonly property real headerWidth: Math.min(460, Math.ceil(headRow.implicitWidth) + 12)
        height: body.implicitHeight
        radius: 10
        color: Theme.shade(29, 29, 34)
        opacity: (node.enabled ? 1 : 0.74) * lifeOpacity
        property real lifeScale: 1
        property real lifeOpacity: 1
        scale: lifeScale
        Component.onCompleted: if (node.id === root.bornId && Theme.d(600) > 0) { lifeScale = 0.6; lifeOpacity = 0; bornAnim.start() }
        ParallelAnimation {
            id: bornAnim
            NumberAnimation { target: card; property: "lifeScale"; to: 1; duration: Theme.d(600); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: card; property: "lifeOpacity"; to: 1; duration: Theme.d(300) }
        }
        property var dieDone: null
        function die(done) {
            dieDone = done
            if (Theme.d(220) <= 0) { const f = dieDone; dieDone = null; if (f) f(); return }
            dieAnim.restart()
        }
        ParallelAnimation {
            id: dieAnim
            NumberAnimation { target: card; property: "lifeScale"; to: 0.7; duration: Theme.d(220); easing.bezierCurve: Theme.easeOut }
            NumberAnimation { target: card; property: "lifeOpacity"; to: 0; duration: Theme.d(220); easing.bezierCurve: Theme.easeOut }
            onFinished: { const f = card.dieDone; card.dieDone = null; if (f) f() }
        }
        border.width: willDetach || root.marked.indexOf(node.id) >= 0 ? 2 : 1
        border.color: willDetach ? Theme.err : root.selectedId === node.id ? "#FFFFFF"
                    : root.marked.indexOf(node.id) >= 0 ? Theme.accent : Qt.rgba(1, 1, 1, 0.1)
        Behavior on border.color { ColorAnimation { duration: Theme.d(200) } }
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: root.selectId(card.node.id)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: eventPoint => {
                root.selectId(card.node.id)
                nodeMenu.targetId = card.node.id
                // The menu opens where the pointer is, not under the card.
                const p = card.mapToItem(canvasCard, eventPoint.position.x, eventPoint.position.y)
                tapAnchor.x = p.x; tapAnchor.y = p.y
                nodeMenu.openAt(tapAnchor, "at")
            }
        }

        NodePort { nodeId: card.node.id !== undefined ? card.node.id : -1; output: false; x: -width / 2; y: 16 - height / 2 }
        NodePort { nodeId: card.node.id !== undefined ? card.node.id : -1; output: true; x: card.width - width / 2; y: 16 - height / 2 }

        // Per-card adapter so ColourPanel edits THIS colour node: reads its own
        // state, writes through the selected-layer setters after selecting it.
        QtObject {
            id: colourApi
            function select() { veyra.selectedColourLayer = card.node.index }
            readonly property var colourState: (veyra.chain, veyra.colourStateAt(card.node.index))
            readonly property var colourParameters: veyra.colourParameters
            property bool colorEnabled: false
            onColorEnabledChanged: if (colorEnabled !== !!colourState.enabled) { select(); veyra.colorEnabled = colorEnabled }
            function setColourParameter(k, v) { select(); return veyra.setColourParameter(k, v) }
            function setColourCurve(c, points) { select(); return veyra.setColourCurve(c, points) }
            function setColourOption(n, v) { select(); return veyra.setColourOption(n, v) }
            function setColourWheel(z, h, s, l) { select(); return veyra.setColourWheel(z, h, s, l) }
            function resetColourGroup(g) { select(); return veyra.resetColourGroup(g) }
            readonly property var lutLibrary: veyra.lutLibrary
            readonly property var colourLooks: veyra.colourLooks
            function importLutDialog() { select(); veyra.importLutDialog() }
            function setColourLut(n) { select(); return veyra.setColourLut(n) }
            function saveColourLook(n, r) { select(); return veyra.saveColourLook(n, r) }
            function applyColourLook(i) { select(); return veyra.applyColourLook(i) }
            function deleteColourLook(i) { return veyra.deleteColourLook(i) }
            function importColourLookDialog() { veyra.importColourLookDialog() }
            function exportColourLookDialog(i) { veyra.exportColourLookDialog(i) }
            function holdOriginal(held) { veyra.holdOriginal(held) }
            readonly property bool colourCanUndo: veyra.colourCanUndo
            readonly property bool colourCanRedo: veyra.colourCanRedo
            readonly property bool colourCanPaste: veyra.colourCanPaste
            function colourUndo() { return veyra.colourUndo() }
            function colourRedo() { return veyra.colourRedo() }
            function colourCopy() { select(); veyra.colourCopy() }
            function colourPaste() { select(); return veyra.colourPaste() }
        }
        Binding { target: colourApi; property: "colorEnabled"; value: !!colourApi.colourState.enabled }

        ColumnLayout {
            id: body
            width: card.width
            spacing: 0

            // Header: the drag handle.
            Rectangle {
                id: head
                objectName: "node-head-" + card.node.id
                Layout.fillWidth: true
                implicitHeight: 34
                radius: 9
                // .bnode .bh: linear-gradient(90deg, hue 55 % into the card
                // colour, hue 18 %), per node type.
                readonly property color hue: root.hueOf(card.node.type)
                function mix(a) { return Qt.rgba(hue.r * a + 0.114 * (1 - a), hue.g * a + 0.114 * (1 - a), hue.b * a + 0.133 * (1 - a), 1) }
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: head.mix(0.55) }
                    GradientStop { position: 1; color: head.mix(0.18) }
                }
                // The grip stops 14px short of both sides: the ports straddle the
                // card's edges at header height, and a press there belongs to the
                // port (drag a wire), not to moving the card.
                Item {
                    id: headGrip
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                HoverHandler { cursorShape: Qt.SizeAllCursor }
                DragHandler {
                    id: headDrag
                    target: null
                    property real ox: 0
                    property real oy: 0
                    property point start: Qt.point(0, 0)
                    onActiveChanged: {
                        if (active) { card.animate = false; ox = card.x; oy = card.y; start = centroid.scenePressPosition; root.selectId(card.node.id); return }
                        const id = card.node.id, index = card.node.index
                        const alt = (centroid.modifiers & Qt.AltModifier) !== 0
                        const hot = root.hotFrom >= 0 ? { from: root.hotFrom, to: root.hotTo } : null
                        root.hotFrom = root.hotTo = -1
                        card.willDetach = false
                        veyra.logUi("ui-node", "drop " + id + " at " + Math.round(card.x) + "," + Math.round(card.y) + " linked=" + card.linked + " hot=" + (hot ? hot.from + ">" + hot.to : "none"))
                        veyra.setEffectPosition(index, card.x, card.y)
                        card.x = Qt.binding(() => card.node.x); card.y = Qt.binding(() => card.node.y)
                        card.animate = true
                        if (hot) {
                            if (veyra.insertNodeAfter(id, hot.from)) veyra.logUi("ui-node", "insert " + id + " after " + hot.from)
                        } else if (card.linked && (alt || Math.abs(card.y - oy) > 220)) {
                            root.detach(id, false); return
                        }
                        Qt.callLater(() => root.settle("n" + id))
                    }
                    onCentroidChanged: {
                        if (!active) return
                        card.x = ox + (centroid.scenePosition.x - start.x) / root.zoom
                        card.y = oy + (centroid.scenePosition.y - start.y) / root.zoom
                        const alt = (centroid.modifiers & Qt.AltModifier) !== 0
                        const e = (!card.linked || alt) ? root.nearestLink(card.x + card.width / 2, card.y + 30, card.node.id) : null
                        root.hotFrom = e ? e.from : -1; root.hotTo = e ? e.to : -1
                        card.willDetach = card.linked && (alt || Math.abs(card.y - oy) > 220)
                        wires.requestPaint()
                    }
                }
                }
                RowLayout {
                    id: headRow
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 5
                    Item {
                        implicitWidth: 18; implicitHeight: 18
                        VIcon { anchors.centerIn: parent; name: "down"; size: 12; color: Theme.t2; rotation: card.collapsed ? -90 : 0
                            Behavior on rotation { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } } }
                        TapHandler { onTapped: { card.collapsed = !card.collapsed; Qt.callLater(() => root.settle("n" + card.node.id)) } }
                    }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: card.node.enabled ? "#FFFFFF" : Qt.rgba(1, 1, 1, 0.35)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.node.label || ""
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        elide: Text.ElideRight
                    }
                    // .ms badge: this node's own GPU cost, or why there is none.
                    Rectangle {
                        implicitWidth: msText.width + 12
                        implicitHeight: 18
                        radius: 5
                        color: Qt.rgba(0, 0, 0, 0.35)
                        HoverHandler { id: timingHover }
                        ToolTip.visible: timingHover.hovered
                        ToolTip.text: msText.timing.reason || msText.text
                        ToolTip.delay: 400
                        Text {
                            id: msText
                            objectName: "node-ms-" + card.node.id
                            // A long status ("SDR display, not converted") must not push the
                            // node name out of the header: it is capped and elided, and the
                            // tooltip carries the whole phrase. 118 fits every status word in
                            // the interface languages except that one phrase.
                            width: Math.min(implicitWidth, 118)
                            elide: Text.ElideRight
                            readonly property var timing: veyra.nodeTimings[String(card.node.id)] || ({})
                            anchors.centerIn: parent
                            text: {
                                switch (timing.state) {
                                case "measured": return timing.ms.toFixed(2) + " ms"
                                case "fused": return qsTr("Merge into input")
                                case "sdk": return qsTr("SDK internal")
                                case "disabled": return qsTr("Disabled")
                                case "idle": return qsTr("Not connected")
                                case "draft": return qsTr("Draft not run")
                                case "pending": return qsTr("Waiting for samples")
                                case "failed": return qsTr("Failed to enable")
                                case "stopped": return qsTr("Not playing")
                                case "hdr-sdr": return qsTr("SDR display, not converted")
                                case "fg-skipped": return qsTr("Over budget")
                                default: return qsTr("Not timed")
                                }
                            }
                            color: timing.state === "measured" ? Theme.t1 : Theme.t3
                            font.family: Theme.fontMono
                            font.pixelSize: 10
                            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        }
                    }
                    VIcon { visible: card.locked; name: "key"; size: 10; color: Theme.t3 }
                    VSwitch {
                        objectName: "node-enable-" + card.node.id
                        checked: card.node.enabled === true
                        enabled: card.node.enabled === true || Support.available(veyra, card.node.type)
                        opacity: enabled ? 1 : 0.4
                        onToggled: checked => veyra.setEffectEnabled(card.node.index, checked)
                    }
                    VButton {
                        objectName: "node-unlink-" + card.node.id
                        visible: card.linked
                        icon: true; ghost: true; iconName: "unlink"
                        onClicked: root.detach(card.node.id, true)
                    }
                }
            }

            Loader {
                id: editorLoader
                Layout.fillWidth: true
                Layout.margins: 10
                visible: !card.collapsed
                active: card.node.type !== undefined
                sourceComponent: card.node.type === "nr" ? nrEditor
                               : card.node.type === "color" ? colourEditor
                               : card.node.type === "sr" ? srEditor
                               : card.node.type === "aa" ? aaEditor
                               : card.node.type === "beauty" ? beautyEditor
                               : card.node.type === "video-hdr" ? hdrEditor
                               : card.node.type === "protection" ? protectionEditor
                               : card.node.type === "exclusion" ? exclusionEditor
                               : card.node.type === "nr-stabilizer" ? stabilizerEditor
                               : card.node.type === "frame-generation" ? fgEditor : null
            }
            Item { implicitHeight: card.collapsed ? 0 : 4 }
        }

        Component {
            id: nrEditor
            NrLayerEditor {
                objectName: "node-nr-editor-" + card.node.id
                listControls: false
                layerData: card.nr || ({index: card.node.index, sizePolicy: 0, intensity: 0, tone: 0, structure: 0, skin: -1, style: 0, autoMask: false, uiCorrection: false, total: 0, darken: 0, brighten: 0, color: 0, luminance: 0, temporal: false})
                layerCount: veyra.nrLayers.length
                onEdited: (i, k, v) => veyra.setNrLayerParameter(i, k, v)
            }
        }
        Component {
            id: protectionEditor
            MaskEditor { namePrefix: "node-protection" }
        }
        Component {
            id: exclusionEditor
            MaskEditor { allEffects: true; namePrefix: "node-exclusion" }
        }
        Component {
            id: stabilizerEditor
            ColumnLayout {
                Layout.fillWidth: true
                NodeLabel {
                    Layout.fillWidth: true
                    text: qsTr("Stabilize unchanged pixels after the final NR layer.")
                    wrapMode: Text.WordWrap
                }
                VRow {
                    label: qsTr("Stabilization strength")
                    value: Math.round(veyra.nrStabilizerState.strength*100)+"%"
                    VSlider {
                        objectName: "node-hold-strength"
                        implicitWidth: 110
                        valueFromModel: true
                        from: 0.1; to: 1; inputScale: 100
                        value: veyra.nrStabilizerState.strength
                        onMoved: value=>veyra.setStabilizerParameter(card.node.index,"strength",value)
                    }
                }
                VRow {
                    label: qsTr("Change tolerance")
                    value: (veyra.nrStabilizerState.tolerance*100).toFixed(1)+"%"
                    VSlider {
                        objectName: "node-hold-tolerance"
                        implicitWidth: 110
                        valueFromModel: true
                        from: 0; to: 1; inputScale: 100
                        value: veyra.nrStabilizerState.tolerance
                        onMoved: value=>veyra.setStabilizerParameter(card.node.index,"tolerance",value)
                    }
                }
            }
        }
        Component {
            id: colourEditor
            ColourPanel { objectName: "node-colour-editor-" + card.node.id; api: colourApi; compact: true }
        }
        Component {
            id: srEditor
            ColumnLayout {
                objectName: "node-sr-editor-" + card.node.id
                spacing: 6
                VRow {
                    label: qsTr("Upscaling algorithm")
                    VSelect {
                        objectName: "node-sr-backend"
                        implicitWidth: 210 // the RTX Video SR family names need more than the default 150
                        options: veyra.srBackendChoices || []
                        value: options.find(o => o.id === veyra.srAlgorithmOf(veyra.videoSrQuality))?.label || ""
                        onPicked: id => veyra.videoSrQuality = veyra.srQualityFor(id, veyra.srTierOf(veyra.videoSrQuality))
                    }
                }
                VRow {
                    label: qsTr("DLSS model")
                    hint: qsTr("K retains the original model; M and L use DLSS 4.5. L targets lower input resolutions. Quality and processing time depend on the source.")
                    // Also the model of a DLSS SR second pass under double SR.
                    visible: veyra.videoSrQuality === 0 || (veyra.dualSrEnabled && veyra.dualSrQuality === 0)
                    enabled: Support.available(veyra, "sr0")
                    VSelect {
                        objectName: "node-dlss-sr-model"
                        options: veyra.dlssSrModelChoices || []
                        value: options.find(o => o.id === String(veyra.dlssSrModel))?.label || ""
                        onPicked: id => veyra.dlssSrModel = Number(id)
                    }
                }
                SrStageEditor { Layout.fillWidth: true; namePrefix: "node" }


            }
        }
        Component {
            id: aaEditor
            AntiAliasEditor {
                objectName: "node-aa-editor-" + card.node.id
                namePrefix: "node-aa-" + card.node.id
                params: (veyra.chain, veyra.antiAliasParams, veyra.antiAliasParamsAt(card.node.index))
                onEdited: (key, amount) => veyra.setAntiAliasParameterAt(card.node.index, key, amount)
            }
        }
        Component {
            id: beautyEditor
            BeautyEditor {
                objectName: "node-beauty-editor-" + card.node.id
                namePrefix: "node-beauty-" + card.node.id
                params: (veyra.chain, veyra.beautyLayers, veyra.beautyParamsAt(card.node.index))
                detection: veyra.beautyDetection
                status: veyra.beautyStatus
                onEdited: (key, amount) => veyra.setBeautyParameterAt(card.node.index, key, amount)
                onPresetPicked: preset => veyra.applyBeautyPreset(card.node.index, preset)
                onDetectionEdited: (key, amount) => veyra.setBeautyDetection(key, amount)
            }
        }
        Component {
            id: hdrEditor
            ColumnLayout {
                objectName: "node-hdr-editor-" + card.node.id
                spacing: 4
                enabled: Support.available(veyra, "video-hdr")
                opacity: enabled ? 1 : 0.4
                Text {
                    Layout.fillWidth: true
                    text: veyra.videoHdrStatus.length > 0 ? veyra.videoHdrStatus : qsTr("SDR → HDR, before frame generation")
                    color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                Repeater {
                    model: [{key:"contrast",label:qsTr("Contrast"),from:0,to:200,def:125},
                            {key:"saturation",label:qsTr("Saturation"),from:0,to:200,def:75},
                            {key:"middleGray",label:qsTr("Mid gray"),from:10,to:100,def:44},
                            {key:"peakNits",label:qsTr("Peak brightness (nit)"),from:400,to:2000,def:1000}]
                    delegate: VRow {
                        required property var modelData
                        label: modelData.label
                        value: String(veyra.videoHdrParams[modelData.key] ?? "—")
                        VSlider {
                            objectName: "node-hdr-" + modelData.key
                            implicitWidth: 110
                            valueFromModel: true
                            resettable: true; defaultValue: modelData.def
                            from: modelData.from; to: modelData.to
                            value: veyra.videoHdrParams[modelData.key] ?? modelData.def
                            onMoved: value => veyra.setVideoHdrParameter(modelData.key, Math.round(value))
                        }
                    }
                }
            }
        }
        Component {
            id: fgEditor
            ColumnLayout {
                objectName: "node-fg-editor-" + card.node.id
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Fixed as the last step of the chain; can be moved on the canvas")
                    color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                VRow {
                    label: qsTr("Frame generation method")
                    hint: veyra.fgProviderText
                    VSelect {
                        objectName: "node-fg-backend"
                        options: veyra.fgBackendChoices
                        value: (options.find(o => o.id === veyra.fgBackendName) || options[0] || {}).label || ""
                        displayValue: Support.fgBackendShortLabel(selectedOption) || value
                        onPicked: id => veyra.fgBackendName = id
                    }
                }
                VRow {
                    label: qsTr("Multiplier")
                    // Steps match the author's original (numbers only); automatic selection is the separate switch below.
                    hint: veyra.fgDisplayHint.length > 0 ? veyra.fgDisplayHint : veyra.fgMaxMultiplier + qsTr("X is the cap")
                    hintColor: veyra.fgDisplayHint.length > 0 ? Theme.warn : Theme.t3
                    VSeg {
                        objectName: "node-fg-multiplier"
                        visible: veyra.fgBackendName !== "vfg"
                        options: veyra.fgMultiplierChoices
                        current: String(veyra.fgMultiplier)
                        onPicked: id => veyra.fgMultiplier = parseInt(id)
                    }
                    VSelect {
                        objectName: "node-vfg-multiplier"
                        visible: veyra.fgBackendName === "vfg"
                        options: veyra.fgMultiplierChoices
                        value: String(veyra.fgMultiplier) + "X"
                        onPicked: id => veyra.fgMultiplier = Number(id)
                    }
                }
                VRow {
                    label: qsTr("DLSS performance mode")
                    visible: veyra.fgBackendName === "dlss"
                    enabled: veyra.dlssgLiteAvailable
                    opacity: enabled ? 1 : 0.5
                    hint: veyra.dlssgLiteHint
                    VSwitch { objectName: "node-dlssg-lite"; checked: veyra.dlssgLite; onToggled: checked => veyra.dlssgLite = checked }
                }
                VRow {
                    label: qsTr("VFG quality")
                    visible: veyra.fgBackendName === "vfg"
                    hint: qsTr("Higher quality takes longer. Mixed tiers: the middle frame of each source pair uses the first model, the others the second.")
                    VSelect {
                        objectName: "node-vfg-quality"
                        options: [{id:"0",label:qsTr("Low")},{id:"1",label:qsTr("Medium")},{id:"2",label:qsTr("High")},{id:"3",label:qsTr("High+Medium")},{id:"4",label:qsTr("High+Low")},{id:"5",label:qsTr("Medium+Low")}]
                        value: options[veyra.vfgQuality].label
                        onPicked: id => veyra.vfgQuality = Number(id)
                    }
                }
                VRow {
                    label: qsTr("VFG auto regulation")
                    visible: veyra.fgBackendName === "vfg"
                    enabled: veyra.vfgQuality !== 0 && veyra.fgMultiplier === 2
                    opacity: enabled ? 1 : 0.5
                    hint: qsTr("During a live stream at 2X, when the current level cannot make the screen refresh, that frame pair uses the next faster level: no dropped frame and no added latency. Uses about 2 GB more VRAM; does nothing when VRAM is short")
                    VSwitch { objectName: "node-vfg-auto-regulate"; checked: veyra.vfgAutoRegulate; onToggled: checked => veyra.vfgAutoRegulate = checked }
                }
                VRow {
                    label: qsTr("Adaptive multiplier")
                    visible: veyra.fgMultiplier > 2 && (veyra.fgBackendName === "dlss" || veyra.fgBackendName === "anr" || veyra.fgBackendName === "vfg")
                    hint: qsTr("Treats the chosen multiplier as a ceiling and, for each pair of frames, uses the one that lands closest to the target (the output cap or the screen refresh rate) at the game's live frame rate, so the output stays even when the game dips; frame generation is not rebuilt · off by default")
                    VSwitch { objectName: "node-fg-adaptive"; checked: veyra.fgAdaptive; onToggled: checked => veyra.fgAdaptive = checked }
                }
                VRow {
                    label: qsTr("Frame pacing repair")
                    visible: veyra.fgBackendName !== "xess"
                    hint: qsTr("For console games locked to 30 FPS whose frames arrive unevenly (17/50 ms in turn), puts the real frames back on an even spacing before frames are generated; it only acts on what it can confirm as uneven delivery and shows a frame at most one capture refresh later · needs automatic duplicate removal · off by default")
                    VSwitch { objectName: "node-pacing-repair"; checked: veyra.pacingRepair; onToggled: checked => veyra.pacingRepair = checked }
                }
                VRow {
                    label: qsTr("Auto HUD lock")
                    visible: veyra.fgBackendName !== "xess"
                    hint: qsTr("Finds pixels that stay still and have an edge (text, crosshairs, health bars, minimaps) and takes them from the real frame in generated frames, so the HUD smears and wobbles less; no boxes needed · off by default")
                    VSwitch { objectName: "node-hud-lock"; checked: veyra.hudLock; onToggled: checked => veyra.hudLock = checked }
                }
                VRow {
                    label: qsTr("Frame generation motion source")
                    visible: veyra.fgBackendName !== "vfg"
                    hint: qsTr("XeSS uses zero motion by default; FSR still computes its own optical flow")
                    VSeg { objectName: "node-fg-motion"; options: [{id:"0",label:qsTr("Zero motion")},{id:"1",label:qsTr("Optical flow")}]; current: String(veyra.fgMotionSource); onPicked: id => veyra.fgMotionSource = Number(id) }
                }
                VRow {
                    label: qsTr("Strict frame generation pacing")
                    hint: qsTr("Frame sync · off by default")
                    VSwitch { checked: veyra.fgStrict; onToggled: checked => veyra.fgStrict = checked }
                }
                NvidiaLowLatencyEditor { namePrefix: "node" }
                VRow {
                    objectName: "node-fg-auto-multiplier"
                    label: qsTr("Automatic multiplier")
                    visible: veyra.fgAutoMultiplierAvailable
                    hint: veyra.fgAutoMultiplierEnabled
                          ? qsTr("Choose a multiplier from source FPS and the output limit; disable frame generation when source FPS exceeds the limit")
                          : qsTr("Off uses the manually selected multiplier. Off by default")
                    hintBelow: veyra.fgAutoMultiplierEnabled
                    VSwitch {
                        checked: veyra.fgAutoMultiplierEnabled
                        onToggled: checked => veyra.fgAutoMultiplierEnabled = checked
                    }
                }
                VRow {
                    visible: !veyra.nvidiaLowLatencyAvailable
                    label: qsTr("Low-latency queue")
                    hint: qsTr("Reduce processing queue depth")
                    VSwitch { checked: veyra.fgLowQueue; onToggled: checked => veyra.fgLowQueue = checked }
                }
            }
        }
    }

    component NodePort: Rectangle {
        id: port
        required property int nodeId
        required property bool output
        objectName: (output ? "node-out-" : "node-in-") + nodeId
        z: 5; width: 22; height: 22; radius: 11
        color: root.wireFrom === nodeId && output ? Theme.accent : (portHover.hovered ? Theme.accentSoft : Theme.card3)
        border.width: 2; border.color: portHover.hovered ? Theme.accent : Theme.stroke2
        HoverHandler { id: portHover; cursorShape: Qt.CrossCursor }
        // Click an output, then an input (or press Esc / click empty canvas).
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                if (port.output) { root.wireFrom = port.nodeId; root.pointerWorld = root.endpoint(port.nodeId, true); wires.requestPaint() }
                else if (root.wireFrom >= 0) { root.rewire(root.wireFrom, port.nodeId); root.wireFrom = -1; wires.requestPaint() }
            }
        }
        // Or drag from an output straight onto an input.
        DragHandler {
            enabled: port.output
            target: null
            onActiveChanged: {
                if (active) { root.wireFrom = port.nodeId; return }
                const to = root.inputAt(root.pointerWorld)
                if (to >= 0) root.rewire(port.nodeId, to)
                root.wireFrom = -1; wires.requestPaint()
            }
            onCentroidChanged: if (active) {
                root.pointerWorld = world.mapFromItem(port, centroid.position.x, centroid.position.y)
                wires.requestPaint()
            }
        }
        TapHandler { acceptedButtons: Qt.RightButton; onTapped: if (port.output) veyra.disconnectNode(port.nodeId) }
        ToolTip.visible: portHover.hovered
        ToolTip.text: output ? qsTr("Drag to an input port to connect, or click and then click an input port; right-click to disconnect") : qsTr("Connect to this input")
    }

    VMenu {
        id: nodePresetMenu
        objectName: "node-preset-menu"
        title: qsTr("Node presets")
        items: veyra.presets.filter(p => p.nodeMode === true)
            .map(p => ({ label: p.name, note: p.note, checked: p.name === veyra.currentPresetName,
                         tag: p.builtin ? qsTr("Built-in") : "", preset: p.index }))
            .concat([{ sep: true },
                     { label: qsTr("Save the current settings as a preset…"), icon: "plus", act: "save" },
                     { label: qsTr("Manage presets…"), icon: "settings", act: "manage" }])
        onPicked: (i, o) => {
            if (o.act) root.requestDialog(o.act)
            else veyra.applyPresetIndex(o.preset)
        }
    }

    VMenu {
        id: nodeMenu
        objectName: "node-context-menu"
        property int targetId: -1
        readonly property var targetNode: veyra.chain.find(n => n.id === targetId) || ({})
        readonly property var descriptor: veyra.effectDescriptor(targetNode.type || "")
        readonly property bool canCopy: descriptor.repeatable === true &&
            veyra.chain.filter(n => n.type === targetNode.type).length < descriptor.maxInstances
        title: targetNode.label || qsTr("Nodes")
        items: [
            {label: qsTr("Delete node"), id: "delete", disabled: targetNode.mustBeLast === true, note: targetNode.mustBeLast ? qsTr("Frame generation is fixed at the end and can be turned off") : ""},
            {label: qsTr("Copy node"), id: "copy", disabled: !canCopy, note: canCopy ? qsTr("Full copy of the settings, placed aside, not connected") : qsTr("Single-instance node, or the limit has been reached")},
            {label: qsTr("Reset node"), id: "reset"}
        ]
        onPicked: (i, item) => {
            const index = veyra.nodeIndexForId(targetId)
            if (index < 0) return
            if (item.id === "delete") root.removeNodeAnimated(targetId)
            else if (item.id === "reset") veyra.resetNode(targetId)
            else {
                const copy = veyra.duplicateNode(targetId)
                if (copy >= 0) { root.markBorn(veyra.chain[copy].id); root.selectId(veyra.chain[copy].id) }
            }
        }
    }

    VMenu {
        id: addMenu
        objectName: "node-add-menu"
        title: qsTr("Add node")
        items: veyra.effectCatalog.map(e => ({ label: e.label, id: e.id, disabled: e.disabled === true, note: e.note || "", tag: e.experimental ? qsTr("Experimental") : "", tagKind: e.experimental ? "warn" : "" }))
        onPicked: (i, o) => {
            // The new card's id is only known after it exists; the next id the
            // chain will hand out is one past the largest.
            root.markBorn(veyra.chain.reduce((m, n) => Math.max(m, n.id), 1) + 1)
            const index = veyra.addEffect(o.id)
            if (index >= 0 && root.addAt)
                veyra.setEffectPosition(index, root.addAt.x, root.addAt.y)
            root.addAt = null
            if (index >= 0 && index < veyra.chain.length) root.markBorn(veyra.chain[index].id)
            if (index >= 0 && index < veyra.chain.length) root.selectId(veyra.chain[index].id)
        }
    }

    VMenu {
        id: nodeSourceMenu
        objectName: "node-source-menu"
        title: qsTr("Source")
        items: (veyra.sourceTitle.length > 0
                ? [{ label: veyra.sourceTitle, note: veyra.sourceFormatText, checked: true, icon: "video", act: "" }] : [])
            .concat([{ label: qsTr("Open file…"), icon: "folder", act: "file" },
                { label: qsTr("Open media URL…"), icon: "cast", act: "network" },
                     { label: qsTr("PC streaming…"), icon: "cast", act: "moonlight" },
                     { label: qsTr("Xbox streaming…"), icon: "gamepad", act: "xbox" },
                     ])
        onPicked: (i, o) => {
            if (o.act === "file") veyra.openFileDialog()
            else if (o.act === "network") veyra.openNetworkDialog()
            else if (o.act === "moonlight") veyra.openMoonlightDialog()
            else if (o.act === "xbox") veyra.openXboxDialog()
        }
    }
    VConfirm {
        id: returnDialog
        objectName: "node-return-dialog"
        cardWidth: 480
        glyph: "list"
        title: qsTr("Switch back to list mode?")
        acceptText: qsTr("Switch to list mode")
        acceptIcon: "list"
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [qsTr("Back to list mode, restoring the list's previous settings and presets."), qsTr("The node chain is kept and used again the next time you switch to node mode."),
                        qsTr("Switching rebuilds the processing chain; the picture may pause for a few hundred milliseconds.")]
                delegate: RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle { Layout.alignment: Qt.AlignTop; Layout.topMargin: 7; implicitWidth: 4; implicitHeight: 4; radius: 2; color: Theme.t3 }
                    Text {
                        Layout.fillWidth: true
                        text: modelData
                        color: Theme.t2
                        font.family: Theme.fontUi; font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        onAccepted: {
            veyra.nodeMode = 0
            if (veyra.nodeMode === 0) root.requestPage("pro")
        }
    }

    // [data-in] entrance order from pages-node.js.
    VRise { target: nvTop; d: 0 }
    VRise { target: canvasCard; d: 1 }
}
