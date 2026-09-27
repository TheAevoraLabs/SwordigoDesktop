import QtQuick
import QtQuick.Controls
import ".."

// Files-tab header: storage picker + current location + up + search.
// Contract preserved: currentPath / currentFolder / searchVisible /
// searchText(alias) and upClicked, storageRootsClicked,
// storagePickerRequested, searchChanged, searchFilterChanged, pathSelected.
Rectangle {
    id: root

    height: searchVisible ? Theme.dp(106) : Theme.dp(56)
    color: Theme.surface0

    property string currentPath: ""
    property string currentFolder: ""
    property bool searchVisible: true
    property alias searchText: searchField.text

    signal upClicked()
    signal storageRootsClicked()
    signal storagePickerRequested()
    signal searchChanged(string text)
    signal searchFilterChanged(string filterText)
    signal pathSelected(string target)

    Behavior on height { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.borderSubtle
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.topMargin: Theme.dp(Theme.spacingSm)
        spacing: Theme.dp(Theme.spacingSm)

        // ── Row 1: storage | location | up ─────────────────────────────────
        Row {
            width: parent.width
            height: Theme.dp(40)
            spacing: Theme.dp(Theme.spacingSm)

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "drive"
                variant: "soft"
                buttonSize: Theme.dp(40)
                iconSize: Theme.dp(20)
                square: true
                onClicked: {
                    root.storageRootsClicked()
                    root.storagePickerRequested()
                }
            }

            Item {
                id: locationPill
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - Theme.dp(40) - Theme.dp(40) - Theme.dp(Theme.spacingSm) * 2 - ((typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.isResourceRoot) ? Theme.dp(40) + Theme.dp(Theme.spacingSm) : 0)
                height: Theme.dp(40)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: Theme.surface1
                    border.color: Theme.border
                    border.width: 1
                    clip: true

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.dp(Theme.spacingMd)
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.dp(Theme.spacingSm)
                        spacing: Theme.dp(Theme.spacingSm)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "folder"
                            size: Theme.dp(Theme.iconSm)
                            color: Theme.colorFolder
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(18) - Theme.dp(Theme.spacingSm)
                            spacing: 0

                            Text {
                                width: parent.width
                                text: root.currentFolder.length > 0 ? root.currentFolder : "/"
                                font.pixelSize: Theme.dp(Theme.fontMd)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                                elide: Text.ElideMiddle
                            }

                            Text {
                                width: parent.width
                                text: root.currentPath
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.pathSelected(root.currentPath)
                }
            }

            IconButton {
                id: playEngineBtn
                visible: typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.isResourceRoot && rubyFileModel.playInGameAvailable
                anchors.verticalCenter: parent.verticalCenter
                iconName: "play"
                variant: "soft"
                tint: "#10B981"
                plate: Theme.alpha("#10B981", 0.18)
                plateEdge: Theme.alpha("#10B981", 0.40)
                buttonSize: Theme.dp(40)
                iconSize: Theme.dp(20)
                square: true
                onClicked: {
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                        rubyFileModel.launchGame()
                    }
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "arrow-up"
                variant: "soft"
                buttonSize: Theme.dp(40)
                iconSize: Theme.dp(20)
                square: true
                enabled: root.currentPath !== "/"
                onClicked: root.upClicked()
            }
        }

        // ── Row 2: search ──────────────────────────────────────────────────
        Rectangle {
            id: searchContainer
            width: parent.width
            height: Theme.dp(40)
            radius: Theme.radiusMd
            color: Theme.surface1
            border.color: searchField.activeFocus ? Theme.borderFocus : Theme.border
            border.width: 1
            visible: root.searchVisible

            Row {
                anchors.fill: parent
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.rightMargin: Theme.dp(Theme.spacingSm)
                spacing: Theme.dp(Theme.spacingSm)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: Theme.dp(Theme.iconSm)
                    color: searchField.activeFocus ? Theme.accentInk : Theme.textMuted
                }

                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.dp(18) - Theme.dp(Theme.spacingSm) * 2
                            - (clearButton.visible ? Theme.dp(24) : 0)
                    height: parent.height

                    Timer {
                        id: searchDebounceTimer
                        interval: 180
                        repeat: false
                        onTriggered: {
                            root.searchFilterChanged(searchField.text)
                        }
                    }

                    TextInput {
                        id: searchField
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(Theme.fontMd)
                        clip: true
                        selectByMouse: true

                        Text {
                            text: qsTr("Search files & folders…")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            visible: !searchField.text && !searchField.activeFocus
                        }

                        onTextChanged: {
                            root.searchChanged(text)
                            if (text.trim().length === 0) {
                                searchDebounceTimer.stop()
                                root.searchFilterChanged("")
                            } else {
                                searchDebounceTimer.restart()
                            }
                        }
                    }
                }

                IconButton {
                    id: clearButton
                    anchors.verticalCenter: parent.verticalCenter
                    visible: searchField.text.length > 0
                    iconName: "close"
                    buttonSize: Theme.dp(26)
                    iconSize: Theme.dp(14)
                    onClicked: {
                        searchDebounceTimer.stop()
                        searchField.text = ""
                        root.searchFilterChanged("")
                    }
                }
            }
        }
    }
}
