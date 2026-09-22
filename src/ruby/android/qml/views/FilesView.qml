import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    property alias currentPath: breadcrumbs.currentPath

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
            onStoragePickerRequested: {
                storageSheet.open(false)
            }
            onSearchFilterChanged: (filterText) => {
                rubyFileModel.setFilter(filterText)
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

        // ── Smooth non-blocking folder scan progress bar ───────────────────
        Item {
            width: parent.width
            height: Theme.dp(3)
            visible: rubyFileModel.isLoading
            clip: true

            Rectangle {
                id: fileLoadingBar
                width: parent.width * 0.35
                height: parent.height
                radius: Theme.dp(1.5)
                color: Theme.accentInk

                SequentialAnimation on x {
                    running: rubyFileModel.isLoading
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

        // ── File list ──────────────────────────────────────────────────────
        ListView {
            id: fileListView
            width: parent.width
            height: parent.height - breadcrumbs.height - Theme.dp(48) - statusBar.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            model: rubyFileModel

            delegate: FileRowDelegate {
                width: fileListView.width
                fileName: model.fileName
                filePath: model.filePath
                fileType: model.fileType
                isDir: model.isDir
                fileSizeStr: model.fileSizeStr
                fileDateStr: model.fileDateStr
                isPinned: model.isPinned

                function popContext() {
                    contextSheet.targetFilePath = model.filePath
                    contextSheet.targetFileName = model.fileName
                    contextSheet.targetIsDir = model.isDir
                    contextSheet.targetFileType = model.fileType
                    contextSheet.open(false)
                }

                onRowClicked: {
                    if (model.isDir) {
                        rubyFileModel.navigateTo(model.filePath)
                    } else {
                        root.fileOpened(model.filePath, model.fileType)
                    }
                }

                onRowLongPressed: popContext()

                // The ⋮ button previously emitted actionRequested("menu") into
                // the void — nothing in FilesView listened, so it looked broken.
                onActionRequested: (action) => {
                    if (action === "menu") popContext()
                }

                onDeleteClicked: {
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

            // Empty state
            Item {
                anchors.centerIn: parent
                visible: fileListView.count === 0
                width: parent.width * 0.7
                height: Theme.dp(120)

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
                        text: qsTr("Nothing here")
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textSecondary
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("This folder is empty, or the filter hides everything in it.")
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

    // ── Speed dial ─────────────────────────────────────────────────────────
    SpeedDialFab {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.dp(Theme.spacingLg)
        anchors.bottomMargin: Theme.dp(44)

        onNewFileWizardRequested: {
            newFileWizard.activeCategory = "All"
            newFileWizard.selectedIndex = 0
            newFileWizard.open()
        }

        onNewSceneRequested: {
            newFileWizard.activeCategory = "Swordigo"
            newFileWizard.selectedIndex = 0
            newFileWizard.open()
        }

        onNewScriptRequested: {
            newFileWizard.activeCategory = "Scripting"
            newFileWizard.selectedIndex = 0
            newFileWizard.open()
        }

        onNewFolderRequested: {
            newFolderDialog.open()
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
        peekHeight: Theme.dp(340)

        Column {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            spacing: Theme.dp(Theme.spacingSm)

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
                visible: contextSheet.targetFileType === "scene"
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
        property string targetPath: ""
        property string targetName: ""
        title: qsTr("Delete")
        message: qsTr("Permanently delete “%1”?").arg(targetName)
        danger: true
        confirmText: qsTr("Delete")
        onConfirmed: rubyFileModel.deleteItem(deleteDialog.targetPath)
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

    function openNewFileWizard() {
        newFileWizard.activeCategory = "All"
        newFileWizard.selectedIndex = 0
        newFileWizard.open()
    }

    NewFileDialog {
        id: newFileWizard
        onFileCreated: (path, ext) => {
            root.fileOpenRequested(path)
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
}
