import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    property alias currentPath: breadcrumbs.currentPath
    property bool isSelectionMode: false
    property string viewMode: "list"

    signal fileOpened(string path, string fileType)
    signal modelConvertRequested(string path)

    readonly property var categories: [
        { label: qsTr("All"),       cat: "all",      icon: "grid"         },
        { label: qsTr("Scenes"),    cat: "scene",    icon: "map"          },
        { label: qsTr("Templates"), cat: "scl",      icon: "layers"       },
        { label: qsTr("Models"),    cat: "models",   icon: "cube"         },
        { label: qsTr("Archives"),  cat: "archives", icon: "archive"      },
        { label: qsTr("Scripts"),   cat: "lua",      icon: "braces"       },
        { label: qsTr("Audio"),     cat: "audio",    icon: "volume"       }
    ]

    Column {
        anchors.fill: parent

        // ── Sticky breadcrumb + search ─────────────────────────────────────
        BreadcrumbHeader {
            id: breadcrumbs
            width: parent.width
            currentPath: rubyFileModel.currentPath
            currentFolder: rubyFileModel.folderName

            onPathSelected: (target) => {
                rubyFileModel.navigateTo(target)
            }
            onUpClicked: {
                rubyFileModel.navigateUp()
            }
            onStorageRootsClicked: {
                placesDrawer.open()
            }
            onStoragePickerRequested: {
                placesDrawer.open()
            }
            onSearchFilterChanged: (filterText) => {
                rubyFileModel.setFilter(filterText)
            }
            onRecursiveSearchRequested: (query) => {
                rubyFileModel.searchRecursive(query)
            }
            onClearSearchRequested: {
                rubyFileModel.clearSearch()
            }
        }

        // ── Index FM Storage & Deep Search Status Strip ────────────────────
        Rectangle {
            width: parent.width
            height: Theme.dp(24)
            color: Theme.surface0

            // Normal storage & folder status
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(Theme.spacingSm)
                visible: !rubyFileModel.isSearchMode

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "drive"
                    size: Theme.dp(11)
                    color: Theme.accentInk
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (rubyFileModel.availableStorageStr ? (rubyFileModel.availableStorageStr + " free") : "") + "  ·  " + rubyFileModel.statusText
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                }
            }

            // Deep Search results strip
            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.right: parent.right
                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(Theme.spacingSm)
                visible: rubyFileModel.isSearchMode

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "layers"
                    size: Theme.dp(12)
                    color: Theme.accentInk
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: rubyFileModel.isSearching
                          ? qsTr("Searching subdirectories…")
                          : qsTr("Deep Search: Found %1 matches").arg(rubyFileModel.searchResultCount)
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.DemiBold
                    color: Theme.accentInk
                }

                Item { width: Theme.dp(10); height: 1 }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Exit Search")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    font.weight: Font.Bold
                    color: Theme.colorErrorBright
                    visible: !rubyFileModel.isSearching

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            breadcrumbs.searchText = ""
                            rubyFileModel.clearSearch()
                        }
                    }
                }
            }
        }

        // ── Category chips + view controls ─────────────────────────────────
        Item {
            width: parent.width
            height: Theme.dp(48)

            ListView {
                id: chipRow
                anchors.left: parent.left
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.right: viewControls.left
                anchors.rightMargin: Theme.dp(Theme.spacingSm)
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(34)
                orientation: ListView.Horizontal
                spacing: Theme.dp(Theme.spacingSm)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                model: root.categories

                delegate: Chip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    iconName: modelData.icon
                    accent: Theme.colorForType(modelData.cat === "all" ? "generic" : modelData.cat)
                    selected: rubyFileModel.category === modelData.cat
                    onClicked: rubyFileModel.setCategory(modelData.cat)
                }
            }

            Row {
                id: viewControls
                anchors.right: parent.right
                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(Theme.spacingXs)

                // Multi-select toggle button (Index FM SelectionBar style)
                IconButton {
                    iconName: root.isSelectionMode ? "check-square" : "check"
                    tint: root.isSelectionMode ? Theme.accentInk : Theme.textMuted
                    plate: root.isSelectionMode ? Theme.alpha(Theme.accentInk, 0.20) : "transparent"
                    plateEdge: root.isSelectionMode ? Theme.alpha(Theme.accentInk, 0.40) : "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    square: true
                    onClicked: {
                        root.isSelectionMode = !root.isSelectionMode
                        if (!root.isSelectionMode) {
                            rubyFileModel.clearSelection()
                        }
                    }
                }

                // Grid / List toggle button (Index FM ViewMode style)
                IconButton {
                    iconName: root.viewMode === "grid" ? "list" : "grid"
                    tint: root.viewMode === "grid" ? Theme.accentInk : Theme.textMuted
                    plate: root.viewMode === "grid" ? Theme.alpha(Theme.accentInk, 0.20) : "transparent"
                    plateEdge: root.viewMode === "grid" ? Theme.alpha(Theme.accentInk, 0.40) : "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    square: true
                    onClicked: {
                        root.viewMode = (root.viewMode === "grid" ? "list" : "grid")
                    }
                }

                IconButton {
                    iconName: "sort"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(18)
                    square: true
                    onClicked: sortMenu.popup(this)
                }

                IconButton {
                    iconName: rubyFileModel.showHidden ? "eye" : "eye-off"
                    tint: rubyFileModel.showHidden ? Theme.accentInk : Theme.textMuted
                    plate: rubyFileModel.showHidden ? Theme.alpha(Theme.accentInk, 0.14) : "transparent"
                    plateEdge: rubyFileModel.showHidden ? Theme.alpha(Theme.accentInk, 0.28) : "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(18)
                    square: true
                    onClicked: rubyFileModel.setShowHidden(!rubyFileModel.showHidden)
                }
            }
        }

        // ── Smooth non-blocking folder scan & search progress bar ─────────
        Item {
            width: parent.width
            height: Theme.dp(3)
            visible: rubyFileModel.isLoading || rubyFileModel.isSearching
            clip: true

            Rectangle {
                id: fileLoadingBar
                width: parent.width * 0.35
                height: parent.height
                radius: Theme.dp(1.5)
                color: Theme.accentInk

                SequentialAnimation on x {
                    running: rubyFileModel.isLoading || rubyFileModel.isSearching
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: -fileLoadingBar.width
                        to: fileLoadingBar.parent.width
                        duration: 850
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }

        // ── Dual View Container (List View & Grid View - Index FM style) ──
        Item {
            id: viewContainer
            width: parent.width
            height: parent.height - breadcrumbs.height - Theme.dp(24) - Theme.dp(48) - Theme.dp(3) - statusBar.height
            clip: true

            // 1. Detailed List View
            ListView {
                id: fileListView
                anchors.fill: parent
                visible: root.viewMode === "list"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                model: rubyFileModel

                delegate: FileRowDelegate {
                    width: fileListView.width
                    fileName: model.fileName != null ? model.fileName : ""
                    filePath: model.filePath != null ? model.filePath : ""
                    fileType: model.fileType != null ? model.fileType : ""
                    isDir: model.isDir === true
                    fileSizeStr: model.fileSizeStr != null ? model.fileSizeStr : ""
                    fileDateStr: model.fileDateStr != null ? model.fileDateStr : ""
                    isPinned: model.isPinned === true
                    isSelectionMode: root.isSelectionMode
                    isSelected: model.isSelected === true

                    function popContext() {
                        if (!model || !model.filePath) return
                        contextSheet.targetFilePath = model.filePath
                        contextSheet.targetFileName = model.fileName
                        contextSheet.targetIsDir = model.isDir
                        contextSheet.targetFileType = model.fileType
                        contextSheet.open(false)
                    }

                    onSelectionToggled: {
                        rubyFileModel.toggleItemSelected(model.filePath)
                    }

                    onRowClicked: {
                        if (!model || !model.filePath) return
                        if (model.isDir) {
                            rubyFileModel.navigateTo(model.filePath)
                        } else {
                            root.fileOpened(model.filePath, model.fileType)
                        }
                    }

                    onRowLongPressed: {
                        if (!model || !model.filePath) return
                        if (!root.isSelectionMode) {
                            root.isSelectionMode = true
                            rubyFileModel.setItemSelected(model.filePath, true)
                        } else {
                            rubyFileModel.toggleItemSelected(model.filePath)
                        }
                        if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                            if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                                androidContext.vibrate(25)
                            }
                        }
                    }

                    onActionRequested: (action) => {
                        if (action === "menu") popContext()
                    }

                    onDeleteClicked: {
                        deleteDialog.isBatch = false
                        deleteDialog.targetPath = model.filePath
                        deleteDialog.targetName = model.fileName
                        deleteDialog.open()
                    }

                    onRenameClicked: {
                        renameDialog.targetPath = model.filePath
                        renameDialog.targetName = model.fileName
                        renameDialog.initialText = model.fileName
                        renameDialog.open()
                    }

                    onPinToggled: {
                        rubyFileModel.togglePin(model.filePath)
                    }
                }
            }

            // 2. Card Grid View (Index FM style)
            GridView {
                id: fileGridView
                anchors.fill: parent
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                visible: root.viewMode === "grid"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                cellWidth: fileGridView.width > Theme.dp(460) ? (fileGridView.width / 3) : (fileGridView.width / 2)
                cellHeight: Theme.dp(140)
                model: rubyFileModel

                delegate: FileCardDelegate {
                    fileName: model.fileName != null ? model.fileName : ""
                    filePath: model.filePath != null ? model.filePath : ""
                    fileType: model.fileType != null ? model.fileType : ""
                    isDir: model.isDir === true
                    fileSizeStr: model.fileSizeStr != null ? model.fileSizeStr : ""
                    fileDateStr: model.fileDateStr != null ? model.fileDateStr : ""
                    isPinned: model.isPinned === true
                    isSelectionMode: root.isSelectionMode
                    isSelected: model.isSelected === true

                    function popContext() {
                        if (!model || !model.filePath) return
                        contextSheet.targetFilePath = model.filePath
                        contextSheet.targetFileName = model.fileName
                        contextSheet.targetIsDir = model.isDir
                        contextSheet.targetFileType = model.fileType
                        contextSheet.open(false)
                    }

                    onSelectionToggled: {
                        rubyFileModel.toggleItemSelected(model.filePath)
                    }

                    onCardClicked: {
                        if (!model || !model.filePath) return
                        if (model.isDir) {
                            rubyFileModel.navigateTo(model.filePath)
                        } else {
                            root.fileOpened(model.filePath, model.fileType)
                        }
                    }

                    onCardLongPressed: {
                        if (!model || !model.filePath) return
                        if (!root.isSelectionMode) {
                            root.isSelectionMode = true
                            rubyFileModel.setItemSelected(model.filePath, true)
                        } else {
                            rubyFileModel.toggleItemSelected(model.filePath)
                        }
                        if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                            if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                                androidContext.vibrate(25)
                            }
                        }
                    }

                    onActionRequested: (action) => {
                        if (action === "menu") popContext()
                    }

                    onPreviewRequested: {
                        if (!model || !model.filePath) return
                        filePreviewModal.openWith(model.filePath, model.fileName, model.fileType, model.isDir)
                    }

                    onPinToggled: {
                        if (!model || !model.filePath) return
                        rubyFileModel.togglePin(model.filePath)
                    }
                }
            }

            // Empty state
            Item {
                anchors.centerIn: parent
                visible: rubyFileModel.count === 0 && !rubyFileModel.isLoading && !rubyFileModel.isSearching
                width: parent.width * 0.7
                height: Theme.dp(140)

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.dp(Theme.spacingMd)

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.dp(56)
                        height: Theme.dp(56)
                        radius: Theme.radiusLg
                        color: Theme.surface2
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Icon {
                            anchors.centerIn: parent
                            name: "folder-open"
                            size: Theme.dp(28)
                            color: Theme.textMuted
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: rubyFileModel.isSearchMode ? qsTr("No search results") : qsTr("Nothing here")
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textSecondary
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: rubyFileModel.isSearchMode ? qsTr("No matching files found across subdirectories.") : qsTr("This folder is empty, or the filter hides everything in it.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ── Status bar (legacy wording) ────────────────────────────────────
        Rectangle {
            id: statusBar
            width: parent.width
            height: Theme.dp(30)
            color: Theme.surface0

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderSubtle
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                text: rubyFileModel.statusText
                font.pixelSize: Theme.dp(Theme.fontXs)
                color: Theme.textMuted
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(Theme.spacingXs)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "pin"
                    size: Theme.dp(11)
                    color: Theme.colorWarning
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("swipe rows for actions")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textDisabled
                }
            }
        }
    }

    // ── Sort menu (legacy option set, same order) ──────────────────────────
    Menu {
        id: sortMenu
        width: Theme.dp(220)

        MenuItem {
            text: qsTr("Name (A to Z)")
            onTriggered: { rubyFileModel.setSortMode("name"); rubyFileModel.setSortAscending(true) }
        }
        MenuItem {
            text: qsTr("Name (Z to A)")
            onTriggered: { rubyFileModel.setSortMode("name"); rubyFileModel.setSortAscending(false) }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Date (Newest First)")
            onTriggered: { rubyFileModel.setSortMode("date"); rubyFileModel.setSortAscending(false) }
        }
        MenuItem {
            text: qsTr("Date (Oldest First)")
            onTriggered: { rubyFileModel.setSortMode("date"); rubyFileModel.setSortAscending(true) }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Size (Largest First)")
            onTriggered: { rubyFileModel.setSortMode("size"); rubyFileModel.setSortAscending(false) }
        }
        MenuItem {
            text: qsTr("Type (.ext)")
            onTriggered: { rubyFileModel.setSortMode("type"); rubyFileModel.setSortAscending(true) }
        }
    }

    // ── OperationsBar (Batch / Single file quick action dock) ──────────────
    OperationsBar {
        id: operationsBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.bottomMargin: Theme.dp(16)
        z: 10

        selectedCount: root.isSelectionMode ? rubyFileModel.selectedCount : (operationsBar.targetPath.length > 0 ? 1 : 0)
        totalSizeStr: root.isSelectionMode ? rubyFileModel.getSelectedTotalBytesStr() : ""

        onCloseRequested: {
            root.isSelectionMode = false
            rubyFileModel.clearSelection()
            operationsBar.targetPath = ""
            operationsBar.targetFileName = ""
        }

        onZipRequested: (path) => {
            if (root.isSelectionMode && rubyFileModel.selectedCount > 1) {
                operationsBar.isBusy = true
                operationsBar.statusText = qsTr("Compressing %1 files…").arg(rubyFileModel.selectedCount)
                rubyFileModel.compressMultiple(rubyFileModel.getSelectedPaths())
                operationsBar.isBusy = false
                root.isSelectionMode = false
                rubyFileModel.clearSelection()
            } else {
                var p = path || operationsBar.targetPath
                if (!p) return
                operationsBar.isBusy = true
                operationsBar.statusText = qsTr("Compressing…")
                rubyFileModel.compressToZip(p)
                operationsBar.isBusy = false
                operationsBar.targetPath = ""
                operationsBar.targetFileName = ""
            }
        }

        onCopyRequested: (path) => {
            if (root.isSelectionMode && rubyFileModel.selectedCount > 1) {
                rubyFileModel.copyMultiple(rubyFileModel.getSelectedPaths())
                root.isSelectionMode = false
                rubyFileModel.clearSelection()
            } else {
                var p = path || operationsBar.targetPath
                if (!p) return
                rubyFileModel.copyItem(p)
                operationsBar.targetPath = ""
                operationsBar.targetFileName = ""
            }
        }

        onCutRequested: (path) => {
            if (root.isSelectionMode && rubyFileModel.selectedCount > 1) {
                rubyFileModel.cutMultiple(rubyFileModel.getSelectedPaths())
                root.isSelectionMode = false
                rubyFileModel.clearSelection()
            } else {
                var p = path || operationsBar.targetPath
                if (!p) return
                rubyFileModel.cutItem(p)
                operationsBar.targetPath = ""
                operationsBar.targetFileName = ""
            }
        }

        onDeleteRequested: (path) => {
            if (root.isSelectionMode && rubyFileModel.selectedCount > 1) {
                deleteDialog.isBatch = true
                deleteDialog.targetPaths = rubyFileModel.getSelectedPaths()
                deleteDialog.targetName = qsTr("%1 selected items").arg(rubyFileModel.selectedCount)
                deleteDialog.open()
            } else {
                var p = path || operationsBar.targetPath
                if (!p) return
                deleteDialog.isBatch = false
                deleteDialog.targetPath = p
                deleteDialog.targetName = operationsBar.targetFileName
                deleteDialog.open()
            }
        }
    }

    // ── Floating Action Button (Create) ────────────────────────────────────
    Rectangle {
        id: createFab
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.dp(Theme.spacingLg)
        anchors.bottomMargin: Theme.dp(44)
        width: Theme.dp(56)
        height: Theme.dp(56)
        radius: Theme.dp(28)
        color: fabMouseArea.pressed ? Theme.accentEnd : Theme.accentStart
        border.color: Theme.alpha(Theme.accentInk, 0.5)
        border.width: 1
        visible: !createSheet.isOpen && !operationsBar.active

        Icon {
            anchors.centerIn: parent
            name: "plus"
            size: Theme.dp(24)
            weight: 2.2
            color: Theme.onAccent
        }

        MouseArea {
            id: fabMouseArea
            anchors.fill: parent
            onClicked: createSheet.open(false)
        }
    }

    // ── Create New BottomSheet ─────────────────────────────────────────────
    BottomSheet {
        id: createSheet
        title: qsTr("Create New")
        peekHeight: Theme.dp(360)
        expandedHeight: Theme.dp(360)

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Theme.dp(Theme.spacingSm)

            // Option 1: New File Wizard
            Rectangle {
                width: parent.width
                height: Theme.dp(58)
                radius: Theme.radiusMd
                color: opt1Mouse.pressed ? Theme.surface2 : Theme.surface1
                border.color: opt1Mouse.pressed ? Theme.borderFocus : Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(12)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(38)
                        height: Theme.dp(38)
                        radius: Theme.dp(19)
                        color: Theme.accentSoft

                        Icon {
                            anchors.centerIn: parent
                            name: "sparkles"
                            size: Theme.dp(18)
                            color: Theme.accentInk
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(50)
                        spacing: Theme.dp(2)

                        Text {
                            text: qsTr("New File...")
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Create from 11 templates (Scenes, Lua, Shaders, JSON)")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    id: opt1Mouse
                    anchors.fill: parent
                    onClicked: {
                        createSheet.close()
                        root.openNewFileWizard("All", 0)
                    }
                }
            }

            // Option 2: New Folder
            Rectangle {
                width: parent.width
                height: Theme.dp(58)
                radius: Theme.radiusMd
                color: opt2Mouse.pressed ? Theme.surface2 : Theme.surface1
                border.color: opt2Mouse.pressed ? Theme.borderFocus : Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(12)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(38)
                        height: Theme.dp(38)
                        radius: Theme.dp(19)
                        color: Theme.alpha(Theme.colorFolder, 0.18)

                        Icon {
                            anchors.centerIn: parent
                            name: "folder-plus"
                            size: Theme.dp(18)
                            color: Theme.colorFolder
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(50)
                        spacing: Theme.dp(2)

                        Text {
                            text: qsTr("New Folder")
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Create a new directory in this location")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    id: opt2Mouse
                    anchors.fill: parent
                    onClicked: {
                        createSheet.close()
                        newFolderDialog.openWith("")
                    }
                }
            }

            // Option 3: Quick Swordigo Scene (.scene)
            Rectangle {
                width: parent.width
                height: Theme.dp(58)
                radius: Theme.radiusMd
                color: opt3Mouse.pressed ? Theme.surface2 : Theme.surface1
                border.color: opt3Mouse.pressed ? Theme.borderFocus : Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(12)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(38)
                        height: Theme.dp(38)
                        radius: Theme.dp(19)
                        color: Theme.alpha(Theme.colorScene, 0.18)

                        Icon {
                            anchors.centerIn: parent
                            name: "map"
                            size: Theme.dp(18)
                            color: Theme.colorScene
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(50)
                        spacing: Theme.dp(2)

                        Text {
                            text: qsTr("New Scene (.scene)")
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Swordigo 3D level with lights, background & platform")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    id: opt3Mouse
                    anchors.fill: parent
                    onClicked: {
                        createSheet.close()
                        root.openNewFileWizard("Swordigo", 0)
                    }
                }
            }

            // Option 4: Quick Lua Script (.lua)
            Rectangle {
                width: parent.width
                height: Theme.dp(58)
                radius: Theme.radiusMd
                color: opt4Mouse.pressed ? Theme.surface2 : Theme.surface1
                border.color: opt4Mouse.pressed ? Theme.borderFocus : Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(12)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(38)
                        height: Theme.dp(38)
                        radius: Theme.dp(19)
                        color: Theme.alpha(Theme.colorCode, 0.18)

                        Icon {
                            anchors.centerIn: parent
                            name: "braces"
                            size: Theme.dp(18)
                            color: Theme.colorCode
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(50)
                        spacing: Theme.dp(2)

                        Text {
                            text: qsTr("New Script (.lua)")
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: qsTr("Game logic script chunk with object callbacks")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    id: opt4Mouse
                    anchors.fill: parent
                    onClicked: {
                        createSheet.close()
                        root.openNewFileWizard("Scripting", 0)
                    }
                }
            }
        }
    }

    // ── Storage picker ─────────────────────────────────────────────────────
    BottomSheet {
        id: storageSheet
        title: qsTr("Select Storage Location")
        peekHeight: Theme.dp(260)

        ListView {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            clip: true
            spacing: Theme.dp(Theme.spacingSm)

            model: [
                { label: qsTr("Internal Shared Storage"), path: "/storage/emulated/0", icon: "drive" },
                { label: qsTr("Downloads"),               path: "/storage/emulated/0/Download", icon: "download" },
                { label: qsTr("Documents"),               path: "/storage/emulated/0/Documents", icon: "rule" },
                { label: qsTr("Pictures"),                path: "/storage/emulated/0/Pictures", icon: "image" },
                { label: qsTr("App Sandbox"),             path: "", icon: "lock" },
                { label: qsTr("Filesystem Root"),         path: "/", icon: "drive" }
            ]

            delegate: Rectangle {
                width: ListView.view.width
                height: Theme.dp(56)
                radius: Theme.radiusCard
                color: storageArea.pressed ? Theme.surface2 : Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1
                visible: modelData.path.length > 0

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(Theme.spacingLg)
                    spacing: Theme.dp(Theme.spacingMd)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.icon
                        size: Theme.dp(Theme.iconMd)
                        color: Theme.accentInk
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(38) - Theme.dp(Theme.spacingMd)
                        spacing: 0

                        Text {
                            text: modelData.label
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            font.weight: Font.Medium
                            color: Theme.textPrimary
                        }

                        Text {
                            text: modelData.path
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            color: Theme.textMuted
                            elide: Text.ElideMiddle
                            width: parent.width
                        }
                    }
                }

                MouseArea {
                    id: storageArea
                    anchors.fill: parent
                    onClicked: {
                        rubyFileModel.navigateTo(modelData.path)
                        storageSheet.close()
                    }
                }
            }
        }
    }

    // ── Per-item context actions ───────────────────────────────────────────
    BottomSheet {
        id: contextSheet
        property string targetFilePath: ""
        property string targetFileName: ""
        property bool targetIsDir: false
        property string targetFileType: ""
        title: targetFileName
        peekHeight: Theme.dp(390)

        Column {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            spacing: Theme.dp(Theme.spacingSm)

            MenuRow {
                width: parent.width
                iconName: "info"
                accent: true
                text: qsTr("Quick Preview & Properties")
                onClicked: {
                    var p = contextSheet.targetFilePath
                    var n = contextSheet.targetFileName
                    var ft = contextSheet.targetFileType
                    var idr = contextSheet.targetIsDir
                    contextSheet.close()
                    filePreviewModal.openWith(p, n, ft, idr)
                }
            }

            MenuRow {
                width: parent.width
                visible: !contextSheet.targetIsDir && (contextSheet.targetFileType === "scene" || contextSheet.targetFileType === "model")
                iconName: "view3d"
                text: qsTr("Open in 3D Viewport")
                onClicked: {
                    contextSheet.close()
                    root.fileOpened(contextSheet.targetFilePath, contextSheet.targetFileType)
                }
            }

            MenuRow {
                width: parent.width
                visible: !contextSheet.targetIsDir
                iconName: "braces"
                text: qsTr("Open in Code Editor")
                onClicked: {
                    contextSheet.close()
                    root.fileOpened(contextSheet.targetFilePath, "code")
                }
            }

            MenuRow {
                width: parent.width
                visible: contextSheet.targetFileType === "scene" && rubyFileModel.playInGameAvailable
                iconName: "play"
                accent: true
                text: qsTr("Start Scene in Game")
                onClicked: {
                    contextSheet.close()
                    rubyFileModel.startSceneInGame(contextSheet.targetFilePath)
                }
            }

            MenuRow {
                width: parent.width
                visible: contextSheet.targetFileType === "scene"
                iconName: "terminal"
                text: qsTr("Extract Embedded Lua Script")
                onClicked: {
                    contextSheet.close()
                    var extracted = rubyFileModel.extractLuaFromScene(contextSheet.targetFilePath)
                    if (extracted && extracted.length > 0) {
                        root.fileOpened(extracted, "code")
                    }
                }
            }

            MenuRow {
                width: parent.width
                visible: contextSheet.targetFileType === "archive"
                iconName: "archive"
                text: qsTr("Extract All Here")
                onClicked: {
                    contextSheet.close()
                    rubyFileModel.extractArchive(contextSheet.targetFilePath, false)
                }
            }

            MenuRow {
                width: parent.width
                visible: contextSheet.targetFileType === "archive"
                iconName: "folder"
                text: qsTr("Extract to Folder")
                onClicked: {
                    contextSheet.close()
                    rubyFileModel.extractArchive(contextSheet.targetFilePath, true)
                }
            }

            MenuRow {
                width: parent.width
                visible: contextSheet.targetFileType === "texture" && (contextSheet.targetFilePath.toLowerCase().endsWith(".pvr") || contextSheet.targetFilePath.toLowerCase().endsWith(".tex") || contextSheet.targetFilePath.toLowerCase().endsWith(".tex.png"))
                iconName: "image"
                text: qsTr("Export as PNG")
                onClicked: {
                    contextSheet.close()
                    rubyFileModel.exportTextureToPng(contextSheet.targetFilePath)
                }
            }

            MenuRow {
                width: parent.width
                visible: contextSheet.targetFileType === "model" && !contextSheet.targetFilePath.toLowerCase().endsWith(".pod")
                iconName: "cube"
                text: qsTr("Convert to Game POD…")
                onClicked: {
                    contextSheet.close()
                    podConvertDialog.initForSource(contextSheet.targetFilePath)
                    podConvertDialog.open()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "link"
                text: qsTr("Copy Full Path")
                onClicked: {
                    rubyFileModel.copyToClipboard(contextSheet.targetFilePath)
                    contextSheet.close()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "pencil"
                text: qsTr("Rename")
                onClicked: {
                    contextSheet.close()
                    renameDialog.targetPath = contextSheet.targetFilePath
                    renameDialog.targetName = contextSheet.targetFileName
                    renameDialog.initialText = contextSheet.targetFileName
                    renameDialog.open()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "star"
                text: qsTr("Pin / Unpin")
                onClicked: {
                    rubyFileModel.togglePin(contextSheet.targetFilePath)
                    contextSheet.close()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "trash"
                text: qsTr("Delete")
                danger: true
                onClicked: {
                    contextSheet.close()
                    deleteDialog.targetPath = contextSheet.targetFilePath
                    deleteDialog.targetName = contextSheet.targetFileName
                    deleteDialog.open()
                }
            }
        }
    }

    // ── Dialogs ────────────────────────────────────────────────────────────
    ConfirmDialog {
        id: deleteDialog
        property bool isBatch: false
        property var targetPaths: []
        property string targetPath: ""
        property string targetName: ""
        title: qsTr("Delete")
        message: isBatch ? qsTr("Permanently delete %1 selected items?").arg(targetPaths ? targetPaths.length : 0) : qsTr("Permanently delete “%1”?").arg(targetName)
        danger: true
        confirmText: qsTr("Delete")
        onConfirmed: {
            if (isBatch && targetPaths && targetPaths.length > 0) {
                rubyFileModel.deleteMultiple(targetPaths)
                root.isSelectionMode = false
                rubyFileModel.clearSelection()
            } else if (targetPath.length > 0) {
                rubyFileModel.deleteItem(targetPath)
            }
        }
    }

    TextInputDialog {
        id: renameDialog
        property string targetPath: ""
        property string targetName: ""
        title: qsTr("Rename")
        label: qsTr("New name")
        initialText: targetName
        onAccepted: (value) => rubyFileModel.renameItem(renameDialog.targetPath, value)
    }

    function openNewFileWizard(category, index) {
        newFileWizard.reset(category, index)
        newFileWizard.open(true)
    }

    NewFileDialog {
        id: newFileWizard
        onFileCreated: (path, ext) => {
            root.fileOpened(path, ext)
        }
    }

    TextInputDialog {
        id: newFolderDialog
        title: qsTr("New Folder")
        label: qsTr("Folder name")
        placeholder: qsTr("New_Folder")
        onAccepted: (value) => rubyFileModel.createDirectory(value)
    }

    PodConvertDialog {
        id: podConvertDialog
        onOpenConvertedModel: (podPath) => {
            root.fileOpened(podPath, "model")
        }
    }

    // ── Index FM Places & Storage Drawer ───────────────────────────────────
    PlacesDrawer {
        id: placesDrawer
        onPlaceSelected: (path) => {
            rubyFileModel.navigateTo(path)
        }
    }

    // ── Index FM File Preview & Properties Modal ───────────────────────────
    FilePreviewModal {
        id: filePreviewModal
        onFileOpenRequested: (path, type) => {
            root.fileOpened(path, type)
        }
        onStartSceneRequested: (path) => {
            rubyFileModel.startSceneInGame(path)
        }
        onExtractArchiveRequested: (path, toSubfolder) => {
            rubyFileModel.extractArchive(path, toSubfolder)
        }
        onExtractLuaRequested: (path) => {
            var extracted = rubyFileModel.extractLuaFromScene(path)
            if (extracted && extracted.length > 0) {
                root.fileOpened(extracted, "code")
            }
        }
        onConvertPodRequested: (path) => {
            podConvertDialog.initForSource(path)
            podConvertDialog.open()
        }
    }
}
