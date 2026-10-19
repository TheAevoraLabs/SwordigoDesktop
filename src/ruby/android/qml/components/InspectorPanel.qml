import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// InspectorPanel.qml — the landscape studio's right-hand inspector.
//
// WHY THIS SHAPE
//   The portrait build presented the inspector as a BottomSheet, which in
//   landscape collapses to a short strip that covers the viewport it is meant
//   to be adjusting. Here it is a full-height dock that slides in from the right
//   and covers roughly a third of the width, so the viewport stays maximised
//   and every field is reachable without fighting a sheet's drag handle. In
//   `maximized` mode it grows to two thirds of the width — still an overlay, so
//   the 3D scene is never destroyed or re-laid-out, just partly covered.
//
// WHY THE FIELDS ARE READ-ONLY
//   In landscape the soft keyboard covers the viewport and makes typing
//   "-12.75" a precision exercise. Every numeric field routes its tap into
//   NumericKeypad instead (see KeypadField.qml). Names and templates keep the
//   soft keyboard because they need letters.
//
// HOST CONTRACT (RubyQuickViewport satisfies it):
//   host.selectedObject, host.objectCount
//   host.getSelectedObjectData() -> { name, template, posX/Y/Z, rotX/Y/Z, scaleX/Y/Z }
//   host.setObjectName(str), host.setObjectTemplate(str)
//   host.applyTransform(px,py,pz, rx,ry,rz, sx,sy,sz)
//   host.focusObject(i), host.duplicateObject(i), host.deleteObject(i), host.addObject()
//   signals: selectedObjectChanged, objectListChanged, objectTransformChanged
//
// Rotation crosses this boundary in DEGREES (see getSelectedObjectData); the
// viewport stores radians. The .scene format persists Y rotation and a single
// uniform scale, which the Transform card states plainly instead of pretending
// X/Z rotation survives a save.
// ============================================================================
Rectangle {
    id: root

    property var host: null
    property real dockWidth: Theme.dp(320)
    property bool maximized: false
    property bool isGroundMesh: false
    property bool isWater: false
    property bool isModel: false

    signal openWaterStudioRequested()

    /// Key of the transform field the keypad is currently editing, or "".
    property string routedKey: ""

    /// Skips the auto-refresh while a field has focus, so typing is never
    /// clobbered by an incoming transform notification.
    readonly property bool editing: nameField.activeFocus || templateField.activeFocus ||
                                    (typeof topTexField !== "undefined" && topTexField.activeFocus) ||
                                    (typeof groundTexField !== "undefined" && groundTexField.activeFocus) ||
                                    (typeof texScaleField !== "undefined" && texScaleField.activeFocus)

    signal closeRequested()
    /// Asks the studio to change `maximized`. The panel must NOT assign that
    /// property itself: the studio binds it (`maximized: root.inspectorMaximized`)
    /// and an imperative write from inside would destroy that binding for good,
    /// leaving the panel stuck wide after the next close.
    signal maximizeRequested(bool value)

    readonly property real maximalWidth: Math.max(dockWidth,
                                                  Math.min(availableWidth * 0.68, Theme.dp(620)))
    readonly property real availableWidth: parent ? parent.width : Theme.dp(412)

    width: maximized ? maximalWidth : dockWidth
    color: Theme.surface1

    Behavior on width { NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic } }

    // ── Field registry ─────────────────────────────────────────────────────
    function fieldFor(key) {
        switch (key) {
        case "posX":   return posXField
        case "posY":   return posYField
        case "posZ":   return posZField
        case "rotX":   return rotXField
        case "rotY":   return rotYField
        case "rotZ":   return (root.isGroundMesh || root.isWater) ? planarRotField : rotZField
        case "scaleX": return scaleXField
        case "scaleY": return scaleYField
        case "scaleZ": return scaleZField
        }
        return null
    }

    function labelFor(key) {
        switch (key) {
        case "posX":   return qsTr("Position X")
        case "posY":   return qsTr("Position Y")
        case "posZ":   return qsTr("Position Z")
        case "rotX":   return qsTr("Pitch · X")
        case "rotY":   return qsTr("Yaw · Y")
        case "rotZ":   return (root.isGroundMesh || root.isWater) ? qsTr("In-Plane Roll · 2.5D") : qsTr("Roll · Z")
        case "scaleX": return qsTr("Scale X")
        case "scaleY": return qsTr("Scale Y")
        case "scaleZ": return qsTr("Scale Z")
        }
        return qsTr("Value")
    }

    function unitFor(key) {
        return key.indexOf("rot") === 0 ? qsTr("deg") : (key.indexOf("pos") === 0 ? qsTr("u") : qsTr("x"))
    }

    function routeTo(key) {
        routedKey = key
        keypad.target = fieldFor(key)
        keypad.label = labelFor(key)
        keypad.unit = unitFor(key)
        // Maximise on first tap: the pad plus the fields needs the width, and
        // the user has already committed to editing a value.
        if (!maximized) root.maximizeRequested(true)
    }

    function commitKeypad() {
        applyFromFields()
        routedKey = ""
        keypad.target = null
    }

    function hasSelection() {
        return root.host && root.host.selectedObject >= 0
    }

    function syncFromHost() {
        if (!root.hasSelection()) return
        var obj = root.host.getSelectedObjectData()
        if (!obj || obj.name === undefined) return

        nameField.text = obj.name
        templateField.text = obj.template
        posXField.text = Number(obj.posX).toFixed(2)
        posYField.text = Number(obj.posY).toFixed(2)
        posZField.text = Number(obj.posZ).toFixed(2)
        // The host hands rotation over in degrees (see getSelectedObjectData).
        rotXField.text = Number(obj.rotX).toFixed(1)
        rotYField.text = Number(obj.rotY).toFixed(1)
        rotZField.text = Number(obj.rotZ).toFixed(1)
        if (typeof planarRotField !== "undefined" && planarRotField) {
            planarRotField.text = Number(obj.rotZ).toFixed(1)
        }
        scaleXField.text = Number(obj.scaleX).toFixed(2)
        scaleYField.text = Number(obj.scaleY).toFixed(2)
        scaleZField.text = Number(obj.scaleZ).toFixed(2)

        root.isGroundMesh = !!obj.isGroundMesh
        root.isWater = !!obj.isWater || (root.host && root.host.isSelectedWater && root.host.isSelectedWater())
        root.isModel = !!obj.isModel
        if (root.isGroundMesh && typeof topTexField !== "undefined" && topTexField) {
            topTexField.text = obj.topTexture || "grass_subtle"
            groundTexField.text = obj.groundTexture || "maybegood"
            texScaleField.text = Number(obj.textureScale || 250).toFixed(0)
        }
    }

    function applyGroundMeshTextures() {
        if (!root.hasSelection() || !root.host) return
        var top = (typeof topTexField !== "undefined" && topTexField) ? topTexField.text.trim() : ""
        var ground = (typeof groundTexField !== "undefined" && groundTexField) ? groundTexField.text.trim() : ""
        var scale = (typeof texScaleField !== "undefined" && texScaleField) ? (parseFloat(texScaleField.text) || -1.0) : -1.0
        root.host.setGroundMeshTextures(top, ground, scale)
    }

    function applyFromFields() {
        if (!root.hasSelection()) return
        var rz = (root.isGroundMesh || root.isWater)
                 ? (parseFloat(planarRotField.text) || 0)
                 : (parseFloat(rotZField.text) || 0)
        var ry = (root.isGroundMesh || root.isWater)
                 ? 0
                 : (parseFloat(rotYField.text) || 0)
        root.host.applyTransform(
            parseFloat(posXField.text) || 0,
            parseFloat(posYField.text) || 0,
            parseFloat(posZField.text) || 0,
            parseFloat(rotXField.text) || 0,
            ry,
            rz,
            parseFloat(scaleXField.text) || 1,
            parseFloat(scaleYField.text) || 1,
            parseFloat(scaleZField.text) || 1)
    }

    /// Owns a whole axis triplet: reads them back from the host when the
    /// selection changes so the fields can never show another object's
    /// transform.
    function resetScaleToUniform() {
        if (!root.hasSelection()) return
        var v = parseFloat(scaleXField.text) || 1
        scaleYField.text = v.toFixed(2)
        scaleZField.text = v.toFixed(2)
        root.applyFromFields()
    }

    // Left hairline separating the dock from the (still live) viewport
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        color: Theme.alpha(Theme.accentInk, 0.30)
    }

    Component.onCompleted: syncFromHost()

    Connections {
        target: root.host
        enabled: root.host !== null
        function onSelectedObjectChanged() {
            // A new object starts a fresh edit; never carry a routed field over.
            root.routedKey = ""
            keypad.target = null
            if (!root.editing) root.syncFromHost()
        }
        function onObjectTransformChanged() { if (!root.editing) root.syncFromHost() }
    }

    // ── Header ─────────────────────────────────────────────────────────────
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.topMargin: Theme.dp(Theme.spacingMd)
        height: Theme.dp(40)

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(Theme.spacingSm)

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "sliders-vertical"
                size: Theme.dp(Theme.iconSm)
                color: Theme.accentInk
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Text {
                    text: qsTr("Inspector")
                    font.pixelSize: Theme.dp(Theme.fontLg)
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }

                Text {
                    text: root.hasSelection()
                          ? qsTr("Object %1 of %2").arg(root.host.selectedObject + 1).arg(root.host.objectCount)
                          : qsTr("Nothing selected")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(2)

            IconButton {
                iconName: root.maximized ? "close" : "grid"
                buttonSize: Theme.dp(34)
                iconSize: Theme.dp(16)
                tint: root.maximized ? Theme.textSecondary : Theme.accentInk
                plate: root.maximized ? "transparent" : Theme.alpha(Theme.accentInk, 0.14)
                onClicked: root.maximizeRequested(!root.maximized)
            }

            IconButton {
                iconName: "chevron-right"
                buttonSize: Theme.dp(34)
                iconSize: Theme.dp(18)
                onClicked: root.closeRequested()
            }
        }
    }

    // ── Empty state ────────────────────────────────────────────────────────
    Column {
        anchors.top: header.bottom
        anchors.topMargin: Theme.dp(Theme.spacingXl)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.dp(Theme.spacingLg)
        anchors.rightMargin: Theme.dp(Theme.spacingLg)
        visible: !root.hasSelection()
        spacing: Theme.dp(Theme.spacingMd)

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.dp(54)
            height: Theme.dp(54)
            radius: Theme.radiusLg
            color: Theme.surface2
            border.color: Theme.borderSubtle
            border.width: 1

            Icon {
                anchors.centerIn: parent
                name: "target"
                size: Theme.dp(26)
                color: Theme.textDisabled
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Tap an object in the outliner, or tap the viewport to select one.")
            font.pixelSize: Theme.dp(Theme.fontSm)
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        PrimaryButton {
            width: parent.width
            text: qsTr("Add Object")
            iconName: "plus"
            onClicked: if (root.host) root.host.addObject()
        }
    }

    // ── Scrollable body ────────────────────────────────────────────────────
    Flickable {
        id: bodyScroll
        anchors.top: header.bottom
        anchors.topMargin: Theme.dp(Theme.spacingMd)
        anchors.left: parent.left
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.right: keypadColumn.left
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.dp(Theme.spacingMd)
        contentHeight: body.implicitHeight + Theme.dp(Theme.spacingSm)
        visible: root.hasSelection()
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body
            width: bodyScroll.width
            spacing: Theme.dp(Theme.spacingMd)

            SectionCard {
                width: parent.width
                label: qsTr("Identity")
                accentIcon: "file-text"

                ThemedField {
                    id: nameField
                    placeholderText: qsTr("Object name")
                    onEditingFinished: if (root.host) root.host.setObjectName(text)
                }

                ThemedField {
                    id: templateField
                    placeholderText: qsTr("Template")
                    onEditingFinished: if (root.host) root.host.setObjectTemplate(text)
                }
            }

            SectionCard {
                id: groundMeshCard
                width: parent.width
                label: qsTr("Ground Mesh Textures")
                accentIcon: "layers"
                visible: root.isGroundMesh

                FieldLabel { text: qsTr("Top Surface Texture (Cap / Grass)") }
                ThemedField {
                    id: topTexField
                    placeholderText: qsTr("e.g. grass_subtle, forest_grass, snowy_snow")
                    onEditingFinished: root.applyGroundMeshTextures()
                }

                FieldLabel { text: qsTr("Ground / Front Texture (Cliff / Wall)") }
                ThemedField {
                    id: groundTexField
                    placeholderText: qsTr("e.g. maybegood, forest_ground, atlon_ground")
                    onEditingFinished: root.applyGroundMeshTextures()
                }

                FieldLabel { text: qsTr("Texture UV Scale") }
                ThemedField {
                    id: texScaleField
                    placeholderText: qsTr("Default: 250")
                    onEditingFinished: root.applyGroundMeshTextures()
                }

                FieldLabel { text: qsTr("Quick Biome Presets (Authentic Swordigo)") }
                Flow {
                    width: parent.width
                    spacing: Theme.dp(5)

                    Repeater {
                        model: [
                            { name: "Plains", top: "grass_subtle", ground: "maybegood", scale: 250 },
                            { name: "Forest", top: "forest_grass", ground: "forest_ground", scale: 250 },
                            { name: "Grove", top: "grove_grass", ground: "grove_ground", scale: 250 },
                            { name: "Florennum", top: "florennum_ground", ground: "florennum_ground", scale: 250 },
                            { name: "Wasteland", top: "grass_orange", ground: "wasteland_ground", scale: 200 },
                            { name: "Snowy Peaks", top: "snowy_snow", ground: "atlon_ground", scale: 200 },
                            { name: "Ice Castle", top: "icicle", ground: "icecastle_ground", scale: 200 },
                            { name: "Volcano", top: "fire_grass", ground: "graveyard_ground", scale: 250 },
                            { name: "Caves", top: "wasteland_ground", ground: "wasteland_ground2", scale: 250 },
                            { name: "Crypt", top: "crypt_tiles", ground: "crypt_tiles", scale: 250 }
                        ]

                        delegate: Rectangle {
                            height: Theme.dp(26)
                            width: chipText.implicitWidth + Theme.dp(16)
                            radius: Theme.radiusPill
                            color: Theme.alpha(Theme.accentInk, 0.12)
                            border.color: Theme.alpha(Theme.accentInk, 0.30)
                            border.width: 1

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: modelData.name
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                font.weight: Font.DemiBold
                                color: Theme.accentInk
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(15)
                                    topTexField.text = modelData.top
                                    groundTexField.text = modelData.ground
                                    texScaleField.text = String(modelData.scale || 250)
                                    root.applyGroundMeshTextures()
                                }
                            }
                        }
                    }
                }

                FieldLabel { text: qsTr("Custom Modder Presets") }

                Flow {
                    width: parent.width
                    spacing: Theme.dp(6)
                    visible: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.customMeshPresets && rubySettings.customMeshPresets.length > 0)

                    Repeater {
                        model: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.customMeshPresets : []

                        delegate: Rectangle {
                            height: Theme.dp(28)
                            width: chipRow.implicitWidth + Theme.dp(16)
                            radius: Theme.radiusPill
                            color: chipClick.pressed ? Theme.alpha(Theme.accentStart, 0.30) : Theme.alpha(Theme.accentStart, 0.15)
                            border.color: Theme.alpha(Theme.accentStart, 0.40)
                            border.width: 1

                            MouseArea {
                                id: chipClick
                                anchors.fill: parent
                                onClicked: {
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(15)
                                    topTexField.text = modelData.top || ""
                                    groundTexField.text = modelData.ground || ""
                                    root.applyGroundMeshTextures()
                                }
                            }

                            Row {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (modelData.name && modelData.name.length > 0) ? modelData.name : (modelData.top + " / " + modelData.ground)
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                }

                                Rectangle {
                                    width: Theme.dp(16)
                                    height: Theme.dp(16)
                                    radius: Theme.dp(8)
                                    color: delMouse.pressed ? Theme.alpha(Theme.colorErrorBright, 0.3) : "transparent"
                                    anchors.verticalCenter: parent.verticalCenter

                                    Icon {
                                        anchors.centerIn: parent
                                        name: "close"
                                        size: Theme.dp(10)
                                        color: Theme.colorErrorBright
                                    }

                                    MouseArea {
                                        id: delMouse
                                        anchors.fill: parent
                                        anchors.margins: -Theme.dp(4)
                                        onClicked: {
                                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                                screenOrientation.vibrateTouch(25)
                                            if (typeof rubySettings !== "undefined" && rubySettings)
                                                rubySettings.deleteCustomMeshPreset(index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(6)

                    ThemedField {
                        id: customPresetNameField
                        width: parent.width - savePresetBtn.width - Theme.dp(6)
                        placeholderText: qsTr("Save as Preset Name...")
                    }

                    Rectangle {
                        id: savePresetBtn
                        width: Theme.dp(80)
                        height: customPresetNameField.height
                        radius: Theme.radiusSm
                        color: savePresetHit.pressed ? Theme.accentEnd : Theme.accentStart
                        border.color: Theme.accentEnd
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(4)
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "save"
                                size: Theme.dp(Theme.iconXs)
                                color: Theme.onAccent
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Save")
                                font.pixelSize: Theme.dp(Theme.fontSm)
                                font.weight: Font.Bold
                                color: Theme.onAccent
                            }
                        }

                        MouseArea {
                            id: savePresetHit
                            anchors.fill: parent
                            onClicked: {
                                if (topTexField.text.trim() === "" && groundTexField.text.trim() === "")
                                    return
                                if (typeof screenOrientation !== "undefined" && screenOrientation)
                                    screenOrientation.vibrateTouch(20)
                                var pName = customPresetNameField.text.trim()
                                if (typeof rubySettings !== "undefined" && rubySettings) {
                                    rubySettings.saveCustomMeshPreset(pName, topTexField.text.trim(), groundTexField.text.trim())
                                }
                                customPresetNameField.text = ""
                            }
                        }
                    }
                }

                PrimaryButton {
                    width: parent.width
                    text: qsTr("Apply Textures")
                    iconName: "check"
                    onClicked: root.applyGroundMeshTextures()
                }
            }

            SectionCard {
                width: parent.width
                label: qsTr("Fluid & WaterMesh")
                accentIcon: "water"
                visible: root.isWater

                FieldLabel { text: qsTr("Interactive Fluid Volume") }

                Text {
                    width: parent.width
                    text: qsTr("Authentic fluid mechanics with wave simulation, dual surface-crest tinting, and customizable presets.")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }

                PrimaryButton {
                    width: parent.width
                    text: qsTr("Open Fluid Studio")
                    iconName: "water"
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(20)
                        root.openWaterStudioRequested()
                    }
                }
            }

            SectionCard {
                width: parent.width
                label: qsTr("Transform")
                accentIcon: "move"

                FieldLabel { text: qsTr("Position · world units") }
                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)
                    KeypadField { id: posXField; keyLabel: qsTr("Position X"); routed: root.routedKey === "posX"; width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3; onTapRequested: root.routeTo("posX") }
                    KeypadField { id: posYField; keyLabel: qsTr("Position Y"); routed: root.routedKey === "posY"; width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3; onTapRequested: root.routeTo("posY") }
                    KeypadField { id: posZField; keyLabel: qsTr("Position Z"); routed: root.routedKey === "posZ"; width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3; onTapRequested: root.routeTo("posZ") }
                }

                FieldLabel {
                    text: (root.isGroundMesh || root.isWater)
                          ? qsTr("In-Plane Roll · degrees")
                          : (root.isModel ? qsTr("3D Rotation · Pitch, Yaw, Roll") : qsTr("Rotation · degrees"))
                }

                // 3D Rotation Fields (Pitch X, Yaw Y, Roll Z) for Models and generic objects
                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)
                    visible: !root.isGroundMesh && !root.isWater

                    KeypadField {
                        id: rotXField
                        keyLabel: qsTr("Pitch · X")
                        routed: root.routedKey === "rotX"
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                        onTapRequested: root.routeTo("rotX")
                    }
                    KeypadField {
                        id: rotYField
                        keyLabel: qsTr("Yaw · Y")
                        routed: root.routedKey === "rotY"
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                        onTapRequested: root.routeTo("rotY")
                    }
                    KeypadField {
                        id: rotZField
                        keyLabel: qsTr("Roll · Z")
                        routed: root.routedKey === "rotZ"
                        width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3
                        onTapRequested: root.routeTo("rotZ")
                    }
                }

                // Dedicated clean single angle field for GroundMesh / WaterMesh
                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)
                    visible: root.isGroundMesh || root.isWater

                    KeypadField {
                        id: planarRotField
                        keyLabel: qsTr("Angle (Roll · 2.5D)")
                        text: rotZField.text
                        routed: root.routedKey === "rotZ"
                        width: parent.width - Theme.dp(96)
                        onTapRequested: root.routeTo("rotZ")
                    }

                    Rectangle {
                        width: Theme.dp(88)
                        height: Theme.dp(36)
                        anchors.verticalCenter: planarRotField.verticalCenter
                        radius: Theme.radiusMd
                        color: resetPlanarArea.pressed ? Theme.accentStart : Theme.surface2
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Reset 0°")
                            font.pixelSize: Theme.dp(Theme.fontSm)
                            font.weight: Font.Medium
                            color: Theme.textSecondary
                        }

                        MouseArea {
                            id: resetPlanarArea
                            anchors.fill: parent
                            onClicked: {
                                rotZField.text = "0.0"
                                if (typeof planarRotField !== "undefined" && planarRotField)
                                    planarRotField.text = "0.0"
                                root.applyFromFields()
                            }
                        }
                    }
                }

                // Quick Action Preset Pills for 3D Models
                Row {
                    width: parent.width
                    spacing: Theme.dp(5)
                    visible: root.isModel

                    Repeater {
                        model: [
                            { label: qsTr("Flip H"), action: "flipH" },
                            { label: qsTr("Flip V"), action: "flipV" },
                            { label: qsTr("Lay Flat"), action: "layFlat" },
                            { label: qsTr("Reset 0°"), action: "reset" }
                        ]

                        delegate: Rectangle {
                            height: Theme.dp(26)
                            width: (parent.width - Theme.dp(5) * 3) / 4
                            radius: Theme.radiusPill
                            color: pillArea.pressed ? Theme.accentStart : Theme.alpha(Theme.surface2, 0.8)
                            border.color: Theme.alpha(Theme.accentInk, 0.25)
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: Theme.dp(10)
                                font.weight: Font.Medium
                                color: Theme.textPrimary
                            }

                            MouseArea {
                                id: pillArea
                                anchors.fill: parent
                                onClicked: {
                                    if (modelData.action === "flipH") {
                                        var cy = (parseFloat(rotYField.text) || 0) + 180.0
                                        while (cy >= 360.0) cy -= 360.0
                                        rotYField.text = cy.toFixed(1)
                                        root.applyFromFields()
                                    } else if (modelData.action === "flipV") {
                                        var cr = (parseFloat(rotZField.text) || 0) + 180.0
                                        while (cr >= 360.0) cr -= 360.0
                                        rotZField.text = cr.toFixed(1)
                                        root.applyFromFields()
                                    } else if (modelData.action === "layFlat") {
                                        rotXField.text = "90.0"
                                        root.applyFromFields()
                                    } else if (modelData.action === "reset") {
                                        rotXField.text = "0.0"
                                        rotYField.text = "0.0"
                                        rotZField.text = "0.0"
                                        root.applyFromFields()
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: (root.isGroundMesh || root.isWater)
                          ? qsTr("Ground & water meshes use 2D planar physics. Rotates in-plane (Roll) only.")
                          : (root.isModel ? qsTr("3D model rotation (Pitch X, Yaw Y, Roll Z). Fully saved into .scene ModelComponent.")
                                          : qsTr("A .scene persists Y rotation and one uniform scale; X/Z rotation is a live preview only."))
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    FieldLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - uniformButton.width - Theme.dp(Theme.spacingSm)
                        text: qsTr("Scale")
                    }

                    Rectangle {
                        id: uniformButton
                        width: uniformLabel.implicitWidth + Theme.dp(20)
                        height: Theme.dp(26)
                        radius: Theme.radiusPill
                        color: uniformArea.pressed ? Theme.accentStart : Theme.alpha(Theme.accentInk, 0.12)
                        border.color: Theme.alpha(Theme.accentInk, 0.30)
                        border.width: 1

                        Text {
                            id: uniformLabel
                            anchors.centerIn: parent
                            text: qsTr("Uniform")
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: Font.DemiBold
                            color: Theme.accentInk
                        }

                        MouseArea {
                            id: uniformArea
                            anchors.fill: parent
                            onClicked: root.resetScaleToUniform()
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)
                    KeypadField { id: scaleXField; keyLabel: qsTr("Scale X"); routed: root.routedKey === "scaleX"; width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3; onTapRequested: root.routeTo("scaleX") }
                    KeypadField { id: scaleYField; keyLabel: qsTr("Scale Y"); routed: root.routedKey === "scaleY"; width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3; onTapRequested: root.routeTo("scaleY") }
                    KeypadField { id: scaleZField; keyLabel: qsTr("Scale Z"); routed: root.routedKey === "scaleZ"; width: (parent.width - Theme.dp(Theme.spacingSm) * 2) / 3; onTapRequested: root.routeTo("scaleZ") }
                }

                PrimaryButton {
                    width: parent.width
                    text: qsTr("Apply Transform")
                    iconName: "check"
                    onClicked: root.applyFromFields()
                }
            }

            SectionCard {
                width: parent.width
                label: qsTr("Actions")
                accentIcon: "bolt"

                Row {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    Rectangle {
                        width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                        height: Theme.dp(40)
                        radius: Theme.radiusSm
                        color: focusArea.pressed ? Theme.accentStart : Theme.alpha(Theme.accentInk, 0.12)
                        border.color: Theme.alpha(Theme.accentInk, 0.30)
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: "target"; size: Theme.dp(Theme.iconSm); color: Theme.accentInk }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Focus"); font.pixelSize: Theme.dp(Theme.fontSm); color: Theme.accentInk; font.weight: Font.DemiBold }
                        }

                        MouseArea {
                            id: focusArea
                            anchors.fill: parent
                            onClicked: if (root.host) root.host.focusObject(root.host.selectedObject)
                        }
                    }

                    Rectangle {
                        width: (parent.width - Theme.dp(Theme.spacingSm)) / 2
                        height: Theme.dp(40)
                        radius: Theme.radiusSm
                        color: dupArea.pressed ? Theme.surface2 : "transparent"
                        border.color: Theme.borderStrong
                        border.width: 1

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: "copy"; size: Theme.dp(Theme.iconSm); color: Theme.textSecondary }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Duplicate"); font.pixelSize: Theme.dp(Theme.fontSm); color: Theme.textSecondary; font.weight: Font.DemiBold }
                        }

                        MouseArea {
                            id: dupArea
                            anchors.fill: parent
                            onClicked: if (root.host) root.host.duplicateObject(root.host.selectedObject)
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Theme.dp(40)
                    radius: Theme.radiusSm
                    color: delArea.pressed ? Theme.colorError : Theme.alpha(Theme.colorError, 0.22)
                    border.color: Theme.alpha(Theme.colorErrorBright, 0.35)
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.dp(5)
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "trash"; size: Theme.dp(Theme.iconSm); color: Theme.colorErrorBright }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Delete Object"); font.pixelSize: Theme.dp(Theme.fontSm); color: Theme.colorErrorBright; font.weight: Font.DemiBold }
                    }

                    MouseArea {
                        id: delArea
                        anchors.fill: parent
                        onClicked: if (root.host) root.host.deleteObject(root.host.selectedObject)
                    }
                }
            }
        }
    }

    // ── Keypad column ──────────────────────────────────────────────────────
    // Beside the fields, not below them. Portrait would stack the pad under the
    // form, but in landscape the panel is wide and the screen is short: putting
    // the pad in its own column keeps every field on screen while a value is
    // being edited, instead of scrolling the field you tapped out of view.
    Rectangle {
        id: keypadColumn
        anchors.top: header.bottom
        anchors.topMargin: Theme.dp(Theme.spacingMd)
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.dp(Theme.spacingMd)
        width: root.maximized ? Math.min(Theme.dp(236), parent.width * 0.42) : 0
        visible: width > Theme.dp(160) && root.hasSelection()
        color: Theme.surfaceSunken
        radius: Theme.radiusCard
        border.color: Theme.borderSubtle
        border.width: 1
        clip: true

        Behavior on width { NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic } }

        NumericKeypad {
            id: keypad
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.dp(Theme.spacingMd)
            visible: root.routedKey.length > 0
            opacity: root.routedKey.length > 0 ? 1.0 : 0.0
            onEditingFinished: root.commitKeypad()
            onCancelled: {
                root.routedKey = ""
                keypad.target = null
            }
            Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        }

        // Hint shown in place of the pad while no field is routed.
        Column {
            anchors.centerIn: parent
            width: parent.width - Theme.dp(Theme.spacingLg) * 2
            spacing: Theme.dp(Theme.spacingSm)
            visible: root.routedKey.length === 0

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "chip"
                size: Theme.dp(Theme.iconLg)
                color: Theme.textDisabled
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Tap any value to edit it on the number pad")
                font.pixelSize: Theme.dp(Theme.fontXs)
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
        }
    }
}
