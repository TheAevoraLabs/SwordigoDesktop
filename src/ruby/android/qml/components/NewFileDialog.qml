import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// NewFileDialog.qml — Mobile New File Wizard for Ruby GG
// Ported from Ruby Desktop NewFileDialog with full template categories,
// preconfigured boilerplates, live preview, and FileRift protobuf compilation.
// ============================================================================
BottomSheet {
    id: root

    title: qsTr("New File")
    peekHeight: Math.min(root.height * 0.90, Theme.dp(620))
    expandedHeight: root.height * 0.94

    signal fileCreated(string fullPath, string extension)

    readonly property var allTemplates: [
        {
            id: "scene",
            name: qsTr("Swordigo Scene"),
            ext: "scene",
            defaultName: "new_level.scene",
            category: "Swordigo",
            icon: "cube",
            canCompile: true,
            desc: qsTr("Complete level scene with background, directional/ambient lights, and ground platform.")
        },
        {
            id: "scl",
            name: qsTr("Object Library"),
            ext: "scl",
            defaultName: "custom_objects.scl",
            category: "Swordigo",
            icon: "folder",
            canCompile: true,
            desc: qsTr("Reusable prefabs, collectables, models, and collision shapes.")
        },
        {
            id: "lua",
            name: qsTr("Lua Script"),
            ext: "lua",
            defaultName: "trigger_action.lua",
            category: "Scripting",
            icon: "code",
            canCompile: false,
            desc: qsTr("Script chunk with self/target object handles and engine callbacks.")
        },
        {
            id: "vert",
            name: qsTr("Vertex Shader"),
            ext: "vert",
            defaultName: "custom_shader.vert",
            category: "Shaders",
            icon: "sparkles",
            canCompile: false,
            desc: qsTr("OpenGL ES 2.0/3.0 vertex transform shader.")
        },
        {
            id: "frag",
            name: qsTr("Fragment Shader"),
            ext: "frag",
            defaultName: "custom_shader.frag",
            category: "Shaders",
            icon: "sparkles",
            canCompile: false,
            desc: qsTr("OpenGL ES 2.0/3.0 fragment lighting and texture shader.")
        },
        {
            id: "cpp",
            name: qsTr("C++ Source"),
            ext: "cpp",
            defaultName: "custom_module.cpp",
            category: "Native",
            icon: "code",
            canCompile: false,
            desc: qsTr("Native C++ game implementation file.")
        },
        {
            id: "h",
            name: qsTr("C++ Header"),
            ext: "h",
            defaultName: "custom_module.h",
            category: "Native",
            icon: "code",
            canCompile: false,
            desc: qsTr("Native C++ header with pragma once.")
        },
        {
            id: "json",
            name: qsTr("JSON Config"),
            ext: "json",
            defaultName: "config.json",
            category: "Data",
            icon: "archive",
            canCompile: false,
            desc: qsTr("Structured JSON data and configuration.")
        },
        {
            id: "xml",
            name: qsTr("XML Document"),
            ext: "xml",
            defaultName: "layout.xml",
            category: "Data",
            icon: "archive",
            canCompile: false,
            desc: qsTr("Structured XML hierarchy.")
        },
        {
            id: "fnt",
            name: qsTr("BMFont Font"),
            ext: "fnt",
            defaultName: "font_custom.fnt",
            category: "Data",
            icon: "palette",
            canCompile: false,
            desc: qsTr("AngelCode typography metric definition.")
        },
        {
            id: "txt",
            name: qsTr("Plain Text"),
            ext: "txt",
            defaultName: "notes.txt",
            category: "Data",
            icon: "search",
            canCompile: false,
            desc: qsTr("Raw text notes and documentation.")
        }
    ]

    property string activeCategory: "All"
    property int selectedIndex: 0
    property var selectedTemplate: filteredTemplates.length > selectedIndex ? filteredTemplates[selectedIndex] : null

    readonly property var filteredTemplates: {
        if (activeCategory === "All") return allTemplates
        var list = []
        for (var i = 0; i < allTemplates.length; i++) {
            if (allTemplates[i].category === activeCategory) list.push(allTemplates[i])
        }
        return list
    }

    onSelectedTemplateChanged: {
        if (selectedTemplate) {
            fileNameField.text = selectedTemplate.defaultName
            boilerplateText.text = rubyFileModel.getTemplateBoilerplate(selectedTemplate.id)
        }
    }

    function reset(category, index) {
        if (category !== undefined && category.length > 0) activeCategory = category
        else activeCategory = "All"
        if (index !== undefined) selectedIndex = index
        else selectedIndex = 0
        if (selectedTemplate) {
            fileNameField.text = selectedTemplate.defaultName
            boilerplateText.text = rubyFileModel.getTemplateBoilerplate(selectedTemplate.id)
        }
    }

    onOpened: {
        if (selectedTemplate) {
            if (fileNameField.text.trim().length === 0) {
                fileNameField.text = selectedTemplate.defaultName
            }
            boilerplateText.text = rubyFileModel.getTemplateBoilerplate(selectedTemplate.id)
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: contentCol.implicitHeight + Theme.dp(Theme.spacingXl)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)

            // Category Chips
            ScrollView {
                width: parent.width
                height: Theme.dp(36)
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                Row {
                    spacing: Theme.dp(Theme.spacingXs)

                    Repeater {
                        model: ["All", "Swordigo", "Scripting", "Shaders", "Native", "Data"]

                        delegate: Rectangle {
                            id: chipRect
                            height: Theme.dp(32)
                            width: chipText.implicitWidth + Theme.dp(20)
                            radius: Theme.radiusPill
                            color: root.activeCategory === modelData ? Theme.accentSoft : Theme.surface1
                            border.color: root.activeCategory === modelData ? Theme.accentEnd : Theme.border
                            border.width: 1

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: root.activeCategory === modelData ? Font.DemiBold : Font.Normal
                                color: root.activeCategory === modelData ? Theme.accentInk : Theme.textSecondary
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.activeCategory = modelData
                                    root.selectedIndex = 0
                                }
                            }
                        }
                    }
                }
            }

            // Template Carousel / List
            ScrollView {
                width: parent.width
                height: Theme.dp(94)
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                Row {
                    spacing: Theme.dp(Theme.spacingSm)

                    Repeater {
                        model: root.filteredTemplates

                        delegate: Rectangle {
                            width: Theme.dp(160)
                            height: Theme.dp(88)
                            radius: Theme.radiusCard
                            color: root.selectedIndex === index ? Theme.surface2 : Theme.surface1
                            border.color: root.selectedIndex === index ? Theme.accentEnd : Theme.border
                            border.width: root.selectedIndex === index ? 2 : 1

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.dp(10)
                                spacing: Theme.dp(4)

                                Row {
                                    spacing: Theme.dp(6)
                                    Icon {
                                        name: modelData.icon
                                        size: Theme.dp(16)
                                        color: root.selectedIndex === index ? Theme.accentInk : Theme.textMuted
                                    }
                                    Text {
                                        text: "." + modelData.ext
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        font.weight: Font.Bold
                                        color: Theme.accentInk
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.name
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: modelData.desc
                                    font.pixelSize: Theme.dp(10)
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectedIndex = index
                            }
                        }
                    }
                }
            }

            // File Configuration Box
            Rectangle {
                width: parent.width
                height: configCol.implicitHeight + Theme.dp(24)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: configCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(12)
                    spacing: Theme.dp(Theme.spacingSm)

                    Text {
                        text: qsTr("File Name")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }

                    TextField {
                        id: fileNameField
                        width: parent.width
                        font.pixelSize: Theme.dp(Theme.fontMd)
                        color: Theme.textPrimary
                        placeholderText: qsTr("filename")
                        selectByMouse: true
                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: Theme.surface2
                            border.color: fileNameField.activeFocus ? Theme.accentStart : Theme.border
                            border.width: 1
                        }
                    }

                    Row {
                        spacing: Theme.dp(6)
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "folder"; size: Theme.dp(14); color: Theme.textMuted }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Folder: %1").arg(rubyFileModel.currentPath)
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                            elide: Text.ElideMiddle
                            width: configCol.width - Theme.dp(30)
                        }
                    }

                    // Protobuf Checkbox (for .scene and .scl)
                    CheckBox {
                        id: compileBinaryCheck
                        visible: root.selectedTemplate ? (root.selectedTemplate.canCompile === true) : false
                        checked: false
                        text: qsTr("Compile directly to binary with FileRift")
                        contentItem: Text {
                            leftPadding: compileBinaryCheck.indicator.width + Theme.dp(8)
                            text: compileBinaryCheck.text
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            color: Theme.colorWarning
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            // Live Boilerplate Preview (Collapsible)
            Rectangle {
                width: parent.width
                height: previewCol.implicitHeight + Theme.dp(16)
                radius: Theme.radiusCard
                color: Theme.surface1
                border.color: Theme.border
                border.width: 1

                Column {
                    id: previewCol
                    anchors.fill: parent
                    anchors.margins: Theme.dp(10)
                    spacing: Theme.dp(6)

                    Row {
                        width: parent.width
                        Text {
                            text: qsTr("Boilerplate Preview")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: Font.DemiBold
                            color: Theme.textMuted
                        }
                    }

                    ScrollView {
                        width: parent.width
                        height: Theme.dp(90)
                        clip: true

                        Text {
                            id: boilerplateText
                            width: parent.width
                            font.pixelSize: Theme.dp(11)
                            font.family: "monospace"
                            color: Theme.textSecondary
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }

            // Action Buttons
            Row {
                anchors.right: parent.right
                spacing: Theme.dp(Theme.spacingSm)

                Rectangle {
                    width: cancelBtnText.implicitWidth + Theme.dp(28)
                    height: Theme.dp(40)
                    radius: Theme.radiusPill
                    color: cancelMouse.pressed ? Theme.surface2 : Theme.surface1
                    border.color: Theme.border
                    border.width: 1

                    Text {
                        id: cancelBtnText
                        anchors.centerIn: parent
                        text: qsTr("Cancel")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.DemiBold
                        color: Theme.textSecondary
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        onClicked: root.close()
                    }
                }

                Rectangle {
                    width: createBtnText.implicitWidth + Theme.dp(32)
                    height: Theme.dp(40)
                    radius: Theme.radiusPill
                    color: createMouse.pressed ? Theme.accentEnd : Theme.accentStart
                    border.color: Theme.alpha(Theme.accentInk, 0.45)
                    border.width: 1

                    Text {
                        id: createBtnText
                        anchors.centerIn: parent
                        text: qsTr("Create & Open")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Bold
                        color: Theme.onAccent
                    }

                    MouseArea {
                        id: createMouse
                        anchors.fill: parent
                        onClicked: {
                            if (!root.selectedTemplate) return
                            var name = fileNameField.text.trim()
                            if (name.length === 0) name = root.selectedTemplate.defaultName
                            var created = rubyFileModel.createFileFromTemplate(
                                name,
                                root.selectedTemplate.id,
                                compileBinaryCheck.checked
                            )
                            if (created && created.length > 0) {
                                root.close()
                                root.fileCreated(created, root.selectedTemplate.ext)
                            }
                        }
                    }
                }
            }

            // Bottom spacer for comfortable scrolling
            Item {
                width: 1
                height: Theme.dp(48)
            }
        }
    }
}
