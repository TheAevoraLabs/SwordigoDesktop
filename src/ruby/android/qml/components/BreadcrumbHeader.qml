import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// Files-tab header: storage picker + path + search + overflow.
//
// The header is deliberately only two rows tall. Row 1 is the breadcrumb pill
// with the search toggle and the overflow menu; row 2 is a search field that
// only exists while the user is actually searching. Android mount paths are
// aliased ("/storage/emulated/0" -> "Internal Storage") so the bar never reads
// like a raw Linux path.
//
// Contract preserved: currentPath / currentFolder / searchVisible /
// searchText(alias) and upClicked, storageRootsClicked,
// storagePickerRequested, searchChanged, searchFilterChanged, pathSelected.
Rectangle {
    id: root

    height: searchVisible ? Theme.dp(102) : Theme.dp(56)
    color: Theme.surface0

    property string currentPath: ""
    property string currentFolder: ""
    property bool searchVisible: false
    property alias searchText: searchField.text
    property bool isPathEditable: false
    property bool isRecursiveSearch: false

    signal upClicked()
    signal storageRootsClicked()
    signal storagePickerRequested()
    signal overflowRequested()
    signal searchChanged(string text)
    signal searchFilterChanged(string filterText)
    signal recursiveSearchRequested(string query)
    signal clearSearchRequested()
    signal pathSelected(string target)

    Behavior on height { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }

    /// Turn a filesystem path into display crumbs. Android's primary shared
    /// volume is shown as "Internal Storage" instead of storage/emulated/0.
    function buildBreadcrumbs(path) {
        var p = path || "/"
        var volume = "/storage/emulated/0"
        var out = []
        var i

        if (p === volume || p.indexOf(volume + "/") === 0) {
            out.push({
                name: qsTr("Internal Storage"),
                path: volume,
                isLast: p === volume,
                isStorage: true
            })
            var rest = p.substring(volume.length).split("/")
            var parts = []
            for (i = 0; i < rest.length; ++i) {
                if (rest[i].length > 0) parts.push(rest[i])
            }
            var accum = volume
            for (i = 0; i < parts.length; ++i) {
                accum += "/" + parts[i]
                out.push({
                    name: parts[i],
                    path: accum,
                    isLast: i === parts.length - 1,
                    isStorage: false
                })
            }
            return out
        }

        if (p === "/") {
            return [{ name: qsTr("Filesystem Root"), path: "/", isLast: true, isStorage: true }]
        }

        var clean = p.replace(/\\/g, "/")
        var bits = clean.split("/")
        var segs = []
        for (i = 0; i < bits.length; ++i) {
            if (bits[i].length > 0) segs.push(bits[i])
        }
        var acc = ""
        for (i = 0; i < segs.length; ++i) {
            acc += "/" + segs[i]
            out.push({
                name: segs[i],
                path: acc,
                isLast: i === segs.length - 1,
                isStorage: false
            })
        }
        return out
    }

    function beginPathEdit() {
        directPathInput.text = root.currentPath
        root.isPathEditable = true
        directPathInput.forceActiveFocus()
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.topMargin: Theme.dp(Theme.spacingSm)
        anchors.bottomMargin: Theme.dp(Theme.spacingSm)
        spacing: Theme.dp(Theme.spacingSm)

        // ── Row 1: storage | breadcrumb | search | overflow ────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.dp(40)
            spacing: Theme.dp(4)

            IconButton {
                Layout.preferredWidth: Theme.dp(38)
                Layout.preferredHeight: Theme.dp(38)
                Layout.alignment: Qt.AlignVCenter
                iconName: "drive"
                tint: Theme.accentInk
                plate: "transparent"
                plateEdge: "transparent"
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(20)
                square: true
                onClicked: {
                    root.storageRootsClicked()
                    root.storagePickerRequested()
                }
            }

            Rectangle {
                id: locationPill
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(38)
                radius: Theme.radiusMd
                color: Theme.surface1
                border.color: root.isPathEditable ? Theme.borderFocus : "transparent"
                border.width: root.isPathEditable ? 1 : 0
                clip: true

                // 1. Direct editable path input
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(10)
                    anchors.rightMargin: Theme.dp(4)
                    spacing: Theme.dp(4)
                    visible: root.isPathEditable

                    Icon {
                        Layout.alignment: Qt.AlignVCenter
                        name: "folder"
                        size: Theme.dp(15)
                        color: Theme.accentInk
                    }

                    TextInput {
                        id: directPathInput
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        verticalAlignment: TextInput.AlignVCenter
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
                        Layout.preferredWidth: Theme.dp(26)
                        Layout.preferredHeight: Theme.dp(26)
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
                        Layout.preferredWidth: Theme.dp(26)
                        Layout.preferredHeight: Theme.dp(26)
                        iconName: "close"
                        buttonSize: Theme.dp(26)
                        iconSize: Theme.dp(13)
                        onClicked: root.isPathEditable = false
                    }
                }

                // 2. Interactive segmented breadcrumbs
                Flickable {
                    id: breadcrumbFlickable
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(10)
                    anchors.rightMargin: Theme.dp(28)
                    contentWidth: breadcrumbRow.implicitWidth
                    contentHeight: height
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    clip: true
                    visible: !root.isPathEditable

                    onContentWidthChanged: {
                        contentX = contentWidth > width ? contentWidth - width : 0
                    }
                    onWidthChanged: {
                        contentX = contentWidth > width ? contentWidth - width : 0
                    }

                    Row {
                        id: breadcrumbRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.dp(1)

                        Repeater {
                            model: root.buildBreadcrumbs(root.currentPath)

                            Row {
                                id: crumbSegment
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.dp(1)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: index > 0
                                    name: "chevron-right"
                                    size: Theme.dp(10)
                                    color: Theme.textMuted
                                }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: Theme.dp(28)
                                    width: crumbContent.implicitWidth + Theme.dp(14)
                                    radius: Theme.radiusSm
                                    color: crumbMouse.pressed
                                           ? Theme.surface3
                                           : (modelData.isLast ? Theme.alpha(Theme.accentInk, 0.14) : "transparent")

                                    Row {
                                        id: crumbContent
                                        anchors.centerIn: parent
                                        spacing: Theme.dp(4)

                                        Icon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: modelData.isStorage
                                            name: "drive"
                                            size: Theme.dp(12)
                                            color: Theme.accentInk
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            font.weight: modelData.isLast ? Font.DemiBold : Font.Normal
                                            color: modelData.isLast ? Theme.accentInk : Theme.textSecondary
                                        }
                                    }

                                    MouseArea {
                                        id: crumbMouse
                                        anchors.fill: parent
                                        onClicked: root.pathSelected(modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }

                // 3. Optional direct-path editing stays tucked into the pill
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.dp(3)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.isPathEditable
                    iconName: "pencil"
                    tint: Theme.textMuted
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(24)
                    iconSize: Theme.dp(11)
                    onClicked: root.beginPathEdit()
                }
            }

            // Only present when the current folder is a real Swordigo
            // resource root; launching the in-engine runner is contextual.
            IconButton {
                Layout.preferredWidth: Theme.dp(38)
                Layout.preferredHeight: Theme.dp(38)
                Layout.alignment: Qt.AlignVCenter
                visible: typeof rubyFileModel !== "undefined" && rubyFileModel
                         && rubyFileModel.isResourceRoot && rubyFileModel.playInGameAvailable
                iconName: "play"
                tint: "#10B981"
                plate: Theme.alpha("#10B981", 0.14)
                plateEdge: "transparent"
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: rubyFileModel.launchGame()
            }

            IconButton {
                Layout.preferredWidth: Theme.dp(38)
                Layout.preferredHeight: Theme.dp(38)
                Layout.alignment: Qt.AlignVCenter
                iconName: "search"
                tint: (root.searchVisible || searchField.text.length > 0) ? Theme.accentInk : Theme.textSecondary
                plate: root.searchVisible ? Theme.alpha(Theme.accentInk, 0.14) : "transparent"
                plateEdge: "transparent"
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: root.searchVisible = !root.searchVisible
            }

            IconButton {
                id: overflowButton
                Layout.preferredWidth: Theme.dp(38)
                Layout.preferredHeight: Theme.dp(38)
                Layout.alignment: Qt.AlignVCenter
                iconName: "more-vertical"
                tint: Theme.textSecondary
                plate: "transparent"
                plateEdge: "transparent"
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: root.overflowRequested()
            }
        }

        // ── Row 2: search (collapsible) ────────────────────────────────────
        Rectangle {
            id: searchContainer
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.dp(38)
            visible: root.searchVisible
            radius: Theme.radiusMd
            color: Theme.surface1
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.rightMargin: Theme.dp(Theme.spacingXs)
                spacing: Theme.dp(Theme.spacingXs)

                Icon {
                    Layout.alignment: Qt.AlignVCenter
                    name: "search"
                    size: Theme.dp(Theme.iconSm)
                    color: searchField.activeFocus ? Theme.accentInk : Theme.textMuted
                }

                TextInput {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.textPrimary
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    clip: true
                    selectByMouse: true

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.isRecursiveSearch ? qsTr("Deep recursive search…") : qsTr("Search in this folder…")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(Theme.fontMd)
                        visible: !searchField.text && !searchField.activeFocus
                    }

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

                // Deep recursive search toggle
                Rectangle {
                    id: deepSearchBtn
                    Layout.preferredWidth: deepSearchRow.implicitWidth + Theme.dp(14)
                    Layout.preferredHeight: Theme.dp(26)
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSm
                    color: root.isRecursiveSearch ? Theme.alpha(Theme.accentInk, 0.20) : Theme.surface2
                    border.width: 0

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
                            font.weight: root.isRecursiveSearch ? Font.DemiBold : Font.Normal
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
                    Layout.preferredWidth: Theme.dp(26)
                    Layout.preferredHeight: Theme.dp(26)
                    visible: searchField.text.length > 0
                    iconName: "close"
                    plate: "transparent"
                    plateEdge: "transparent"
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

    onSearchVisibleChanged: {
        if (searchVisible) {
            searchField.forceActiveFocus()
        }
    }
}
