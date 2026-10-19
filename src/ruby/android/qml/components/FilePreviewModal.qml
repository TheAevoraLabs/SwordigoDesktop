import QtQuick
import QtQuick.Controls
import ".."

// Index FM style unified File Preview & Properties Modal for Ruby Touch
Rectangle {
    id: root

    anchors.fill: parent
    color: Theme.alpha("#000000", 0.75)
    visible: opacity > 0
    opacity: 0
    z: 200

    property string targetPath: ""
    property string targetName: ""
    property string targetFileType: ""
    property bool targetIsDir: false
    property var fileProps: ({})
    property string activeTab: "preview" // "preview" | "properties" | "archive"
    property string codeSnippet: ""
    property var archiveEntries: []
    property string md5Hash: ""

    signal fileOpenRequested(string path, string fileType)
    signal startSceneRequested(string path)
    signal extractArchiveRequested(string path, bool toSubfolder)
    signal extractLuaRequested(string path)
    signal convertPodRequested(string path)

    Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    function openWith(path, name, fileType, isDir) {
        targetPath = path
        targetName = name
        targetFileType = fileType
        targetIsDir = isDir
        activeTab = "preview"
        md5Hash = ""
        codeSnippet = ""
        archiveEntries = []

        if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
            fileProps = rubyFileModel.getDetailedFileProperties(path)

            if (fileType === "code" || path.endsWith(".lua") || path.endsWith(".scl") || path.endsWith(".json") || path.endsWith(".txt") || path.endsWith(".scene")) {
                codeSnippet = rubyFileModel.readFileSnippet(path, 120)
            } else if (fileType === "archive" || path.endsWith(".zip") || path.endsWith(".apk")) {
                archiveEntries = rubyFileModel.getArchiveEntries(path)
            }
        }
        opacity = 1
    }

    function close() {
        opacity = 0
    }

    // Modal Sheet Card
    Rectangle {
        id: modalSheet
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        width: Math.min(parent.width, Theme.dp(620))
        height: Math.min(parent.height * 0.88, Theme.dp(640))
        radius: Theme.radiusLg
        color: Theme.surface0
        border.color: Theme.borderFocus
        border.width: 1
        clip: true

        y: root.opacity > 0 ? (parent.height - height) : parent.height
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

        Column {
            anchors.fill: parent
            spacing: 0

            // ── Top Drag Pill & Header ──────────────────────────────────────
            Item {
                width: parent.width
                height: Theme.dp(72)

                // Drag indicator pill
                Rectangle {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.dp(8)
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.dp(36)
                    height: Theme.dp(4)
                    radius: Theme.dp(2)
                    color: Theme.borderSubtle
                }

                // File Icon + Title + Close Button
                Row {
                    anchors.fill: parent
                    anchors.topMargin: Theme.dp(16)
                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                    anchors.rightMargin: Theme.dp(Theme.spacingMd)
                    spacing: Theme.dp(Theme.spacingMd)

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(42)
                        height: Theme.dp(42)
                        radius: Theme.radiusMd
                        color: Theme.alpha(Theme.colorForType(root.targetFileType), 0.16)
                        border.color: Theme.alpha(Theme.colorForType(root.targetFileType), 0.35)
                        border.width: 1

                        Icon {
                            anchors.centerIn: parent
                            name: root.targetIsDir ? "folder" : Theme.iconForType(root.targetFileType)
                            size: Theme.dp(22)
                            color: Theme.colorForType(root.targetFileType)
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(42) - Theme.dp(36) - Theme.dp(Theme.spacingMd) * 2
                        spacing: Theme.dp(2)

                        Text {
                            width: parent.width
                            text: root.targetName
                            font.pixelSize: Theme.dp(Theme.fontLg)
                            font.weight: Font.Bold
                            color: Theme.textPrimary
                            elide: Text.ElideMiddle
                        }

                        Text {
                            width: parent.width
                            text: (root.fileProps.sizeStr ? root.fileProps.sizeStr : "") + "  ·  " + (root.fileProps.modifiedStr ? root.fileProps.modifiedStr : "")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textSecondary
                            elide: Text.ElideRight
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
            }

            // ── Tab Bar (Index FM style) ────────────────────────────────────
            Rectangle {
                width: parent.width
                height: Theme.dp(42)
                color: Theme.surface1

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.dp(Theme.spacingSm)

                    // Tab 1: Preview
                    Rectangle {
                        width: Theme.dp(110)
                        height: Theme.dp(32)
                        radius: Theme.radiusSm
                        color: root.activeTab === "preview" ? Theme.accentInk : "transparent"
                        border.color: root.activeTab === "preview" ? Theme.accentInk : Theme.borderSubtle
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "eye"
                                size: Theme.dp(14)
                                color: root.activeTab === "preview" ? Theme.onAccent : Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Preview")
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: root.activeTab === "preview" ? Font.Bold : Font.Normal
                                color: root.activeTab === "preview" ? Theme.onAccent : Theme.textSecondary
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.activeTab = "preview"
                        }
                    }

                    // Tab 2: Properties
                    Rectangle {
                        width: Theme.dp(110)
                        height: Theme.dp(32)
                        radius: Theme.radiusSm
                        color: root.activeTab === "properties" ? Theme.accentInk : "transparent"
                        border.color: root.activeTab === "properties" ? Theme.accentInk : Theme.borderSubtle
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "info"
                                size: Theme.dp(14)
                                color: root.activeTab === "properties" ? Theme.onAccent : Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Properties")
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: root.activeTab === "properties" ? Font.Bold : Font.Normal
                                color: root.activeTab === "properties" ? Theme.onAccent : Theme.textSecondary
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.activeTab = "properties"
                        }
                    }

                    // Tab 3: Archive Contents (only if archive)
                    Rectangle {
                        width: Theme.dp(120)
                        height: Theme.dp(32)
                        radius: Theme.radiusSm
                        visible: root.targetFileType === "archive" || root.targetName.toLowerCase().endsWith(".zip") || root.targetName.toLowerCase().endsWith(".apk")
                        color: root.activeTab === "archive" ? Theme.accentInk : "transparent"
                        border.color: root.activeTab === "archive" ? Theme.accentInk : Theme.borderSubtle
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "archive"
                                size: Theme.dp(14)
                                color: root.activeTab === "archive" ? Theme.onAccent : Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Archive (%1)").arg(root.archiveEntries.length)
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: root.activeTab === "archive" ? Font.Bold : Font.Normal
                                color: root.activeTab === "archive" ? Theme.onAccent : Theme.textSecondary
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.activeTab = "archive"
                        }
                    }
                }
            }

            // ── Scrollable Tab Content ──────────────────────────────────────
            Flickable {
                width: parent.width
                height: parent.height - Theme.dp(72) - Theme.dp(42) - Theme.dp(64)
                contentHeight: contentColumn.implicitHeight + Theme.dp(Theme.spacingLg)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: contentColumn
                    width: parent.width - Theme.dp(Theme.spacingMd) * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: Theme.dp(Theme.spacingMd)
                    spacing: Theme.dp(Theme.spacingMd)

                    // ── View A: PREVIEW TAB ─────────────────────────────────
                    Column {
                        width: parent.width
                        visible: root.activeTab === "preview"
                        spacing: Theme.dp(Theme.spacingMd)

                        // 1. Image Preview
                        Item {
                            width: parent.width
                            height: Theme.dp(220)
                            visible: root.targetFileType === "texture" || root.targetName.toLowerCase().endsWith(".png") || root.targetName.toLowerCase().endsWith(".jpg") || root.targetName.toLowerCase().endsWith(".jpeg")

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMd
                                color: Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1
                                clip: true

                                Image {
                                    id: previewImg
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width - 16, sourceSize.width > 0 ? sourceSize.width : parent.width)
                                    height: Math.min(parent.height - 16, sourceSize.height > 0 ? sourceSize.height : parent.height)
                                    source: visible ? ("file://" + root.targetPath) : ""
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }

                                Text {
                                    anchors.bottom: parent.bottom
                                    anchors.right: parent.right
                                    anchors.margins: Theme.dp(8)
                                    text: previewImg.sourceSize.width > 0 ? (previewImg.sourceSize.width + " × " + previewImg.sourceSize.height + " px") : ""
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    color: Theme.textMuted
                                }
                            }
                        }

                        // 2. Text / Code Snippet Preview (Monospace dark viewer)
                        Item {
                            width: parent.width
                            height: Theme.dp(240)
                            visible: root.codeSnippet.length > 0

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMd
                                color: "#060709"
                                border.color: Theme.borderSubtle
                                border.width: 1
                                clip: true

                                Flickable {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(10)
                                    contentWidth: codeText.implicitWidth
                                    contentHeight: codeText.implicitHeight
                                    clip: true

                                    Text {
                                        id: codeText
                                        text: root.codeSnippet
                                        font.family: "Monospace, Courier New, sans-serif"
                                        font.pixelSize: Theme.dp(11)
                                        color: "#A6ACCD"
                                        wrapMode: Text.NoWrap
                                    }
                                }
                            }
                        }

                        // 3. Scene Quick Specs Card
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(96)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1
                            visible: root.targetFileType === "scene"

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(12)
                                spacing: Theme.dp(6)

                                Row {
                                    spacing: Theme.dp(8)
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "map"
                                        size: Theme.dp(16)
                                        color: Theme.colorScene
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Swordigo 3D Level Scene")
                                        font.pixelSize: Theme.dp(Theme.fontMd)
                                        font.weight: Font.DemiBold
                                        color: Theme.textPrimary
                                    }
                                }

                                Text {
                                    text: qsTr("Directly bootable in Swordigo runner or editable in 3D Viewport with terrain mesh sculpting.")
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    color: Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                    width: parent.width
                                }
                            }
                        }

                        // 4. Model / POD Quick Specs Card
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(88)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1
                            visible: root.targetFileType === "model"

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(12)
                                spacing: Theme.dp(6)

                                Row {
                                    spacing: Theme.dp(8)
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "cube"
                                        size: Theme.dp(16)
                                        color: Theme.colorModel
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("3D Game Geometry (.POD / Mesh)")
                                        font.pixelSize: Theme.dp(Theme.fontMd)
                                        font.weight: Font.DemiBold
                                        color: Theme.textPrimary
                                    }
                                }

                                Text {
                                    text: qsTr("Swordigo native PowerVR 3D model format or source GLTF/OBJ asset.")
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    color: Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                    width: parent.width
                                }
                            }
                        }

                        // Audio Preview Card (Index FM AudioPreview style)
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(136)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1
                            visible: root.targetFileType === "audio" || root.targetName.toLowerCase().endsWith(".wav") || root.targetName.toLowerCase().endsWith(".ogg") || root.targetName.toLowerCase().endsWith(".mp3")

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(12)
                                spacing: Theme.dp(10)

                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(10)

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.dp(40)
                                        height: Theme.dp(40)
                                        radius: Theme.radiusSm
                                        color: Theme.alpha(Theme.colorAudio, 0.16)
                                        border.color: Theme.alpha(Theme.colorAudio, 0.35)
                                        border.width: 1

                                        Icon {
                                            anchors.centerIn: parent
                                            name: "volume"
                                            size: Theme.dp(20)
                                            color: Theme.colorAudio
                                        }
                                    }

                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - Theme.dp(40) - Theme.dp(10)
                                        spacing: Theme.dp(2)

                                        Text {
                                            width: parent.width
                                            text: root.targetName
                                            font.pixelSize: Theme.dp(Theme.fontMd)
                                            font.weight: Font.DemiBold
                                            color: Theme.textPrimary
                                            elide: Text.ElideMiddle
                                        }

                                        Text {
                                            text: qsTr("Swordigo Engine Sound Asset")
                                            font.pixelSize: Theme.dp(Theme.fontXs)
                                            color: Theme.textMuted
                                        }
                                    }
                                }

                                // Quick Sanitize Audio button inside preview
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    height: Theme.dp(36)
                                    width: parent.width
                                    radius: Theme.radiusSm
                                    color: sanAudM.pressed ? Theme.surface3 : Theme.alpha(Theme.colorAudio, 0.14)
                                    border.color: Theme.alpha(Theme.colorAudio, 0.38)
                                    border.width: 1

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: Theme.dp(6)
                                        Icon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "sparkles"
                                            size: Theme.dp(14)
                                            color: Theme.colorAudio
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("Sanitize Audio for Swordigo Engine")
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            font.weight: Font.DemiBold
                                            color: Theme.colorAudio
                                        }
                                    }

                                    MouseArea {
                                        id: sanAudM
                                        anchors.fill: parent
                                        onClicked: {
                                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                                rubyFileModel.sanitizeAudioForEngine(root.targetPath, true)
                                                root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── View B: PROPERTIES TAB (Index FM style) ─────────────
                    Column {
                        width: parent.width
                        visible: root.activeTab === "properties"
                        spacing: Theme.dp(Theme.spacingSm)

                        // Path row
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(62)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1

                            Row {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(10)
                                spacing: Theme.dp(10)

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.dp(36)
                                    spacing: Theme.dp(2)

                                    Text {
                                        text: qsTr("Full Path")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                    Text {
                                        width: parent.width
                                        text: root.targetPath
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        color: Theme.textPrimary
                                        elide: Text.ElideMiddle
                                    }
                                }

                                IconButton {
                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: "link"
                                    buttonSize: Theme.dp(32)
                                    iconSize: Theme.dp(14)
                                    onClicked: {
                                        if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                            rubyFileModel.copyToClipboard(root.targetPath)
                                        }
                                    }
                                }
                            }
                        }

                        // Size & Permissions Grid
                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Rectangle {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                height: Theme.dp(58)
                                radius: Theme.radiusMd
                                color: Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(10)
                                    spacing: Theme.dp(2)
                                    Text {
                                        text: qsTr("Size")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                    Text {
                                        text: root.fileProps.sizeStr ? (root.fileProps.sizeStr + " (" + root.fileProps.sizeBytes + " B)") : ""
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.DemiBold
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                            }

                            Rectangle {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                height: Theme.dp(58)
                                radius: Theme.radiusMd
                                color: Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(10)
                                    spacing: Theme.dp(2)
                                    Text {
                                        text: qsTr("Type")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                    Text {
                                        text: root.targetIsDir ? qsTr("Folder") : (root.fileProps.fileType ? root.fileProps.fileType.toUpperCase() : qsTr("File"))
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.DemiBold
                                        color: Theme.accentInk
                                    }
                                }
                            }
                        }

                        // Directory Stats Card (Index FM DirInfo style)
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(72)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1
                            visible: root.targetIsDir

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(10)
                                spacing: Theme.dp(4)

                                Text {
                                    text: qsTr("Directory Contents (Recursive)")
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.weight: Font.DemiBold
                                    color: Theme.textMuted
                                }

                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(16)

                                    Row {
                                        spacing: Theme.dp(4)
                                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "file"; size: Theme.dp(13); color: Theme.textSecondary }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("%1 Files").arg(root.fileProps.filesCount !== undefined ? root.fileProps.filesCount : 0)
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            color: Theme.textPrimary
                                        }
                                    }

                                    Row {
                                        spacing: Theme.dp(4)
                                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "folder"; size: Theme.dp(13); color: Theme.colorFolder }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("%1 Folders").arg(root.fileProps.dirsCount !== undefined ? root.fileProps.dirsCount : 0)
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            color: Theme.textPrimary
                                        }
                                    }

                                    Row {
                                        spacing: Theme.dp(4)
                                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "archive"; size: Theme.dp(13); color: Theme.colorArchive }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root.fileProps.recursiveSizeStr || "0 B"
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            font.weight: Font.DemiBold
                                            color: Theme.accentInk
                                        }
                                    }
                                }
                            }
                        }

                        // Index FM Interactive Permissions Matrix
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(160)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(10)
                                spacing: Theme.dp(6)

                                Row {
                                    width: parent.width
                                    Text {
                                        text: qsTr("POSIX Access Permissions")
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.Bold
                                        color: Theme.textPrimary
                                    }
                                    Item { width: Theme.dp(10); height: 1 }
                                    Text {
                                        text: (root.fileProps.permissionsOctal ? root.fileProps.permissionsOctal : "0755") + "  " + (root.fileProps.permissions ? root.fileProps.permissions : "")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        font.family: "Monospace"
                                        font.weight: Font.DemiBold
                                        color: Theme.accentInk
                                    }
                                }

                                // Header row: User | Read | Write | Execute
                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(4)
                                    Text { width: Theme.dp(60); text: ""; font.pixelSize: Theme.dp(Theme.fontXs) }
                                    Text { width: (parent.width - Theme.dp(60) - 12) / 3; horizontalAlignment: Text.AlignHCenter; text: qsTr("Read (r)"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.textMuted }
                                    Text { width: (parent.width - Theme.dp(60) - 12) / 3; horizontalAlignment: Text.AlignHCenter; text: qsTr("Write (w)"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.textMuted }
                                    Text { width: (parent.width - Theme.dp(60) - 12) / 3; horizontalAlignment: Text.AlignHCenter; text: qsTr("Exec (x)"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.textMuted }
                                }

                                // Row 1: Owner
                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(4)
                                    Text {
                                        width: Theme.dp(60)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Owner")
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.DemiBold
                                        color: Theme.textPrimary
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.ownerRead ? Theme.alpha(Theme.accentInk, 0.20) : Theme.surface2
                                        border.color: root.fileProps.ownerRead ? Theme.accentInk : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "R"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.ownerRead ? Theme.accentInk : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "owner", "read", !root.fileProps.ownerRead); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.ownerWrite ? Theme.alpha(Theme.colorWarning, 0.20) : Theme.surface2
                                        border.color: root.fileProps.ownerWrite ? Theme.colorWarning : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "W"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.ownerWrite ? Theme.colorWarning : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "owner", "write", !root.fileProps.ownerWrite); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.ownerExecute ? Theme.alpha(Theme.colorSuccess, 0.20) : Theme.surface2
                                        border.color: root.fileProps.ownerExecute ? Theme.colorSuccess : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "X"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.ownerExecute ? Theme.colorSuccess : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "owner", "execute", !root.fileProps.ownerExecute); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                }

                                // Row 2: Group
                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(4)
                                    Text {
                                        width: Theme.dp(60)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Group")
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.DemiBold
                                        color: Theme.textPrimary
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.groupRead ? Theme.alpha(Theme.accentInk, 0.20) : Theme.surface2
                                        border.color: root.fileProps.groupRead ? Theme.accentInk : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "R"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.groupRead ? Theme.accentInk : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "group", "read", !root.fileProps.groupRead); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.groupWrite ? Theme.alpha(Theme.colorWarning, 0.20) : Theme.surface2
                                        border.color: root.fileProps.groupWrite ? Theme.colorWarning : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "W"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.groupWrite ? Theme.colorWarning : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "group", "write", !root.fileProps.groupWrite); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.groupExecute ? Theme.alpha(Theme.colorSuccess, 0.20) : Theme.surface2
                                        border.color: root.fileProps.groupExecute ? Theme.colorSuccess : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "X"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.groupExecute ? Theme.colorSuccess : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "group", "execute", !root.fileProps.groupExecute); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                }

                                // Row 3: Others
                                Row {
                                    width: parent.width
                                    spacing: Theme.dp(4)
                                    Text {
                                        width: Theme.dp(60)
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Others")
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.DemiBold
                                        color: Theme.textPrimary
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.otherRead ? Theme.alpha(Theme.accentInk, 0.20) : Theme.surface2
                                        border.color: root.fileProps.otherRead ? Theme.accentInk : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "R"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.otherRead ? Theme.accentInk : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "other", "read", !root.fileProps.otherRead); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.otherWrite ? Theme.alpha(Theme.colorWarning, 0.20) : Theme.surface2
                                        border.color: root.fileProps.otherWrite ? Theme.colorWarning : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "W"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.otherWrite ? Theme.colorWarning : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "other", "write", !root.fileProps.otherWrite); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                    Rectangle {
                                        width: (parent.width - Theme.dp(60) - 12) / 3; height: Theme.dp(26); radius: Theme.radiusSm
                                        color: root.fileProps.otherExecute ? Theme.alpha(Theme.colorSuccess, 0.20) : Theme.surface2
                                        border.color: root.fileProps.otherExecute ? Theme.colorSuccess : Theme.borderSubtle; border.width: 1
                                        Text { anchors.centerIn: parent; text: "X"; font.pixelSize: Theme.dp(Theme.fontXs); font.weight: Font.Bold; color: root.fileProps.otherExecute ? Theme.colorSuccess : Theme.textDisabled }
                                        MouseArea { anchors.fill: parent; onClicked: { rubyFileModel.setFilePermission(root.targetPath, "other", "execute", !root.fileProps.otherExecute); root.fileProps = rubyFileModel.getDetailedFileProperties(root.targetPath); } }
                                    }
                                }
                            }
                        }

                        // Dates Grid
                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Rectangle {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                height: Theme.dp(58)
                                radius: Theme.radiusMd
                                color: Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(10)
                                    spacing: Theme.dp(2)
                                    Text {
                                        text: qsTr("Modified")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                    Text {
                                        text: root.fileProps.modifiedStr ? root.fileProps.modifiedStr : ""
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                            }

                            Rectangle {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                height: Theme.dp(58)
                                radius: Theme.radiusMd
                                color: Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(10)
                                    spacing: Theme.dp(2)
                                    Text {
                                        text: qsTr("Created")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                    Text {
                                        text: root.fileProps.createdStr ? root.fileProps.createdStr : ""
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                            }
                        }

                        // MD5 Checksum Row
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(58)
                            radius: Theme.radiusMd
                            color: Theme.surface1
                            border.color: Theme.borderSubtle
                            border.width: 1
                            visible: !root.targetIsDir

                            Row {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(10)
                                spacing: Theme.dp(10)

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.dp(90)
                                    spacing: Theme.dp(2)

                                    Text {
                                        text: qsTr("MD5 Checksum")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                    Text {
                                        width: parent.width
                                        text: root.md5Hash.length > 0 ? root.md5Hash : qsTr("Tap calculate to verify hash")
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.family: "Monospace"
                                        color: root.md5Hash.length > 0 ? Theme.accentInk : Theme.textDisabled
                                        elide: Text.ElideMiddle
                                    }
                                }

                                PrimaryButton {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.md5Hash.length > 0 ? qsTr("Copy") : qsTr("Calculate")
                                    height: Theme.dp(32)
                                    width: Theme.dp(80)
                                    onClicked: {
                                        if (root.md5Hash.length === 0) {
                                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                                root.md5Hash = rubyFileModel.calculateMd5(root.targetPath)
                                            }
                                        } else {
                                            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                                rubyFileModel.copyToClipboard(root.md5Hash)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── View C: ARCHIVE ENTRIES TAB (Index FM style) ────────
                    Column {
                        width: parent.width
                        visible: root.activeTab === "archive"
                        spacing: Theme.dp(Theme.spacingSm)

                        Repeater {
                            model: root.archiveEntries

                            Rectangle {
                                width: parent.width
                                height: Theme.dp(44)
                                radius: Theme.radiusSm
                                color: Theme.surface1
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: Theme.dp(10)
                                    spacing: Theme.dp(10)

                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: modelData.name.endsWith("/") ? "folder" : "file"
                                        size: Theme.dp(16)
                                        color: modelData.name.endsWith("/") ? Theme.colorFolder : Theme.textSecondary
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - Theme.dp(80)
                                        text: modelData.name
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        color: Theme.textPrimary
                                        elide: Text.ElideMiddle
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.sizeStr ? modelData.sizeStr : ""
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        color: Theme.textMuted
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Bottom Action Bar ───────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: Theme.dp(64)
                color: Theme.surface1
                border.color: Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.dp(Theme.spacingMd)

                    // Primary Open button
                    PrimaryButton {
                        visible: !root.targetIsDir && (root.targetFileType === "scene" || root.targetFileType === "model" || root.targetFileType === "code" || root.targetFileType === "texture")
                        text: (root.targetFileType === "scene" || root.targetFileType === "model") ? qsTr("Open in Viewport") : qsTr("Open in Editor")
                        height: Theme.dp(40)
                        width: Theme.dp(150)
                        iconName: (root.targetFileType === "scene" || root.targetFileType === "model") ? "view3d" : "braces"
                        onClicked: {
                            root.close()
                            root.fileOpenRequested(root.targetPath, root.targetFileType)
                        }
                    }

                    // Direct scene boot
                    PrimaryButton {
                        visible: root.targetFileType === "scene" && (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.playInGameAvailable)
                        text: qsTr("Play Scene")
                        height: Theme.dp(40)
                        width: Theme.dp(120)
                        iconName: "play"
                        onClicked: {
                            root.close()
                            root.startSceneRequested(root.targetPath)
                        }
                    }

                    // Extract archive button
                    PrimaryButton {
                        visible: root.targetFileType === "archive"
                        text: qsTr("Extract All")
                        height: Theme.dp(40)
                        width: Theme.dp(130)
                        iconName: "archive"
                        onClicked: {
                            root.close()
                            root.extractArchiveRequested(root.targetPath, true)
                        }
                    }

                    // Close button
                    IconButton {
                        iconName: "check"
                        buttonSize: Theme.dp(40)
                        iconSize: Theme.dp(20)
                        onClicked: root.close()
                    }
                }
            }
        }
    }

    // Dismiss by tapping outside
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.close()
    }
}
