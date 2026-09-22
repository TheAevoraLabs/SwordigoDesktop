import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    Flickable {
        anchors.fill: parent
        contentHeight: settingsCol.implicitHeight + Theme.dp(Theme.spacingXl) * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: settingsCol
            x: Theme.dp(Theme.paddingScreen)
            width: parent.width - Theme.dp(Theme.paddingScreen) * 2
            topPadding: Theme.dp(Theme.spacingLg)
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
                    text: qsTr("Personalize profile, workspace, viewport & modding compiler")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                }
            }

            // ── Section 1: Modder Profile & Identity Hero Card ──────────────
            Rectangle {
                width: parent.width
                height: profileCol.implicitHeight + Theme.dp(Theme.spacingLg) * 2
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: profileCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.dp(Theme.spacingLg)
                    spacing: Theme.dp(Theme.spacingMd)

                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingLg)

                        // Big Avatar with Glow Ring
                        Item {
                            width: Theme.dp(64)
                            height: Theme.dp(64)
                            anchors.verticalCenter: parent.verticalCenter

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + Theme.dp(8)
                                height: parent.height + Theme.dp(8)
                                radius: width / 2
                                color: Theme.alpha(Theme.accentStart, 0.25)
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: width / 2
                                color: "#1A1622"
                                border.color: Theme.accentEnd
                                border.width: Theme.dp(2)

                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        var n = (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "R"
                                        return n.length > 0 ? n.charAt(0).toUpperCase() : "R"
                                    }
                                    font.pixelSize: Theme.dp(24)
                                    font.weight: Font.Bold
                                    font.family: Theme.monoFamily
                                    color: Theme.accentInk
                                }
                            }

                            // Active Online Dot
                            Rectangle {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                width: Theme.dp(14)
                                height: Theme.dp(14)
                                radius: width / 2
                                color: "#10B981"
                                border.color: Theme.surface1
                                border.width: Theme.dp(2.5)
                            }
                        }

                        // Identity Info
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - Theme.dp(64) - Theme.dp(Theme.spacingLg)
                            spacing: Theme.dp(4)

                            Row {
                                spacing: Theme.dp(6)

                                Text {
                                    text: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
                                    font.pixelSize: Theme.dp(17)
                                    font.weight: Font.Bold
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: Theme.dp(18)
                                    width: devTag.implicitWidth + Theme.dp(8)
                                    radius: Theme.radiusPill
                                    color: Theme.alpha(Theme.accentInk, 0.16)
                                    border.color: Theme.alpha(Theme.accentEnd, 0.4)
                                    border.width: 1

                                    Text {
                                        id: devTag
                                        anchors.centerIn: parent
                                        text: "RUBY DEV"
                                        font.pixelSize: Theme.dp(9)
                                        font.weight: Font.Bold
                                        color: Theme.accentInk
                                    }
                                }
                            }

                            Text {
                                text: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userTitle : "Swordigo Modder"
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                color: Theme.textSecondary
                            }

                            Text {
                                text: {
                                    var count = (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.nameDatabaseCount : 200;
                                    return qsTr("%1 Community Names Pool").arg(count);
                                }
                                font.pixelSize: Theme.dp(10)
                                color: Theme.textMuted
                            }
                        }
                    }

                    // Profile Actions: Change Name & Randomize
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        Rectangle {
                            height: Theme.dp(36)
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            radius: Theme.radiusSm
                            color: editNameMouse.pressed ? Theme.surface3 : Theme.surface2
                            border.color: Theme.border
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "pencil"
                                    size: Theme.dp(14)
                                    color: Theme.textPrimary
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Edit Name")
                                    font.pixelSize: Theme.dp(12)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                }
                            }

                            MouseArea {
                                id: editNameMouse
                                anchors.fill: parent
                                onClicked: {
                                    var cur = (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev";
                                    nameEditDialog.openWith(cur);
                                }
                            }
                        }

                        Rectangle {
                            height: Theme.dp(36)
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            radius: Theme.radiusSm
                            color: randMouse.pressed ? Theme.accentEnd : Theme.accentSoft
                            border.color: Theme.alpha(Theme.accentEnd, 0.4)
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "refresh"
                                    size: Theme.dp(14)
                                    color: Theme.accentInk
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Randomize")
                                    font.pixelSize: Theme.dp(12)
                                    font.weight: Font.DemiBold
                                    color: Theme.accentInk
                                }
                            }

                            MouseArea {
                                id: randMouse
                                anchors.fill: parent
                                onClicked: {
                                    if (typeof rubySettings !== "undefined" && rubySettings) {
                                        rubySettings.randomizeUserName();
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Section 2: Workspace & Storage ──────────────────────────────
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
                    border.color: Theme.borderSubtle
                    border.width: 1

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
                            elide: Text.ElideMiddle
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Clear Recent Assets History")
                    description: {
                        var count = (typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.recentFiles) ? rubyFileModel.recentFiles.length : 0;
                        return qsTr("Clear %1 remembered files from Home dashboard").arg(count);
                    }
                    leadingIcon: "trash"
                    onClicked: clearRecentsDialog.open()
                }
            }

            // ── Section 3: 3D Scene Viewport & Graphics ─────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("3D Viewport & Rendering")
                accentIcon: "view3d"

                SettingRow {
                    title: qsTr("Ground Reference Grid")
                    description: qsTr("Draw coordinate reference grid below entities in 3D viewport")
                    leadingIcon: "grid"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.showGrid : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.showGrid = checked;
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Wireframe Mesh Overlay")
                    description: qsTr("Render polygonal wireframe edges on 3D entities")
                    leadingIcon: "cube"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.wireframeMode : false
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.wireframeMode = checked;
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Multisample Anti-Aliasing (MSAA)")
                    description: qsTr("4x hardware multisampling for clean geometry edges on GLES 3.0")
                    leadingIcon: "layers"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.msaaEnabled : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.msaaEnabled = checked;
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Snap Camera on Node Select")
                    description: qsTr("Smoothly focus camera target when selecting an entity in hierarchy")
                    leadingIcon: "target"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.snapCamera : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.snapCamera = checked;
                        }
                    }
                }

                // Which generator rebuilds a ground mesh when a mesh edit is
                // committed. Applies to the next regeneration, not the next
                // launch. BoulderX is Caver-faithful (concave outlines, arcs,
                // per-node depth) but declines shapes it cannot yet reproduce —
                // dome hats — and falls back to Boulder for those, which the
                // mesh-edit HUD reports.
                SettingRow {
                    title: qsTr("Ground Mesh Generator")
                    description: qsTr("Which generator rebuilds a ground mesh when you commit an edit. BoulderX is engine-faithful and falls back to Boulder for shapes it cannot yet reproduce.")
                    leadingIcon: "cube"
                    choiceOptions: [
                        { value: "boulder", label: qsTr("Boulder") },
                        { value: "boulderx", label: qsTr("BoulderX") }
                    ]
                    choiceValue: (typeof rubySettings !== "undefined" && rubySettings)
                                 ? rubySettings.groundGenerator : "boulder"
                    onChoiceSelected: (value) => {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.groundGenerator = value;
                        }
                    }
                }
            }

            // ── Section 4: Modding Compiler & Pipeline ──────────────────────
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
                            rubySettings.autoCompileProtobuf = checked;
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
                            rubySettings.flipUvOnExport = checked;
                        }
                    }
                }

                SettingRow {
                    title: qsTr("FileRift Syntax Highlighting")
                    description: qsTr("Apply syntax color grammar in code and script editor")
                    leadingIcon: "braces"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.syntaxHighlighting : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.syntaxHighlighting = checked;
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
                            rubySettings.lineNumbers = checked;
                        }
                    }
                }
            }

            // ── Section 5: Experience & Preferences ─────────────────────────
            SectionCard {
                width: parent.width
                label: qsTr("Preferences & Haptics")
                accentIcon: "heart"

                SettingRow {
                    title: qsTr("Haptic Touch Feedback")
                    description: qsTr("Provide tactile vibrations on button taps and 3D gizmo interactions")
                    leadingIcon: "sliders"
                    hasSwitch: true
                    checked: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.hapticsEnabled : true
                    onCheckedChanged: {
                        if (typeof rubySettings !== "undefined" && rubySettings) {
                            rubySettings.hapticsEnabled = checked;
                        }
                    }
                }

                SettingRow {
                    title: qsTr("Reset All Settings")
                    description: qsTr("Restore all viewport, compiler, and IDE options to defaults")
                    leadingIcon: "refresh"
                    onClicked: resetSettingsDialog.open()
                }
            }

            // ── Section 6: About & Community ────────────────────────────────
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
                        size: Theme.dp(24)
                        color: Theme.accentInk
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.dp(2)

                        Text {
                            text: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.appName) ? rubySettings.appName : qsTr("Ruby Touch (Ruby Mobile)")
                            font.pixelSize: Theme.dp(16)
                            font.weight: Font.Bold
                            color: Theme.textPrimary
                        }

                        Text {
                            text: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.appVersion : "v1.1"
                            font.pixelSize: Theme.dp(11)
                            color: Theme.textMuted
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("The standalone Swordigo 3D level editor, model converter, and FileRift toolchain for Android.\nBuilt with Qt Quick 6.6.3 and native C++20 engine backends.")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Crafted for the Swordigo Modding Community\nby The Aevora Labs & Quantum Creeper.")
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

                // ── Provenance ──────────────────────────────────────────────
                // Read from rubySettings so the desktop and mobile About panels
                // cannot disagree about who wrote this or under what licence.
                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Text {
                        text: qsTr("COPYRIGHT")
                        width: Theme.dp(88)
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.Bold
                        font.letterSpacing: 1.1
                        color: Theme.textMuted
                        topPadding: Theme.dp(2)
                    }

                    Text {
                        text: "\u00A9 " + ((typeof rubySettings !== "undefined" && rubySettings)
                                           ? rubySettings.copyrightHolder : "MrSinup")
                        width: parent.width - Theme.dp(88) - Theme.dp(Theme.spacingSm)
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }
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
                        topPadding: Theme.dp(2)
                    }

                    Text {
                        width: parent.width - Theme.dp(88) - Theme.dp(Theme.spacingSm)
                        text: ((typeof rubySettings !== "undefined" && rubySettings)
                               ? rubySettings.licenseName : "GNU General Public License v3.0")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Text {
                        text: qsTr("PUBLISHED BY")
                        width: Theme.dp(88)
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.Bold
                        font.letterSpacing: 1.1
                        color: Theme.textMuted
                        topPadding: Theme.dp(2)
                    }

                    Text {
                        text: (typeof rubySettings !== "undefined" && rubySettings)
                              ? rubySettings.vendorName : "Aevora Labs"
                        width: parent.width - Theme.dp(88) - Theme.dp(Theme.spacingSm)
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("This program is free software: you can redistribute it and/or modify it "
                               + "under the terms of the GNU General Public License as published by the "
                               + "Free Software Foundation, either version 3 of the License, or (at your "
                               + "option) any later version. It is distributed in the hope that it will "
                               + "be useful, but WITHOUT ANY WARRANTY; without even the implied warranty "
                               + "of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: qsTr("Swordigo is \u00A9 Ville M\u00E4kynen / Touch Foo. Ruby Touch (Ruby Mobile) ships no original "
                               + "assets or binaries and is a research and preservation effort only.")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
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
                rubySettings.userName = val;
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
                rubyFileModel.clearRecentFiles();
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
                rubySettings.resetToDefaults();
            }
        }
    }
}
