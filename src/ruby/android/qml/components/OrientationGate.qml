import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// OrientationGate.qml — the landscape guard for the scene editor.
// Provides guaranteed escape paths, full responsiveness on any screen height,
// and smooth auto-dismissal when landscape orientation is confirmed.
// ============================================================================
Rectangle {
    id: root

    /// True when the usable area is portrait (or too narrow to host the deck).
    property bool blocking: true
    /// Whether the platform can actually rotate for us.
    property bool platformCanRotate: false
    property string targetName: ""

    signal backRequested()
    signal bypassRequested()

    color: Theme.surface0
    visible: opacity > 0.0
    opacity: blocking ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

    // Intercept clicks so underlying 3D viewport doesn't receive ghost taps
    MouseArea {
        anchors.fill: parent
        preventStealing: true
    }

    // Top Escape Navigation Bar (Guaranteed exit so user is NEVER stuck)
    Row {
        id: topNav
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.dp(Theme.spacingMd)
        height: Theme.dp(40)
        z: 10

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-left"
            buttonSize: Theme.dp(36)
            iconSize: Theme.dp(18)
            tint: Theme.textPrimary
            tooltip: qsTr("Back to Files")
            onClicked: root.backRequested()
        }

        Item { width: parent.width - Theme.dp(96); height: 1 }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "close"
            buttonSize: Theme.dp(36)
            iconSize: Theme.dp(18)
            tint: Theme.textSecondary
            tooltip: qsTr("Dismiss")
            onClicked: root.bypassRequested()
        }
    }

    Flickable {
        id: flick
        anchors.top: topNav.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: contentCol.height + Theme.dp(40)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width - Theme.dp(36), Theme.dp(340))
            spacing: Theme.dp(Theme.spacingSm)
            topPadding: Theme.dp(Theme.spacingSm)
            bottomPadding: Theme.dp(Theme.spacingLg)

            // Schematic diagram of landscape studio
            Item {
                id: diagram
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.dp(120)
                height: Theme.dp(64)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: "transparent"
                    border.color: Theme.borderStrong
                    border.width: 2
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.dp(6)
                    width: Theme.dp(20)
                    height: Theme.dp(20)
                    radius: 10
                    color: Theme.alpha(Theme.accentInk, 0.22)
                    border.color: Theme.alpha(Theme.accentInk, 0.55)
                    border.width: 1
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.dp(6)
                    width: Theme.dp(20)
                    height: Theme.dp(20)
                    radius: 10
                    color: Theme.alpha(Theme.colorWarning, 0.20)
                    border.color: Theme.alpha(Theme.colorWarning, 0.55)
                    border.width: 1
                }

                Grid {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.dp(8)
                    columns: 3
                    spacing: Theme.dp(2)

                    Repeater {
                        model: 9
                        delegate: Rectangle {
                            width: Theme.dp(9)
                            height: Theme.dp(9)
                            radius: Theme.radiusXs
                            color: index === 4 ? Theme.alpha(Theme.accentInk, 0.30)
                                               : Theme.alpha(Theme.textMuted, 0.16)
                        }
                    }
                }
            }

            // Animated rotating glyph
            Icon {
                id: rotateGlyph
                anchors.horizontalCenter: parent.horizontalCenter
                name: "refresh"
                size: Theme.dp(26)
                color: Theme.accentInk

                SequentialAnimation on rotation {
                    running: root.blocking
                    loops: Animation.Infinite
                    NumberAnimation { from: 0; to: 90; duration: 700; easing.type: Easing.InOutQuad }
                    PauseAnimation { duration: 550 }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Rotate to Landscape")
                font.pixelSize: Theme.dp(Theme.fontTitle)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.targetName.length > 0
                      ? qsTr("The scene editor for %1 is optimized for landscape.").arg(root.targetName)
                      : qsTr("The scene editor is optimized for landscape.")
                font.pixelSize: Theme.dp(Theme.fontMd)
                color: Theme.textSecondary
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: !root.platformCanRotate
                text: qsTr("Turn off rotation lock, or use the buttons below.")
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textMuted
            }

            Item { width: 1; height: Theme.dp(4) }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: qsTr("Switch to Landscape")
                highlighted: true
                onClicked: {
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.lockLandscape()
                    if (typeof androidContext !== "undefined" && androidContext)
                        androidContext.lockLandscape()
                    root.bypassRequested()
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: qsTr("Open Studio Anyway")
                flat: true
                onClicked: root.bypassRequested()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: qsTr("Back to Menu")
                flat: true
                onClicked: root.backRequested()
            }
        }
    }

    Connections {
        target: (typeof screenOrientation !== "undefined" && screenOrientation) ? screenOrientation : null
        function onOrientationPolicyChanged() {
            if (screenOrientation && screenOrientation.isLandscape) {
                root.bypassRequested()
            }
        }
    }
}
