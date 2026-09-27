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
        contentHeight: contentCol.implicitHeight + Theme.dp(Theme.spacingXl) * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            x: Theme.dp(Theme.paddingScreen)
            width: parent.width - Theme.dp(Theme.paddingScreen) * 2
            topPadding: Theme.dp(Theme.spacingMd)
            spacing: Theme.dp(Theme.spacingLg)

            // ── Tier 1: Local Identity / Profile Header ────────────────────
            IdentityHeader {
                id: identityHeader
                width: parent.width
                userName: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
                userTitle: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.userTitle) ? rubySettings.userTitle : "DEV"
                workspaceName: (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.folderName && rubyFileModel.folderName.length > 0)
                               ? rubyFileModel.folderName
                               : "Swordigo Workspace"
                statusText: qsTr("Local Active")
                onSettingsClicked: root.openSettingsRequested()
                onProfileClicked: root.openSettingsRequested()
            }

            // ── Tier 2: Contextual Hero Workshop Banner ────────────────────
            Rectangle {
                id: heroCard
                width: parent.width
                height: Theme.dp(192)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1
                clip: true

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

                // Background SVG Artwork
                Image {
                    anchors.fill: parent
                    source: "qrc:/svg/banner_hero_modding.svg"
                    fillMode: Image.PreserveAspectCrop
                    opacity: 0.55
                    smooth: true
                }

                // Dark gradient scrim for perfect contrast
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.alpha(Theme.surface1, 0.4) }
                        GradientStop { position: 0.7; color: Theme.alpha(Theme.surface1, 0.88) }
                        GradientStop { position: 1.0; color: Theme.surface1 }
                    }
                }

                // Inner Content
                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.dp(Theme.spacingLg)
                    spacing: Theme.dp(6)

                    // Workshop Badge Tag
                    Rectangle {
                        height: Theme.dp(20)
                        width: wsTag.implicitWidth + Theme.dp(16)
                        radius: Theme.radiusPill
                        color: Theme.accentSoft
                        border.color: Theme.alpha(Theme.accentEnd, 0.5)
                        border.width: 1

                        Text {
                            id: wsTag
                            anchors.centerIn: parent
                            text: qsTr("SWORDIGO MODDING WORKSHOP")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: Font.Bold
                            font.letterSpacing: 0.6
                            color: Theme.accentInk
                        }
                    }

                    Text {
                        text: qsTr("Welcome to Ruby Touch")
                        font.pixelSize: Theme.dp(20)
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }

                    Text {
                        width: parent.width - Theme.dp(40)
                        text: {
                            var recCount = (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles) ? rubyFileModel.recentFiles.length : 0;
                            var folder = (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.folderName && rubyFileModel.folderName.length > 0) ? rubyFileModel.folderName : "Storage";
                            if (heroCard.latestFn.length > 0) {
                                return "Last Asset: " + heroCard.latestFn + " • " + (recCount + " Recent Files");
                            }
                            return "Workspace: " + folder + " • Ready to Mod";
                        }
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }

                    Item { height: Theme.dp(2); width: 1 }

                    // Action Row (Primary Resume Button + Browse Files Link)
                    Row {
                        spacing: Theme.dp(Theme.spacingMd)

                        PrimaryButton {
                            height: Theme.dp(38)
                            text: heroCard.latestPath.length > 0
                                  ? (qsTr("Resume ") + heroCard.latestFn + " →")
                                  : qsTr("Open Workspace →")
                            iconName: heroCard.latestPath.length > 0 ? "play" : "folder"
                            onClicked: {
                                if (heroCard.latestPath.length > 0) {
                                    root.openPathRequested(heroCard.latestPath)
                                } else {
                                    root.openFilesRequested()
                                }
                            }
                        }

                        Text {
                            visible: heroCard.latestPath.length > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Browse Files →")
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.Medium
                            color: browseMouse.pressed ? Theme.accentEnd : Theme.textMuted

                            MouseArea {
                                id: browseMouse
                                anchors.fill: parent
                                anchors.margins: -Theme.dp(8)
                                onClicked: root.openFilesRequested()
                            }
                        }
                    }
                }
            }

            // ── Tier 3: Modding Guides / Knowledge Carousel ────────────────
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
                        text: qsTr("Modding Guides")
                        font.pixelSize: Theme.dp(17)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("See all →")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Medium
                        color: guidesMoreMouse.pressed ? Theme.accentEnd : Theme.accentInk

                        MouseArea {
                            id: guidesMoreMouse
                            anchors.fill: parent
                            anchors.margins: -Theme.dp(8)
                            onClicked: {
                                var guides = (typeof rubyToolsBridge !== "undefined" && rubyToolsBridge)
                                             ? rubyToolsBridge.getModdingGuides()
                                             : ((typeof toolsBridge !== "undefined" && toolsBridge) ? toolsBridge.getModdingGuides() : []);
                                if (guides.length > 0) {
                                    var g = guides[0];
                                    guideReader.open(g.id, g.title, g.category, g.svg, g.qrcPath);
                                }
                            }
                        }
                    }
                }

                // Horizontal Carousel
                ListView {
                    id: guidesCarousel
                    width: parent.width
                    height: Theme.dp(220)
                    orientation: ListView.Horizontal
                    spacing: Theme.dp(Theme.spacingMd)
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
                            guideReader.open(modelData.id, modelData.title, modelData.category, modelData.svg, modelData.qrcPath);
                        }
                    }
                }
            }

            // ── Tier 4: Continue Working (Recent Files) ─────────────────────
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
                        text: qsTr("Continue Working")
                        font.pixelSize: Theme.dp(17)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Files tab →")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Medium
                        color: filesMoreMouse.pressed ? Theme.accentEnd : Theme.textSecondary

                        MouseArea {
                            id: filesMoreMouse
                            anchors.fill: parent
                            anchors.margins: -Theme.dp(8)
                            onClicked: root.openFilesRequested()
                        }
                    }
                }

                // Recent Files List / Cards
                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Repeater {
                        model: (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles)
                               ? rubyFileModel.recentFiles.slice(0, 4)
                               : []

                        delegate: Rectangle {
                            id: recentCard
                            width: parent.width
                            height: Theme.dp(64)
                            radius: Theme.radiusMd
                            color: recentMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: recentMouse.pressed ? Theme.accentEnd : Theme.border
                            border.width: 1

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
                                if (fType === "scene" || ext === "scene" || ext === "scl") return "#F97316"
                                if (fType === "model" || ext === "pod" || ext === "glb" || ext === "fbx" || ext === "obj") return "#38BDF8"
                                if (fType === "texture" || ext === "pvr" || ext === "png") return "#C084FC"
                                if (fType === "code" || ext === "lua") return "#34D399"
                                if (fType === "audio" || ext === "wav" || ext === "ogg") return "#FBBF24"
                                return Theme.accentInk
                            }

                            // Type Icon Plate
                            Rectangle {
                                id: typePlate
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.dp(40)
                                height: Theme.dp(40)
                                radius: Theme.radiusSm
                                color: Theme.alpha(recentCard.accentColor, 0.15)
                                border.color: Theme.alpha(recentCard.accentColor, 0.35)
                                border.width: 1

                                Icon {
                                    anchors.centerIn: parent
                                    name: recentCard.iconStr
                                    size: Theme.dp(20)
                                    color: recentCard.accentColor
                                }
                            }

                            // Open Arrow Button
                            IconButton {
                                id: arrowBtn
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "arrow-right"
                                variant: "soft"
                                buttonSize: Theme.dp(32)
                                iconSize: Theme.dp(16)
                                square: true
                                onClicked: root.openPathRequested(recentCard.rawPath)
                            }

                            // Details
                            Column {
                                anchors.left: typePlate.right
                                anchors.leftMargin: Theme.dp(Theme.spacingMd)
                                anchors.right: arrowBtn.left
                                anchors.rightMargin: Theme.dp(Theme.spacingMd)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.dp(2)
                                clip: true

                                Text {
                                    width: parent.width
                                    text: recentCard.fn
                                    font.pixelSize: Theme.dp(14)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: recentCard.fSize.length > 0 ? (recentCard.fSize + " • " + recentCard.rawPath) : recentCard.rawPath
                                    font.pixelSize: Theme.dp(11)
                                    color: Theme.textMuted
                                    elide: Text.ElideMiddle
                                }
                            }

                            MouseArea {
                                id: recentMouse
                                anchors.fill: parent
                                onClicked: root.openPathRequested(recentCard.rawPath)
                                onPressAndHold: {
                                    recentOptionsSheet.targetFilePath = recentCard.rawPath
                                    recentOptionsSheet.targetFileName = recentCard.fn
                                    recentOptionsSheet.open()
                                }
                            }
                        }
                    }

                    // Empty State if no recents
                    Rectangle {
                        width: parent.width
                        height: Theme.dp(88)
                        radius: Theme.radiusMd
                        color: Theme.surface1
                        border.color: Theme.borderSubtle
                        border.width: 1
                        visible: !(typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles && rubyFileModel.recentFiles.length > 0)

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(Theme.spacingMd)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "folder-open"
                                size: Theme.dp(22)
                                color: Theme.textMuted
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.dp(2)

                                Text {
                                    text: qsTr("No recent files yet")
                                    font.pixelSize: Theme.dp(13)
                                    font.weight: Font.Medium
                                    color: Theme.textSecondary
                                }

                                Text {
                                    text: qsTr("Open a scene or model in the Files tab to begin")
                                    font.pixelSize: Theme.dp(11)
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                }
            }

            // ── Tier 5: Quick Tools (Compact Horizontal Carousel) ──────────
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
                        text: qsTr("Quick Tools")
                        font.pixelSize: Theme.dp(17)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Tools tab →")
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

                // Compact Horizontal Tool Pills
                Flickable {
                    width: parent.width
                    height: Theme.dp(84)
                    contentWidth: toolPillsRow.implicitWidth
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick

                    Row {
                        id: toolPillsRow
                        spacing: Theme.dp(Theme.spacingSm)

                        // Tool 1: POD Convert
                        Rectangle {
                            width: Theme.dp(125)
                            height: Theme.dp(78)
                            radius: Theme.radiusMd
                            color: podToolMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.border
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "cube"
                                    size: Theme.dp(20)
                                    color: "#38BDF8"
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("POD Convert")
                                    font.pixelSize: Theme.dp(12)
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

                        // Tool 2: New File Wizard
                        Rectangle {
                            width: Theme.dp(125)
                            height: Theme.dp(78)
                            radius: Theme.radiusMd
                            color: newFileMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.border
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "file-plus"
                                    size: Theme.dp(20)
                                    color: Theme.accentInk
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("New File")
                                    font.pixelSize: Theme.dp(12)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                }
                            }

                            MouseArea {
                                id: newFileMouse
                                anchors.fill: parent
                                onClicked: root.newFileWizardRequested()
                            }
                        }

                        // Tool 3: Texture Studio
                        Rectangle {
                            width: Theme.dp(125)
                            height: Theme.dp(78)
                            radius: Theme.radiusMd
                            color: texToolMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.border
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "image"
                                    size: Theme.dp(20)
                                    color: "#C084FC"
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("Texture PVR")
                                    font.pixelSize: Theme.dp(12)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                }
                            }

                            MouseArea {
                                id: texToolMouse
                                anchors.fill: parent
                                onClicked: root.openToolsRequested("texture")
                            }
                        }

                        // Tool 4: FileRift Recode
                        Rectangle {
                            width: Theme.dp(125)
                            height: Theme.dp(78)
                            radius: Theme.radiusMd
                            color: riftToolMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.border
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "code"
                                    size: Theme.dp(20)
                                    color: "#10B981"
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("FileRift")
                                    font.pixelSize: Theme.dp(12)
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

                        // Tool 5: Ground Mesh Generator
                        Rectangle {
                            width: Theme.dp(125)
                            height: Theme.dp(78)
                            radius: Theme.radiusMd
                            color: groundToolMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.border
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "layers"
                                    size: Theme.dp(20)
                                    color: "#F59E0B"
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("Ground Mesh")
                                    font.pixelSize: Theme.dp(12)
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

                        // Tool 6: Audio Player
                        Rectangle {
                            width: Theme.dp(125)
                            height: Theme.dp(78)
                            radius: Theme.radiusMd
                            color: audioToolMouse.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.border
                            border.width: 1

                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "volume"
                                    size: Theme.dp(20)
                                    color: "#F43F5E"
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("Audio Player")
                                    font.pixelSize: Theme.dp(12)
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
