import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    Flickable {
        id: settingsFlickable
        anchors.fill: parent
        contentHeight: settingsCol.implicitHeight + Theme.dp(Theme.spacingXl) * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: settingsCol
            x: Theme.dp(Theme.paddingScreen)
            width: parent.width - Theme.dp(Theme.paddingScreen) * 2
            topPadding: Theme.dp(Theme.spacingMd)
            bottomPadding: Theme.dp(Theme.spacingXl)
            spacing: Theme.dp(Theme.spacingLg)

            // ── Screen Title ────────────────────────────────────────────────
            Column {
                width: parent.width
                spacing: Theme.dp(2)

                Text {
                    text: qsTr("Settings")
                    font.pixelSize: Theme.dp(Theme.fontTitle)
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }

                Text {
                    text: qsTr("Workspace, Viewport & Compiler Configuration")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                }
            }

            // ── 1. User Identity & Avatar Card (Clean M3, Zero AI Slop) ─────
            Rectangle {
                width: parent.width
                implicitHeight: profileRow.implicitHeight + Theme.dp(Theme.spacingLg) * 2
                radius: Theme.radiusCard
                color: Theme.surface1

                Row {
                    id: profileRow
                    anchors.fill: parent
                    anchors.margins: Theme.dp(Theme.spacingLg)
                    spacing: Theme.dp(Theme.spacingMd)

                    // Avatar with interactive quick shuffle
                    Item {
                        width: Theme.dp(56)
                        height: Theme.dp(56)
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: Theme.surface2
                            border.width: 0
                            clip: true

                            // Avatar image preview
                            Image {
                                id: avatarPreview
                                anchors.fill: parent
                                anchors.margins: Theme.dp(3)
                                source: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.avatarImage)
                                        ? rubySettings.avatarImage : ""
                                visible: source.toString().length > 0
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }

                            // Monogram Fallback
                            Text {
                                anchors.centerIn: parent
                                visible: avatarPreview.source.toString().length === 0
                                text: {
                                    var n = (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "R"
                                    return n.length > 0 ? n.charAt(0).toUpperCase() : "R"
                                }
                                font.pixelSize: Theme.dp(22)
                                font.weight: Font.Bold
                                font.family: Theme.monoFamily
                                color: Theme.accentInk
                            }
                        }

                        // Small camera/shuffle badge overlay
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            width: Theme.dp(20)
                            height: Theme.dp(20)
                            radius: Theme.dp(10)
                            color: Theme.accentInk

                            Icon {
                                anchors.centerIn: parent
                                name: "image"
                                size: Theme.dp(11)
                                color: Theme.onAccent
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (typeof rubySettings !== "undefined" && rubySettings) {
                                        rubySettings.randomizeAvatarImage()
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (typeof rubySettings !== "undefined" && rubySettings) {
                                    rubySettings.randomizeAvatarImage()
                                }
                            }
                        }
                    }

                    // User name & workspace text
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(56) - Theme.dp(78) - Theme.dp(Theme.spacingMd) * 2
                        spacing: Theme.dp(3)

                        Text {
                            text: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
                            font.pixelSize: Theme.dp(17)
                            font.weight: Font.Bold
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                            width: parent.width
                        }

                        Text {
                            text: (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.folderName && rubyFileModel.folderName.length > 0)
                                  ? rubyFileModel.folderName : "Local Workspace"
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.family: Theme.monoFamily
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }

                    // Compact action buttons: Edit Name, Small Randomize Name & Random Image
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.dp(6)

                        // Edit Name Button
                        IconButton {
                            iconName: "pencil"
                            buttonSize: Theme.dp(34)
                            iconSize: Theme.dp(16)
                            square: true
                            onClicked: {
                                var cur = (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
                                nameEditDialog.openWith(cur)
                            }
                        }

                        // Compact Randomize Name Button (Small dice/refresh icon)
                        IconButton {
                            iconName: "refresh"
                            tint: Theme.accentInk
                            plate: Theme.alpha(Theme.accentInk, 0.14)
                            plateEdge: Theme.alpha(Theme.accentInk, 0.32)
                            buttonSize: Theme.dp(34)
                            iconSize: Theme.dp(16)
                            square: true
                            onClicked: {
                                if (typeof rubySettings !== "undefined" && rubySettings) {
                                    rubySettings.randomizeUserName()
                                }
                            }
                        }
                    }
                }
            }

            // ── 2. Appearance & Visual Themes (Clean M3 Segmented Selector) ─
            SectionCard {
                width: parent.width
                label: qsTr("Visual Theme & Palette")
                accentIcon: "sliders"

                Text {
                    width: parent.width
                    text: qsTr("Studio theme and contrast tone applied across all views, file explorers, and tools.")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }

                // Grid of Theme Swatches
                Grid {
                    width: parent.width
                    columns: parent.width > Theme.dp(440) ? 3 : 2
                    spacing: Theme.dp(Theme.spacingSm)

                    Repeater {
                        model: Theme.availableThemes

                        Rectangle {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * (parent.columns - 1)) / parent.columns
                            height: Theme.dp(72)
                            radius: Theme.radiusMd
                            color: isSelected ? Theme.surface2 : Theme.surface1
                            readonly property bool isSelected: Theme.activeThemeId === modelData.id
                            border.color: isSelected ? modelData.accent : Theme.borderSubtle
                            border.width: isSelected ? 1.5 : 1

                            Row {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(10)
                                spacing: Theme.dp(10)

                                // Mini Palette Preview (3 circular dots)
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.dp(3)

                                    Row {
                                        spacing: Theme.dp(3)
                                        Rectangle { width: Theme.dp(12); height: Theme.dp(12); radius: Theme.dp(6); color: modelData.bg; border.color: Theme.border; border.width: 1 }
                                        Rectangle { width: Theme.dp(12); height: Theme.dp(12); radius: Theme.dp(6); color: modelData.card; border.color: Theme.border; border.width: 1 }
                                    }
                                    Rectangle { width: Theme.dp(27); height: Theme.dp(8); radius: Theme.dp(4); color: modelData.accent }
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - Theme.dp(38)
                                    spacing: Theme.dp(1)

                                    Text {
                                        text: modelData.name
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: isSelected ? Font.Bold : Font.Medium
                                        color: isSelected ? modelData.accent : Theme.textPrimary
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }

                                    Text {
                                        text: isSelected ? qsTr("Active") : modelData.id.toUpperCase()
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        font.family: Theme.monoFamily
                                        color: isSelected ? Theme.accentInk : Theme.textMuted
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                                        if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                                            androidContext.vibrate(20)
                                        }
                                    }
                                    Theme.setTheme(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            // ── 3. 3D Scene Viewport & Graphics ─────────────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("3D Viewport & Rendering")
                accentIcon: "view3d"

                SettingRow {
                    title: qsTr("Ground Reference Grid")
                    description: qsTr("Render coordinate grid below entities in 3D viewport")
                    leadingIcon: "grid"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.showGrid : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.showGrid = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Wireframe Mesh Overlay")
                    description: qsTr("Render polygonal wireframe edges on 3D geometry")
                    leadingIcon: "cube"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.wireframeMode : false
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.wireframeMode = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Multisample Anti-Aliasing (MSAA)")
                    description: qsTr("4x hardware multisampling for smooth geometric edges")
                    leadingIcon: "layers"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.msaaEnabled : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.msaaEnabled = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Snap Camera on Node Select")
                    description: qsTr("Smoothly focus camera target when selecting an entity")
                    leadingIcon: "target"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.snapCamera : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.snapCamera = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Hold to Select Objects")
                    description: qsTr("Require a deliberate 0.5s press-and-hold to select in 3D scene")
                    leadingIcon: "hand"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.holdToSelect : false
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.holdToSelect = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Ground Mesh Generator")
                    description: qsTr("Engine used to generate procedural ground meshes on edits")
                    leadingIcon: "cube"
                    choiceOptions: [
                        { value: "boulder", label: qsTr("Boulder") },
                        { value: "boulderx", label: qsTr("Zenith (2011)") },
                        { value: "zypher", label: qsTr("Zypher (Gen 3)") }
                    ]
                    choiceValue: (typeof rubySettings !== "undefined" && rubySettings)
                                 ? rubySettings.groundGenerator : "zypher"
                    onChoiceSelected: (value) => {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.groundGenerator = value
                        }
                    }
                }
            }

            // ── 4. Modding Pipeline & Compiler ──────────────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("Modding Pipeline & Compiler")
                accentIcon: "tune"

                SettingRow {
                    title: qsTr("Auto-Compile Protobuf on Save")
                    description: qsTr("Compile .scl and .scene FileRift markup directly to binary Protobuf")
                    leadingIcon: "code"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.autoCompileProtobuf : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.autoCompileProtobuf = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Flip Vertical UV (OpenGL)")
                    description: qsTr("Invert V texture coordinate when converting glTF/FBX/OBJ models to POD")
                    leadingIcon: "image"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.flipUvOnExport : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.flipUvOnExport = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("FileRift Syntax Highlighting")
                    description: qsTr("Apply syntax color grammar in script editor")
                    leadingIcon: "braces"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.syntaxHighlighting : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.syntaxHighlighting = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Code Gutter Line Numbers")
                    description: qsTr("Show numbered gutter column in script editor")
                    leadingIcon: "rule"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.lineNumbers : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.lineNumbers = checked
                        }
                    }
                }
            }

            // ── 5. Workspace & Storage ──────────────────────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("Workspace & Storage")
                accentIcon: "drive"

                SettingRow {
                    title: qsTr("Current Workspace Path")
                    description: qsTr("Location where scenes, models, and scripts reside")
                    leadingIcon: "folder"
                }

                Rectangle {
                    width: parent.width
                    height: Theme.dp(44)
                    radius: Theme.radiusSm
                    color: Theme.surface2

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(Theme.spacingMd)
                        anchors.rightMargin: Theme.dp(Theme.spacingSm)
                        spacing: Theme.dp(Theme.spacingSm)

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "drive"
                            size: Theme.dp(Theme.iconSm)
                            color: Theme.accentInk
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(40)
                            text: (typeof rubyFileModel !== "undefined" && rubyFileModel) ? rubyFileModel.currentPath : "/storage/emulated/0"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.family: Theme.monoFamily
                            elide: Text.ElideMiddle
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Clear Recent Assets History")
                    description: {
                        var count = (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles) ? rubyFileModel.recentFiles.length : 0
                        return qsTr("Clear %1 remembered files from Home dashboard").arg(count)
                    }
                    leadingIcon: "trash"
                    onClicked: clearRecentsDialog.open()
                }
            }

            // ── 6. Preferences & System ─────────────────────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("Preferences & System")
                accentIcon: "heart"

                SettingRow {
                    title: qsTr("Haptic Touch Feedback")
                    description: qsTr("Provide tactile vibrations on button taps and 3D gizmos")
                    leadingIcon: "sliders"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.hapticsEnabled : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.hapticsEnabled = checked
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Reset All Settings")
                    description: qsTr("Restore all viewport, compiler, and modding preferences to defaults")
                    leadingIcon: "refresh"
                    onClicked: resetSettingsDialog.open()
                }
            }

            // ── 7. About & Credits ──────────────────────────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("About Ruby Touch (Ruby Mobile)")
                accentIcon: "info"

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingMd)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "ruby"
                        size: Theme.dp(26)
                        color: Theme.accentInk
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.dp(2)

                        Text {
                            text: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.appName)
                                  ? rubySettings.appName : qsTr("Ruby Touch (Ruby Mobile)")
                            font.pixelSize: Theme.dp(16)
                            font.weight: Font.Bold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: (typeof rubySettings !== "undefined" && rubySettings)
                                  ? (rubySettings.appVersion + " • Qt 6.6.3 Android C++20") : "v1.4.5"
                            font.pixelSize: Theme.dp(11)
                            font.family: Theme.monoFamily
                            color: Theme.textMuted
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("The standalone Swordigo 3D level editor, model converter, and FileRift toolchain for Android.\nResearch and preservation platform for the modding community.")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Crafted for the Swordigo Modding Community by The Aevora Labs & Quantum Creeper.")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    font.weight: Font.Medium
                    color: Theme.accentInk
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.borderSubtle
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Text {
                        text: qsTr("LICENSE")
                        width: Theme.dp(88)
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.Bold
                        font.letterSpacing: 1.1
                        color: Theme.textMuted
                    }

                    Text {
                        width: parent.width - Theme.dp(88) - Theme.dp(Theme.spacingSm)
                        text: (typeof rubySettings !== "undefined" && rubySettings)
                              ? rubySettings.licenseName : "GNU General Public License v3.0"
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ── Dialogs ─────────────────────────────────────────────────────────────
    TextInputDialog {
        id: nameEditDialog
        title: qsTr("Change Display Name")
        label: qsTr("USER / MODDER NAME")
        placeholder: qsTr("Enter your display name")
        acceptText: qsTr("Save Name")
        onAccepted: (val) => {
            if (typeof rubySettings !== "undefined" && rubySettings) {
                rubySettings.userName = val
            }
        }
    }

    ConfirmDialog {
        id: clearRecentsDialog
        title: qsTr("Clear Recent Assets")
        message: qsTr("Are you sure you want to clear your recent assets history? Your files on storage will not be affected.")
        confirmText: qsTr("Clear History")
        danger: true
        onConfirmed: {
            if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                rubyFileModel.clearRecentFiles()
            }
        }
    }

    ConfirmDialog {
        id: resetSettingsDialog
        title: qsTr("Reset Settings")
        message: qsTr("Are you sure you want to restore all viewport, compiler, and modding preferences to defaults?")
        confirmText: qsTr("Reset Defaults")
        danger: true
        onConfirmed: {
            if (typeof rubySettings !== "undefined" && rubySettings) {
                rubySettings.resetToDefaults()
            }
        }
    }
}
