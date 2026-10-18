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
    property bool isPathEditable: false
    property bool isRecursiveSearch: false

    signal upClicked()
    signal storageRootsClicked()
    signal storagePickerRequested()
    signal searchChanged(string text)
    signal searchFilterChanged(string filterText)
    signal recursiveSearchRequested(string query)
    signal clearSearchRequested()
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
                    border.color: root.isPathEditable ? Theme.borderFocus : Theme.border
                    border.width: 1
                    clip: true

                    // 1. Direct editable path input (Index FM PathBar style)
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(8)
                        anchors.rightMargin: Theme.dp(6)
                        spacing: Theme.dp(6)
                        visible: root.isPathEditable

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "folder"
                            size: Theme.dp(16)
                            color: Theme.accentInk
                        }

                        TextInput {
                            id: directPathInput
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(16) - Theme.dp(6) - Theme.dp(56)
                            text: root.currentPath
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            color: Theme.textPrimary
                            clip: true
                            selectByMouse: true
                            onAccepted: {
                                if (text.trim().length > 0) {
                                    root.pathSelected(text.trim())
                                }
                                root.isPathEditable = false
                            }
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "check"
                            buttonSize: Theme.dp(26)
                            iconSize: Theme.dp(13)
                            onClicked: {
                                if (directPathInput.text.trim().length > 0) {
                                    root.pathSelected(directPathInput.text.trim())
                                }
                                root.isPathEditable = false
                            }
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "close"
                            buttonSize: Theme.dp(26)
                            iconSize: Theme.dp(13)
                            onClicked: root.isPathEditable = false
                        }
                    }

                    // 2. Interactive Segmented Breadcrumbs
                    Flickable {
                        id: breadcrumbFlickable
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(8)
                        anchors.rightMargin: Theme.dp(32)
                        contentWidth: breadcrumbRow.implicitWidth + Theme.dp(16)
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick
                        clip: true
                        visible: !root.isPathEditable

                        onContentWidthChanged: {
                            if (contentWidth > width) {
                                contentX = contentWidth - width
                            } else {
                                contentX = 0
                            }
                        }

                        Row {
                            id: breadcrumbRow
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.dp(3)

                            // Storage root drive button
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                height: Theme.dp(28)
                                width: Theme.dp(28)
                                radius: Theme.radiusSm
                                color: rootMouse.pressed ? Theme.surface3 : "transparent"

                                Icon {
                                    anchors.centerIn: parent
                                    name: "drive"
                                    size: Theme.dp(14)
                                    color: Theme.accentInk
                                }

                                MouseArea {
                                    id: rootMouse
                                    anchors.fill: parent
                                    onClicked: {
                                        root.storageRootsClicked()
                                        root.storagePickerRequested()
                                    }
                                }
                            }

                            Repeater {
                                model: {
                                    var p = root.currentPath || ""
                                    if (!p || p === "/") return [{ name: "/", path: "/", isLast: true }]
                                    var clean = p.replace(/\\/g, "/")
                                    var parts = clean.split("/").filter(function(s) { return s.length > 0 })
                                    var out = []
                                    var accum = ""
                                    for (var i = 0; i < parts.length; ++i) {
                                        accum += "/" + parts[i]
                                        out.push({
                                            name: parts[i],
                                            path: accum,
                                            isLast: (i === parts.length - 1)
                                        })
                                    }
                                    return out
                                }

                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.dp(2)

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "/"
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        font.weight: Font.Bold
                                        color: Theme.textMuted
                                    }

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: Theme.dp(28)
                                        width: segLabel.implicitWidth + Theme.dp(12)
                                        radius: Theme.radiusSm
                                        color: segMouse.pressed ? Theme.surface3 : (modelData.isLast ? Theme.alpha(Theme.accentEnd, 0.15) : "transparent")
                                        border.color: modelData.isLast ? Theme.alpha(Theme.accentEnd, 0.35) : "transparent"
                                        border.width: modelData.isLast ? 1 : 0

                                        Text {
                                            id: segLabel
                                            anchors.centerIn: parent
                                            text: modelData.name
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            font.weight: modelData.isLast ? Font.Bold : Font.Normal
                                            color: modelData.isLast ? Theme.accentInk : Theme.textPrimary
                                        }

                                        MouseArea {
                                            id: segMouse
                                            anchors.fill: parent
                                            onClicked: {
                                                root.pathSelected(modelData.path)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Toggle Path Edit button (Index FM style)
                    IconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.dp(4)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !root.isPathEditable
                        iconName: "pencil"
                        buttonSize: Theme.dp(26)
                        iconSize: Theme.dp(12)
                        onClicked: {
                            directPathInput.text = root.currentPath
                            root.isPathEditable = true
                        }
                    }
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
                    width: parent.width - Theme.dp(18) - Theme.dp(Theme.spacingSm) * 3
                            - (clearButton.visible ? Theme.dp(24) : 0) - deepSearchBtn.width
                    height: parent.height

                    Timer {
                        id: searchDebounceTimer
                        interval: 180
                        repeat: false
                        onTriggered: {
                            if (root.isRecursiveSearch) {
                                root.recursiveSearchRequested(searchField.text.trim())
                            } else {
                                root.searchFilterChanged(searchField.text.trim())
                            }
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
                            text: root.isRecursiveSearch ? qsTr("Deep recursive search…") : qsTr("Search in this folder…")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            visible: !searchField.text && !searchField.activeFocus
                        }

                        onTextChanged: {
                            root.searchChanged(text)
                            if (text.trim().length === 0) {
                                searchDebounceTimer.stop()
                                root.clearSearchRequested()
                                root.searchFilterChanged("")
                            } else {
                                searchDebounceTimer.restart()
                            }
                        }
                    }
                }

                // Deep Recursive Search toggle (Index FM & HyprFM style)
                Rectangle {
                    id: deepSearchBtn
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.dp(26)
                    width: deepSearchRow.implicitWidth + Theme.dp(12)
                    radius: Theme.radiusSm
                    color: root.isRecursiveSearch ? Theme.alpha(Theme.accentInk, 0.22) : Theme.surface2
                    border.color: root.isRecursiveSearch ? Theme.accentInk : Theme.borderSubtle
                    border.width: 1

                    Row {
                        id: deepSearchRow
                        anchors.centerIn: parent
                        spacing: Theme.dp(4)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "layers"
                            size: Theme.dp(12)
                            color: root.isRecursiveSearch ? Theme.accentInk : Theme.textMuted
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Deep")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: root.isRecursiveSearch ? Font.Bold : Font.Normal
                            color: root.isRecursiveSearch ? Theme.accentInk : Theme.textMuted
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.isRecursiveSearch = !root.isRecursiveSearch
                            if (searchField.text.trim().length > 0) {
                                if (root.isRecursiveSearch) {
                                    root.recursiveSearchRequested(searchField.text.trim())
                                } else {
                                    root.searchFilterChanged(searchField.text.trim())
                                }
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
                        root.clearSearchRequested()
                        root.searchFilterChanged("")
                    }
                }
            }
        }
    }
}
