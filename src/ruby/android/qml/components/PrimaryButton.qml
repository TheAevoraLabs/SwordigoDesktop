import QtQuick
import QtQuick.Controls
import ".."

// Filled primary action. `text` is redeclared intentionally so callers set it
// the same way they would on a Button, with a leading vector icon support.
Rectangle {
    id: root

    property string text: ""
    property string iconName: ""
    property alias buttonWidth: root.width
    property alias buttonHeight: root.height

    signal clicked()

    implicitHeight: Theme.dp(44)
    implicitWidth: btnRow.implicitWidth + Theme.dp(Theme.spacingLg) * 2
    height: implicitHeight
    width: implicitWidth
    radius: Theme.radiusSm
    color: hit.pressed ? Theme.accentEnd : Theme.accentStart
    border.color: Theme.accentEnd
    border.width: 1
    opacity: enabled ? 1.0 : 0.4

    Row {
        id: btnRow
        anchors.centerIn: parent
        spacing: Theme.dp(Theme.spacingSm)

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName.length > 0
            name: root.iconName
            size: Theme.dp(Theme.iconSm)
            color: Theme.onAccent
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: Theme.dp(Theme.fontMd)
            font.weight: Font.DemiBold
            color: Theme.onAccent
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
