import QtQuick
import QtQuick.Controls
import ".."

// Filter / segmented chip. Replaces the QSS-styled QPushButton "chip" row from
// MobileAssetBrowser::setup_ui(), including its exact category semantics.
Rectangle {
    id: root

    property string text: ""
    property string iconName: ""
    property bool selected: false
    /// Draw the hairline outline. Dense toolbars set this false so the chips
    /// read as tonal fills instead of a row of boxed buttons.
    property bool outlined: true
    property color accent: Theme.accentEnd
    /// Optional trailing count badge.
    property int count: -1

    signal clicked()

    height: Theme.dp(34)
    implicitWidth: chipRow.implicitWidth + Theme.dp(26)
    width: implicitWidth
    radius: Theme.radiusPill
    color: root.selected ? Theme.alpha(root.accent, 0.20) : (root.outlined ? Theme.surface2 : Theme.surface1)
    border.color: root.outlined ? (root.selected ? root.accent : Theme.borderSubtle) : "transparent"
    border.width: root.outlined ? 1 : 0

    Behavior on color { ColorAnimation { duration: Theme.durFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: Theme.dp(6)

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName.length > 0
            name: root.iconName
            size: Theme.dp(Theme.iconXs)
            weight: root.selected ? 2.0 : Icons.stroke
            color: root.selected ? root.accent : Theme.textMuted
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: Theme.dp(Theme.fontSm)
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            color: root.selected ? Theme.textPrimary : Theme.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.count >= 0
            text: root.count
            font.pixelSize: Theme.dp(Theme.fontXs)
            color: root.selected ? root.accent : Theme.textMuted
        }
    }

    Rectangle { // pressed veil
        anchors.fill: parent
        radius: parent.radius
        color: root.accent
        opacity: chipArea.pressed ? 0.12 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
    }

    MouseArea {
        id: chipArea
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
