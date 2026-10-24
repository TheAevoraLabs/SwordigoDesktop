import QtQuick
import QtQuick.Controls
import ".."
import "."

Dialog {
    id: root
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.92, Theme.dp(400))
    modal: true
    dim: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

    signal projectCreated(string path)

    background: Rectangle {
        color: Theme.surface1
        radius: Theme.radiusCard
        border.color: Theme.borderSubtle
        border.width: 1
    }

    property bool userEditedId: false

    onAboutToShow: {
        nameField.text = ""
        idField.text = ""
        root.userEditedId = false
        abiRow.selectedAbi = "arm64"
    }

    Column {
        id: formCol
        width: parent.width
        spacing: Theme.dp(12)

        // Header
        Row {
            width: parent.width
            spacing: Theme.dp(10)

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(36)
                height: Theme.dp(36)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.accentInk, 0.15)

                Icon {
                    anchors.centerIn: parent
                    name: "cube"
                    size: Theme.dp(18)
                    color: Theme.accentInk
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(2)

                Text {
                    text: qsTr("New Ruby Project")
                    font.pixelSize: Theme.dp(16)
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }

                Text {
                    text: qsTr("Authoritative Swordigo mod project")
                    font.pixelSize: Theme.dp(11)
                    color: Theme.textMuted
                }
            }
        }

        // Project Name
        Column {
            width: parent.width
            spacing: Theme.dp(4)

            Text {
                text: qsTr("Project Name")
                font.pixelSize: Theme.dp(11)
                font.weight: Font.DemiBold
                color: Theme.textSecondary
            }

            TextField {
                id: nameField
                width: parent.width
                placeholderText: "e.g. Emerald Realm"
                font.pixelSize: Theme.dp(13)
                color: Theme.textPrimary
                background: Rectangle {
                    color: Theme.surface2
                    radius: Theme.radiusSm
                    border.color: nameField.activeFocus ? Theme.accentInk : Theme.borderSubtle
                    border.width: 1
                }
                onTextChanged: {
                    if (!root.userEditedId) {
                        var clean = text.trim().toLowerCase().replace(/[^a-z0-9_]/g, "")
                        idField.text = clean.length > 0 ? ("net.aevora.mod." + clean) : ""
                    }
                }
            }
        }

        // Package ID / Mod ID (Required by Launcher properties.toml)
        Column {
            width: parent.width
            spacing: Theme.dp(4)

            Row {
                width: parent.width
                spacing: Theme.dp(6)

                Text {
                    text: qsTr("Package Name / Mod ID")
                    font.pixelSize: Theme.dp(11)
                    font.weight: Font.DemiBold
                    color: Theme.textSecondary
                }

                Text {
                    text: qsTr("(Launcher identifier)")
                    font.pixelSize: Theme.dp(10)
                    color: Theme.textMuted
                }
            }

            TextField {
                id: idField
                width: parent.width
                placeholderText: "net.aevora.mod.emeraldrealm"
                font.pixelSize: Theme.dp(12)
                font.family: Theme.monoFamily
                color: Theme.textPrimary
                background: Rectangle {
                    color: Theme.surface2
                    radius: Theme.radiusSm
                    border.color: idField.activeFocus ? Theme.accentInk : Theme.borderSubtle
                    border.width: 1
                }
                onTextEdited: {
                    root.userEditedId = true
                }
            }
        }

        // Author & Version Row
        Row {
            width: parent.width
            spacing: Theme.dp(10)

            Column {
                width: (parent.width - Theme.dp(10)) * 0.6
                spacing: Theme.dp(4)

                Text {
                    text: qsTr("Author")
                    font.pixelSize: Theme.dp(11)
                    font.weight: Font.DemiBold
                    color: Theme.textSecondary
                }

                TextField {
                    id: authorField
                    width: parent.width
                    text: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.userName : "Ruby Dev"
                    font.pixelSize: Theme.dp(13)
                    color: Theme.textPrimary
                    background: Rectangle {
                        color: Theme.surface2
                        radius: Theme.radiusSm
                        border.color: authorField.activeFocus ? Theme.accentInk : Theme.borderSubtle
                        border.width: 1
                    }
                }
            }

            Column {
                width: (parent.width - Theme.dp(10)) * 0.4
                spacing: Theme.dp(4)

                Text {
                    text: qsTr("Version")
                    font.pixelSize: Theme.dp(11)
                    font.weight: Font.DemiBold
                    color: Theme.textSecondary
                }

                TextField {
                    id: versionField
                    width: parent.width
                    text: "1.0.0"
                    font.pixelSize: Theme.dp(13)
                    font.family: Theme.monoFamily
                    color: Theme.textPrimary
                    background: Rectangle {
                        color: Theme.surface2
                        radius: Theme.radiusSm
                        border.color: versionField.activeFocus ? Theme.accentInk : Theme.borderSubtle
                        border.width: 1
                    }
                }
            }
        }

        // Target ABI Selector
        Column {
            width: parent.width
            spacing: Theme.dp(6)

            Text {
                text: qsTr("Target Architecture")
                font.pixelSize: Theme.dp(11)
                font.weight: Font.DemiBold
                color: Theme.textSecondary
            }

            Row {
                id: abiRow
                width: parent.width
                spacing: Theme.dp(8)

                property string selectedAbi: "arm64"

                Rectangle {
                    width: (parent.width - Theme.dp(8)) / 2
                    height: Theme.dp(36)
                    radius: Theme.radiusSm
                    color: abiRow.selectedAbi === "arm64" ? Theme.alpha(Theme.accentInk, 0.2) : Theme.surface2
                    border.color: abiRow.selectedAbi === "arm64" ? Theme.accentInk : Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "ARM64 Only"
                        font.pixelSize: Theme.dp(11)
                        font.weight: abiRow.selectedAbi === "arm64" ? Font.DemiBold : Font.Normal
                        color: abiRow.selectedAbi === "arm64" ? Theme.accentInk : Theme.textSecondary
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: abiRow.selectedAbi = "arm64"
                    }
                }

                Rectangle {
                    width: (parent.width - Theme.dp(8)) / 2
                    height: Theme.dp(36)
                    radius: Theme.radiusSm
                    color: abiRow.selectedAbi === "both" ? Theme.alpha(Theme.accentInk, 0.2) : Theme.surface2
                    border.color: abiRow.selectedAbi === "both" ? Theme.accentInk : Theme.borderSubtle
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "ARM64 + ARM32"
                        font.pixelSize: Theme.dp(11)
                        font.weight: abiRow.selectedAbi === "both" ? Font.DemiBold : Font.Normal
                        color: abiRow.selectedAbi === "both" ? Theme.accentInk : Theme.textSecondary
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: abiRow.selectedAbi = "both"
                    }
                }
            }
        }

        // Creation progress bar
        Column {
            width: parent.width
            spacing: Theme.dp(4)
            visible: typeof rubyProject !== "undefined" && rubyProject && rubyProject.isCreating

            Row {
                width: parent.width
                Text {
                    text: (typeof rubyProject !== "undefined" && rubyProject) ? rubyProject.creationStatus : ""
                    font.pixelSize: Theme.dp(11)
                    color: Theme.accentInk
                    elide: Text.ElideRight
                    width: parent.width - pctText.width - Theme.dp(4)
                }
                Text {
                    id: pctText
                    text: (typeof rubyProject !== "undefined" && rubyProject) ? (rubyProject.creationProgress + "%") : ""
                    font.pixelSize: Theme.dp(11)
                    font.bold: true
                    color: Theme.accentInk
                }
            }

            Rectangle {
                width: parent.width
                height: Theme.dp(4)
                radius: Theme.dp(2)
                color: Theme.surface2
                clip: true

                Rectangle {
                    height: parent.height
                    radius: Theme.dp(2)
                    color: Theme.accentInk
                    width: parent.width * (Math.max(5, (typeof rubyProject !== "undefined" && rubyProject) ? rubyProject.creationProgress : 0) / 100.0)
                    Behavior on width { NumberAnimation { duration: 150 } }
                }
            }
        }

        // Buttons
        Row {
            anchors.right: parent.right
            spacing: Theme.dp(8)
            topPadding: Theme.dp(6)

            Button {
                text: qsTr("Cancel")
                enabled: !(typeof rubyProject !== "undefined" && rubyProject && rubyProject.isCreating)
                onClicked: root.close()
            }

            Button {
                text: (typeof rubyProject !== "undefined" && rubyProject && rubyProject.isCreating) ? qsTr("Creating...") : qsTr("Create Project")
                enabled: !(typeof rubyProject !== "undefined" && rubyProject && rubyProject.isCreating) && nameField.text.trim().length > 0 && idField.text.trim().length > 0
                onClicked: {
                    var abis = ["arm64-v8a"]
                    if (abiRow.selectedAbi === "both") {
                        abis.push("armeabi-v7a")
                    }
                    var pName = nameField.text.trim()
                    var pAuthor = authorField.text.trim()
                    var pVer = versionField.text.trim()
                    var pId = idField.text.trim().toLowerCase()
                    if (pId.length === 0) {
                        var clean = pName.toLowerCase().replace(/[^a-z0-9_]/g, "")
                        pId = "net.aevora.mod." + (clean.length > 0 ? clean : "mod")
                    }

                    if (typeof rubyProject !== "undefined" && rubyProject) {
                        rubyProject.createProject(pName, pAuthor, pVer, pId, abis, "")
                    }
                }
            }
        }
    }

    Connections {
        target: (typeof rubyProject !== "undefined") ? rubyProject : null
        function onActiveProjectChanged() {
            if (rubyProject && rubyProject.hasActiveProject) {
                root.close()
            }
        }
    }
}

