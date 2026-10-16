import QtQuick
import QtQuick.Controls
import ".."

// ─────────────────────────────────────────────────────────────────────────────
// BubbleStepper.qml — a floating press-and-hold +/- pad for a numeric value.
//
// This replaces the mesh-tune TEXT FIELDS. Typing a number on a phone keyboard
// inside a 3D editor was the worst part of the old panel, so the control is now
// the way a thumb wants it: a small floating bubble with a "+" zone on top and
// a "-" zone on the bottom. A tap steps once; holding ramps and speeds up, so a
// value can be swept across its range without forty taps and without the
// keyboard ever appearing.
//
// The OWNER keeps the value: `value` is an input the parent binds, and the pad
// reports signed deltas through `stepped`. A pad that wrote its own `value`
// would break the parent's binding on the first edit, so the display would go
// stale the next time the host object changed.
// ─────────────────────────────────────────────────────────────────────────────
Item {
    id: root

    property string label: ""
    property string suffix: ""
    /// Display value, owned (bound) by the parent.
    property real value: 0
    /// One tap / first tick increment, before hold acceleration.
    property real step: 1
    property int decimals: 1
    property color tone: Theme.accentInk
    property int holdDelay: 330
    property int holdInterval: 70
    /// Ticks between speed-ups (1x, 2x, 3x ...).
    property int accelEvery: 5

    /// Signed value delta requested by the user. The parent applies and clamps.
    signal stepped(real delta)

    width: Theme.dp(50)
    height: Theme.dp(104)

    property int holdSign: 0
    property real rampMult: 1

    readonly property string display: {
        var v = Number(root.value)
        var s = (root.decimals > 0) ? v.toFixed(root.decimals) : String(Math.round(v))
        return s + root.suffix
    }

    function fire(sign) {
        root.stepped(sign * root.step * root.rampMult)
    }

    function beginHold(sign) {
        root.holdSign = sign
        root.rampMult = 1
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(12)
        root.fire(sign)
        delayTimer.restart()
    }

    function endHold() {
        root.holdSign = 0
        root.rampMult = 1
        delayTimer.stop()
        rampTimer.stop()
    }

    Timer {
        id: delayTimer
        interval: root.holdDelay
        onTriggered: rampTimer.start()
    }

    Timer {
        id: rampTimer
        property int ticks: 0
        interval: root.holdInterval
        repeat: true
        onTriggered: {
            ticks++
            root.rampMult = 1 + Math.floor(ticks / Math.max(1, root.accelEvery))
            root.fire(root.holdSign)
        }
        onRunningChanged: { if (!running) ticks = 0 }
    }

    // ── Bubble ─────────────────────────────────────────────────────────────
    Rectangle {
        id: bubble
        anchors.fill: parent
        radius: width / 2
        border.color: root.holdSign !== 0 ? root.tone : Theme.borderStrong
        border.width: root.holdSign !== 0 ? 2 : 1

        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.alpha(Theme.surface3, 0.92) }
            GradientStop { position: 1.0; color: Theme.alpha(Theme.surface1, 0.96) }
        }

        Behavior on border.width { NumberAnimation { duration: Theme.durFast } }

        Column {
            anchors.fill: parent
            anchors.topMargin: Theme.dp(8)
            anchors.bottomMargin: Theme.dp(8)

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.label
                font.pixelSize: Theme.dp(8)
                font.weight: Font.Bold
                font.letterSpacing: 0.4
                color: Theme.textMuted
            }

            // ── "+" zone (hold to increase) ────────────────────────────────
            Item {
                width: parent.width
                height: Theme.dp(24)
                Icon {
                    anchors.centerIn: parent
                    name: "plus"
                    size: Theme.dp(15)
                    color: upArea.pressed ? root.tone : Theme.textSecondary
                }
                MouseArea {
                    id: upArea
                    anchors.fill: parent
                    pressAndHoldInterval: 600000  // ramping is handled here
                    onPressed: root.beginHold(1)
                    onReleased: root.endHold()
                    onCanceled: root.endHold()
                }
            }

            // ── Readout ────────────────────────────────────────────────────
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.display
                font.pixelSize: Theme.dp(12)
                font.family: Theme.monoFamily
                font.weight: Font.Bold
                color: root.holdSign !== 0 ? root.tone : Theme.textPrimary
            }

            // ── "-" zone (hold to decrease) ────────────────────────────────
            Item {
                width: parent.width
                height: Theme.dp(24)
                Icon {
                    anchors.centerIn: parent
                    name: "minus"
                    size: Theme.dp(15)
                    color: downArea.pressed ? root.tone : Theme.textSecondary
                }
                MouseArea {
                    id: downArea
                    anchors.fill: parent
                    pressAndHoldInterval: 600000
                    onPressed: root.beginHold(-1)
                    onReleased: root.endHold()
                    onCanceled: root.endHold()
                }
            }
        }
    }
}
