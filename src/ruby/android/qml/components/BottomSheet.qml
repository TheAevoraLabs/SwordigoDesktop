import QtQuick
import QtQuick.Controls
import ".."

// Draggable Material bottom sheet. Same public API as before:
//   open(expanded) / close() / toggle() / isOpen / peekHeight / expandedHeight
//   title / default content / closed() / opened()
Item {
    id: root
    anchors.fill: parent
    visible: sheetY < root.height

    property bool isOpen: false
    property real peekHeight: Theme.dp(280)
    property real expandedHeight: root.height * 0.85
    property string title: ""
    default property alias contentData: contentContainer.data

    signal closed()
    signal opened()

    // Scrim / backdrop
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: "#000000"
        opacity: Math.max(0.0, Math.min(0.62, (root.height - sheetY) / root.height * 0.85))
        visible: root.isOpen || sheetY < root.height

        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    property real sheetY: root.height

    function open(expanded) {
        root.isOpen = true
        animY.stop()
        animY.to = expanded ? (root.height - expandedHeight) : (root.height - peekHeight)
        animY.start()
        root.opened()
    }

    function close() {
        root.isOpen = false
        animY.stop()
        animY.to = root.height
        animY.start()
        root.closed()
    }

    function toggle() {
        if (isOpen) close(); else open(false);
    }

    NumberAnimation {
        id: animY
        target: root
        property: "sheetY"
        duration: Theme.durSlow
        easing.type: Easing.OutCubic
    }

    Rectangle {
        id: sheet
        x: 0
        y: root.sheetY
        width: root.width
        height: root.height - y + Theme.radiusSheet
        color: Theme.surface1
        radius: Theme.radiusSheet
        border.color: Theme.border
        border.width: 1

        // Consume touch events so they don't leak through to views behind
        MouseArea {
            anchors.fill: parent
        }

        // Drag handle
        Item {
            id: handleZone
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.dp(32)

            Rectangle {
                anchors.centerIn: parent
                width: Theme.dp(40)
                height: Theme.dp(4)
                radius: Theme.dp(2)
                color: Theme.borderStrong
            }

            DragHandler {
                yAxis.minimum: root.height - root.expandedHeight
                yAxis.maximum: root.height
                onActiveChanged: {
                    if (!active) {
                        var currentH = root.height - root.sheetY
                        if (currentH < root.peekHeight * 0.5) {
                            root.close()
                        } else if (currentH < (root.peekHeight + root.expandedHeight) * 0.5) {
                            root.open(false)
                        } else {
                            root.open(true)
                        }
                    }
                }
                onTranslationChanged: (delta) => {
                    root.sheetY = Math.max(root.height - root.expandedHeight,
                                           Math.min(root.height, root.sheetY + delta.y))
                }
            }
        }

        // Header: title + explicit close affordance
        Item {
            id: headerRow
            anchors.top: handleZone.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.dp(Theme.paddingScreen)
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            height: root.title.length > 0 ? Theme.dp(34) : 0
            visible: root.title.length > 0

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: sheetClose.left
                anchors.rightMargin: Theme.dp(Theme.spacingSm)
                text: root.title
                font.pixelSize: Theme.dp(Theme.fontXl)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            IconButton {
                id: sheetClose
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                iconSize: Theme.dp(18)
                buttonSize: Theme.dp(32)
                onClicked: root.close()
            }
        }

        Item {
            id: contentContainer
            anchors.top: headerRow.bottom
            anchors.topMargin: Theme.dp(root.title.length > 0 ? Theme.spacingXs : Theme.spacingSm)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.radiusSheet
            clip: true
        }
    }
}
