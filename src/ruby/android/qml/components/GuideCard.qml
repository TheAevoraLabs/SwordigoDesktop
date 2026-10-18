import QtQuick
import QtQuick.Controls
import ".."

// Sleek horizontal modding guide card with clipped rounded thumbnail
Rectangle {
    id: root

    width: Theme.dp(270)
    height: Theme.dp(84)
    radius: Theme.radiusMd
    color: mouseArea.pressed ? Theme.surface2 : Theme.surface1
    border.width: 0

    property string title: ""
    property string category: ""
    property string tag: ""
    property string description: ""
    property string readTime: "4 min read"
    property string svgSource: ""
    property string qrcPath: ""

    signal clicked()

    Row {
        anchors.fill: parent
        anchors.margins: Theme.dp(10)
        spacing: Theme.dp(12)

        // ── Clipped Rounded Thumbnail ────────────────────────────────────
        Rectangle {
            width: Theme.dp(64)
            height: Theme.dp(64)
            radius: Theme.radiusSm
            color: Theme.surface2
            clip: true
            anchors.verticalCenter: parent.verticalCenter

            Image {
                anchors.fill: parent
                source: root.svgSource
                fillMode: Image.PreserveAspectCrop
                smooth: true
                asynchronous: true
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.alpha("#000000", 0.15)
            }
        }

        // ── Content Column ───────────────────────────────────────────────
        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - Theme.dp(64) - Theme.dp(12)
            spacing: Theme.dp(3)

            Row {
                spacing: Theme.dp(6)

                Text {
                    text: root.category.toUpperCase()
                    font.pixelSize: Theme.dp(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                    color: Theme.accentInk
                }

                Text {
                    text: "•"
                    font.pixelSize: Theme.dp(9)
                    color: Theme.textMuted
                }

                Text {
                    text: root.readTime
                    font.pixelSize: Theme.dp(9)
                    color: Theme.textMuted
                }
            }

            Text {
                width: parent.width
                text: root.title
                font.pixelSize: Theme.dp(13)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                width: parent.width
                text: root.description
                font.pixelSize: Theme.dp(10)
                color: Theme.textSecondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
