import QtQuick
import QtQuick.Controls
import ".."
import "../components"

Item {
    id: root

    property string activeTool: "converter"

    signal fileSelectRequested(string targetField)
    signal openMeshStudioRequested()

    readonly property var tools: [
        { id: "converter",   label: qsTr("Model POD"),    icon: "cube",     accent: Theme.colorModel },
        { id: "ground_mesh", label: qsTr("Ground Mesh"),  icon: "mountain", accent: Theme.colorSuccessBright },
        { id: "scene_gen",   label: qsTr("Scene Gen"),    icon: "layers",   accent: Theme.accentInk },
        { id: "batch",       label: qsTr("Batch"),        icon: "bolt",     accent: Theme.colorArchive }
    ]

    Connections {
        target: (typeof androidContext !== "undefined" && androidContext) ? androidContext : null
        function onFolderPicked(tag, path) {
            if (tag === "batch_folder") {
                batchFolderPath.text = path
            } else if (tag === "ground_mesh_folder") {
                rbmFilePath.text = path
            } else if (tag === "scene_gen_dest") {
                sceneGenOutputPath.text = path
            } else if (tag === "model_dest") {
                outputPodPath.text = path + "/model.pod"
            }
        }
        function onFilePicked(tag, path) {
            if (tag === "model_source") {
                sourceModelPath.text = path
            } else if (tag === "ground_mesh_file") {
                rbmFilePath.text = path
            } else if (tag === "scene_gen_dest_file") {
                sceneGenOutputPath.text = path
            }
        }
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.paddingScreen)
        anchors.rightMargin: Theme.dp(Theme.paddingScreen)
        anchors.topMargin: Theme.dp(Theme.spacingLg)
        spacing: Theme.dp(Theme.spacingMd)

        Text {
            text: qsTr("Production Tools")
            font.pixelSize: Theme.dp(Theme.fontTitle)
            font.weight: Font.Bold
            color: Theme.textPrimary
        }

        // ── Tool selector ──────────────────────────────────────────────────
        Flickable {
            width: parent.width
            height: Theme.dp(36)
            contentWidth: toolChipsRow.implicitWidth
            flickableDirection: Flickable.HorizontalFlick
            clip: true

            Row {
                id: toolChipsRow
                spacing: Theme.dp(Theme.spacingSm)

                Repeater {
                    model: root.tools

                    Chip {
                        text: modelData.label
                        iconName: modelData.icon
                        accent: modelData.accent
                        selected: root.activeTool === modelData.id
                        onClicked: root.activeTool = modelData.id
                    }
                }
            }
        }

        // ── Tool body ──────────────────────────────────────────────────────
        Flickable {
            width: parent.width
            height: parent.height - Theme.dp(96)
            contentHeight: body.implicitHeight + Theme.dp(Theme.spacingXl)
            clip: true

            Column {
                id: body
                width: parent.width
                spacing: Theme.dp(Theme.spacingMd)

                // 1 ── Model → POD
                SectionCard {
                    width: parent.width
                    visible: root.activeTool === "converter"
                    label: qsTr("Convert 3D Model to Game POD")
                    accentIcon: "cube"
                    accent: Theme.colorModel

                    Text {
                        width: parent.width
                        text: qsTr("Converts GLTF, GLB, FBX or OBJ into a game-ready .POD optimised for Swordigo.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    FieldLabel { text: qsTr("Source 3D model") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: sourceModelPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Source GLB, OBJ, FBX path")
                            onTextChanged: {
                                var s = text.trim()
                                if (s.length > 0 && outputPodPath.text.length === 0) {
                                    var dot = s.lastIndexOf(".")
                                    if (dot > 0) outputPodPath.text = s.substring(0, dot) + ".pod"
                                    else outputPodPath.text = s + ".pod"
                                }
                            }
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                                    androidContext.pickFile("model_source", "models")
                                } else if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    sourceModelPath.text = rubyFileModel.currentPath + "/model.glb"
                                }
                            }
                        }
                    }

                    FieldLabel { text: qsTr("Output POD path") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: outputPodPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Target .pod destination path")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "drive"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                                    androidContext.pickFolder("model_dest")
                                } else if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    outputPodPath.text = rubyFileModel.currentPath + "/model.pod"
                                }
                            }
                        }
                    }

                    FieldLabel { text: qsTr("Swordigo scale preset") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)
                        Repeater {
                            model: [
                                { label: qsTr("Hero 1.8m"), val: 1.0 },
                                { label: qsTr("Prop 1.0m"), val: 0.55 },
                                { label: qsTr("Decor 0.5m"), val: 0.28 },
                                { label: qsTr("Large 4.0m"), val: 2.22 }
                            ]
                            delegate: Chip {
                                text: modelData.label
                                accent: Theme.colorModel
                                onClicked: scaleField.text = modelData.val.toString()
                            }
                        }
                    }

                    ThemedField {
                        id: scaleField
                        width: Theme.dp(120)
                        text: "1.0"
                        placeholderText: qsTr("scale")
                    }

                    SettingRow {
                        id: flipUvRow
                        width: parent.width
                        title: qsTr("Flip vertical UV coordinates")
                        description: qsTr("OpenGL standard — leave on unless the texture looks mirrored")
                        hasSwitch: true
                        checked: true
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Run Conversion to POD")
                        iconName: "play"
                        onClicked: toolsBridge.convertModel(
                                       sourceModelPath.text,
                                       outputPodPath.text,
                                       parseFloat(scaleField.text) || 1.0,
                                       flipUvRow.checked)
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }

                // 2 ── Ground mesh
                SectionCard {
                    width: parent.width
                    visible: root.activeTool === "ground_mesh"
                    label: qsTr("Ground Mesh & Collision Zones")
                    accentIcon: "mountain"
                    accent: Theme.colorSuccessBright

                    Text {
                        width: parent.width
                        text: qsTr("Build collision terrain and walking paths from a scene's model vertices.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    FieldLabel { text: qsTr("Scene / RBM file") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: rbmFilePath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Path to .scene or .rbm file")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                                    androidContext.pickFile("ground_mesh_file", "*/*")
                                } else if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    rbmFilePath.text = rubyFileModel.currentPath
                                }
                            }
                        }
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Generate Ground Collision")
                        iconName: "mountain"
                        onClicked: toolsBridge.generateGroundMesh(rbmFilePath.text)
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Open Ground Mesh Sheet Studio (Model B)")
                        iconName: "edit"
                        onClicked: root.openMeshStudioRequested()
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }

                // 3 ── Scene Generator (All Families & Options)
                SectionCard {
                    id: sceneGenCard
                    width: parent.width
                    visible: root.activeTool === "scene_gen"
                    label: qsTr("Procedural Scene Generator")
                    accentIcon: "layers"
                    accent: Theme.accentInk

                    property int selectedFamily: 3 // 0: Creator, 1: V1, 2: V2, 3: V3, 4: V3-DB, 5: V2-3D
                    property int selectedBiome: 0  // 0..7

                    Text {
                        width: parent.width
                        text: qsTr("Generate playable Swordigo .scene files from vanilla biomes and procedural terrain.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    // Generator Family
                    FieldLabel { text: qsTr("Generator Family") }
                    Flickable {
                        width: parent.width
                        height: Theme.dp(36)
                        contentWidth: familyRow.implicitWidth
                        flickableDirection: Flickable.HorizontalFlick
                        clip: true

                        Row {
                            id: familyRow
                            spacing: Theme.dp(Theme.spacingSm)

                            Repeater {
                                model: [
                                    { label: qsTr("Procedural V3"),   val: 3 },
                                    { label: qsTr("Procedural V2-3D"), val: 5 },
                                    { label: qsTr("Procedural V2"),   val: 2 },
                                    { label: qsTr("Procedural V1"),   val: 1 },
                                    { label: qsTr("Procedural V3-DB"), val: 4 },
                                    { label: qsTr("Scene Creator"),   val: 0 }
                                ]
                                delegate: Chip {
                                    text: modelData.label
                                    accent: Theme.accentInk
                                    selected: sceneGenCard.selectedFamily === modelData.val
                                    onClicked: sceneGenCard.selectedFamily = modelData.val
                                }
                            }
                        }
                    }

                    // Biome Selector (for procedural families)
                    Item {
                        width: parent.width
                        height: biomeCol.implicitHeight
                        visible: sceneGenCard.selectedFamily !== 0

                        Column {
                            id: biomeCol
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            FieldLabel { text: qsTr("Biome Preset") }
                            Flickable {
                                width: parent.width
                                height: Theme.dp(36)
                                contentWidth: biomeRow.implicitWidth
                                flickableDirection: Flickable.HorizontalFlick
                                clip: true

                                Row {
                                    id: biomeRow
                                    spacing: Theme.dp(Theme.spacingSm)

                                    Repeater {
                                        model: [
                                            { label: qsTr("Grasslands"), val: 0 },
                                            { label: qsTr("Forest"),     val: 1 },
                                            { label: qsTr("Grove"),      val: 2 },
                                            { label: qsTr("Wasteland"),  val: 3 },
                                            { label: qsTr("IceCastle"),  val: 4 },
                                            { label: qsTr("Cave"),       val: 5 },
                                            { label: qsTr("Fire"),       val: 6 },
                                            { label: qsTr("Florennum"),  val: 7 }
                                        ]
                                        delegate: Chip {
                                            text: modelData.label
                                            accent: Theme.colorSuccessBright
                                            selected: sceneGenCard.selectedBiome === modelData.val
                                            onClicked: sceneGenCard.selectedBiome = modelData.val
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Template Selector (for Scene Creator family)
                    Item {
                        width: parent.width
                        height: templateCol.implicitHeight
                        visible: sceneGenCard.selectedFamily === 0

                        Column {
                            id: templateCol
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            property int selectedTemplate: 1 // Standard

                            FieldLabel { text: qsTr("Scene Template") }
                            Flickable {
                                width: parent.width
                                height: Theme.dp(36)
                                contentWidth: templateRow.implicitWidth
                                flickableDirection: Flickable.HorizontalFlick
                                clip: true

                                Row {
                                    id: templateRow
                                    spacing: Theme.dp(Theme.spacingSm)

                                    Repeater {
                                        model: [
                                            { label: qsTr("Standard"),  val: 1 },
                                            { label: qsTr("Outdoor"),   val: 2 },
                                            { label: qsTr("Indoor"),    val: 3 },
                                            { label: qsTr("Dungeon"),   val: 4 },
                                            { label: qsTr("BossArena"), val: 5 },
                                            { label: qsTr("Minimal"),   val: 0 },
                                            { label: qsTr("Portal"),    val: 6 },
                                            { label: qsTr("Menu"),      val: 7 }
                                        ]
                                        delegate: Chip {
                                            text: modelData.label
                                            accent: Theme.accentInk
                                            selected: templateCol.selectedTemplate === modelData.val
                                            onClicked: templateCol.selectedTemplate = modelData.val
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Seed & Scene Name
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            spacing: Theme.dp(Theme.spacingXs)

                            FieldLabel { text: qsTr("Seed") }
                            Row {
                                width: parent.width
                                spacing: Theme.dp(Theme.spacingXs)

                                ThemedField {
                                    id: sceneSeedField
                                    width: parent.width - Theme.dp(36) - Theme.dp(Theme.spacingXs)
                                    text: "12345"
                                }

                                IconButton {
                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: "rotate-cw"
                                    variant: "soft"
                                    buttonSize: Theme.dp(36)
                                    onClicked: sceneSeedField.text = Math.floor(Math.random() * 999999 + 1).toString()
                                }
                            }
                        }

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            spacing: Theme.dp(Theme.spacingXs)

                            FieldLabel { text: qsTr("Scene Name") }
                            ThemedField {
                                id: sceneNameField
                                width: parent.width
                                text: "custom_level"
                            }
                        }
                    }

                    // Output path
                    FieldLabel { text: qsTr("Output Destination (.scene)") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: sceneGenOutputPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Target folder or .scene path")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                                    androidContext.pickFolder("scene_gen_dest")
                                } else if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    sceneGenOutputPath.text = rubyFileModel.currentPath
                                }
                            }
                        }
                    }

                    // Dimension Parameters
                    FieldLabel { text: qsTr("Terrain Dimensions & Octaves") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Width") }
                            ThemedField { id: widthField; width: parent.width; text: "2400" }
                        }

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Height") }
                            ThemedField { id: heightField; width: parent.width; text: "900" }
                        }

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Platforms") }
                            ThemedField { id: platformsField; width: parent.width; text: "6" }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Octaves") }
                            ThemedField { id: octavesField; width: parent.width; text: "4" }
                        }

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Roughness") }
                            ThemedField { id: roughnessField; width: parent.width; text: "1.0" }
                        }

                        Column {
                            width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Deco Density") }
                            ThemedField { id: decoField; width: parent.width; text: "1.0" }
                        }
                    }

                    // Feature Switches
                    SettingRow {
                        id: waterSwitch
                        width: parent.width
                        title: qsTr("Add Water Mesh")
                        description: qsTr("Synthesize fluid sheet at the lowest valley floor")
                        hasSwitch: true
                        checked: true
                    }

                    SettingRow {
                        id: torchesSwitch
                        width: parent.width
                        title: qsTr("Spill Torches & Glow Lights")
                        description: qsTr("Place vanilla torches and point lights along walkable edges")
                        hasSwitch: true
                        checked: true
                    }

                    SettingRow {
                        id: mountainsSwitch
                        width: parent.width
                        title: qsTr("Mountain Ridges")
                        description: qsTr("Use multifractal ridged noise for steep mountain crests")
                        hasSwitch: true
                        checked: false
                    }

                    SettingRow {
                        id: islandsSwitch
                        width: parent.width
                        title: qsTr("Floating Sky Islands")
                        description: qsTr("Generate disconnected floating terrain blobs")
                        hasSwitch: true
                        checked: false
                    }

                    SettingRow {
                        id: portalSwitch
                        width: parent.width
                        title: qsTr("Decorative Portal Hub")
                        description: qsTr("Emit transition portal with ancient stone ruins")
                        hasSwitch: true
                        checked: false
                    }

                    PrimaryButton {
                        width: parent.width
                        text: qsTr("Generate Playable Scene")
                        iconName: "play"
                        onClicked: {
                            var opts = {
                                "family": sceneGenCard.selectedFamily,
                                "biome": sceneGenCard.selectedBiome,
                                "seed": parseInt(sceneSeedField.text) || 12345,
                                "sceneName": sceneNameField.text.trim() || "custom_level",
                                "outputPath": sceneGenOutputPath.text.trim(),
                                "width": parseFloat(widthField.text) || 2400.0,
                                "height": parseFloat(heightField.text) || 900.0,
                                "platformCount": parseInt(platformsField.text) || 6,
                                "octaves": parseInt(octavesField.text) || 4,
                                "roughness": parseFloat(roughnessField.text) || 1.0,
                                "decoDensity": parseFloat(decoField.text) || 1.0,
                                "addWater": waterSwitch.checked,
                                "spillTorches": torchesSwitch.checked,
                                "mountains": mountainsSwitch.checked,
                                "islands": islandsSwitch.checked,
                                "addPortal": portalSwitch.checked,
                                "templateIndex": templateCol.selectedTemplate
                            }
                            toolsBridge.generateScene(opts)
                        }
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }

                // 4 ── Batch textures
                SectionCard {
                    width: parent.width
                    visible: root.activeTool === "batch"
                    label: qsTr("Batch Texture Converter")
                    accentIcon: "bolt"
                    accent: Theme.colorArchive

                    Text {
                        width: parent.width
                        text: qsTr("Converts every texture in a folder between .PVR and .PNG.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    FieldLabel { text: qsTr("Folder") }
                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        ThemedField {
                            id: batchFolderPath
                            width: parent.width - Theme.dp(Theme.iconButtonSize) - Theme.dp(Theme.spacingSm)
                            placeholderText: qsTr("Folder containing textures")
                        }

                        IconButton {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder-open"
                            variant: "soft"
                            buttonSize: Theme.dp(Theme.iconButtonSize)
                            onClicked: {
                                if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                                    androidContext.pickFolder("batch_folder")
                                } else if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                                    batchFolderPath.text = rubyFileModel.currentPath
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        PrimaryButton {
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            text: qsTr("PVR → PNG")
                            iconName: "image"
                            onClicked: toolsBridge.batchConvertTextures(batchFolderPath.text, "png")
                        }

                        PrimaryButton {
                            width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                            text: qsTr("PNG → PVR")
                            iconName: "archive"
                            onClicked: toolsBridge.batchConvertTextures(batchFolderPath.text, "pvr")
                        }
                    }

                    StatusLine { text: toolsBridge.lastStatusMessage }
                }
            }
        }
    }
}
