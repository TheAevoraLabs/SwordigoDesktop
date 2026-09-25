import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../components"

// ============================================================================
// PlaceObjectDialog.qml — Mobile Object & Archetype Placement Studio
//
// Features:
// 1. Model Library: scans all 3D .POD models in game assets & storage (hiro,
//    enemies, props, collectibles) with search and one-tap ModelComponent import.
// 2. Ground Prefabs: parses groundmeshes.scl (275 vanilla terrain meshes)
//    with biome category chips (Plains, Forest, Caves, Keep, Snowy, etc.).
// 3. SCL Libraries: browse external archetype collections (rocks, platforms).
// 4. Primitives: quick placement of SpawnPoints, Portals, and Empty Objects.
// ============================================================================
Popup {
    id: root

    property var viewportItem: null
    property int currentTab: 0 // 0: Models, 1: Ground Prefabs, 2: Libraries, 3: Primitives
    property string filterText: ""
    property string selectedBiome: "All"
    property string selectedLibrary: "groundmeshes"
    property string selectedLibraryName: "Ground Meshes"

    property var availableModels: []
    property var availableLibraries: []
    property var libraryTemplates: []

    signal objectImported(string name, string type)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.dp(32) : Theme.dp(640), Theme.dp(680))
    height: Math.min(parent ? parent.height - Theme.dp(20) : Theme.dp(350), Theme.dp(370))
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    Overlay.modal: Rectangle {
        color: "#99000000"
    }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.surface1
        border.color: Theme.border
        border.width: 1
    }

    function openDialog() {
        refreshData()
        root.open()
    }

    function refreshData() {
        if (!viewportItem) return
        availableModels = viewportItem.getAvailableModels()
        availableLibraries = viewportItem.getAvailableLibraries()
        loadLibraryTemplates(selectedLibrary)
    }

    function loadLibraryTemplates(libNameOrPath) {
        if (!viewportItem) return
        selectedLibrary = libNameOrPath
        libraryTemplates = viewportItem.getTemplatesInLibrary(libNameOrPath)
    }

    function selectLibrary(lib) {
        selectedLibrary = lib.path && lib.path.length > 0 ? lib.path : lib.name
        selectedLibraryName = lib.displayName || lib.name
        loadLibraryTemplates(selectedLibrary)
        currentTab = 1 // Switch to templates tab to view its items
    }

    readonly property var biomes: [
        "All", "Plains", "Forest", "Forgotten_Keep", "Caves",
        "Fiery_Depths", "Snowy", "Icecastle", "Wasteland", "Florennum", "House"
    ]

    readonly property var filteredModels: {
        var query = filterText.trim().toLowerCase()
        if (query.length === 0) return availableModels
        return availableModels.filter(function(m) {
            return m.name.toLowerCase().indexOf(query) !== -1
        })
    }

    readonly property var filteredTemplates: {
        var query = filterText.trim().toLowerCase()
        var biome = selectedBiome.toLowerCase()
        return libraryTemplates.filter(function(t) {
            var nameMatch = query.length === 0 || t.name.toLowerCase().indexOf(query) !== -1
            if (!nameMatch) return false
            if (selectedBiome === "All") return true
            return t.name.toLowerCase().indexOf(biome) !== -1 ||
                   (t.category && t.category.toLowerCase().indexOf(biome) !== -1)
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.dp(12)
        spacing: Theme.dp(8)

        // ── Header Row ──────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.dp(8)

            Icon {
                name: currentTab === 0 ? "cube" : (currentTab === 1 ? "layers" : (currentTab === 2 ? "folder" : "plus"))
                size: Theme.dp(20)
                color: Theme.accentEnd
            }

            Column {
                Layout.fillWidth: true
                spacing: Theme.dp(2)

                Text {
                    text: currentTab === 0 ? qsTr("Place 3D Model (.POD)") :
                          (currentTab === 1 ? qsTr("Place Prefab — %1").arg(root.selectedLibraryName) :
                          (currentTab === 2 ? qsTr("Browse Archetype Libraries") : qsTr("Place Primitive / Logic")))
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }

                Text {
                    text: currentTab === 0 ? qsTr("%1 models available").arg(filteredModels.length) :
                          (currentTab === 1 ? qsTr("%1 templates in %2").arg(filteredTemplates.length).arg(root.selectedLibraryName) :
                          (currentTab === 2 ? qsTr("%1 libraries discovered").arg(availableLibraries.length) : qsTr("Quick spawn markers")))
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                }
            }

            // Close button
            Rectangle {
                width: Theme.dp(32)
                height: Theme.dp(32)
                radius: Theme.radiusSm
                color: closeHover.pressed ? Theme.surface3 : (closeHover.containsMouse ? Theme.surface2 : "transparent")

                Icon {
                    anchors.centerIn: parent
                    name: "close"
                    size: Theme.dp(16)
                    color: Theme.textSecondary
                }

                MouseArea {
                    id: closeHover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.close()
                }
            }
        }

        // ── Tabs Row ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.dp(6)

            Chip {
                text: qsTr("3D Models")
                iconName: "cube"
                count: root.availableModels.length
                selected: root.currentTab === 0
                onClicked: { root.currentTab = 0; searchInput.text = "" }
            }

            Chip {
                text: qsTr("Prefabs")
                iconName: "layers"
                count: root.libraryTemplates.length
                selected: root.currentTab === 1
                onClicked: { root.currentTab = 1; searchInput.text = "" }
            }

            Chip {
                text: qsTr("Libraries")
                iconName: "folder"
                count: root.availableLibraries.length
                selected: root.currentTab === 2
                onClicked: { root.currentTab = 2; searchInput.text = "" }
            }

            Chip {
                text: qsTr("Primitives")
                iconName: "plus"
                selected: root.currentTab === 3
                onClicked: { root.currentTab = 3; searchInput.text = "" }
            }

            Item { Layout.fillWidth: true }
        }

        // ── Search & Filter Controls (for Models and Prefabs) ───────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.dp(6)
            visible: root.currentTab === 0 || root.currentTab === 1

            Rectangle {
                Layout.fillWidth: true
                height: Theme.dp(34)
                radius: Theme.radiusSm
                color: Theme.surface2
                border.color: searchInput.activeFocus ? Theme.borderFocus : Theme.borderSubtle
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(8)
                    anchors.rightMargin: Theme.dp(8)
                    spacing: Theme.dp(6)

                    Icon {
                        name: "search"
                        size: Theme.dp(14)
                        color: Theme.textMuted
                    }

                    TextInput {
                        id: searchInput
                        Layout.fillWidth: true
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        clip: true

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            visible: !searchInput.text && !searchInput.activeFocus
                            text: root.currentTab === 0 ? qsTr("Search 3D models (e.g. hiro, bat, chest, tree)...") :
                                                          qsTr("Search prefabs (e.g. plains, forest, house)...")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontSm)
                        }

                        onTextChanged: root.filterText = text
                    }

                    Rectangle {
                        visible: searchInput.text.length > 0
                        width: Theme.dp(20)
                        height: Theme.dp(20)
                        radius: Theme.radiusSm
                        color: "transparent"

                        Icon {
                            anchors.centerIn: parent
                            name: "close"
                            size: Theme.dp(12)
                            color: Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                searchInput.text = ""
                                root.filterText = ""
                            }
                        }
                    }
                }
            }
        }

        // ── Biome Filter Chips (Only for Ground Prefabs) ────────────────────
        Flickable {
            Layout.fillWidth: true
            height: Theme.dp(30)
            contentWidth: biomeRow.implicitWidth
            contentHeight: Theme.dp(30)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            visible: root.currentTab === 1

            Row {
                id: biomeRow
                spacing: Theme.dp(4)

                Repeater {
                    model: root.biomes
                    delegate: Rectangle {
                        height: Theme.dp(26)
                        width: bText.implicitWidth + Theme.dp(16)
                        radius: Theme.radiusPill
                        color: root.selectedBiome === modelData ? Theme.alpha(Theme.accentEnd, 0.25) : Theme.surface2
                        border.color: root.selectedBiome === modelData ? Theme.accentEnd : Theme.borderSubtle
                        border.width: 1

                        Text {
                            id: bText
                            anchors.centerIn: parent
                            text: modelData.replace("_", " ")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: root.selectedBiome === modelData ? Font.DemiBold : Font.Normal
                            color: root.selectedBiome === modelData ? Theme.accentEnd : Theme.textSecondary
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectedBiome = modelData
                        }
                    }
                }
            }
        }

        // ── Main Content Area ───────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // 1. Models View
            GridView {
                id: modelsGrid
                anchors.fill: parent
                visible: root.currentTab === 0
                cellWidth: Math.floor(width / 3)
                cellHeight: Theme.dp(48)
                model: root.filteredModels

                delegate: Rectangle {
                    width: modelsGrid.cellWidth - Theme.dp(6)
                    height: Theme.dp(42)
                    radius: Theme.radiusSm
                    color: itemArea.pressed ? Theme.surface3 : (itemArea.containsMouse ? Theme.surface2 : Theme.surface1)
                    border.color: itemArea.containsMouse ? Theme.accentEnd : Theme.borderSubtle
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(8)
                        anchors.rightMargin: Theme.dp(8)
                        spacing: Theme.dp(6)

                        Rectangle {
                            width: Theme.dp(28)
                            height: Theme.dp(28)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.accentInk, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: "cube"
                                size: Theme.dp(14)
                                color: Theme.accentEnd
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: Theme.dp(1)

                            Text {
                                width: parent.width
                                text: modelData.name
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: Font.Medium
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                            }

                            Text {
                                text: modelData.sizeStr || "POD"
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                            }
                        }
                    }

                    MouseArea {
                        id: itemArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.viewportItem) {
                                var idx = root.viewportItem.importModelObject(modelData.name)
                                if (idx >= 0) {
                                    root.objectImported(modelData.name, "Model")
                                    root.close()
                                }
                            }
                        }
                    }
                }

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: root.filteredModels.length === 0
                    text: root.availableModels.length === 0 ?
                          qsTr("No .POD models found in assets or storage.") :
                          qsTr("No models match '%1'").arg(root.filterText)
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textMuted
                }
            }

            // 2. Prefabs View (Ground Meshes & Archetypes)
            GridView {
                id: prefabsGrid
                anchors.fill: parent
                visible: root.currentTab === 1
                cellWidth: Math.floor(width / 3)
                cellHeight: Theme.dp(48)
                model: root.filteredTemplates

                delegate: Rectangle {
                    width: prefabsGrid.cellWidth - Theme.dp(6)
                    height: Theme.dp(42)
                    radius: Theme.radiusSm
                    color: pArea.pressed ? Theme.surface3 : (pArea.containsMouse ? Theme.surface2 : Theme.surface1)
                    border.color: pArea.containsMouse ? Theme.accentEnd : Theme.borderSubtle
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(8)
                        anchors.rightMargin: Theme.dp(8)
                        spacing: Theme.dp(6)

                        Rectangle {
                            width: Theme.dp(28)
                            height: Theme.dp(28)
                            radius: Theme.radiusSm
                            color: Theme.alpha(modelData.hasGround ? "#22c55e" : "#3b82f6", 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: modelData.hasGround ? "layers" : (modelData.hasModel ? "cube" : "star")
                                size: Theme.dp(14)
                                color: modelData.hasGround ? "#22c55e" : (modelData.hasModel ? "#3b82f6" : "#eab308")
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: Theme.dp(1)

                            Text {
                                width: parent.width
                                text: modelData.name
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: Font.Medium
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                            }

                            Text {
                                text: modelData.kind + (modelData.category ? (" • " + modelData.category) : "")
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                            }
                        }
                    }

                    MouseArea {
                        id: pArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.viewportItem) {
                                var idx = root.viewportItem.importTemplateObject(root.selectedLibrary, modelData.name)
                                if (idx >= 0) {
                                    root.objectImported(modelData.name, modelData.kind)
                                    root.close()
                                }
                            }
                        }
                    }
                }

                // Empty state
                Text {
                    anchors.centerIn: parent
                    visible: root.filteredTemplates.length === 0
                    text: root.libraryTemplates.length === 0 ?
                          qsTr("No templates in %1").arg(root.selectedLibraryName) :
                          qsTr("No prefabs match filter.")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textMuted
                }
            }

            // 3. Libraries View (.SCL archetype files)
            ListView {
                id: libList
                anchors.fill: parent
                visible: root.currentTab === 2
                spacing: Theme.dp(6)
                model: root.availableLibraries

                delegate: Rectangle {
                    width: libList.width
                    height: Theme.dp(44)
                    radius: Theme.radiusSm
                    color: lArea.pressed ? Theme.surface3 : (lArea.containsMouse ? Theme.surface2 : Theme.surface1)
                    border.color: lArea.containsMouse ? Theme.accentEnd : Theme.borderSubtle
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(12)
                        anchors.rightMargin: Theme.dp(12)
                        spacing: Theme.dp(10)

                        Rectangle {
                            width: Theme.dp(30)
                            height: Theme.dp(30)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.accentEnd, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: "folder"
                                size: Theme.dp(16)
                                color: Theme.accentEnd
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            Text {
                                text: modelData.displayName || modelData.name
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: modelData.path && modelData.path.length > 0 ? modelData.path : qsTr("Embedded scene library")
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                                elide: Text.ElideMiddle
                                width: parent.width
                            }
                        }

                        Icon {
                            name: "chevron-right"
                            size: Theme.dp(14)
                            color: Theme.textMuted
                        }
                    }

                    MouseArea {
                        id: lArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.selectLibrary(modelData)
                    }
                }
            }

            // 4. Primitives View
            GridView {
                id: primGrid
                anchors.fill: parent
                visible: root.currentTab === 3
                cellWidth: Math.floor(width / 2)
                cellHeight: Theme.dp(64)
                model: [
                    { name: "Spawn Point", kind: "SpawnPoint", icon: "star", desc: "Player entrance & checkpoint spawn marker", color: "#eab308" },
                    { name: "Portal Gate", kind: "Portal", icon: "external", desc: "Level transition door / portal zone", color: "#3b82f6" },
                    { name: "Empty Object", kind: "Empty", icon: "cube", desc: "Empty SceneObject ready for components", color: "#a855f7" }
                ]

                delegate: Rectangle {
                    width: primGrid.cellWidth - Theme.dp(8)
                    height: Theme.dp(56)
                    radius: Theme.radiusSm
                    color: primArea.pressed ? Theme.surface3 : (primArea.containsMouse ? Theme.surface2 : Theme.surface1)
                    border.color: primArea.containsMouse ? Theme.accentEnd : Theme.borderSubtle
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.dp(10)
                        anchors.rightMargin: Theme.dp(10)
                        spacing: Theme.dp(8)

                        Rectangle {
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(modelData.color, 0.15)

                            Icon {
                                anchors.centerIn: parent
                                name: modelData.icon
                                size: Theme.dp(18)
                                color: modelData.color
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            Text {
                                text: modelData.name
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }

                            Text {
                                text: modelData.desc
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                    }

                    MouseArea {
                        id: primArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (root.viewportItem) {
                                var idx = root.viewportItem.importPrimitiveObject(modelData.kind)
                                if (idx >= 0) {
                                    root.objectImported(modelData.name, "Primitive")
                                    root.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
