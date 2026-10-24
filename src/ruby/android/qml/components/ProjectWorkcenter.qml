import QtQuick
import QtQuick.Controls
import ".."
import "."

// ============================================================================
// ProjectWorkcenter.qml - Interactive home surface for an open Ruby project.
//
// Layout contract with HomeView.qml is unchanged:
//   signals openFilesRequested() and exitProjectRequested()
//
// Three tabs (Overview / Assets / Publish) share the live RubyProject state:
// diff counters, scan progress, target ABIs, installed rbpkg libraries and
// Launcher metadata. Destructive actions stay behind the typed "Ruby Delete"
// confirmation required by the project specification.
// ============================================================================
Item {
    id: root

    width: parent ? parent.width : 400
    implicitHeight: mainCol.implicitHeight + Theme.dp(8)

    signal openFilesRequested()
    signal exitProjectRequested()

    property int tabIndex: 0
    property real intro: 0
    property int confirmMode: 0   // 0 = delete, 1 = revert
    property string bannerText: ""
    property bool bannerError: false

    readonly property var tabs: [qsTr("Overview"), qsTr("Assets"), qsTr("Publish")]

    readonly property bool projectReady: (typeof rubyProject !== "undefined" && rubyProject)
    readonly property bool isScanning: projectReady ? rubyProject.isScanning : false
    readonly property int scanProgress: projectReady ? rubyProject.scanProgress : 0
    readonly property int modifiedCount: projectReady ? rubyProject.modifiedFileCount : 0
    readonly property int newCount: projectReady ? rubyProject.newFileCount : 0
    readonly property int dirtyCount: modifiedCount + newCount
    readonly property bool clean: dirtyCount === 0

    readonly property string projectName: projectReady ? rubyProject.projectName : qsTr("Ruby Project")
    readonly property string projectId: projectReady ? rubyProject.projectId : ""
    readonly property string projectAuthor: projectReady ? rubyProject.projectAuthor : ""
    readonly property string projectVersion: projectReady ? rubyProject.projectVersion : ""
    readonly property string projectDescription: projectReady ? rubyProject.projectDescription : ""
    readonly property string projectRoot: projectReady ? rubyProject.projectRoot : ""
    readonly property string projectAssetsDir: projectReady ? rubyProject.projectAssetsDir : ""
    readonly property string descriptorPath: projectReady ? rubyProject.rbpFilePath : ""
    readonly property string manifestPath: projectReady ? rubyProject.propertiesTomlPath : ""
    readonly property string iconSource: projectReady ? rubyProject.iconPath : ""
    readonly property string lastScan: projectReady ? rubyProject.lastScanTimeStr : qsTr("Never")
    readonly property var installedPackages: (projectReady && rubyProject.installedPackages)
                                             ? rubyProject.installedPackages : []
    readonly property int packageCount: installedPackages.length
    readonly property var targetAbis: (projectReady && rubyProject.targetAbis) ? rubyProject.targetAbis : []
    readonly property string targetAbiLabel: targetAbis.length > 0 ? targetAbis.join(" + ") : qsTr("not set")

    readonly property bool idValid: {
        if (projectId.length === 0)
            return false
        return /^[a-z0-9_]+(\.[a-z0-9_]+)+$/.test(projectId)
    }

    NumberAnimation on intro {
        from: 0
        to: 1
        duration: 620
        easing.type: Easing.OutCubic
    }

    Timer {
        id: bannerTimer
        interval: 3600
        onTriggered: root.bannerText = ""
    }

    function haptic(ms) {
        if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled
                && typeof androidContext !== "undefined" && androidContext
                && typeof androidContext.vibrate === "function") {
            androidContext.vibrate(ms || 12)
        }
    }

    function sectionOpacity(index) {
        var value = (root.intro - index * 0.08) / 0.30
        return 0.05 + 0.95 * Math.max(0, Math.min(1, value))
    }

    function sectionShift(index) {
        var value = (root.intro - index * 0.08) / 0.30
        return (1 - Math.max(0, Math.min(1, value))) * 16
    }

    function syncTint() {
        if (root.isScanning)
            return Theme.accentInk
        return root.clean ? Theme.colorSuccessBright : Theme.colorWarning
    }

    function syncTitle() {
        if (root.isScanning)
            return qsTr("Scanning project assets")
        if (root.clean)
            return qsTr("Everything matches vanilla")
        return qsTr("%1 modified  ·  %2 new").arg(root.modifiedCount).arg(root.newCount)
    }

    function syncDetail() {
        if (root.isScanning)
            return qsTr("Hashing changed files and refreshing the diff ledger.")
        if (root.clean)
            return qsTr("No asset differs from the baseline. Export would be empty.")
        return qsTr("Changed files are included in the Launcher package.")
    }

    function showBanner(message, isError) {
        if (!message || message.length === 0)
            return
        root.bannerText = message
        root.bannerError = isError
        bannerTimer.restart()
    }

    function copyPath(path) {
        if (typeof rubyFileModel !== "undefined" && rubyFileModel)
            rubyFileModel.copyToClipboard(path)
        showBanner(qsTr("Copied to clipboard"), false)
    }

    function runProjectAction(action) {
        root.haptic()
        switch (action) {
        case "browse":
            if (typeof rubyFileModel !== "undefined" && rubyFileModel && root.projectAssetsDir.length > 0)
                rubyFileModel.navigateTo(root.projectAssetsDir)
            root.openFilesRequested()
            break
        case "root":
            if (typeof rubyFileModel !== "undefined" && rubyFileModel && root.projectRoot.length > 0)
                rubyFileModel.navigateTo(root.projectRoot)
            root.openFilesRequested()
            break
        case "export":
            root.tabIndex = 2
            break
        case "scan":
            if (root.projectReady)
                rubyProject.refreshDiffContext()
            break
        }
    }

    function requestConfirm(mode) {
        root.haptic()
        root.confirmMode = mode
        confirmInput.text = ""
        confirmDialog.open()
    }

    function applyConfirm() {
        var text = confirmInput.text
        confirmInput.text = ""
        confirmDialog.close()
        if (!root.projectReady)
            return
        if (root.confirmMode === 0)
            rubyProject.deleteProject(text)
        else
            rubyProject.revertProjectToVanilla(text)
    }

    function handleBack() {
        if (confirmDialog.visible) {
            confirmDialog.close()
            return true
        }
        return false
    }

    Connections {
        target: (typeof rubyProject !== "undefined" && rubyProject) ? rubyProject : null

        function onToastRequested(message, isError) {
            root.showBanner(message, isError)
        }
    }

    Column {
        id: mainCol
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.dp(10)

        // ── Hero ───────────────────────────────────────────────────────────
        Rectangle {
            id: hero
            width: parent.width
            implicitHeight: Theme.dp(126)
            radius: Theme.radiusLg
            border.color: Theme.alpha(Theme.accentInk, 0.30)
            border.width: 1
            clip: true
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.accentDeep }
                GradientStop { position: 0.58; color: Theme.surface1 }
                GradientStop { position: 1.0; color: Theme.surface2 }
            }
            opacity: root.sectionOpacity(0)
            transform: Translate { y: root.sectionShift(0) }

            Icon {
                anchors.right: parent.right
                anchors.rightMargin: -Theme.dp(38)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -Theme.dp(58)
                name: "gem"
                size: Theme.dp(180)
                weight: 1.0
                color: Theme.alpha(Theme.onAccent, 0.06)
                rotation: 18
            }

            Row {
                anchors.fill: parent
                anchors.margins: Theme.dp(14)
                spacing: Theme.dp(12)

                Rectangle {
                    width: Theme.dp(36)
                    height: Theme.dp(36)
                    radius: Theme.radiusSm
                    color: exitArea.pressed ? Theme.alpha(Theme.surface3, 0.95) : Theme.alpha(Theme.surfaceGlass, 0.82)
                    border.color: Theme.alpha(Theme.onAccent, 0.14)
                    border.width: 1

                    Icon {
                        anchors.centerIn: parent
                        name: "arrow-left"
                        size: Theme.dp(17)
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: exitArea
                        anchors.fill: parent
                        onClicked: {
                            root.haptic()
                            if (root.projectReady)
                                rubyProject.closeProject()
                            root.exitProjectRequested()
                        }
                    }
                }

                Rectangle {
                    width: Theme.dp(50)
                    height: Theme.dp(50)
                    radius: Theme.radiusMd
                    color: Theme.alpha(Theme.onAccent, 0.10)
                    border.color: Theme.alpha(Theme.onAccent, 0.18)
                    border.width: 1
                    clip: true

                    Image {
                        id: projectIcon
                        anchors.fill: parent
                        anchors.margins: Theme.dp(3)
                        source: root.iconSource
                        visible: status === Image.Ready
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        asynchronous: true
                    }

                    Icon {
                        anchors.centerIn: parent
                        visible: projectIcon.status !== Image.Ready
                        name: "gem"
                        size: Theme.dp(24)
                        color: Theme.accentInk
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.dp(36) - Theme.dp(50) - Theme.dp(24)
                    spacing: Theme.dp(4)

                    Text {
                        width: parent.width
                        text: root.projectName
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(19)
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(5)

                        Text {
                            width: parent.width - copyIdButton.width - Theme.dp(4)
                            text: root.projectId
                            color: Theme.alpha(Theme.textPrimary, 0.82)
                            font.pixelSize: Theme.dp(9.5)
                            font.family: Theme.monoFamily
                            elide: Text.ElideMiddle
                        }

                        IconButton {
                            id: copyIdButton
                            iconName: "copy"
                            variant: "plain"
                            buttonSize: Theme.dp(22)
                            iconSize: Theme.dp(12)
                            tint: Theme.alpha(Theme.textPrimary, 0.55)
                            onClicked: root.copyPath(root.projectId)
                        }
                    }

                    Row {
                        spacing: Theme.dp(6)

                        Rectangle {
                            height: Theme.dp(20)
                            width: versionLabel.implicitWidth + Theme.dp(14)
                            radius: height / 2
                            color: Theme.alpha(Theme.accentInk, 0.18)
                            border.color: Theme.alpha(Theme.accentInk, 0.28)
                            border.width: 1

                            Text {
                                id: versionLabel
                                anchors.centerIn: parent
                                text: "v" + (root.projectVersion.length > 0 ? root.projectVersion : "0.0.0")
                                color: Theme.accentInk
                                font.pixelSize: Theme.dp(9)
                                font.family: Theme.monoFamily
                                font.weight: Font.DemiBold
                            }
                        }

                        Rectangle {
                            height: Theme.dp(20)
                            width: Math.max(Theme.dp(42), Math.min(abiLabel.implicitWidth + Theme.dp(14),
                                                                     parent.width - versionLabel.implicitWidth - Theme.dp(6)))
                            radius: height / 2
                            color: Theme.alpha(Theme.onAccent, 0.08)
                            clip: true

                            Text {
                                id: abiLabel
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Theme.dp(7)
                                anchors.rightMargin: Theme.dp(7)
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.targetAbiLabel
                                color: Theme.alpha(Theme.textPrimary, 0.78)
                                font.pixelSize: Theme.dp(8.5)
                                font.family: Theme.monoFamily
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        // ── Live counters ──────────────────────────────────────────────────
        Row {
            id: statRow
            width: parent.width
            spacing: Theme.dp(Theme.spacingSm)
            opacity: root.sectionOpacity(1)
            transform: Translate { y: root.sectionShift(1) }

            Repeater {
                model: [
                    { label: qsTr("MODIFIED"), value: root.modifiedCount, icon: "pencil",
                      tint: Theme.colorWarning, tab: 1 },
                    { label: qsTr("NEW"), value: root.newCount, icon: "plus",
                      tint: Theme.colorSuccessBright, tab: 1 },
                    { label: qsTr("LIBRARIES"), value: root.packageCount, icon: "archive",
                      tint: Theme.accentInk, tab: 2 }
                ]

                Rectangle {
                    id: statTile
                    width: (statRow.width - Theme.dp(Theme.spacingSm) * 2) / 3
                    height: Theme.dp(82)
                    radius: Theme.radiusMd
                    color: statArea.pressed ? Theme.surface2 : Theme.surface1
                    border.color: Theme.alpha(modelData.tint, 0.22)
                    border.width: 1
                    scale: statArea.pressed ? 0.96 : 1.0
                    Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

                    property int valueCount: modelData.value
                    property int animatedCount: 0

                    NumberAnimation {
                        id: countAnim
                        target: statTile
                        property: "animatedCount"
                        to: statTile.valueCount
                        duration: 620
                        easing.type: Easing.OutCubic
                    }

                    Component.onCompleted: countAnim.start()
                    onValueCountChanged: {
                        countAnim.stop()
                        countAnim.to = valueCount
                        countAnim.start()
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.dp(2)

                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: Theme.dp(4)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: Theme.dp(12)
                                color: modelData.tint
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(8)
                                font.weight: Font.Bold
                                font.letterSpacing: Theme.dp(0.7)
                                font.family: Theme.monoFamily
                            }
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Math.round(statTile.animatedCount)
                            color: modelData.tint
                            font.pixelSize: Theme.dp(24)
                            font.weight: Font.Bold
                            font.family: Theme.monoFamily
                        }
                    }

                    MouseArea {
                        id: statArea
                        anchors.fill: parent
                        onClicked: {
                            root.haptic()
                            root.tabIndex = modelData.tab
                        }
                    }
                }
            }
        }

        // ── Segmented tabs ─────────────────────────────────────────────────
        Rectangle {
            id: tabTrack
            width: parent.width
            height: Theme.dp(44)
            radius: Theme.radiusCard
            color: Theme.surface1
            border.color: Theme.borderSubtle
            border.width: 1
            opacity: root.sectionOpacity(2)
            transform: Translate { y: root.sectionShift(2) }

            Rectangle {
                id: tabThumb
                y: Theme.dp(4)
                height: Theme.dp(36)
                width: (tabTrack.width - Theme.dp(8)) / 3
                x: Theme.dp(4) + root.tabIndex * width
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.accentInk, 0.16)
                border.color: Theme.alpha(Theme.accentInk, 0.30)
                border.width: 1
                Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            }

            Row {
                anchors.fill: parent
                anchors.margins: Theme.dp(4)

                Repeater {
                    model: root.tabs

                    Item {
                        width: (tabTrack.width - Theme.dp(8)) / 3
                        height: Theme.dp(36)

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: root.tabIndex === index ? Theme.textPrimary : Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: root.tabIndex === index ? Font.DemiBold : Font.Normal
                            Behavior on color { ColorAnimation { duration: Theme.durMed } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (root.tabIndex !== index) {
                                    root.haptic()
                                    root.tabIndex = index
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Inline action banner ───────────────────────────────────────────
        Rectangle {
            width: parent.width
            height: root.bannerText.length > 0 ? Theme.dp(42) : 0
            visible: root.bannerText.length > 0
            radius: Theme.radiusSm
            color: Theme.alpha(root.bannerError ? Theme.colorError : Theme.colorSuccessBright, 0.13)
            border.color: Theme.alpha(root.bannerError ? Theme.colorError : Theme.colorSuccessBright, 0.28)
            border.width: 1
            clip: true

            Row {
                anchors.fill: parent
                anchors.leftMargin: Theme.dp(11)
                anchors.rightMargin: Theme.dp(8)
                spacing: Theme.dp(8)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.bannerError ? "alert" : "check-circle"
                    size: Theme.dp(15)
                    color: root.bannerError ? Theme.colorErrorBright : Theme.colorSuccessBright
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.dp(30)
                    text: root.bannerText
                    color: Theme.textPrimary
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.bannerText = ""
            }
        }

        // ── Overview ───────────────────────────────────────────────────────
        Column {
            id: overviewPanel
            width: parent.width
            spacing: Theme.dp(10)
            visible: root.tabIndex === 0
            opacity: root.sectionOpacity(3)
            transform: Translate { y: root.sectionShift(3) }

            Rectangle {
                id: scanCard
                width: parent.width
                implicitHeight: scanColumn.implicitHeight + Theme.dp(26)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.alpha(root.syncTint(), 0.26)
                border.width: 1

                Column {
                    id: scanColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(13)
                    spacing: Theme.dp(9)

                    Row {
                        width: parent.width
                        spacing: Theme.dp(10)

                        Rectangle {
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(root.syncTint(), 0.14)

                            Icon {
                                id: scanIcon
                                anchors.centerIn: parent
                                name: "refresh"
                                size: Theme.dp(18)
                                color: root.syncTint()

                                RotationAnimator on rotation {
                                    from: 0
                                    to: 360
                                    duration: 1400
                                    loops: Animation.Infinite
                                    running: root.isScanning
                                }
                            }
                        }

                        Column {
                            width: parent.width - Theme.dp(36) - Theme.dp(10) - statusPill.width - Theme.dp(8)
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.dp(2)

                            Text {
                                width: parent.width
                                text: root.syncTitle()
                                color: Theme.textPrimary
                                font.pixelSize: Theme.dp(13)
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: root.syncDetail()
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(9.5)
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            id: statusPill
                            anchors.verticalCenter: parent.verticalCenter
                            height: Theme.dp(20)
                            width: statusLabel.implicitWidth + Theme.dp(14)
                            radius: height / 2
                            color: Theme.alpha(root.syncTint(), 0.14)
                            border.color: Theme.alpha(root.syncTint(), 0.26)
                            border.width: 1

                            Text {
                                id: statusLabel
                                anchors.centerIn: parent
                                text: root.isScanning
                                      ? (root.scanProgress + "%")
                                      : (root.clean ? qsTr("CLEAN") : qsTr("DIRTY"))
                                color: root.syncTint()
                                font.pixelSize: Theme.dp(8)
                                font.weight: Font.Bold
                                font.family: Theme.monoFamily
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.dp(4)
                        radius: height / 2
                        color: Theme.surface2
                        visible: root.isScanning
                        clip: true

                        Rectangle {
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accentInk
                            width: parent.width * Math.max(0, Math.min(1, root.scanProgress / 100.0))
                            Behavior on width { NumberAnimation { duration: 180 } }
                        }
                    }

                    Item {
                        width: parent.width
                        height: Theme.dp(26)

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Last verified %1").arg(root.lastScan)
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(9)
                            font.family: Theme.monoFamily
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: scanNowLabel.implicitWidth + Theme.dp(30)
                            height: Theme.dp(26)
                            radius: height / 2
                            color: scanNowArea.pressed ? Theme.surface3 : Theme.alpha(Theme.accentInk, 0.14)
                            border.color: Theme.alpha(Theme.accentInk, 0.28)
                            border.width: 1
                            opacity: root.isScanning ? 0.55 : 1.0

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(4)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "refresh"
                                    size: Theme.dp(11)
                                    color: Theme.accentInk
                                }

                                Text {
                                    id: scanNowLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Scan now")
                                    color: Theme.accentInk
                                    font.pixelSize: Theme.dp(9.5)
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                id: scanNowArea
                                anchors.fill: parent
                                enabled: !root.isScanning
                                onClicked: root.runProjectAction("scan")
                            }
                        }
                    }
                }
            }

            Grid {
                id: actionGrid
                width: parent.width
                columns: 2
                spacing: Theme.dp(Theme.spacingSm)

                Repeater {
                    model: [
                        { icon: "folder", title: qsTr("Browse assets"), sub: qsTr("project/ tree"), action: "browse", tint: Theme.colorModel },
                        { icon: "download", title: qsTr("Export mod"), sub: qsTr("Launcher package"), action: "export", tint: Theme.accentInk },
                        { icon: "folder-open", title: qsTr("Project root"), sub: qsTr("Descriptor and metadata"), action: "root", tint: Theme.colorScene },
                        { icon: "refresh", title: qsTr("Rescan"), sub: qsTr("Diff against vanilla"), action: "scan", tint: Theme.colorCode }
                    ]

                    Rectangle {
                        id: actionTile
                        width: (actionGrid.width - Theme.dp(Theme.spacingSm)) / 2
                        height: Theme.dp(66)
                        radius: Theme.radiusMd
                        color: actionArea.pressed ? Theme.surface2 : Theme.surface1
                        border.color: Theme.borderSubtle
                        border.width: 1
                        scale: actionArea.pressed ? 0.97 : 1.0
                        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.dp(10)
                            spacing: Theme.dp(8)

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.dp(30)
                                height: Theme.dp(30)
                                radius: Theme.radiusSm
                                color: Theme.alpha(modelData.tint, 0.14)

                                Icon {
                                    anchors.centerIn: parent
                                    name: modelData.icon
                                    size: Theme.dp(15)
                                    color: modelData.tint
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Theme.dp(30) - Theme.dp(8)
                                spacing: Theme.dp(1)

                                Text {
                                    width: parent.width
                                    text: modelData.title
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.dp(10.5)
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.sub
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.dp(8.5)
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        MouseArea {
                            id: actionArea
                            anchors.fill: parent
                            onClicked: root.runProjectAction(modelData.action)
                        }
                    }
                }
            }

            Rectangle {
                id: detailsCard
                width: parent.width
                implicitHeight: detailsColumn.implicitHeight + Theme.dp(26)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1

                Column {
                    id: detailsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(13)
                    spacing: Theme.dp(7)

                    Text {
                        text: qsTr("Project details")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(13)
                        font.weight: Font.Bold
                    }

                    Text {
                        width: parent.width
                        visible: root.projectDescription.length > 0
                        text: root.projectDescription
                        color: Theme.textSecondary
                        font.pixelSize: Theme.dp(10.5)
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(8)

                        Text {
                            width: Theme.dp(74)
                            text: qsTr("Author")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(10)
                        }

                        Text {
                            width: parent.width - Theme.dp(82)
                            text: root.projectAuthor.length > 0 ? root.projectAuthor : qsTr("Not set")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(10.5)
                            elide: Text.ElideRight
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(8)

                        Text {
                            width: Theme.dp(74)
                            text: qsTr("Version")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(10)
                        }

                        Text {
                            width: parent.width - Theme.dp(82)
                            text: root.projectVersion.length > 0 ? root.projectVersion : qsTr("Not set")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(10.5)
                            font.family: Theme.monoFamily
                            elide: Text.ElideRight
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(8)

                        Text {
                            width: Theme.dp(74)
                            text: qsTr("Targets")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(10)
                        }

                        Text {
                            width: parent.width - Theme.dp(82)
                            text: root.targetAbiLabel
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(10)
                            font.family: Theme.monoFamily
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

                    Text {
                        text: qsTr("DESCRIPTOR")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(8)
                        font.weight: Font.Bold
                        font.letterSpacing: Theme.dp(0.8)
                        font.family: Theme.monoFamily
                    }

                    Item {
                        width: parent.width
                        height: Theme.dp(24)

                        Text {
                            anchors.left: parent.left
                            anchors.right: descriptorCopy.left
                            anchors.rightMargin: Theme.dp(6)
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.descriptorPath
                            color: Theme.textSecondary
                            font.pixelSize: Theme.dp(9)
                            font.family: Theme.monoFamily
                            elide: Text.ElideLeft
                        }

                        IconButton {
                            id: descriptorCopy
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "copy"
                            variant: "plain"
                            buttonSize: Theme.dp(24)
                            iconSize: Theme.dp(12)
                            tint: Theme.textMuted
                            onClicked: root.copyPath(root.descriptorPath)
                        }
                    }

                    Text {
                        text: qsTr("LAUNCHER MANIFEST")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(8)
                        font.weight: Font.Bold
                        font.letterSpacing: Theme.dp(0.8)
                        font.family: Theme.monoFamily
                    }

                    Item {
                        width: parent.width
                        height: Theme.dp(24)

                        Text {
                            anchors.left: parent.left
                            anchors.right: manifestCopy.left
                            anchors.rightMargin: Theme.dp(6)
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.manifestPath
                            color: Theme.textSecondary
                            font.pixelSize: Theme.dp(9)
                            font.family: Theme.monoFamily
                            elide: Text.ElideLeft
                        }

                        IconButton {
                            id: manifestCopy
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "copy"
                            variant: "plain"
                            buttonSize: Theme.dp(24)
                            iconSize: Theme.dp(12)
                            tint: Theme.textMuted
                            onClicked: root.copyPath(root.manifestPath)
                        }
                    }
                }
            }

            Rectangle {
                id: maintenanceCard
                width: parent.width
                implicitHeight: maintenanceColumn.implicitHeight + Theme.dp(26)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.alpha(Theme.colorDanger, 0.24)
                border.width: 1

                Column {
                    id: maintenanceColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(13)
                    spacing: Theme.dp(8)

                    Row {
                        width: parent.width
                        spacing: Theme.dp(8)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "alert"
                            size: Theme.dp(15)
                            color: Theme.colorDanger
                        }

                        Column {
                            width: parent.width - Theme.dp(23)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Project maintenance")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.dp(12.5)
                                font.weight: Font.Bold
                            }

                            Text {
                                text: qsTr("Destructive actions require typing Ruby Delete.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(9)
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.dp(50)
                        radius: Theme.radiusSm
                        color: revertArea.pressed ? Theme.surface3 : Theme.surface2

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.dp(10)
                            anchors.rightMargin: Theme.dp(10)
                            spacing: Theme.dp(9)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "rotate-ccw"
                                size: Theme.dp(16)
                                color: Theme.colorWarning
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Theme.dp(16) - Theme.dp(9) - Theme.dp(14)
                                spacing: Theme.dp(1)

                                Text {
                                    text: qsTr("Revert project to vanilla")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.dp(10.5)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    text: qsTr("Restore every asset to its baseline state")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.dp(8.5)
                                    elide: Text.ElideRight
                                    width: parent.width
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
                            id: revertArea
                            anchors.fill: parent
                            onClicked: root.requestConfirm(1)
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.dp(50)
                        radius: Theme.radiusSm
                        color: deleteArea.pressed ? Theme.alpha(Theme.colorDanger, 0.18) : Theme.alpha(Theme.colorDanger, 0.10)
                        border.color: Theme.alpha(Theme.colorDanger, 0.22)
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.dp(10)
                            anchors.rightMargin: Theme.dp(10)
                            spacing: Theme.dp(9)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "trash"
                                size: Theme.dp(16)
                                color: Theme.colorDanger
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - Theme.dp(16) - Theme.dp(9) - Theme.dp(14)
                                spacing: Theme.dp(1)

                                Text {
                                    text: qsTr("Delete project permanently")
                                    color: Theme.colorDanger
                                    font.pixelSize: Theme.dp(10.5)
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    text: qsTr("Remove the full project folder from this device")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.dp(8.5)
                                    elide: Text.ElideRight
                                    width: parent.width
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
                            id: deleteArea
                            anchors.fill: parent
                            onClicked: root.requestConfirm(0)
                        }
                    }
                }
            }
        }

        // ── Assets ─────────────────────────────────────────────────────────
        Column {
            id: assetsPanel
            width: parent.width
            spacing: Theme.dp(10)
            visible: root.tabIndex === 1
            opacity: root.sectionOpacity(3)
            transform: Translate { y: root.sectionShift(3) }

            Row {
                id: diffSummary
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                Rectangle {
                    width: (diffSummary.width - Theme.dp(Theme.spacingSm)) / 2
                    height: Theme.dp(104)
                    radius: Theme.radiusCard
                    color: Theme.surface1
                    border.color: Theme.alpha(Theme.colorWarning, 0.26)
                    border.width: 1

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.dp(12)
                        spacing: Theme.dp(3)

                        Row {
                            spacing: Theme.dp(5)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "pencil"
                                size: Theme.dp(13)
                                color: Theme.colorWarning
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("MODIFIED")
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(8.5)
                                font.weight: Font.Bold
                                font.letterSpacing: Theme.dp(0.8)
                                font.family: Theme.monoFamily
                            }
                        }

                        Text {
                            text: root.modifiedCount
                            color: Theme.colorWarning
                            font.pixelSize: Theme.dp(27)
                            font.weight: Font.Bold
                            font.family: Theme.monoFamily
                        }

                        Text {
                            width: parent.width
                            text: qsTr("Hash differs from the vanilla baseline.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(8.5)
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: (diffSummary.width - Theme.dp(Theme.spacingSm)) / 2
                    height: Theme.dp(104)
                    radius: Theme.radiusCard
                    color: Theme.surface1
                    border.color: Theme.alpha(Theme.colorSuccessBright, 0.26)
                    border.width: 1

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.dp(12)
                        spacing: Theme.dp(3)

                        Row {
                            spacing: Theme.dp(5)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "plus"
                                size: Theme.dp(13)
                                color: Theme.colorSuccessBright
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("NEW")
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(8.5)
                                font.weight: Font.Bold
                                font.letterSpacing: Theme.dp(0.8)
                                font.family: Theme.monoFamily
                            }
                        }

                        Text {
                            text: root.newCount
                            color: Theme.colorSuccessBright
                            font.pixelSize: Theme.dp(27)
                            font.weight: Font.Bold
                            font.family: Theme.monoFamily
                        }

                        Text {
                            width: parent.width
                            text: qsTr("Not present in the vanilla manifest.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(8.5)
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: Theme.dp(52)
                radius: Theme.radiusMd
                color: Theme.alpha(Theme.colorSuccessBright, 0.10)
                border.color: Theme.alpha(Theme.colorSuccessBright, 0.26)
                border.width: 1
                visible: root.clean

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(12)
                    anchors.rightMargin: Theme.dp(12)
                    spacing: Theme.dp(9)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "check-circle"
                        size: Theme.dp(18)
                        color: Theme.colorSuccessBright
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(30)
                        text: qsTr("Vanilla-clean. Export would contain no game assets.")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(10)
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: ledgerColumn.implicitHeight + Theme.dp(26)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1

                Column {
                    id: ledgerColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(13)
                    spacing: Theme.dp(8)

                    Text {
                        text: qsTr("Diff ledger")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(13)
                        font.weight: Font.Bold
                    }

                    Text {
                        width: parent.width
                        text: qsTr("Ruby compares every file in project/ against the embedded vanilla manifest. Only modified and new assets are packed.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(9.5)
                        wrapMode: Text.WordWrap
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(9)

                        Rectangle {
                            width: Theme.dp(30)
                            height: Theme.dp(30)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorWarning, 0.14)

                            Icon {
                                anchors.centerIn: parent
                                name: "pencil"
                                size: Theme.dp(15)
                                color: Theme.colorWarning
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(30) - Theme.dp(9) - Theme.dp(28)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Modified assets")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.dp(10.5)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: parent.width
                                text: qsTr("Same path, different content hash.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(8.5)
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.modifiedCount
                            color: Theme.colorWarning
                            font.pixelSize: Theme.dp(13)
                            font.weight: Font.Bold
                            font.family: Theme.monoFamily
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(9)

                        Rectangle {
                            width: Theme.dp(30)
                            height: Theme.dp(30)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorSuccessBright, 0.14)

                            Icon {
                                anchors.centerIn: parent
                                name: "plus"
                                size: Theme.dp(15)
                                color: Theme.colorSuccessBright
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(30) - Theme.dp(9) - Theme.dp(28)
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("New assets")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.dp(10.5)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: parent.width
                                text: qsTr("Not part of the vanilla game.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.dp(8.5)
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.newCount
                            color: Theme.colorSuccessBright
                            font.pixelSize: Theme.dp(13)
                            font.weight: Font.Bold
                            font.family: Theme.monoFamily
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

                    Row {
                        width: parent.width

                        Text {
                            width: parent.width - totalPill.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Queued for export")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.dp(10)
                        }

                        Rectangle {
                            id: totalPill
                            height: Theme.dp(22)
                            width: totalLabel.implicitWidth + Theme.dp(16)
                            radius: height / 2
                            color: Theme.alpha(Theme.accentInk, 0.14)

                            Text {
                                id: totalLabel
                                anchors.centerIn: parent
                                text: qsTr("%1 files").arg(root.dirtyCount)
                                color: Theme.accentInk
                                font.pixelSize: Theme.dp(9)
                                font.weight: Font.DemiBold
                                font.family: Theme.monoFamily
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: hintRow.implicitHeight + Theme.dp(22)
                radius: Theme.radiusMd
                color: Theme.alpha(Theme.colorModel, 0.08)
                border.color: Theme.alpha(Theme.colorModel, 0.20)
                border.width: 1

                Row {
                    id: hintRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(11)
                    spacing: Theme.dp(8)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "info"
                        size: Theme.dp(15)
                        color: Theme.colorModel
                    }

                    Text {
                        width: parent.width - Theme.dp(23)
                        text: qsTr("Deleting a vanilla file cannot remove it from the installed APK. The Launcher falls back to the original asset at runtime.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.dp(9)
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.dp(8)

                PrimaryButton {
                    width: (parent.width - Theme.dp(8)) / 2
                    height: Theme.dp(42)
                    text: qsTr("Scan now")
                    iconName: "refresh"
                    enabled: !root.isScanning
                    onClicked: root.runProjectAction("scan")
                }

                Rectangle {
                    width: (parent.width - Theme.dp(8)) / 2
                    height: Theme.dp(42)
                    radius: Theme.radiusSm
                    color: browseAssetsArea.pressed ? Theme.surface2 : "transparent"
                    border.color: Theme.borderStrong
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
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Browse assets")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.dp(10.5)
                            font.weight: Font.DemiBold
                        }
                    }

                    MouseArea {
                        id: browseAssetsArea
                        anchors.fill: parent
                        onClicked: root.runProjectAction("browse")
                    }
                }
            }
        }

        // ── Publish ────────────────────────────────────────────────────────
        Column {
            id: publishPanel
            width: parent.width
            spacing: Theme.dp(10)
            visible: root.tabIndex === 2
            opacity: root.sectionOpacity(3)
            transform: Translate { y: root.sectionShift(3) }

            Rectangle {
                id: exportCard
                width: parent.width
                implicitHeight: exportColumn.implicitHeight + Theme.dp(30)
                radius: Theme.radiusLg
                border.color: Theme.alpha(Theme.accentInk, 0.30)
                border.width: 1
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.accentDeep }
                    GradientStop { position: 0.70; color: Theme.surface1 }
                    GradientStop { position: 1.0; color: Theme.surface1 }
                }

                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: -Theme.dp(24)
                    anchors.top: parent.top
                    anchors.topMargin: -Theme.dp(26)
                    name: "upload"
                    size: Theme.dp(122)
                    weight: 1.2
                    color: Theme.alpha(Theme.onAccent, 0.06)
                }

                Column {
                    id: exportColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(16)
                    spacing: Theme.dp(9)

                    Row {
                        spacing: Theme.dp(9)

                        Rectangle {
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.onAccent, 0.12)

                            Icon {
                                anchors.centerIn: parent
                                name: "upload"
                                size: Theme.dp(18)
                                color: Theme.accentInk
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.dp(1)

                            Text {
                                text: qsTr("Launcher package")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.dp(14)
                                font.weight: Font.Bold
                            }

                            Text {
                                text: qsTr("Only new and modified assets are packed.")
                                color: Theme.alpha(Theme.textPrimary, 0.68)
                                font.pixelSize: Theme.dp(9)
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.onAccent, 0.12) }

                    Row {
                        width: parent.width
                        height: Theme.dp(22)
                        spacing: Theme.dp(7)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: root.idValid ? "check-circle" : "alert"
                            size: Theme.dp(13)
                            color: root.idValid ? Theme.colorSuccessBright : Theme.colorWarning
                        }

                        Text {
                            width: parent.width - Theme.dp(20) - checklistValueId.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Launcher mod ID")
                            color: Theme.alpha(Theme.textPrimary, 0.82)
                            font.pixelSize: Theme.dp(9.5)
                        }

                        Text {
                            id: checklistValueId
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.projectId.length > 0 ? root.projectId : qsTr("missing")
                            color: root.idValid ? Theme.textPrimary : Theme.colorWarning
                            font.pixelSize: Theme.dp(8.5)
                            font.family: Theme.monoFamily
                            elide: Text.ElideMiddle
                        }
                    }

                    Row {
                        width: parent.width
                        height: Theme.dp(22)
                        spacing: Theme.dp(7)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: root.projectVersion.length > 0 ? "check-circle" : "alert"
                            size: Theme.dp(13)
                            color: root.projectVersion.length > 0 ? Theme.colorSuccessBright : Theme.colorWarning
                        }

                        Text {
                            width: parent.width - Theme.dp(20) - checklistValueVersion.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Version")
                            color: Theme.alpha(Theme.textPrimary, 0.82)
                            font.pixelSize: Theme.dp(9.5)
                        }

                        Text {
                            id: checklistValueVersion
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.projectVersion.length > 0 ? root.projectVersion : qsTr("missing")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(8.5)
                            font.family: Theme.monoFamily
                        }
                    }

                    Row {
                        width: parent.width
                        height: Theme.dp(22)
                        spacing: Theme.dp(7)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: root.targetAbis.length > 0 ? "check-circle" : "alert"
                            size: Theme.dp(13)
                            color: root.targetAbis.length > 0 ? Theme.colorSuccessBright : Theme.colorWarning
                        }

                        Text {
                            width: parent.width - Theme.dp(20) - checklistValueAbi.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Target ABIs")
                            color: Theme.alpha(Theme.textPrimary, 0.82)
                            font.pixelSize: Theme.dp(9.5)
                        }

                        Text {
                            id: checklistValueAbi
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.targetAbis.length > 0 ? root.targetAbis.join(" + ") : qsTr("missing")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(8.5)
                            font.family: Theme.monoFamily
                        }
                    }

                    Row {
                        width: parent.width
                        height: Theme.dp(22)
                        spacing: Theme.dp(7)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: root.dirtyCount > 0 ? "check-circle" : "info"
                            size: Theme.dp(13)
                            color: root.dirtyCount > 0 ? Theme.colorSuccessBright : Theme.colorModel
                        }

                        Text {
                            width: parent.width - Theme.dp(20) - checklistValueAssets.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Assets to include")
                            color: Theme.alpha(Theme.textPrimary, 0.82)
                            font.pixelSize: Theme.dp(9.5)
                        }

                        Text {
                            id: checklistValueAssets
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 files").arg(root.dirtyCount)
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(8.5)
                            font.family: Theme.monoFamily
                        }
                    }

                    Row {
                        width: parent.width
                        height: Theme.dp(22)
                        spacing: Theme.dp(7)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "check-circle"
                            size: Theme.dp(13)
                            color: Theme.colorSuccessBright
                        }

                        Text {
                            width: parent.width - Theme.dp(20) - checklistValueLibs.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("rbpkg libraries")
                            color: Theme.alpha(Theme.textPrimary, 0.82)
                            font.pixelSize: Theme.dp(9.5)
                        }

                        Text {
                            id: checklistValueLibs
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("%1 bundled").arg(root.packageCount)
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(8.5)
                            font.family: Theme.monoFamily
                        }
                    }

                    Item { width: 1; height: Theme.dp(2) }

                    PrimaryButton {
                        width: parent.width
                        height: Theme.dp(46)
                        text: qsTr("Export to Launcher")
                        iconName: "download"
                        onClicked: {
                            root.haptic(18)
                            if (root.projectReady)
                                rubyProject.exportToLauncher()
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.dp(40)
                        radius: Theme.radiusSm
                        color: reviewArea.pressed ? Theme.alpha(Theme.onAccent, 0.10) : "transparent"
                        border.color: Theme.alpha(Theme.onAccent, 0.20)
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(6)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "eye"
                                size: Theme.dp(14)
                                color: Theme.alpha(Theme.textPrimary, 0.82)
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Review included assets")
                                color: Theme.alpha(Theme.textPrimary, 0.82)
                                font.pixelSize: Theme.dp(9.5)
                                font.weight: Font.DemiBold
                            }
                        }

                        MouseArea {
                            id: reviewArea
                            anchors.fill: parent
                            onClicked: {
                                root.haptic()
                                root.tabIndex = 1
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: packagesColumn.implicitHeight + Theme.dp(26)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1

                Column {
                    id: packagesColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(13)
                    spacing: Theme.dp(7)

                    Row {
                        width: parent.width

                        Text {
                            width: parent.width - packagesPill.width
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Bundled libraries")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(13)
                            font.weight: Font.Bold
                        }

                        Rectangle {
                            id: packagesPill
                            height: Theme.dp(20)
                            width: packagesLabel.implicitWidth + Theme.dp(16)
                            radius: height / 2
                            color: Theme.alpha(Theme.accentInk, 0.13)

                            Text {
                                id: packagesLabel
                                anchors.centerIn: parent
                                text: root.packageCount
                                color: Theme.accentInk
                                font.pixelSize: Theme.dp(9)
                                font.weight: Font.Bold
                                font.family: Theme.monoFamily
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: root.packageCount === 0
                        text: qsTr("No rbpkg libraries installed yet. Runtime and devtime .so files appear here once added to plugins/.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.dp(9.5)
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.installedPackages

                        Item {
                            width: packagesColumn.width
                            height: Theme.dp(40)

                            Row {
                                anchors.fill: parent
                                spacing: Theme.dp(8)

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.dp(28)
                                    height: Theme.dp(28)
                                    radius: Theme.radiusXs
                                    color: Theme.alpha(Theme.colorArchive, 0.14)

                                    Icon {
                                        anchors.centerIn: parent
                                        name: "chip"
                                        size: Theme.dp(14)
                                        color: Theme.colorArchive
                                    }
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.dp(28) - Theme.dp(8) - typePill.width - Theme.dp(6)
                                    spacing: Theme.dp(1)

                                    Text {
                                        width: parent.width
                                        text: modelData.id
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.dp(10)
                                        font.family: Theme.monoFamily
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: "v" + modelData.version
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.dp(8)
                                        font.family: Theme.monoFamily
                                    }
                                }

                                Rectangle {
                                    id: typePill
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: Theme.dp(18)
                                    width: typeLabel.implicitWidth + Theme.dp(12)
                                    radius: height / 2
                                    color: modelData.type === "devtime"
                                           ? Theme.alpha(Theme.colorTexture, 0.16)
                                           : Theme.alpha(Theme.colorSuccessBright, 0.14)

                                    Text {
                                        id: typeLabel
                                        anchors.centerIn: parent
                                        text: modelData.type
                                        color: modelData.type === "devtime"
                                               ? Theme.colorTexture : Theme.colorSuccessBright
                                        font.pixelSize: Theme.dp(8)
                                        font.family: Theme.monoFamily
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: manifestNote.implicitHeight + Theme.dp(24)
                radius: Theme.radiusMd
                color: Theme.alpha(Theme.colorModel, 0.08)
                border.color: Theme.alpha(Theme.colorModel, 0.20)
                border.width: 1

                Row {
                    id: manifestNote
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(12)
                    spacing: Theme.dp(8)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "lock"
                        size: Theme.dp(15)
                        color: Theme.colorModel
                    }

                    Text {
                        width: parent.width - Theme.dp(23)
                        text: qsTr("properties.toml stays Launcher-only. Ruby writes id, name, author, version and description; build flags and package data live in the .rbp descriptor.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.dp(9)
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                    }
                }
            }
        }
    }

    // ── Typed confirmation for destructive actions ─────────────────────────
    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        width: Math.min((parent ? parent.width : 360) * 0.92, Theme.dp(360))
        modal: true
        dim: true
        closePolicy: Dialog.CloseOnEscape

        background: Rectangle {
            color: Theme.surface2
            radius: Theme.radiusCard
            border.color: root.confirmMode === 0 ? Theme.colorDanger : Theme.colorWarning
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: Theme.dp(11)

            Row {
                width: parent.width
                spacing: Theme.dp(8)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "alert"
                    size: Theme.dp(20)
                    color: root.confirmMode === 0 ? Theme.colorDanger : Theme.colorWarning
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.dp(28)
                    text: root.confirmMode === 0
                          ? qsTr("Delete project permanently?")
                          : qsTr("Revert every asset to vanilla?")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.dp(15)
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.confirmMode === 0
                      ? qsTr("The whole project folder, including project/ and .rbp/, will be removed from this device. This cannot be undone.")
                      : qsTr("Every asset under project/ is restored to its vanilla baseline. Your edits will be lost.")
                color: Theme.textSecondary
                font.pixelSize: Theme.dp(10.5)
                lineHeight: 1.25
            }

            Text {
                width: parent.width
                text: qsTr("Type Ruby Delete to confirm.")
                color: Theme.textPrimary
                font.pixelSize: Theme.dp(10.5)
            }

            TextField {
                id: confirmInput
                width: parent.width
                placeholderText: "Ruby Delete"
                font.pixelSize: Theme.dp(13)
                font.family: Theme.monoFamily
                color: Theme.textPrimary
                background: Rectangle {
                    color: Theme.surface1
                    radius: Theme.radiusSm
                    border.color: confirmInput.text === "Ruby Delete"
                                  ? (root.confirmMode === 0 ? Theme.colorDanger : Theme.colorWarning)
                                  : Theme.borderSubtle
                    border.width: 1
                }
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.dp(8)

                Button {
                    text: qsTr("Cancel")
                    onClicked: {
                        confirmInput.text = ""
                        confirmDialog.close()
                    }
                }

                Button {
                    text: root.confirmMode === 0 ? qsTr("Delete") : qsTr("Revert")
                    enabled: confirmInput.text === "Ruby Delete"
                    onClicked: root.applyConfirm()
                }
            }
        }
    }
}
