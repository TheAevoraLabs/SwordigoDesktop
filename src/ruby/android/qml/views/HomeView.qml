import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    signal openFilesRequested()
    signal openFolderRequested(string folderPath)
    signal openPathRequested(string path)
    signal openToolsRequested(string toolName)
    signal openSettingsRequested()
    signal newFileWizardRequested()

    function handleBack() {
        if (guideReader.visible) {
            guideReader.close()
            return true
        }
        return false
    }

    Flickable {
        anchors.fill: parent
        contentHeight: contentCol.implicitHeight + Theme.dp(40)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: Theme.dp(Theme.paddingScreen)
            width: parent.width - Theme.dp(Theme.paddingScreen) * 2
            topPadding: Theme.dp(Theme.spacingMd)
            bottomPadding: Theme.dp(Theme.spacingXl)
            spacing: Theme.dp(Theme.spacingLg)

            // ── Tier 1: Local Identity / Workspace Header ───────────────────
            IdentityHeader {
                id: identityHeader
                width: parent.width
                userName: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
                avatarImg: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.avatarImage) ? rubySettings.avatarImage : ""
                workspaceName: {
                    var count = (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles) ? rubyFileModel.recentFiles.length : 0
                    if (count > 0) return qsTr("%1 Files").arg(count)
                    return (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.folderName && rubyFileModel.folderName.length > 0 && rubyFileModel.folderName !== "0")
                           ? rubyFileModel.folderName
                           : qsTr("Local Storage")
                }
                statusText: qsTr("Local Active")
                onSearchClicked: root.openFilesRequested()
                onProfileClicked: root.openSettingsRequested()
            }

            // ── Tier 2: Active Workspace & Session Card (No Marketing Fluff) ─
            Rectangle {
                id: activeSessionCard
                width: parent.width
                implicitHeight: sessionCol.implicitHeight + Theme.dp(Theme.spacingLg) * 2
                radius: Theme.radiusCard
                color: Theme.surface1

                property var latestItem: (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles && rubyFileModel.recentFiles.length > 0)
                                         ? rubyFileModel.recentFiles[0] : null
                property string latestPath: (typeof latestItem === "string") ? latestItem : ((latestItem && latestItem.filePath) ? latestItem.filePath : "")
                property string latestFn: {
                    if (typeof latestItem === "string") {
                        var parts = latestPath.split("/")
                        return parts[parts.length - 1]
                    }
                    return (latestItem && latestItem.fileName) ? latestItem.fileName : ""
                }
                property string latestSize: (typeof latestItem === "object" && latestItem && latestItem.fileSizeStr) ? latestItem.fileSizeStr : ""
                property string latestExt: {
                    var idx = latestFn.lastIndexOf(".")
                    return idx >= 0 ? latestFn.substring(idx + 1).toLowerCase() : ""
                }
                property string eyebrowLabel: {
                    if (latestPath.length === 0) return qsTr("ACTIVE WORKSPACE")
                    if (latestExt === "scene" || latestExt === "scl") return qsTr("ACTIVE SCENE")
                    if (latestExt === "png" || latestExt === "pvr" || latestExt === "ktx" || latestExt === "jpg") return qsTr("ACTIVE TEXTURE")
                    if (latestExt === "pod" || latestExt === "glb" || latestExt === "fbx" || latestExt === "obj") return qsTr("ACTIVE MODEL")
                    if (latestExt === "lua" || latestExt === "txt") return qsTr("ACTIVE SCRIPT")
                    if (latestExt === "zip" || latestExt === "pak") return qsTr("ACTIVE ARCHIVE")
                    return qsTr("ACTIVE ASSET")
                }
                property string buttonLabel: {
                    if (latestPath.length === 0) return qsTr("Open Workspace")
                    if (latestExt === "scene" || latestExt === "scl") return qsTr("Resume Scene")
                    if (latestExt === "pod" || latestExt === "glb") return qsTr("Inspect Model")
                    if (latestExt === "png" || latestExt === "pvr") return qsTr("View Texture")
                    if (latestExt === "lua") return qsTr("Edit Script")
                    return qsTr("Open Asset")
                }

                Column {
                    id: sessionCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(Theme.spacingLg)
                    spacing: Theme.dp(6)

                    // Eyebrow Tag
                    Row {
                        spacing: Theme.dp(6)
                        Text {
                            text: activeSessionCard.eyebrowLabel
                            color: Theme.accentInk
                            font.pixelSize: Theme.dp(10)
                            font.weight: Font.Bold
                            font.letterSpacing: 1.1
                            font.family: Theme.monoFamily
                        }
                    }

                    // Main Asset / Workspace Name
                    Text {
                        width: parent.width
                        text: activeSessionCard.latestFn.length > 0
                              ? activeSessionCard.latestFn
                              : ((typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.folderName && rubyFileModel.folderName.length > 0 && rubyFileModel.folderName !== "0")
                                 ? rubyFileModel.folderName : "Swordigo Studio Workspace")
                        font.pixelSize: Theme.dp(20)
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    // Technical Monospace Metadata (Stripped parent directory, elided on left)
                    Text {
                        width: parent.width
                        text: {
                            if (activeSessionCard.latestPath.length > 0) {
                                var rel = activeSessionCard.latestPath
                                if (rel.indexOf("/storage/emulated/0/") === 0) {
                                    rel = rel.substring(19)
                                }
                                var dirIdx = rel.lastIndexOf("/")
                                var pDir = dirIdx > 0 ? rel.substring(0, dirIdx) : "Root directory"
                                return (activeSessionCard.latestSize.length > 0 ? (activeSessionCard.latestSize + "  •  ") : "") + pDir
                            }
                            return (typeof rubyFileModel !== "undefined" && rubyFileModel) ? rubyFileModel.currentPath : "/storage/emulated/0"
                        }
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.family: Theme.monoFamily
                        color: Theme.textSecondary
                        elide: Text.ElideLeft
                    }

                    Item { height: Theme.dp(6); width: 1 }

                    // Action Row (Single clean icon, no double bookended arrows)
                    Row {
                        spacing: Theme.dp(Theme.spacingMd)

                        PrimaryButton {
                            height: Theme.dp(40)
                            text: activeSessionCard.buttonLabel
                            iconName: activeSessionCard.latestPath.length > 0 ? "play" : "folder"
                            onClicked: {
                                if (activeSessionCard.latestPath.length > 0) {
                                    root.openPathRequested(activeSessionCard.latestPath)
                                } else {
                                    root.openFilesRequested()
                                }
                            }
                        }

                        Rectangle {
                            height: Theme.dp(40)
                            width: Theme.dp(120)
                            radius: Theme.radiusSm
                            color: browseMouse.pressed ? Theme.surface3 : Theme.surface2

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "folder"
                                    size: Theme.dp(15)
                                    color: Theme.accentInk
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Browse Files")
                                    font.pixelSize: Theme.dp(12)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                }
                            }

                            MouseArea {
                                id: browseMouse
                                anchors.fill: parent
                                onClicked: root.openFilesRequested()
                            }
                        }
                    }
                }
            }

            // ── Tier 3: 2x2 Quick Asset Category Tiles ──────────────────────
            Grid {
                width: parent.width
                columns: 2
                spacing: Theme.dp(Theme.spacingSm)

                // 1: Scenes
                Rectangle {
                    width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                    height: Theme.dp(62)
                    radius: Theme.radiusMd
                    color: scenesMouse.pressed ? Theme.surface2 : Theme.surface1

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.dp(10)
                        spacing: Theme.dp(10)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorScene, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: "map"
                                size: Theme.dp(18)
                                color: Theme.colorScene
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(46)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Scenes")
                                font.pixelSize: Theme.dp(13)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: ".scene / .scl"
                                font.pixelSize: Theme.dp(10)
                                font.family: Theme.monoFamily
                                color: Theme.textMuted
                            }
                        }
                    }

                    MouseArea {
                        id: scenesMouse
                        anchors.fill: parent
                        onClicked: {
                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                rubyFileModel.setCategory("scene")
                            }
                            root.openFilesRequested()
                        }
                    }
                }

                // 2: Models
                Rectangle {
                    width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                    height: Theme.dp(62)
                    radius: Theme.radiusMd
                    color: modelsMouse.pressed ? Theme.surface2 : Theme.surface1

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.dp(10)
                        spacing: Theme.dp(10)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorModel, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: "cube"
                                size: Theme.dp(18)
                                color: Theme.colorModel
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(46)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Models")
                                font.pixelSize: Theme.dp(13)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: ".pod / .glb"
                                font.pixelSize: Theme.dp(10)
                                font.family: Theme.monoFamily
                                color: Theme.textMuted
                            }
                        }
                    }

                    MouseArea {
                        id: modelsMouse
                        anchors.fill: parent
                        onClicked: {
                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                rubyFileModel.setCategory("models")
                            }
                            root.openFilesRequested()
                        }
                    }
                }

                // 3: Scripts
                Rectangle {
                    width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                    height: Theme.dp(62)
                    radius: Theme.radiusMd
                    color: scriptsMouse.pressed ? Theme.surface2 : Theme.surface1

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.dp(10)
                        spacing: Theme.dp(10)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorCode, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: "braces"
                                size: Theme.dp(18)
                                color: Theme.colorCode
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(46)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Scripts")
                                font.pixelSize: Theme.dp(13)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: ".lua / .txt"
                                font.pixelSize: Theme.dp(10)
                                font.family: Theme.monoFamily
                                color: Theme.textMuted
                            }
                        }
                    }

                    MouseArea {
                        id: scriptsMouse
                        anchors.fill: parent
                        onClicked: {
                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                rubyFileModel.setCategory("lua")
                            }
                            root.openFilesRequested()
                        }
                    }
                }

                // 4: Archives
                Rectangle {
                    width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                    height: Theme.dp(62)
                    radius: Theme.radiusMd
                    color: archivesMouse.pressed ? Theme.surface2 : Theme.surface1

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.dp(10)
                        spacing: Theme.dp(10)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorArchive, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: "archive"
                                size: Theme.dp(18)
                                color: Theme.colorArchive
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(46)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Archives")
                                font.pixelSize: Theme.dp(13)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: ".zip / .pak"
                                font.pixelSize: Theme.dp(10)
                                font.family: Theme.monoFamily
                                color: Theme.textMuted
                            }
                        }
                    }

                    MouseArea {
                        id: archivesMouse
                        anchors.fill: parent
                        onClicked: {
                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                rubyFileModel.setCategory("archives")
                            }
                            root.openFilesRequested()
                        }
                    }
                }
            }

            // ── Tier 4: Continue Working (PROMOTED ABOVE GUIDES!) ───────────
            Column {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                // Section Header
                Item {
                    width: parent.width
                    height: Theme.dp(26)

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Recent Files")
                        font.pixelSize: Theme.dp(16)
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("View All")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Medium
                        color: filesMoreMouse.pressed ? Theme.accentEnd : Theme.accentInk

                        MouseArea {
                            id: filesMoreMouse
                            anchors.fill: parent
                            anchors.margins: -Theme.dp(8)
                            onClicked: root.openFilesRequested()
                        }
                    }
                }

                // Grouped Container (Zero individual row borders)
                Rectangle {
                    width: parent.width
                    implicitHeight: recentsListCol.implicitHeight
                    radius: Theme.radiusCard
                    color: Theme.surface1

                    Column {
                        id: recentsListCol
                        width: parent.width

                        Repeater {
                            id: recentFilesRepeater
                            model: {
                                if (typeof rubyFileModel === "undefined" || !rubyFileModel || !rubyFileModel.recentFiles) return []
                                var recents = rubyFileModel.recentFiles
                                if (activeSessionCard.latestPath.length > 0 && recents.length > 1) {
                                    return recents.slice(1, 5)
                                }
                                return recents.slice(0, 4)
                            }

                            delegate: Rectangle {
                                width: parent.width
                                height: Theme.dp(58)
                                color: recentRowMouse.pressed ? Theme.surface2 : "transparent"

                                property var itemObj: modelData
                                property string rawPath: (typeof itemObj === "string") ? itemObj : ((itemObj && itemObj.filePath) ? itemObj.filePath : "")
                                property string fn: {
                                    if (typeof itemObj === "string") {
                                        var parts = rawPath.split("/")
                                        return parts[parts.length - 1]
                                    }
                                    return (itemObj && itemObj.fileName) ? itemObj.fileName : ""
                                }
                                property string fType: (typeof itemObj === "object" && itemObj && itemObj.fileType) ? itemObj.fileType : ""
                                property string fSize: (typeof itemObj === "object" && itemObj && itemObj.fileSizeStr) ? itemObj.fileSizeStr : ""
                                property string ext: {
                                    var idx = fn.lastIndexOf(".")
                                    return idx >= 0 ? fn.substring(idx + 1).toLowerCase() : ""
                                }
                                property string iconStr: {
                                    if (fType === "scene" || ext === "scene" || ext === "scl") return "map"
                                    if (fType === "model" || ext === "pod" || ext === "glb" || ext === "fbx" || ext === "obj") return "cube"
                                    if (fType === "texture" || ext === "pvr" || ext === "png" || ext === "tex") return "image"
                                    if (fType === "code" || ext === "lua" || ext === "filerift") return "code"
                                    if (fType === "audio" || ext === "wav" || ext === "ogg" || ext === "mp3") return "volume"
                                    return "file"
                                }
                                property color accentColor: {
                                    if (fType === "scene" || ext === "scene" || ext === "scl") return Theme.colorScene
                                    if (fType === "model" || ext === "pod" || ext === "glb" || ext === "fbx" || ext === "obj") return Theme.colorModel
                                    if (fType === "texture" || ext === "pvr" || ext === "png") return Theme.colorTexture
                                    if (fType === "code" || ext === "lua") return Theme.colorCode
                                    if (fType === "audio" || ext === "wav" || ext === "ogg") return Theme.colorAudio
                                    return Theme.accentInk
                                }

                                function parentDir(fullPath) {
                                    if (!fullPath || fullPath.length === 0) return "Root directory"
                                    var p = fullPath
                                    if (p.indexOf("/storage/emulated/0/") === 0) p = p.substring(19)
                                    var idx = p.lastIndexOf("/")
                                    if (idx <= 0) return "Root directory"
                                    return p.substring(0, idx)
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                                    anchors.rightMargin: Theme.dp(Theme.spacingMd)
                                    spacing: Theme.dp(12)

                                    // Type Icon Square
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.dp(36)
                                        height: Theme.dp(36)
                                        radius: Theme.radiusSm
                                        color: Theme.alpha(parent.parent.accentColor, 0.15)

                                        Icon {
                                            anchors.centerIn: parent
                                            name: parent.parent.parent.iconStr
                                            size: Theme.dp(17)
                                            color: parent.parent.parent.accentColor
                                        }
                                    }

                                    // File Details
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - Theme.dp(36) - Theme.dp(24) - Theme.dp(12) * 2
                                        spacing: Theme.dp(2)

                                        Text {
                                            width: parent.width
                                            text: parent.parent.parent.fn
                                            font.pixelSize: Theme.dp(14)
                                            font.weight: Font.Medium
                                            color: Theme.textPrimary
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            width: parent.width
                                            text: (parent.parent.parent.fSize.length > 0 ? (parent.parent.parent.fSize + "  •  ") : "") + parent.parent.parent.parentDir(parent.parent.parent.rawPath)
                                            font.pixelSize: Theme.dp(11)
                                            font.family: Theme.monoFamily
                                            color: Theme.textMuted
                                            elide: Text.ElideLeft
                                        }
                                    }

                                    // Clean subtle chevron ›
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "›"
                                        font.pixelSize: Theme.dp(22)
                                        font.weight: Font.Light
                                        color: Theme.textMuted
                                    }
                                }

                                // Hairline separator between rows (inset to text, not icon)
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.dp(64)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.dp(12)
                                    height: 1
                                    color: Theme.borderSubtle
                                    visible: index < (recentFilesRepeater.count - 1)
                                }

                                MouseArea {
                                    id: recentRowMouse
                                    anchors.fill: parent
                                    onClicked: root.openPathRequested(parent.rawPath)
                                    onPressAndHold: {
                                        recentOptionsSheet.targetFilePath = parent.rawPath
                                        recentOptionsSheet.targetFileName = parent.fn
                                        recentOptionsSheet.open()
                                    }
                                }
                            }
                        }

                        // Empty State if no recents
                        Item {
                            width: parent.width
                            height: Theme.dp(72)
                            visible: recentFilesRepeater.count === 0 && activeSessionCard.latestPath.length === 0

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(Theme.spacingMd)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "folder-open"
                                    size: Theme.dp(20)
                                    color: Theme.textMuted
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("No recent files yet • Open an asset in Files tab")
                                    font.pixelSize: Theme.dp(12)
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                }
            }

            // ── Tier 5: Modding Guides (Compact Horizontal Carousel) ────────
            Column {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                Item {
                    width: parent.width
                    height: Theme.dp(26)

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Modding Guides")
                        font.pixelSize: Theme.dp(16)
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }
                }

                ListView {
                    id: guidesCarousel
                    width: parent.width
                    height: Theme.dp(88)
                    orientation: ListView.Horizontal
                    spacing: Theme.dp(Theme.spacingSm)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick

                    model: (typeof rubyToolsBridge !== "undefined" && rubyToolsBridge)
                           ? rubyToolsBridge.getModdingGuides()
                           : ((typeof toolsBridge !== "undefined" && toolsBridge) ? toolsBridge.getModdingGuides() : [])

                    delegate: GuideCard {
                        title: modelData.title
                        category: modelData.category
                        tag: modelData.tag
                        description: modelData.desc
                        readTime: modelData.readTime
                        svgSource: modelData.svg
                        qrcPath: modelData.qrcPath
                        onClicked: {
                            guideReader.open(modelData.id, modelData.title, modelData.category, modelData.svg, modelData.qrcPath)
                        }
                    }
                }
            }

            // ── Tier 6: Quick Tools ─────────────────────────────────────────
            Column {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                Item {
                    width: parent.width
                    height: Theme.dp(26)

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Quick Tools")
                        font.pixelSize: Theme.dp(16)
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Tools tab")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Medium
                        color: toolsMoreMouse.pressed ? Theme.accentEnd : Theme.accentInk

                        MouseArea {
                            id: toolsMoreMouse
                            anchors.fill: parent
                            anchors.margins: -Theme.dp(8)
                            onClicked: root.openToolsRequested("")
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    // 1: POD Convert
                    Rectangle {
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                        height: Theme.dp(66)
                        radius: Theme.radiusMd
                        color: podToolMouse.pressed ? Theme.surface2 : Theme.surface1

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.dp(4)

                            Icon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "cube"
                                size: Theme.dp(18)
                                color: "#38BDF8"
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("POD 2.0")
                                font.pixelSize: Theme.dp(11)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }
                        }

                        MouseArea {
                            id: podToolMouse
                            anchors.fill: parent
                            onClicked: root.openToolsRequested("converter")
                        }
                    }

                    // 2: FileRift
                    Rectangle {
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                        height: Theme.dp(66)
                        radius: Theme.radiusMd
                        color: riftToolMouse.pressed ? Theme.surface2 : Theme.surface1

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.dp(4)

                            Icon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "braces"
                                size: Theme.dp(18)
                                color: "#34D399"
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("FileRift")
                                font.pixelSize: Theme.dp(11)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }
                        }

                        MouseArea {
                            id: riftToolMouse
                            anchors.fill: parent
                            onClicked: root.openToolsRequested("recoder")
                        }
                    }

                    // 3: Ground Mesh
                    Rectangle {
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                        height: Theme.dp(66)
                        radius: Theme.radiusMd
                        color: groundToolMouse.pressed ? Theme.surface2 : Theme.surface1

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.dp(4)

                            Icon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "layers"
                                size: Theme.dp(18)
                                color: "#F59E0B"
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Zypher")
                                font.pixelSize: Theme.dp(11)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }
                        }

                        MouseArea {
                            id: groundToolMouse
                            anchors.fill: parent
                            onClicked: root.openToolsRequested("ground")
                        }
                    }

                    // 4: Audio Player
                    Rectangle {
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                        height: Theme.dp(66)
                        radius: Theme.radiusMd
                        color: audioToolMouse.pressed ? Theme.surface2 : Theme.surface1

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.dp(4)

                            Icon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: "volume"
                                size: Theme.dp(18)
                                color: "#F43F5E"
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Audio")
                                font.pixelSize: Theme.dp(11)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }
                        }

                        MouseArea {
                            id: audioToolMouse
                            anchors.fill: parent
                            onClicked: {
                                if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    rubyFileModel.setCategory("audio")
                                }
                                root.openFilesRequested()
                            }
                        }
                    }
                }
            }

            // Bottom breathing spacer so content clears comfortably above bottom bar
            Item {
                width: parent.width
                height: Theme.dp(36)
            }
        }
    }

    // ── Context Actions for Recent File ────────────────────────────────────
    BottomSheet {
        id: recentOptionsSheet
        property string targetFilePath: ""
        property string targetFileName: ""
        title: targetFileName.length > 0 ? targetFileName : qsTr("Recent File")
        peekHeight: Theme.dp(250)

        Column {
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            spacing: Theme.dp(Theme.spacingSm)

            MenuRow {
                width: parent.width
                iconName: "play"
                text: qsTr("Open File")
                accent: true
                onClicked: {
                    recentOptionsSheet.close()
                    root.openPathRequested(recentOptionsSheet.targetFilePath)
                }
            }

            MenuRow {
                width: parent.width
                iconName: "folder"
                text: qsTr("Open Containing Folder")
                onClicked: {
                    recentOptionsSheet.close()
                    var p = recentOptionsSheet.targetFilePath
                    var lastSlash = p.lastIndexOf("/")
                    var folder = lastSlash > 0 ? p.substring(0, lastSlash) : "/storage/emulated/0"
                    root.openFolderRequested(folder)
                }
            }

            MenuRow {
                width: parent.width
                iconName: "link"
                text: qsTr("Copy Full Path")
                onClicked: {
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                        rubyFileModel.copyToClipboard(recentOptionsSheet.targetFilePath)
                    }
                    recentOptionsSheet.close()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "trash"
                text: qsTr("Remove from Recents")
                danger: true
                onClicked: {
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                        rubyFileModel.removeRecentFile(recentOptionsSheet.targetFilePath)
                    }
                    recentOptionsSheet.close()
                }
            }
        }
    }

    // ── Dedicated Modular Markdown Guide Reader Modal ──────────────────────
    GuideReaderView {
        id: guideReader
        anchors.fill: parent
        z: 100
    }
}
