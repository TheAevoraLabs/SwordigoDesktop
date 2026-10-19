import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// GizmoPad.qml — touch-first replacement for dragging a 3D gizmo.
//
// Dragging a 3D translate/rotate/scale handle with a thumb that is covering the
// handle is the classic mobile-editor failure mode. This pad gives the same
// job to six discrete, axis-coloured buttons arranged as a diamond, plus a
// press-and-hold repeat so long adjustments do not need 40 taps.
//
// It is mode-aware, so one pad serves the whole gizmo:
//   mode 1 Move   -> one step = 0.25 world units
//   mode 2 Rotate -> one step = 15 degrees
//   mode 3 Scale  -> one step = 5% (uniform, because the .scene format stores
//                    a single uniform Scaling float on Tag 7)
//   mode 0 View   -> pad is inert and says so
//
// Axis colours are the universal 3D convention (X red, Y green, Z blue), which
// is what makes it read as a gizmo rather than as six anonymous buttons.
// ============================================================================
Item {
    id: root

    property int gizmoMode: 0
    /// False when nothing is selected.
    property bool hasSelection: false
    /// Set true when selected object only supports 2D in-plane rotation (GroundMesh / WaterMesh)
    property bool isPlanarOnly: false
    /// Show the four mode dots under the readout so the cycle target is
    /// discoverable without a label.
    property bool showModeDots: true
    property real cellSize: Theme.dp(42)
    property int repeatDelay: 350
    property int repeatInterval: 90

    signal nudged(int axis, real steps)
    /// Tapping the centre readout walks View -> Move -> Rotate -> Scale. In
    /// landscape there is no room for a separate segmented control, and the
    /// centre cell already displays the mode, so making it the switch keeps the
    /// whole gizmo inside one 3x3 block.
    signal modeCycleRequested()
    /// Absolute mode jump, from the long-press / trailing chip menu.
    signal modeRequested(int mode)

    readonly property color axisX: "#E4675F"
    readonly property color axisY: "#4ED08A"
    readonly property color axisZ: "#64C3FF"
    readonly property bool active: root.gizmoMode > 0 && root.hasSelection

    readonly property string modeName: {
        switch (root.gizmoMode) {
        case 1: return "MOVE"
        case 2: return root.isPlanarOnly ? "ROLL (Z)" : "ROTATE"
        case 3: return "SCALE"
        default: return "VIEW"
        }
    }

    readonly property string stepLabel: {
        switch (root.gizmoMode) {
        case 1: return "0.25 u"
        case 2: return root.isPlanarOnly ? "15° (Roll)" : "15°"
        case 3: return "5%"
        default: return "pick a gizmo"
        }
    }

    // ── Held-button repeat ─────────────────────────────────────────────────
    property var held: null

    function press(axis, sign) {
        root.held = { axis: axis, sign: sign }
        root.fire()
        delayTimer.restart()
    }

    function release() {
        delayTimer.stop()
        repeatTimer.stop()
        root.held = null
    }

    function fire() {
        if (!root.active || !root.held) return
        root.nudged(root.held.axis, root.held.sign)
    }

    Timer {
        id: delayTimer
        interval: root.repeatDelay
        onTriggered: repeatTimer.start()
    }

    Timer {
        id: repeatTimer
        interval: root.repeatInterval
        repeat: true
        onTriggered: root.fire()
    }

    // ── Cell table: 3x3 grid, nulls are spacers ────────────────────────────
    readonly property var cells: [
        null,
        { axis: 1, sign:  1 },   // Y+
        { axis: 2, sign:  1 },   // Z+
        { axis: 0, sign: -1 },   // X-
        "center",
        { axis: 0, sign:  1 },   // X+
        null,
        { axis: 1, sign: -1 },   // Y-
        { axis: 2, sign: -1 }    // Z-
    ]

    readonly property real gap: Theme.dp(4)

    width: cellSize * 3 + gap * 2
    height: width

    function axisColor(axis) {
        return axis === 0 ? axisX : (axis === 1 ? axisY : axisZ)
    }

    function axisLetter(axis) {
        return axis === 0 ? "X" : (axis === 1 ? "Y" : "Z")
    }

    /// Rotation applied to the shared `arrow-up` glyph so the arrow points
    /// along the axis the button moves. Qt rotates clockwise, so an up-arrow
    /// needs +90 to point right. Getting this wrong makes the pad actively
    /// misleading: X+ showing an up-arrow reads as "raise", not "move east".
    function arrowRotation(axis, sign) {
        if (axis === 0) return sign > 0 ? 90 : -90     // X: right / left
        if (axis === 1) return sign > 0 ? 0 : 180      // Y: up / down
        return sign > 0 ? 45 : -135                    // Z: depth, down-right / up-left
    }

    Grid {
        id: grid
        anchors.fill: parent
        columns: 3
        columnSpacing: root.gap
        rowSpacing: root.gap

        Repeater {
            model: root.cells

            delegate: Item {
                width: root.cellSize
                height: root.cellSize
                // NOTE: this Item must stay `visible`. A Grid is a positioner
                // and skips invisible children entirely, so hiding the two
                // corner spacers would shift every other cell and collapse the
                // diamond into a wrong-shaped blob. Blank cells are made
                // transparent instead.
                opacity: modelData === null ? 0.0 : 1.0

                // ── Center readout + mode switch ───────────────────────────
                Rectangle {
                    anchors.fill: parent
                    visible: modelData === "center"
                    radius: Theme.radiusMd
                    color: centerArea.pressed ? Theme.alpha(Theme.accentInk, 0.22)
                                             : Theme.surfaceGlass
                    border.color: centerArea.pressed ? Theme.alpha(Theme.accentInk, 0.55)
                                                     : (root.active ? Theme.alpha(Theme.accentInk, 0.35)
                                                                    : Theme.borderSubtle)
                    border.width: 1

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.dp(2)

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.modeName
                            font.pixelSize: Theme.dp(9)
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                            color: root.active ? Theme.accentInk : Theme.textDisabled
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.stepLabel
                            font.pixelSize: Theme.dp(8)
                            color: Theme.textMuted
                        }

                        // Four dots: which of the four modes is live, and that
                        // there are four to walk through.
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.showModeDots
                            spacing: Theme.dp(3)

                            Repeater {
                                model: 4

                                delegate: Rectangle {
                                    width: Theme.dp(4)
                                    height: Theme.dp(4)
                                    radius: 2
                                    color: (index + 1) === root.gizmoMode
                                           ? Theme.accentInk
                                           : Theme.alpha(Theme.textMuted, 0.40)
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: centerArea
                        anchors.fill: parent
                        onClicked: root.modeCycleRequested()
                    }
                }

                // ── Nudge cell ─────────────────────────────────────────────
                Rectangle {
                    id: cell
                    anchors.fill: parent
                    visible: modelData !== null && modelData !== "center"

                    readonly property bool isAxisDisabled: root.isPlanarOnly && root.gizmoMode === 2 && modelData.axis !== 2
                    readonly property color tone: visible ? root.axisColor(modelData.axis) : Theme.textMuted

                    radius: Theme.radiusMd
                    color: isAxisDisabled ? Theme.alpha(Theme.surface2, 0.3)
                                          : (cellArea.pressed ? Theme.alpha(cell.tone, 0.35)
                                                              : Theme.alpha(cell.tone, root.active ? 0.15 : 0.06))
                    border.color: isAxisDisabled ? Theme.borderSubtle
                                                 : Theme.alpha(cell.tone, root.active ? 0.45 : 0.18)
                    border.width: 1
                    opacity: isAxisDisabled ? 0.18 : (root.active ? 1.0 : 0.45)

                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

                    Column {
                        anchors.centerIn: parent
                        spacing: 0

                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: cell.visible
                            name: cell.isAxisDisabled ? "lock" : "arrow-up"
                            size: cell.isAxisDisabled ? Theme.dp(12) : Theme.dp(15)
                            weight: 2.2
                            color: cell.isAxisDisabled ? Theme.textDisabled : cell.tone
                            rotation: (cell.visible && !cell.isAxisDisabled) ? root.arrowRotation(modelData.axis, modelData.sign) : 0
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: cell.visible
                            text: cell.visible
                                  ? (cell.isAxisDisabled ? "LOCKED" : (root.axisLetter(modelData.axis) + (modelData.sign > 0 ? "+" : "-")))
                                  : ""
                            font.pixelSize: cell.isAxisDisabled ? Theme.dp(6) : Theme.dp(8)
                            font.family: Theme.monoFamily
                            font.weight: Font.Bold
                            color: cell.isAxisDisabled ? Theme.textDisabled : cell.tone
                        }
                    }

                    MouseArea {
                        id: cellArea
                        anchors.fill: parent
                        enabled: root.active && !cell.isAxisDisabled
                        onPressed: root.press(modelData.axis, modelData.sign)
                        onReleased: root.release()
                        onCanceled: root.release()
                    }
                }
            }
        }
    }
}
