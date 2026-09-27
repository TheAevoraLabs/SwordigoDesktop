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

                // 3 ── Scene Generator (algorithms × dimensions)
                SectionCard {
                    id: sceneGenCard
                    width: parent.width
                    visible: root.activeTool === "scene_gen"
                    label: qsTr("Scene Generator Algorithms")
                    accentIcon: "layers"
                    accent: Theme.accentInk

                    // ── Why this is a stack of dropdowns, not a chip row ───────
                    // There are two modes, three generations and two spatial
                    // dimensions. A horizontal chip row cannot hold six families
                    // at phone width (the trailing chips scroll off the edge with
                    // no hint that they exist), and far more importantly every
                    // family exposes a DIFFERENT option set — so the controls
                    // below are revealed only for the family that is actually
                    // selected. The previous card rendered one fixed list of
                    // knobs for every algorithm, which is exactly why it felt
                    // identical whichever one you picked, and why 3D looked like
                    // "just another generation" when it is its own world shape.
                    property string mode: "procedural"   // "create" | "procedural"
                    property string generation: "v3"     // "v1" | "v2" | "v3"
                    property string dimension: "2d"      // "2d" | "3d"
                    property string biomeKey: "0"
                    property string templateKey: "1"

                    readonly property bool isCreate: mode === "create"
                    readonly property bool is3d: mode !== "create" && dimension === "3d"

                    Text {
                        width: parent.width
                        text: qsTr("Generate playable Swordigo .scene files. Scene Create lays out an authored template; Procedural Scene synthesises terrain from seeded noise and vanilla biome data.")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    // ── 1. What kind of scene ──────────────────────────────────
                    SettingRow {
                        width: parent.width
                        title: qsTr("Generator Algorithms")
                        description: qsTr("Scene Create: authored level template. Procedural Scene: noise terrain from the vanilla biome database.")
                        leadingIcon: "layers"
                        choiceOptions: [
                            { value: "create",     label: qsTr("Scene Create") },
                            { value: "procedural", label: qsTr("Procedural Scene") }
                        ]
                        choiceValue: sceneGenCard.mode
                        onChoiceSelected: (value) => sceneGenCard.mode = value
                    }

                    // ── 2. Procedural: generation × dimension ──────────────────
                    SettingRow {
                        width: parent.width
                        visible: !sceneGenCard.isCreate
                        title: qsTr("Generation")
                        description: qsTr("v1 classic single-layer · v2 layered Z-terrain · v3 data-driven biome database")
                        choiceOptions: [
                            { value: "v1", label: qsTr("v1 — classic") },
                            { value: "v2", label: qsTr("v2 — layered Z") },
                            { value: "v3", label: qsTr("v3 — Ultimate (biome DB)") }
                        ]
                        choiceValue: sceneGenCard.generation
                        onChoiceSelected: (value) => sceneGenCard.generation = value
                    }

                    SettingRow {
                        width: parent.width
                        visible: !sceneGenCard.isCreate
                        title: qsTr("Dimension")
                        description: qsTr("2.5D: layered side-on world. 3D: voxel world with depth rows behind and in front of the hero plane.")
                        choiceOptions: [
                            { value: "2d", label: qsTr("2.5D") },
                            { value: "3d", label: qsTr("3D") }
                        ]
                        choiceValue: sceneGenCard.dimension
                        onChoiceSelected: (value) => sceneGenCard.dimension = value
                    }

                    SettingRow {
                        width: parent.width
                        visible: !sceneGenCard.isCreate
                        title: qsTr("Biome Preset")
                        description: qsTr("Textures, water colours, tree/rock palettes, torch style and lighting harvested from shipped scenes.")
                        choiceOptions: [
                            { value: "0", label: qsTr("Grasslands") },
                            { value: "1", label: qsTr("Forest") },
                            { value: "2", label: qsTr("Grove") },
                            { value: "3", label: qsTr("Wasteland") },
                            { value: "4", label: qsTr("Ice Castle") },
                            { value: "5", label: qsTr("Cave") },
                            { value: "6", label: qsTr("Fire") },
                            { value: "7", label: qsTr("Florennum") }
                        ]
                        choiceValue: sceneGenCard.biomeKey
                        onChoiceSelected: (value) => sceneGenCard.biomeKey = value
                    }

                    // ── SCENE CREATE options ───────────────────────────────────
                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingMd)
                        visible: sceneGenCard.isCreate

                        SettingRow {
                            width: parent.width
                            title: qsTr("Scene Template")
                            description: qsTr("Bounds scale, lighting slots and object set for the authored level.")
                            choiceOptions: [
                                { value: "1", label: qsTr("Standard") },
                                { value: "2", label: qsTr("Outdoor") },
                                { value: "3", label: qsTr("Indoor") },
                                { value: "4", label: qsTr("Dungeon") },
                                { value: "5", label: qsTr("Boss Arena") },
                                { value: "0", label: qsTr("Minimal") },
                                { value: "6", label: qsTr("Portal") },
                                { value: "7", label: qsTr("Menu") }
                            ]
                            choiceValue: sceneGenCard.templateKey
                            onChoiceSelected: (value) => sceneGenCard.templateKey = value
                        }

                        // The template platform is AUTHORED — it is not the world
                        // size the procedural generators take. Sharing one pair of
                        // width/height fields between the two modes is what fed a
                        // 2400-unit slab to a 320-unit template.
                        FieldLabel { text: qsTr("Ground platform (authored template)") }
                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Width") }
                                ThemedField { id: createWidthField; width: parent.width; text: "320" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Height") }
                                ThemedField { id: createHeightField; width: parent.width; text: "48" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Depth ±") }
                                ThemedField { id: createDepthField; width: parent.width; text: "90" }
                            }
                        }

                        FieldLabel { text: qsTr("Spawn (hero start)") }
                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Spawn X") }
                                ThemedField { id: spawnXField; width: parent.width; text: "0" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Spawn Y") }
                                ThemedField { id: spawnYField; width: parent.width; text: "56" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Facing") }
                                ThemedField { id: spawnFacingField; width: parent.width; text: "1" }
                            }
                        }

                        FieldLabel { text: qsTr("Top surface texture") }
                        ThemedField { id: createTopTexField; width: parent.width; text: "fire_grass" }
                        FieldLabel { text: qsTr("Front / cliff texture") }
                        ThemedField { id: createSideTexField; width: parent.width; text: "graveyard_ground" }
                        FieldLabel { text: qsTr("Background texture") }
                        ThemedField { id: createBgField; width: parent.width; text: "grasslandsbackground_day" }
                    }

                    // ── PROCEDURAL: world size (all generations) ───────────────
                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingMd)
                        visible: !sceneGenCard.isCreate

                        FieldLabel { text: sceneGenCard.is3d ? qsTr("World width & height range")
                                                              : qsTr("Terrain dimensions") }
                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Width") }
                                ThemedField { id: widthField; width: parent.width; text: "2400" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Height range") }
                                ThemedField { id: heightField; width: parent.width; text: "900" }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Platforms") }
                                ThemedField { id: platformsField; width: parent.width; text: "6" }
                            }
                            // Octaves/roughness shape the 2.5D heightfield only; the
                            // 3D world is built from block columns and depth rows,
                            // so those two fields are hidden rather than shown and
                            // silently ignored.
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                visible: !sceneGenCard.is3d
                                FieldLabel { text: qsTr("Octaves") }
                                ThemedField { id: octavesField; width: parent.width; text: "4" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                visible: !sceneGenCard.is3d
                                FieldLabel { text: qsTr("Roughness") }
                                ThemedField { id: roughnessField; width: parent.width; text: "1.0" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Deco density") }
                                ThemedField { id: decoField; width: parent.width; text: "1.0" }
                            }
                        }
                    }

                    // ── v2 only: the layered-Z knobs ───────────────────────────
                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingMd)
                        visible: !sceneGenCard.isCreate && !sceneGenCard.is3d
                                 && sceneGenCard.generation === "v2"

                        FieldLabel { text: qsTr("v2 Z-layers (these belong to v2 only)") }

                        SettingRow {
                            id: bgTerrainSwitch
                            width: parent.width
                            title: qsTr("Background terrain silhouettes")
                            description: qsTr("Rougher, taller ground strips behind the gameplay plane (Z −100…−200)")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: fgTerrainSwitch
                            width: parent.width
                            title: qsTr("Foreground cliff framing")
                            description: qsTr("A framing strip in front of the hero plane (Z +80)")
                            hasSwitch: true
                            checked: false
                        }
                        SettingRow {
                            id: zPathSwitch
                            width: parent.width
                            title: qsTr("Winding terrain path")
                            description: qsTr("Varies each strip's object Z on a smooth curve so the path winds toward and away from the camera")
                            hasSwitch: true
                            checked: false
                        }

                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("BG layers") }
                                ThemedField { id: bgLayersField; width: parent.width; text: "2" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Path amplitude ±Z") }
                                ThemedField { id: zAmpField; width: parent.width; text: "0" }
                            }
                        }

                        SettingRow {
                            id: terracingSwitch
                            width: parent.width
                            title: qsTr("Terracing (stepped bands)")
                            description: qsTr("Quantize the heightfield into flat stepped benches")
                            hasSwitch: true
                            checked: false
                        }
                        SettingRow {
                            id: overhangsSwitch
                            width: parent.width
                            title: qsTr("Overhangs")
                            description: qsTr("Cliff-edge ledges on steep drops")
                            hasSwitch: true
                            checked: false
                        }
                        SettingRow {
                            id: bgDecosSwitch
                            width: parent.width
                            title: qsTr("Background parallax decor")
                            description: qsTr("Large deep-Z trees/rocks (scale 1.4–2.5×)")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: fgDecosSwitch
                            width: parent.width
                            title: qsTr("Foreground parallax decor")
                            description: qsTr("Near-Z rocks and shrubs (scale 0.4–0.8×)")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: cameraShapesSwitch
                            width: parent.width
                            title: qsTr("Camera follows hero")
                            description: qsTr("Emits a vertical-follow camera shape and derives Bounds from the walkable top profile. Still experimental.")
                            hasSwitch: true
                            checked: false
                        }

                        Column {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Terrace strength (0 = none, 1 = fully stepped)") }
                            ThemedField { id: terraceStrengthField; width: parent.width; text: "0.5" }
                        }
                    }

                    // ── 3D only: the voxel-world knobs ─────────────────────────
                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingMd)
                        visible: sceneGenCard.is3d

                        FieldLabel { text: qsTr("Depth rows (these belong to the 3D world only)") }
                        Row {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingSm)

                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Rows behind") }
                                ThemedField { id: depthRowsField; width: parent.width; text: "15" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Rows in front") }
                                ThemedField { id: frontRowsField; width: parent.width; text: "5" }
                            }
                            Column {
                                width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                                spacing: Theme.dp(Theme.spacingXs)
                                FieldLabel { text: qsTr("Row band") }
                                ThemedField { id: rowBandField; width: parent.width; text: "90" }
                            }
                        }

                        Column {
                            width: parent.width
                            spacing: Theme.dp(Theme.spacingXs)
                            FieldLabel { text: qsTr("Block size (blocky mode only)") }
                            ThemedField { id: blockSizeField; width: parent.width; text: "44" }
                        }

                        SettingRow {
                            id: blockySwitch
                            width: parent.width
                            title: qsTr("Blocky voxel columns")
                            description: qsTr("Minecraft-style quantized terraces instead of smooth rolling ground")
                            hasSwitch: true
                            checked: false
                        }
                        SettingRow {
                            id: cavesSwitch
                            width: parent.width
                            title: qsTr("Caves & winding ravines")
                            description: qsTr("Carved canyons across the depth rows")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: skyIslandsSwitch
                            width: parent.width
                            title: qsTr("Floating sky islands")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: farTreesSwitch
                            width: parent.width
                            title: qsTr("Far forest on depth rows")
                            description: qsTr("Trees receding into the rows for perspective depth")
                            hasSwitch: true
                            checked: true
                        }
                    }

                    // ── Shared procedural features ─────────────────────────────
                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingMd)
                        visible: !sceneGenCard.isCreate

                        FieldLabel { text: qsTr("Features") }

                        SettingRow {
                            id: waterSwitch
                            width: parent.width
                            title: qsTr("Water sheet")
                            description: qsTr("Fluid sheet at the lowest valley floor of the selected biome")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: torchesSwitch
                            width: parent.width
                            title: qsTr("Torches & glow lights")
                            description: qsTr("Vanilla torch objects with point lights along walkable edges")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: mountainsSwitch
                            width: parent.width
                            title: qsTr("Mountain ridges")
                            description: qsTr("Ridged multifractal profile for steep crests (2.5D)")
                            hasSwitch: true
                            visible: !sceneGenCard.is3d
                            checked: false
                        }
                        SettingRow {
                            id: islandsSwitch
                            width: parent.width
                            title: qsTr("Island hats on even platforms")
                            hasSwitch: true
                            visible: !sceneGenCard.is3d
                            checked: false
                        }
                        SettingRow {
                            id: portalSwitch
                            width: parent.width
                            title: qsTr("Decorative portal hub")
                            description: qsTr("A portal plus per-biome stone ruins, signs and flanking torches")
                            hasSwitch: true
                            checked: false
                        }

                        FieldLabel { text: qsTr("Portal destination scene") }
                        ThemedField { id: portalDestField; width: parent.width; text: "next_level" }

                        SettingRow {
                            id: randDecoRotSwitch
                            width: parent.width
                            title: qsTr("Randomize decoration rotation")
                            description: qsTr("Uncheck for upright, vanilla-style placement")
                            hasSwitch: true
                            checked: true
                        }
                        SettingRow {
                            id: randDecoScaleSwitch
                            width: parent.width
                            title: qsTr("Randomize decoration size")
                            description: qsTr("Uncheck for uniform 1.0 scaling")
                            hasSwitch: true
                            checked: true
                        }
                    }

                    // ── Seed, name, destination ───────────────────────────────
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

                    PrimaryButton {
                        width: parent.width
                        text: sceneGenCard.isCreate
                              ? qsTr("Create Scene from Template")
                              : (sceneGenCard.is3d
                                 ? qsTr("Generate 3D Scene")
                                 : qsTr("Generate 2.5D Scene (%1)").arg(sceneGenCard.generation))
                        iconName: "play"
                        onClicked: {
                            var opts = {
                                "mode": sceneGenCard.mode,
                                "generation": sceneGenCard.generation,
                                "dimension": sceneGenCard.dimension,
                                "biome": parseInt(sceneGenCard.biomeKey) || 0,
                                "templateIndex": parseInt(sceneGenCard.templateKey) || 1,
                                "seed": parseInt(sceneSeedField.text) || 12345,
                                "sceneName": sceneNameField.text.trim() || "custom_level",
                                "outputPath": sceneGenOutputPath.text.trim()
                            }
                            if (sceneGenCard.isCreate) {
                                // Authored template sizes — deliberately NOT the
                                // procedural width/height fields.
                                opts["platformWidth"]  = parseFloat(createWidthField.text) || 320.0
                                opts["platformHeight"] = parseFloat(createHeightField.text) || 48.0
                                opts["platformDepth"]  = parseFloat(createDepthField.text) || 90.0
                                opts["spawnX"] = parseFloat(spawnXField.text) || 0.0
                                opts["spawnY"] = parseFloat(spawnYField.text) || 56.0
                                opts["spawnFacing"] = parseInt(spawnFacingField.text) || 1
                                opts["groundTopTexture"] = createTopTexField.text.trim()
                                opts["groundSideTexture"] = createSideTexField.text.trim()
                                opts["background"] = createBgField.text.trim()
                            } else {
                                opts["width"]  = parseFloat(widthField.text) || 2400.0
                                opts["height"] = parseFloat(heightField.text) || 900.0
                                opts["platformCount"] = parseInt(platformsField.text) || 6
                                opts["octaves"]   = parseInt(octavesField.text) || 4
                                opts["roughness"] = parseFloat(roughnessField.text) || 1.0
                                opts["decoDensity"] = parseFloat(decoField.text) || 1.0
                                opts["addWater"] = waterSwitch.checked
                                opts["spillTorches"] = torchesSwitch.checked
                                opts["mountains"] = mountainsSwitch.checked
                                opts["islands"] = islandsSwitch.checked
                                opts["addPortal"] = portalSwitch.checked
                                opts["portalDestination"] = portalDestField.text.trim()
                                opts["randomizeDecoRotation"] = randDecoRotSwitch.checked
                                opts["randomizeDecoScale"] = randDecoScaleSwitch.checked
                                if (sceneGenCard.is3d) {
                                    opts["depthRows"] = parseInt(depthRowsField.text) || 15
                                    opts["frontRows"] = parseInt(frontRowsField.text) || 5
                                    opts["rowBand"]   = parseFloat(rowBandField.text) || 90.0
                                    opts["blockSize"] = parseFloat(blockSizeField.text) || 44.0
                                    opts["blocky"] = blockySwitch.checked
                                    opts["addCaves"] = cavesSwitch.checked
                                    opts["skyIslands"] = skyIslandsSwitch.checked
                                    opts["farTrees"] = farTreesSwitch.checked
                                } else if (sceneGenCard.generation === "v2") {
                                    opts["bgTerrain"] = bgTerrainSwitch.checked
                                    opts["fgTerrain"] = fgTerrainSwitch.checked
                                    opts["zPath"] = zPathSwitch.checked
                                    opts["zAmplitude"] = parseFloat(zAmpField.text) || 0.0
                                    opts["bgLayers"] = parseInt(bgLayersField.text) || 2
                                    opts["bgDecos"] = bgDecosSwitch.checked
                                    opts["fgDecos"] = fgDecosSwitch.checked
                                    opts["addTerracing"] = terracingSwitch.checked
                                    opts["terraceStrength"] = parseFloat(terraceStrengthField.text) || 0.5
                                    opts["addOverhangs"] = overhangsSwitch.checked
                                    opts["cameraShapes"] = cameraShapesSwitch.checked
                                }
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
