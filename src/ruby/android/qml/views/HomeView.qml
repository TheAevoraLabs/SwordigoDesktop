import QtQuick
import QtQuick.Controls
import ".."
import "../components"

// ============================================================================
// HomeView.qml - Ruby Touch home surface.
//
// Two states share this component:
//   - No active project: the main menu (project onboarding, quick actions,
//     recent assets, tools and guides).
//   - Active project: the interactive Project Workcenter.
//
// The public contract consumed by Main.qml is unchanged. Everything here is
// built from the shared Theme tokens and the existing vector Icon set.
// ============================================================================
Item {
    id: root

    signal openFilesRequested()
    signal openFolderRequested(string folderPath)
    signal openPathRequested(string path)
    signal openToolsRequested(string toolName)
    signal openSettingsRequested()
    signal newFileWizardRequested()

    readonly property bool hasActiveProject: (typeof rubyProject !== "undefined" && rubyProject && rubyProject.hasActiveProject)
    readonly property bool creatingProject: (typeof rubyProject !== "undefined" && rubyProject && rubyProject.isCreating)

    readonly property var recentItems: {
        if (typeof rubyFileModel === "undefined" || !rubyFileModel || !rubyFileModel.recentFiles)
            return []
        return rubyFileModel.recentFiles.slice(0, 8)
    }
    readonly property int recentCount: recentItems.length

    readonly property var quickActions: [
        { key: "new",    icon: "plus",   label: qsTr("New") },
        { key: "files",  icon: "folder", label: qsTr("Files") },
        { key: "recent", icon: "play",   label: qsTr("Continue") },
        { key: "tools",  icon: "tune",   label: qsTr("Tools") }
    ]

    readonly property var projectSteps: [
        { n: "01", title: qsTr("Author"), body: qsTr("Build scenes, models, textures and scripts under project/.") },
        { n: "02", title: qsTr("Diff"), body: qsTr("Ruby hashes every asset against the vanilla baseline.") },
        { n: "03", title: qsTr("Ship"), body: qsTr("Export a Launcher-ready zip with only your changes.") }
    ]

    readonly property var toolbox: [
        { key: "converter", icon: "cube",   title: qsTr("POD 2.0"),  body: qsTr("Convert classic Swordigo models"), color: Theme.colorModel },
        { key: "recoder",   icon: "braces", title: qsTr("FileRift"), body: qsTr("Decompile and rebuild game data"),  color: Theme.colorCode },
        { key: "ground",    icon: "layers", title: qsTr("Zypher"),   body: qsTr("Sculpt ground meshes"),            color: Theme.colorScene },
        { key: "audio",     icon: "volume", title: qsTr("Audio Lab"), body: qsTr("Preview music and sound effects"), color: Theme.colorAudio }
    ]

    property real intro: 0

    NumberAnimation on intro {
        from: 0
        to: 1
        duration: 680
        easing.type: Easing.OutCubic
    }

    function haptic(ms) {
        if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled
                && typeof androidContext !== "undefined" && androidContext
                && typeof androidContext.vibrate === "function") {
            androidContext.vibrate(ms || 12)
        }
    }

    function sectionOpacity(index) {
        var value = (root.intro - index * 0.07) / 0.30
        return 0.05 + 0.95 * Math.max(0, Math.min(1, value))
    }

    function sectionShift(index) {
        var value = (root.intro - index * 0.07) / 0.30
        return (1 - Math.max(0, Math.min(1, value))) * 18
    }

    function quickTint(key) {
        switch (key) {
        case "new":    return Theme.accentInk
        case "files":  return Theme.colorModel
        case "recent": return Theme.colorSuccessBright
        default:       return Theme.colorCode
        }
    }

    function runQuickAction(key) {
        root.haptic()
        switch (key) {
        case "new":
            newProjectDialog.open()
            break
        case "recent":
            root.jumpToRecents()
            break
        case "tools":
            root.openToolsRequested("")
            break
        default:
            root.openFilesRequested()
            break
        }
    }

    function jumpToRecents() {
        var target = Math.max(0, homeColumn.y + recentsSection.y - Theme.dp(10))
        scrollAnim.stop()
        scrollAnim.from = scroller.contentY
        scrollAnim.to = target
        scrollAnim.start()
    }

    function openTool(item) {
        root.haptic()
        if (item.key === "audio") {
            if (typeof rubyFileModel !== "undefined" && rubyFileModel)
                rubyFileModel.setCategory("audio")
            root.openFilesRequested()
        } else {
            root.openToolsRequested(item.key)
        }
    }

    function handleBack() {
        if (guideReader.visible) {
            guideReader.close()
            return true
        }
        if (recentOptionsSheet.isOpen) {
            recentOptionsSheet.close()
            return true
        }
        if (newProjectDialog.visible) {
            newProjectDialog.close()
            return true
        }
        if (root.hasActiveProject && projectWorkcenter.handleBack())
            return true
        return false
    }

    NumberAnimation {
        id: scrollAnim
        target: scroller
        property: "contentY"
        duration: 340
        easing.type: Easing.OutCubic
    }

    onHasActiveProjectChanged: {
        // Opening or closing a project replaces the whole surface; start the
        // new one from the top instead of inheriting the previous scroll.
        scrollAnim.stop()
        scroller.contentY = 0
    }

    NewProjectDialog {
        id: newProjectDialog
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: (root.hasActiveProject
                        ? projectWorkcenter.implicitHeight + Theme.dp(24)
                        : homeColumn.implicitHeight + Theme.dp(28))
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ProjectWorkcenter {
            id: projectWorkcenter
            x: Theme.dp(Theme.paddingScreen)
            y: Theme.dp(Theme.spacingSm)
            width: parent.width - Theme.dp(Theme.paddingScreen) * 2
            visible: root.hasActiveProject
            onOpenFilesRequested: root.openFilesRequested()
            onExitProjectRequested: {}
        }

        Column {
            id: homeColumn
            x: Theme.dp(Theme.paddingScreen)
            y: Theme.dp(Theme.spacingSm)
            width: parent.width - Theme.dp(Theme.paddingScreen) * 2
            spacing: Theme.dp(Theme.spacingXl)
            visible: !root.hasActiveProject

            // ── Brand bar ──────────────────────────────────────────────────
            Item {
                id: brandBar
                width: parent.width
                implicitHeight: Theme.dp(46)
                opacity: root.sectionOpacity(0)
                transform: Translate { y: root.sectionShift(0) }

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.dp(Theme.spacingMd)

                    Rectangle {
                        width: Theme.dp(42)
                        height: Theme.dp(42)
                        radius: Theme.radiusMd
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.accentStart }
                            GradientStop { position: 1.0; color: Theme.accentEnd }
                        }

                        Icon {
                            anchors.centerIn: parent
                            name: "gem"
                            size: Theme.dp(22)
                            color: Theme.onAccent
                            weight: 1.7
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.dp(1)

                        Text {
                            text: qsTr("RUBY")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(15)
                            font.weight: Font.Bold
                            font.letterSpacing: Theme.dp(2.6)
                        }

                        Text {
                            text: qsTr("Swordigo Mod Studio")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.family: Theme.monoFamily
                        }
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.dp(8)

                    IconButton {
                        iconName: "search"
                        variant: "plain"
                        buttonSize: Theme.dp(38)
                        iconSize: Theme.dp(19)
                        tint: Theme.textSecondary
                        onClicked: {
                            root.haptic()
                            root.openFilesRequested()
                        }
                    }

                    Rectangle {
                        width: Theme.dp(40)
                        height: Theme.dp(40)
                        radius: width / 2
                        color: Theme.surface2
                        border.color: Theme.borderSubtle
                        border.width: 1
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: Theme.dp(3)
                            source: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.avatarImage)
                                    ? rubySettings.avatarImage : ""
                            visible: source.toString().length > 0
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: (typeof rubySettings === "undefined" || !rubySettings
                                      || !rubySettings.avatarImage || rubySettings.avatarImage.length === 0)
                            text: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.userName.length > 0)
                                  ? rubySettings.userName.charAt(0).toUpperCase() : "R"
                            color: Theme.accentInk
                            font.pixelSize: Theme.dp(15)
                            font.weight: Font.Bold
                            font.family: Theme.monoFamily
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.haptic()
                                root.openSettingsRequested()
                            }
                        }
                    }
                }
            }

            // ── Hero: start a project ──────────────────────────────────────
            Rectangle {
                id: heroCard
                width: parent.width
                implicitHeight: heroColumn.implicitHeight + Theme.dp(20) * 2
                radius: Theme.radiusSheet
                border.color: Theme.alpha(Theme.accentInk, 0.28)
                border.width: 1
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.accentDeep }
                    GradientStop { position: 0.62; color: Theme.surface1 }
                    GradientStop { position: 1.0; color: Theme.surface1 }
                }
                opacity: root.sectionOpacity(1)
                transform: Translate { y: root.sectionShift(1) }

                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: -Theme.dp(34)
                    anchors.verticalCenter: parent.verticalCenter
                    name: "gem"
                    size: Theme.dp(176)
                    weight: 1.1
                    color: Theme.alpha(Theme.onAccent, 0.07)
                    rotation: 14
                }

                Column {
                    id: heroColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(20)
                    spacing: Theme.dp(10)

                    Row {
                        spacing: Theme.dp(7)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.dp(6)
                            height: Theme.dp(6)
                            radius: width / 2
                            color: Theme.accentInk
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("RUBY PROJECT SYSTEM")
                            color: Theme.accentInk
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: Font.Bold
                            font.letterSpacing: Theme.dp(1.3)
                            font.family: Theme.monoFamily
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTr("Build worlds.\nShip them as mods.")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(25)
                        font.weight: Font.Bold
                        lineHeight: 0.94
                    }

                    Text {
                        width: parent.width
                        text: qsTr("Author Swordigo assets in an isolated project, diff them against the vanilla baseline, then export a Launcher-ready package.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.dp(12)
                        lineHeight: 1.25
                        wrapMode: Text.WordWrap
                    }

                    Item { width: 1; height: Theme.dp(4) }

                    Row {
                        spacing: Theme.dp(10)

                        PrimaryButton {
                            height: Theme.dp(44)
                            text: qsTr("New project")
                            iconName: "plus"
                            enabled: !root.creatingProject
                            onClicked: {
                                root.haptic()
                                newProjectDialog.open()
                            }
                        }

                        Rectangle {
                            width: browseLabel.implicitWidth + Theme.dp(32)
                            height: Theme.dp(44)
                            radius: Theme.radiusSm
                            color: browseArea.pressed ? Theme.surface3 : Theme.alpha(Theme.surface2, 0.86)
                            border.color: Theme.borderStrong
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(7)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "folder"
                                    size: Theme.dp(16)
                                    color: Theme.textPrimary
                                }

                                Text {
                                    id: browseLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Browse assets")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.dp(Theme.fontMd)
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                id: browseArea
                                anchors.fill: parent
                                onClicked: {
                                    root.haptic()
                                    root.openFilesRequested()
                                }
                            }
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: Theme.dp(6)
                        visible: root.creatingProject

                        Row {
                            width: parent.width

                            Text {
                                width: parent.width - progressText.width - Theme.dp(6)
                                text: (typeof rubyProject !== "undefined" && rubyProject)
                                      ? rubyProject.creationStatus : qsTr("Creating project...")
                                color: Theme.accentInk
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                elide: Text.ElideRight
                            }

                            Text {
                                id: progressText
                                text: (typeof rubyProject !== "undefined" && rubyProject)
                                      ? (rubyProject.creationProgress + "%") : "0%"
                                color: Theme.accentInk
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: Font.Bold
                                font.family: Theme.monoFamily
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.dp(4)
                            radius: height / 2
                            color: Theme.alpha(Theme.onAccent, 0.14)
                            clip: true

                            Rectangle {
                                height: parent.height
                                radius: parent.radius
                                color: Theme.accentInk
                                width: parent.width * (Math.max(3, (typeof rubyProject !== "undefined" && rubyProject)
                                                                   ? rubyProject.creationProgress : 0) / 100.0)
                                Behavior on width { NumberAnimation { duration: 180 } }
                            }
                        }
                    }
                }
            }

            // ── Quick actions ──────────────────────────────────────────────
            Row {
                id: quickRow
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)
                opacity: root.sectionOpacity(2)
                transform: Translate { y: root.sectionShift(2) }

                Repeater {
                    model: root.quickActions

                    Rectangle {
                        id: quickTile
                        width: (quickRow.width - Theme.dp(Theme.spacingSm) * 3) / 4
                        height: Theme.dp(74)
                        radius: Theme.radiusMd
                        color: quickArea.pressed ? Theme.surface2 : Theme.surface1
                        border.color: Theme.borderSubtle
                        border.width: 1
                        scale: quickArea.pressed ? 0.95 : 1.0
                        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.dp(6)

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Theme.dp(32)
                                height: Theme.dp(32)
                                radius: Theme.radiusSm
                                color: Theme.alpha(root.quickTint(modelData.key), 0.15)

                                Icon {
                                    anchors.centerIn: parent
                                    name: modelData.icon
                                    size: Theme.dp(17)
                                    color: root.quickTint(modelData.key)
                                }
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: Theme.textSecondary
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                font.weight: Font.Medium
                            }
                        }

                        MouseArea {
                            id: quickArea
                            anchors.fill: parent
                            onClicked: root.runQuickAction(modelData.key)
                        }
                    }
                }
            }

            // ── Recent assets ──────────────────────────────────────────────
            Item {
                id: recentsSection
                width: parent.width
                implicitHeight: recentsColumn.implicitHeight
                opacity: root.sectionOpacity(3)
                transform: Translate { y: root.sectionShift(3) }

                Column {
                    id: recentsColumn
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Item {
                        width: parent.width
                        height: Theme.dp(24)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Continue working")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(16)
                            font.weight: Font.Bold
                        }

                        Item {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: viewAllRow.implicitWidth
                            height: Theme.dp(20)

                            Row {
                                id: viewAllRow
                                anchors.centerIn: parent
                                spacing: Theme.dp(3)

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("View all")
                                    color: viewAllMouse.pressed ? Theme.accentEnd : Theme.accentInk
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.DemiBold
                                }

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "chevron-right"
                                    size: Theme.dp(13)
                                    color: Theme.accentInk
                                }
                            }

                            MouseArea {
                                id: viewAllMouse
                                anchors.fill: parent
                                anchors.margins: -Theme.dp(8)
                                onClicked: {
                                    root.haptic()
                                    root.openFilesRequested()
                                }
                            }
                        }
                    }

                    ListView {
                        id: recentsList
                        width: parent.width
                        height: root.recentCount > 0 ? Theme.dp(152) : 0
                        visible: root.recentCount > 0
                        orientation: ListView.Horizontal
                        spacing: Theme.dp(Theme.spacingSm)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.HorizontalFlick
                        model: root.recentItems

                        delegate: Rectangle {
                            id: recentCard
                            width: Theme.dp(238)
                            height: Theme.dp(150)
                            radius: Theme.radiusCard
                            color: recentArea.pressed ? Theme.surface2 : Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1
                            clip: true
                            scale: recentArea.pressed ? 0.97 : 1.0
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

                            property var itemObj: modelData
                            property string rawPath: (typeof itemObj === "string")
                                                     ? itemObj
                                                     : ((itemObj && itemObj.filePath) ? itemObj.filePath : "")
                            property string fileName: {
                                if (typeof itemObj === "string") {
                                    var parts = rawPath.split("/")
                                    return parts[parts.length - 1]
                                }
                                return (itemObj && itemObj.fileName) ? itemObj.fileName : ""
                            }
                            property string fileType: (typeof itemObj === "object" && itemObj && itemObj.fileType)
                                                      ? itemObj.fileType : ""
                            property string fileSize: (typeof itemObj === "object" && itemObj && itemObj.fileSizeStr)
                                                      ? itemObj.fileSizeStr : ""
                            property string ext: {
                                var index = fileName.lastIndexOf(".")
                                return index >= 0 ? fileName.substring(index + 1).toLowerCase() : ""
                            }
                            property string typeKey: Theme.normalizeType(fileType.length > 0 ? fileType : ext)
                            property color accent: Theme.colorForType(typeKey)
                            property string iconName: Theme.iconForType(typeKey)

                            function parentDir(fullPath) {
                                if (!fullPath || fullPath.length === 0)
                                    return qsTr("Workspace")
                                var path = fullPath
                                if (typeof rubyProject !== "undefined" && rubyProject && rubyProject.hasActiveProject) {
                                    var projectRoot = rubyProject.projectRoot
                                    if (projectRoot.length > 0 && path.indexOf(projectRoot) === 0)
                                        return "[" + rubyProject.projectName + "] " + path.substring(projectRoot.length)
                                }
                                if (path.indexOf("/storage/emulated/0/") === 0)
                                    path = path.substring(19)
                                var index = path.lastIndexOf("/")
                                return index > 0 ? path.substring(0, index) : qsTr("Root directory")
                            }

                            Rectangle {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                width: Theme.dp(42)
                                height: Theme.dp(3)
                                radius: height / 2
                                color: recentCard.accent
                            }

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(14)
                                spacing: Theme.dp(7)

                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(9)

                                    Rectangle {
                                        width: Theme.dp(34)
                                        height: Theme.dp(34)
                                        radius: Theme.radiusSm
                                        color: Theme.alpha(recentCard.accent, 0.15)

                                        Icon {
                                            anchors.centerIn: parent
                                            name: recentCard.iconName
                                            size: Theme.dp(17)
                                            color: recentCard.accent
                                        }
                                    }

                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - Theme.dp(34) - Theme.dp(9)
                                        spacing: Theme.dp(1)

                                        Text {
                                            width: parent.width
                                            text: recentCard.fileName
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.dp(13)
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideMiddle
                                            maximumLineCount: 2
                                        }

                                        Text {
                                            width: parent.width
                                            text: recentCard.ext.length > 0
                                                  ? ("." + recentCard.ext) : qsTr("asset")
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.dp(Theme.fontXs)
                                            font.family: Theme.monoFamily
                                        }
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: recentCard.parentDir(recentCard.rawPath)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.family: Theme.monoFamily
                                    elide: Text.ElideLeft
                                }

                                Item { width: 1; height: 1 }

                                Item {
                                    width: parent.width
                                    height: Theme.dp(16)

                                    Text {
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: recentCard.typeKey.toUpperCase()
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.dp(8)
                                        font.family: Theme.monoFamily
                                    }

                                    Row {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.dp(5)

                                        Text {
                                            visible: recentCard.fileSize.length > 0
                                            text: recentCard.fileSize
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.dp(8.5)
                                            font.family: Theme.monoFamily
                                        }

                                        Text {
                                            text: qsTr("Open")
                                            color: recentCard.accent
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            font.weight: Font.DemiBold
                                        }

                                        Icon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "arrow-right"
                                            size: Theme.dp(12)
                                            color: recentCard.accent
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                id: recentArea
                                anchors.fill: parent
                                onClicked: root.openPathRequested(recentCard.rawPath)
                                onPressAndHold: {
                                    recentOptionsSheet.targetFilePath = recentCard.rawPath
                                    recentOptionsSheet.targetFileName = recentCard.fileName
                                    recentOptionsSheet.open()
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.dp(112)
                        radius: Theme.radiusCard
                        color: Theme.surface1
                        border.color: Theme.borderSubtle
                        border.width: 1
                        visible: root.recentCount === 0

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.dp(14)
                            spacing: Theme.dp(12)

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.dp(40)
                                height: Theme.dp(40)
                                radius: Theme.radiusSm
                                color: Theme.alpha(Theme.colorModel, 0.14)

                                Icon {
                                    anchors.centerIn: parent
                                    name: "folder-open"
                                    size: Theme.dp(19)
                                    color: Theme.colorModel
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Theme.dp(40) - Theme.dp(12) - emptyAction.width - Theme.dp(12)
                                spacing: Theme.dp(3)

                                Text {
                                    text: qsTr("No recent assets yet")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.dp(13)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    width: parent.width
                                    text: qsTr("Files you open are pinned here for a one-tap return.")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Rectangle {
                                id: emptyAction
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.dp(78)
                                height: Theme.dp(36)
                                radius: Theme.radiusSm
                                color: emptyArea.pressed ? Theme.surface3 : Theme.alpha(Theme.accentInk, 0.14)
                                border.color: Theme.alpha(Theme.accentInk, 0.30)
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Browse")
                                    color: Theme.accentInk
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.DemiBold
                                }

                                MouseArea {
                                    id: emptyArea
                                    anchors.fill: parent
                                    onClicked: root.openFilesRequested()
                                }
                            }
                        }
                    }
                }
            }

            // ── Project onboarding ─────────────────────────────────────────
            Rectangle {
                id: projectSection
                width: parent.width
                implicitHeight: projectColumn.implicitHeight + Theme.dp(18) * 2
                radius: Theme.radiusLg
                color: Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1
                opacity: root.sectionOpacity(4)
                transform: Translate { y: root.sectionShift(4) }

                Column {
                    id: projectColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(18)
                    spacing: Theme.dp(12)

                    Row {
                        width: parent.width
                        spacing: Theme.dp(10)

                        Rectangle {
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.accentInk, 0.14)

                            Icon {
                                anchors.centerIn: parent
                                name: "gem"
                                size: Theme.dp(18)
                                color: Theme.accentInk
                            }
                        }

                        Column {
                            width: parent.width - Theme.dp(36) - Theme.dp(10)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Projects, end to end")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.dp(15)
                                font.weight: Font.Bold
                            }

                            Text {
                                text: qsTr("One folder, one descriptor, no extracted APK required.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Column {
                        id: stepsColumn
                        width: parent.width
                        spacing: Theme.dp(5)

                        Repeater {
                            model: root.projectSteps

                            Row {
                                width: stepsColumn.width
                                spacing: Theme.dp(9)

                                Rectangle {
                                    width: Theme.dp(24)
                                    height: Theme.dp(24)
                                    radius: Theme.dp(12)
                                    color: Theme.alpha(Theme.accentInk, 0.14)
                                    border.color: Theme.alpha(Theme.accentInk, 0.26)
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.n
                                        color: Theme.accentInk
                                        font.pixelSize: Theme.dp(9)
                                        font.weight: Font.Bold
                                        font.family: Theme.monoFamily
                                    }
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.dp(24) - Theme.dp(9)
                                    spacing: Theme.dp(1)

                                    Text {
                                        text: modelData.title
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.dp(11.5)
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        width: parent.width
                                        text: modelData.body
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.dp(9.5)
                                        wrapMode: Text.WordWrap
                                        lineHeight: 1.12
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(10)

                        PrimaryButton {
                            height: Theme.dp(40)
                            text: qsTr("Create project")
                            iconName: "plus"
                            onClicked: {
                                root.haptic()
                                newProjectDialog.open()
                            }
                        }

                        Rectangle {
                            width: openLabel.implicitWidth + Theme.dp(28)
                            height: Theme.dp(40)
                            radius: Theme.radiusSm
                            color: openArea.pressed ? Theme.surface2 : "transparent"
                            border.color: Theme.borderSubtle
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "folder"
                                    size: Theme.dp(15)
                                    color: Theme.textSecondary
                                }

                                Text {
                                    id: openLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Browse files")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                id: openArea
                                anchors.fill: parent
                                onClicked: root.openFilesRequested()
                            }
                        }
                    }
                }
            }

            // ── Toolbox ────────────────────────────────────────────────────
            Item {
                id: toolboxSection
                width: parent.width
                implicitHeight: toolboxColumn.implicitHeight
                opacity: root.sectionOpacity(5)
                transform: Translate { y: root.sectionShift(5) }

                Column {
                    id: toolboxColumn
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Item {
                        width: parent.width
                        height: Theme.dp(24)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Toolbox")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(16)
                            font.weight: Font.Bold
                        }

                        Item {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: allToolsRow.implicitWidth
                            height: Theme.dp(20)

                            Row {
                                id: allToolsRow
                                anchors.centerIn: parent
                                spacing: Theme.dp(3)

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("All tools")
                                    color: allToolsArea.pressed ? Theme.accentEnd : Theme.accentInk
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.DemiBold
                                }

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "chevron-right"
                                    size: Theme.dp(13)
                                    color: Theme.accentInk
                                }
                            }

                            MouseArea {
                                id: allToolsArea
                                anchors.fill: parent
                                anchors.margins: -Theme.dp(8)
                                onClicked: root.openToolsRequested("")
                            }
                        }
                    }

                    Grid {
                        id: toolGrid
                        width: parent.width
                        columns: 2
                        spacing: Theme.dp(Theme.spacingSm)

                        Repeater {
                            model: root.toolbox

                            Rectangle {
                                id: toolCard
                                width: (toolGrid.width - Theme.dp(Theme.spacingSm)) / 2
                                height: Theme.dp(82)
                                radius: Theme.radiusMd
                                color: toolArea.pressed ? Theme.surface2 : Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1
                                scale: toolArea.pressed ? 0.97 : 1.0
                                Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(11)
                                    spacing: Theme.dp(9)

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.dp(34)
                                        height: Theme.dp(34)
                                        radius: Theme.radiusSm
                                        color: Theme.alpha(modelData.color, 0.14)

                                        Icon {
                                            anchors.centerIn: parent
                                            name: modelData.icon
                                            size: Theme.dp(17)
                                            color: modelData.color
                                        }
                                    }

                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - Theme.dp(34) - Theme.dp(9) - Theme.dp(16)
                                        spacing: Theme.dp(2)

                                        Text {
                                            width: parent.width
                                            text: modelData.title
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.dp(12)
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            width: parent.width
                                            text: modelData.body
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.dp(9)
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "chevron-right"
                                        size: Theme.dp(13)
                                        color: Theme.textMuted
                                    }
                                }

                                MouseArea {
                                    id: toolArea
                                    anchors.fill: parent
                                    onClicked: root.openTool(modelData)
                                }
                            }
                        }
                    }
                }
            }

            // ── Guides ─────────────────────────────────────────────────────
            Item {
                id: guidesSection
                width: parent.width
                implicitHeight: guidesColumn.implicitHeight
                opacity: root.sectionOpacity(6)
                transform: Translate { y: root.sectionShift(6) }

                Column {
                    id: guidesColumn
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Item {
                        width: parent.width
                        height: Theme.dp(24)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Field notes")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(16)
                            font.weight: Font.Bold
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Modding guides")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontSm)
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
                               : ((typeof toolsBridge !== "undefined" && toolsBridge)
                                  ? toolsBridge.getModdingGuides() : [])

                        delegate: GuideCard {
                            title: modelData.title
                            category: modelData.category
                            tag: modelData.tag
                            description: modelData.desc
                            readTime: modelData.readTime
                            svgSource: modelData.svg
                            qrcPath: modelData.qrcPath
                            onClicked: {
                                guideReader.open(modelData.id, modelData.title, modelData.category,
                                                 modelData.svg, modelData.qrcPath)
                            }
                        }
                    }
                }
            }

            // ── Footer ─────────────────────────────────────────────────────
            Item {
                width: parent.width
                height: Theme.dp(28)
                opacity: root.sectionOpacity(7)

                Text {
                    anchors.centerIn: parent
                    text: qsTr("RUBY TOUCH  /  PROJECT SPEC v2.1")
                    color: Theme.textMuted
                    font.pixelSize: Theme.dp(8.5)
                    font.family: Theme.monoFamily
                    font.letterSpacing: Theme.dp(0.8)
                }
            }
        }
    }

    // ── Context actions for a recent asset ─────────────────────────────────
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
                    var path = recentOptionsSheet.targetFilePath
                    var lastSlash = path.lastIndexOf("/")
                    var folder = lastSlash > 0 ? path.substring(0, lastSlash) : "/storage/emulated/0"
                    root.openFolderRequested(folder)
                }
            }

            MenuRow {
                width: parent.width
                iconName: "link"
                text: qsTr("Copy Full Path")
                onClicked: {
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel)
                        rubyFileModel.copyToClipboard(recentOptionsSheet.targetFilePath)
                    recentOptionsSheet.close()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "trash"
                text: qsTr("Remove from Recents")
                danger: true
                onClicked: {
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel)
                        rubyFileModel.removeRecentFile(recentOptionsSheet.targetFilePath)
                    recentOptionsSheet.close()
                }
            }
        }
    }

    // ── Markdown guide reader ───────────────────────────────────────────────
    GuideReaderView {
        id: guideReader
        anchors.fill: parent
        z: 100
    }
}
