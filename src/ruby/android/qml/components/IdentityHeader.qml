import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    height: Theme.dp(56)

    property string userName: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
    property string avatarImg: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.avatarImage) ? rubySettings.avatarImage : ""
    property string workspaceName: (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.folderName && rubyFileModel.folderName.length > 0)
                                   ? rubyFileModel.folderName : "Swordigo Workspace"
    property string statusText: qsTr("Local Active")
    property bool isConnected: true

    signal profileClicked()
    signal settingsClicked()
    signal searchClicked()

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.dp(Theme.spacingMd)

        // ── Clean Native Avatar ─────────────────────────────────────────
        Item {
            width: Theme.dp(40)
            height: Theme.dp(40)
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                id: avatarBg
                anchors.fill: parent
                radius: width / 2
                color: Theme.surface2
                border.width: 0
                clip: true

                // Custom / Preset Avatar Image
                Image {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(2)
                    source: root.avatarImg
                    visible: root.avatarImg.length > 0
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                // Monogram Fallback
                Text {
                    anchors.centerIn: parent
                    visible: root.avatarImg.length === 0
                    text: root.userName.length > 0 ? root.userName.charAt(0).toUpperCase() : "R"
                    font.pixelSize: Theme.dp(16)
                    font.weight: Font.Bold
                    font.family: Theme.monoFamily
                    color: Theme.accentInk
                }
            }

            // Local Status Indicator Dot
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: Theme.dp(10)
                height: Theme.dp(10)
                radius: width / 2
                color: "#10B981"
                border.color: Theme.surface0
                border.width: Theme.dp(2)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.profileClicked()
            }
        }

        // ── User Identity Text Column ───────────────────────────────────
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            Text {
                text: root.userName
                font.pixelSize: Theme.dp(16)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }

            Row {
                spacing: Theme.dp(5)
                anchors.left: parent.left

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "folder"
                    size: Theme.dp(11)
                    color: Theme.textMuted
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.workspaceName
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "•"
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.statusText
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: "#34D399"
                    font.weight: Font.Medium
                }
            }
        }
    }

    // ── Right Action Icons ──────────────────────────────────────────────
    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.dp(Theme.spacingXs)

        IconButton {
            iconName: "search"
            variant: "plain"
            buttonSize: Theme.dp(36)
            iconSize: Theme.dp(18)
            tint: Theme.textSecondary
            onClicked: root.searchClicked()
        }
    }
}
