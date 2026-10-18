import QtQuick
import QtQuick.Controls
import ".."

// Grid/Card view delegate for Ruby Touch Files Hub (Index FM style)
Rectangle {
    id: root

    width: GridView.view ? (GridView.view.cellWidth - Theme.dp(Theme.spacingSm)) : Theme.dp(160)
    height: GridView.view ? (GridView.view.cellHeight - Theme.dp(Theme.spacingSm)) : Theme.dp(140)
    radius: Theme.radiusMd
    color: root.isSelected ? Theme.alpha(Theme.accentInk, 0.16) : (cardMouse.pressed ? Theme.surface2 : Theme.surface1)
    border.color: root.isSelected ? Theme.accentInk : (cardMouse.containsMouse ? Theme.borderFocus : Theme.borderSubtle)
    border.width: root.isSelected ? 2 : 1

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

    signal cardClicked()
    signal cardLongPressed()
    signal actionRequested(string action)
    signal pinToggled()
    signal previewRequested()
    signal selectionToggled()

    readonly property color typeColor: Theme.colorForType(root.fileType)
    readonly property string typeIcon: root.isDirectory ? "folder" : Theme.iconForType(root.fileType)
    readonly property bool isImagePreviewable: !root.isDirectory && (root.fileType === "texture" || root.fileName.toLowerCase().endsWith(".png") || root.fileName.toLowerCase().endsWith(".jpg") || root.fileName.toLowerCase().endsWith(".jpeg"))

    // Top Row: Selection Checkbox & Pin indicator
    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.dp(6)
        height: Theme.dp(24)
        z: 2

        // Multi-select circular checkbox
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.dp(20)
            height: Theme.dp(20)
            radius: Theme.dp(10)
            visible: root.isSelectionMode || root.isSelected
            color: root.isSelected ? Theme.accentInk : Theme.surface2
            border.color: root.isSelected ? Theme.accentInk : Theme.borderSubtle
            border.width: 1

            Icon {
                anchors.centerIn: parent
                name: "check"
                size: Theme.dp(12)
                color: Theme.onAccent
                visible: root.isSelected
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.selectionToggled()
            }
        }

        // Pin badge
        Icon {
            anchors.right: overflowBtn.left
            anchors.rightMargin: Theme.dp(2)
            anchors.verticalCenter: parent.verticalCenter
            name: "pin"
            size: Theme.dp(13)
            color: Theme.colorWarning
            visible: root.isPinned
        }

        // Overflow ⋮ button
        IconButton {
            id: overflowBtn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconName: "more-vertical"
            buttonSize: Theme.dp(24)
            iconSize: Theme.dp(13)
            onClicked: root.actionRequested("menu")
        }
    }

    // Center icon / preview thumbnail
    Item {
        id: previewContainer
        anchors.top: parent.top
        anchors.topMargin: Theme.dp(26)
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.dp(52)
        height: Theme.dp(52)

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.alpha(root.typeColor, 0.14)
            border.color: Theme.alpha(root.typeColor, 0.30)
            border.width: 1
            clip: true

            // Image thumbnail if image
            Image {
                id: thumbImg
                anchors.fill: parent
                anchors.margins: 2
                source: root.isImagePreviewable ? ("file://" + root.filePath) : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready
            }

            // Vector file type icon
            Icon {
                anchors.centerIn: parent
                name: root.typeIcon
                size: Theme.dp(26)
                color: root.typeColor
                visible: !thumbImg.visible
            }
        }
    }

    // Bottom file information
    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.dp(8)
        spacing: Theme.dp(2)

        Text {
            width: parent.width
            text: root.fileName
            font.pixelSize: Theme.dp(Theme.fontSm)
            font.weight: root.isDirectory ? Font.DemiBold : Font.Normal
            color: Theme.textPrimary
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 1
        }

        Text {
            width: parent.width
            text: root.isDirectory ? qsTr("Folder") : root.fileSizeText
            font.pixelSize: Theme.dp(Theme.fontXs)
            color: Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    // Hit target mouse area
    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        z: 1

        onClicked: {
            if (root.isSelectionMode) {
                root.selectionToggled()
            } else {
                root.cardClicked()
            }
        }

        onPressAndHold: {
            root.cardLongPressed()
        }
    }
}
