import QtQuick
import QtQuick.Controls
import ".."

// Labeled surface card. Replaces the many hardcoded-height Rectangles that
// previously wrapped each settings / tool group (their fixed heights clipped
// content at large font scales).
Rectangle {
    id: root

    property string label: ""
    property string accentIcon: ""
    property color accent: Theme.accentInk
    default property alias contentData: body.data

    readonly property real contentPadding: Theme.dp(14)

    color: Theme.surface1
    radius: Theme.radiusCard
    border.color: "transparent"
    border.width: 0
    implicitHeight: layout.implicitHeight + contentPadding * 2

    Column {
        id: layout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: root.contentPadding
        spacing: Theme.dp(Theme.spacingMd)

        Row {
            width: parent.width
            spacing: Theme.dp(6)
            visible: root.label.length > 0

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.accentIcon.length > 0
                name: root.accentIcon
                size: Theme.dp(Theme.iconXs)
                color: root.accent
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.label.toUpperCase()
                font.pixelSize: Theme.dp(Theme.fontXs)
                font.weight: Font.Bold
                font.letterSpacing: 1.1
                color: Theme.textMuted
            }
        }

        Column {
            id: body
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)
        }
    }
}
