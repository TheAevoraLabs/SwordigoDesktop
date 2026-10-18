import QtQuick
import QtQuick.Controls
import ".."

// Index FM style Places and Storage sidebar drawer for Ruby Touch
Rectangle {
    id: root

    anchors.fill: parent
    color: Theme.alpha("#000000", 0.70)
    visible: opacity > 0
    opacity: 0
    z: 220

    signal placeSelected(string path)

    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    function open() {
        opacity = 1
    }

    function close() {
        opacity = 0
    }

    // Side Drawer Panel
    Rectangle {
        id: drawerPanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: Math.min(parent.width * 0.85, Theme.dp(340))
        color: Theme.surface0
        border.color: Theme.borderSubtle
        border.width: 1

        x: root.opacity > 0 ? 0 : -width
        Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

        Column {
            anchors.fill: parent
            spacing: 0

            // Header
            Item {
                width: parent.width
                height: Theme.dp(64)

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                    anchors.rightMargin: Theme.dp(Theme.spacingMd)
                    spacing: Theme.dp(Theme.spacingMd)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(36)
                        height: Theme.dp(36)
                        radius: Theme.dp(18)
                        color: Theme.alpha(Theme.accentInk, 0.16)

                        Icon {
                            anchors.centerIn: parent
                            name: "drive"
                            size: Theme.dp(18)
                            color: Theme.accentInk
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(36) - Theme.dp(36) - Theme.dp(Theme.spacingMd) * 2

                        Text {
                            text: qsTr("Places & Storage")
                            font.pixelSize: Theme.dp(Theme.fontLg)
                            font.weight: Font.Bold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Index Fast Access Hub")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                        }
                    }

                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "x"
                        buttonSize: Theme.dp(36)
                        iconSize: Theme.dp(18)
                        onClicked: root.close()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderSubtle
                }
            }

            // Storage Overview Card (Index FM DirInfo)
            Rectangle {
                width: parent.width - Theme.dp(Theme.spacingMd) * 2
                anchors.horizontalCenter: parent.horizontalCenter
                height: Theme.dp(78)
                radius: Theme.radiusMd
                color: Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(6)

                    Item {
                        width: parent.width
                        height: Theme.dp(18)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Device Storage")
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: (typeof rubyFileModel !== "undefined" && rubyFileModel) ? (rubyFileModel.availableStorageStr + " free") : ""
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.accentInk
                        }
                    }

                    // Storage visual bar
                    Rectangle {
                        width: parent.width
                        height: Theme.dp(6)
                        radius: Theme.dp(3)
                        color: Theme.surface3

                        Rectangle {
                            height: parent.height
                            radius: Theme.dp(3)
                            width: (typeof rubyFileModel !== "undefined" && rubyFileModel) ? Math.max(Theme.dp(6), parent.width * rubyFileModel.storageUsagePercent) : Theme.dp(6)
                            color: Theme.accentInk
                        }
                    }

                    Text {
                        text: (typeof rubyFileModel !== "undefined" && rubyFileModel) ? (qsTr("Total: ") + rubyFileModel.totalStorageStr) : ""
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        color: Theme.textMuted
                    }
                }
            }

            Item { width: parent.width; height: Theme.dp(Theme.spacingSm) }

            // Scrollable Places List
            Flickable {
                width: parent.width
                height: parent.height - Theme.dp(64) - Theme.dp(78) - Theme.dp(Theme.spacingSm)
                contentHeight: placesColumn.implicitHeight + Theme.dp(Theme.spacingLg)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: placesColumn
                    width: parent.width
                    spacing: Theme.dp(2)

                    readonly property var defaultPlaces: [
                        { label: qsTr("Internal Storage"), path: "/storage/emulated/0", icon: "drive", accent: Theme.accentInk },
                        { label: qsTr("Downloads"),        path: "/storage/emulated/0/Download", icon: "download", accent: Theme.colorArchive },
                        { label: qsTr("Documents"),        path: "/storage/emulated/0/Documents", icon: "rule", accent: Theme.colorCode },
                        { label: qsTr("Pictures & Media"), path: "/storage/emulated/0/Pictures", icon: "image", accent: Theme.colorTexture },
                        { label: qsTr("Screenshots"),      path: "/storage/emulated/0/DCIM/Screenshots", icon: "image", accent: Theme.colorTexture },
                        { label: qsTr("Ruby Workspace"),   path: "/storage/emulated/0/RubyTouch", icon: "cube", accent: Theme.colorModel },
                        { label: qsTr("Swordigo Data"),    path: "/storage/emulated/0/Android/data/com.touchfoo.swordigo", icon: "map", accent: Theme.colorScene },
                        { label: qsTr("Root Filesystem"),  path: "/", icon: "lock", accent: Theme.textMuted }
                    ]

                    Repeater {
                        model: placesColumn.defaultPlaces

                        Rectangle {
                            width: parent.width
                            height: Theme.dp(48)
                            color: placeMouse.pressed ? Theme.surface2 : "transparent"

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.dp(Theme.spacingLg)
                                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                                spacing: Theme.dp(Theme.spacingMd)

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.dp(32)
                                    height: Theme.dp(32)
                                    radius: Theme.radiusSm
                                    color: Theme.alpha(modelData.accent, 0.14)

                                    Icon {
                                        anchors.centerIn: parent
                                        name: modelData.icon
                                        size: Theme.dp(16)
                                        color: modelData.accent
                                    }
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.dp(32) - Theme.dp(Theme.spacingMd)
                                    spacing: 0

                                    Text {
                                        text: modelData.label
                                        font.pixelSize: Theme.dp(Theme.fontMd)
                                        font.weight: Font.Medium
                                        color: Theme.textPrimary
                                    }

                                    Text {
                                        text: modelData.path
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                        elide: Text.ElideMiddle
                                        width: parent.width
                                    }
                                }
                            }

                            MouseArea {
                                id: placeMouse
                                anchors.fill: parent
                                onClicked: {
                                    root.placeSelected(modelData.path)
                                    root.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Dismiss on tapping outside
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.close()
    }
}
