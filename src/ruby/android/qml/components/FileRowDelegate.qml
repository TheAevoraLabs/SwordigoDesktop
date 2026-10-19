import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// Files list row. Public contract preserved exactly:
//   fileName / filePath / fileType, fileSizeStr|fileSizeText,
//   fileDateStr|fileDateText, isDir|isDirectory, isPinned,
//   rowClicked / rowLongPressed / actionRequested(string) /
//   pinToggled / renameClicked / deleteClicked
SwipeDelegate {
    id: root

    width: ListView.view ? ListView.view.width : parent.width
    height: Theme.dp(Theme.rowHeight)

    property string fileName: ""
    property string filePath: ""
    property string fileType: ""
    property string fileSizeText: ""
    property string fileDateText: ""
    property bool isDirectory: false
    property alias fileSizeStr: root.fileSizeText
    property alias fileDateStr: root.fileDateText
    property alias isDir: root.isDirectory
    property bool isPinned: false
    property bool isSelectionMode: false
    property bool isSelected: false

    signal rowClicked()
    signal rowLongPressed()
    signal actionRequested(string action)
    signal pinToggled()
    signal renameClicked()
    signal deleteClicked()
    signal selectionToggled()
    signal previewRequested()

    readonly property color typeColor: Theme.colorForType(root.fileType)
    readonly property string typeIcon: root.isDirectory ? "folder" : Theme.iconForType(root.fileType)
    readonly property string metaText: {
        if (root.isDirectory) {
            return root.fileDateText.length > 0 ? root.fileDateText : qsTr("Directory")
        }
        if (root.fileSizeText.length > 0 && root.fileDateText.length > 0) {
            return root.fileSizeText + "  ·  " + root.fileDateText
        }
        return root.fileSizeText.length > 0 ? root.fileSizeText : root.fileDateText
    }

    // The background wrapper is auto-sized by the control; the inner tile is
    // inset so selected rows read as a rounded tonal card, not a full-width
    // neon outline.
    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(8)
            anchors.rightMargin: Theme.dp(8)
            anchors.topMargin: Theme.dp(2)
            anchors.bottomMargin: Theme.dp(2)
            radius: Theme.radiusMd
            color: root.isSelected
                   ? Theme.alpha(Theme.accentInk, 0.16)
                   : (root.down ? Theme.surface2 : "transparent")
            border.width: 0

            Behavior on color { ColorAnimation { duration: Theme.durFast } }

            // Inset hairline: starts where the title starts, not under the icon.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.dp(60)
                anchors.rightMargin: Theme.dp(12)
                height: 1
                color: Theme.borderSubtle
                visible: !root.isSelected
            }
        }
    }

    leftPadding: Theme.dp(Theme.spacingLg)
    rightPadding: Theme.dp(Theme.spacingSm)

    contentItem: RowLayout {
        spacing: Theme.dp(Theme.spacingMd)

        // The type plate doubles as the selection indicator. There is no
        // separate empty radio column eating width from every row.
        Rectangle {
            Layout.preferredWidth: Theme.dp(40)
            Layout.preferredHeight: Theme.dp(40)
            Layout.alignment: Qt.AlignVCenter
            radius: root.isSelected ? width / 2 : Theme.radiusMd
            color: root.isSelected ? Theme.accentInk : Theme.alpha(root.typeColor, 0.14)
            border.color: root.isSelected ? "transparent" : Theme.alpha(root.typeColor, 0.28)
            border.width: root.isSelected ? 0 : 1
            clip: true

            Behavior on radius { NumberAnimation { duration: Theme.durFast } }
            Behavior on color { ColorAnimation { duration: Theme.durFast } }

            Icon {
                anchors.centerIn: parent
                name: root.isSelected ? "check" : root.typeIcon
                size: root.isSelected ? Theme.dp(18) : Theme.dp(Theme.iconMd)
                color: root.isSelected ? Theme.onAccent : root.typeColor
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.dp(2)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.dp(5)

                Icon {
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.isPinned
                    name: "pin"
                    size: Theme.dp(12)
                    color: Theme.colorWarning
                }

                Text {
                    Layout.fillWidth: true
                    text: root.fileName
                    font.pixelSize: Theme.dp(Theme.fontLg)
                    font.weight: root.isDirectory ? Font.DemiBold : Font.Normal
                    color: Theme.textPrimary
                    elide: Text.ElideMiddle
                    maximumLineCount: 1
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.metaText
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textSecondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        // Per-row overflow menu
        IconButton {
            Layout.preferredWidth: Theme.dp(32)
            Layout.preferredHeight: Theme.dp(32)
            Layout.alignment: Qt.AlignVCenter
            iconName: "more-vertical"
            tint: Theme.textMuted
            plate: "transparent"
            plateEdge: "transparent"
            buttonSize: Theme.dp(32)
            iconSize: Theme.dp(Theme.iconSm)
            onClicked: root.actionRequested("menu")
        }
    }

    // Leading swipe: pin / unpin
    swipe.left: Rectangle {
        width: Theme.dp(76)
        height: parent.height
        color: root.isPinned ? Theme.colorWarning : Theme.accentStart
        anchors.left: parent.left

        Column {
            anchors.centerIn: parent
            spacing: Theme.dp(2)

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "pin"
                size: Theme.dp(Theme.iconMd)
                color: root.isPinned ? "#2A1B00" : Theme.onAccent
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.isPinned ? qsTr("Unpin") : qsTr("Pin")
                color: root.isPinned ? "#2A1B00" : Theme.onAccent
                font.pixelSize: Theme.dp(Theme.fontSm)
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.swipe.close()
                root.actionRequested("pin")
                root.pinToggled()
            }
        }
    }

    // Trailing swipe: rename / delete
    swipe.right: Row {
        anchors.right: parent.right
        height: parent.height

        Rectangle {
            width: Theme.dp(64)
            height: parent.height
            color: Theme.accentEnd

            Column {
                anchors.centerIn: parent
                spacing: Theme.dp(2)
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "pencil"
                    size: Theme.dp(Theme.iconSm)
                    color: Theme.onAccent
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Rename")
                    color: Theme.onAccent
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.swipe.close()
                    root.actionRequested("rename")
                    root.renameClicked()
                }
            }
        }

        Rectangle {
            width: Theme.dp(64)
            height: parent.height
            color: Theme.colorError

            Column {
                anchors.centerIn: parent
                spacing: Theme.dp(2)
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "trash"
                    size: Theme.dp(Theme.iconSm)
                    color: Theme.onAccent
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Delete")
                    color: Theme.onAccent
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.swipe.close()
                    root.actionRequested("delete")
                    root.deleteClicked()
                }
            }
        }
    }

    onClicked: {
        if (root.isSelectionMode) {
            root.selectionToggled()
        } else {
            root.rowClicked()
        }
    }
    onPressAndHold: root.rowLongPressed()
}
