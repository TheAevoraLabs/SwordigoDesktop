import QtQuick
import QtQuick.Controls
import ".."

// Action row inside a BottomSheet or Menu. Replaces the repeated
// Rectangle+Row+MouseArea blocks that made each sheet ~90% boilerplate.
Item {
    id: root

    property string iconName: ""
    property string text: ""
    property bool danger: false
    property bool trailingChevron: false
    // Emphasise a row as the sheet's primary action (e.g. "Start Scene in Game").
    property bool accent: false

    signal clicked()

    height: Theme.dp(48)
    opacity: enabled ? 1.0 : 0.35

    readonly property color tone: root.danger ? Theme.colorErrorBright
                              : (root.accent ? Theme.accentInk : Theme.textPrimary)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: rowArea.pressed ? Theme.alpha(root.danger ? Theme.colorErrorBright : Theme.textPrimary,
                                             root.danger ? 0.14 : 0.06)
                               : "transparent"
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        spacing: Theme.dp(Theme.spacingMd)

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: Theme.dp(Theme.iconMd)
            color: root.tone
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Theme.dp(22) - Theme.dp(Theme.spacingMd)
                    - (root.trailingChevron ? Theme.dp(18) : 0)
            text: root.text
            font.pixelSize: Theme.dp(Theme.fontMd)
            color: root.tone
            elide: Text.ElideRight
        }
    }

    Icon {
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.verticalCenter: parent.verticalCenter
        visible: root.trailingChevron
        name: "chevron-right"
        size: Theme.dp(Theme.iconSm)
        color: Theme.textMuted
    }

    MouseArea {
        id: rowArea
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
