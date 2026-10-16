import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// ============================================================================
// WaterMeshStudioPanel.qml — Dedicated Fluid Mechanics & WaterMesh Studio.
// Clean, native, professional inspector dock for tuning water sheet parameters:
// in-scene visual editor trigger, geometry dimensions, tile size, textures,
// and body/surface crest color dynamics.
// ============================================================================
Rectangle {
    id: root

    property var host: null
    property bool open: false
    property real dockWidth: Math.min(Theme.dp(330), parent ? parent.width * 0.85 : Theme.dp(330))

    signal closeRequested()

    width: dockWidth
    height: parent ? parent.height : Theme.dp(400)
    x: open ? (parent.width - width) : (parent.width + Theme.dp(10))
    y: 0
    z: 32
    color: Theme.surface0
    border.color: Theme.borderSubtle
    border.width: 1

    Behavior on x { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
    }

    property var currentProps: ({})

    function refreshProperties() {
        if (host && host.isSelectedWater) {
            root.currentProps = host.getSelectedWaterProperties()
        } else {
            root.currentProps = ({})
        }
    }

    Connections {
        target: host
        function onSelectedObjectChanged() {
            root.refreshProperties()
            if (host && !host.isSelectedWater && root.open) {
                root.closeRequested()
            }
        }
        function onSelectedWaterPropertiesChanged() {
            root.refreshProperties()
        }
    }

    onOpenChanged: {
        if (open) refreshProperties()
    }

    // Top Header
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.dp(50)
        color: Theme.surface1
        border.color: Theme.borderSubtle
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(Theme.paddingScreen)
            anchors.rightMargin: Theme.dp(Theme.paddingScreen)
            spacing: Theme.dp(Theme.spacingSm)

            Rectangle {
                width: Theme.dp(30)
                height: Theme.dp(30)
                radius: Theme.radiusSm
                color: Theme.alpha("#06b6d4", 0.20)
                border.color: Theme.alpha("#06b6d4", 0.40)
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "water"
                    size: Theme.dp(16)
                    color: "#06b6d4"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    text: qsTr("WaterMesh Studio")
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }

                Text {
                    text: qsTr("Fluid dynamics & surface simulation")
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
                }
            }

            IconButton {
                iconName: "close"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                tint: Theme.textSecondary
                tooltip: qsTr("Close Studio")
                onClicked: root.closeRequested()
            }
        }
    }

    // Scrollable Controls Body
    Flickable {
        id: flick
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: contentCol.height + Theme.dp(24)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.dp(Theme.paddingScreen)
            spacing: Theme.dp(Theme.spacingMd)
            topPadding: Theme.dp(Theme.spacingSm)

            // ── Primary Action: Enter In-Scene 3D W-Mesh Editor ────────────
            Rectangle {
                width: parent.width
                height: Theme.dp(44)
                radius: Theme.radiusSm
                color: Theme.alpha("#06b6d4", 0.16)
                border.color: "#06b6d4"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(12)
                    anchors.rightMargin: Theme.dp(12)
                    spacing: Theme.dp(8)

                    Icon {
                        name: "water"
                        size: Theme.dp(16)
                        color: "#06b6d4"
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Enter Visual W-Mesh Editor")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.Bold
                        color: "#06b6d4"
                    }

                    Icon {
                        name: "chevron-right"
                        size: Theme.dp(14)
                        color: "#06b6d4"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) {
                            screenOrientation.vibrateTouch(20)
                            screenOrientation.lockLandscape()
                        }
                        if (root.host) {
                            root.closeRequested()
                            root.host.beginWaterMeshEdit()
                        }
                    }
                }
            }

            // 1. Dimensions & Bounds
            SectionCard {
                width: parent.width
                label: qsTr("Dimensions & Position")
                accentIcon: "cube"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    // Width & Depth steppers
                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        // Width
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Width") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("width", -20)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("%1 u").arg(Number(root.currentProps.width || 0).toFixed(0))
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Bold
                                    color: "#06b6d4"
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("width", 20)
                                }
                            }
                        }

                        // Depth
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Depth (Height)") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("height", -20)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("%1 u").arg(Number(root.currentProps.height || 0).toFixed(0))
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Bold
                                    color: "#06b6d4"
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("height", 20)
                                }
                            }
                        }
                    }

                    // Position X & Y steppers
                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        // Pos X
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Position X") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("x", -20)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: Number(root.currentProps.x || 0).toFixed(0)
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Medium
                                    color: Theme.textPrimary
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("x", 20)
                                }
                            }
                        }

                        // Pos Y
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Position Y") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("y", -20)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: Number(root.currentProps.y || 0).toFixed(0)
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Medium
                                    color: Theme.textPrimary
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: root.adjustDimension("y", 20)
                                }
                            }
                        }
                    }
                }
            }

            // 2. Wave Tile Size & Texture
            SectionCard {
                width: parent.width
                label: qsTr("Wave Simulation & Texture")
                accentIcon: "water"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    // Wave Tile Size (clean stepper with label and units)
                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.dp(2)

                        FieldLabel { text: qsTr("Wave Tile Size (wavelength frequency)") }
                        RowLayout {
                            width: parent.width
                            spacing: Theme.dp(8)

                            IconButton {
                                iconName: "minus"
                                buttonSize: Theme.dp(32)
                                iconSize: Theme.dp(14)
                                onClicked: root.adjustTileSize(-16)
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.dp(32)
                                radius: Theme.radiusSm
                                color: Theme.surface2
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Row {
                                    anchors.centerIn: parent
                                    spacing: Theme.dp(4)
                                    Icon {
                                        name: "layers"
                                        size: Theme.dp(12)
                                        color: Theme.textMuted
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Text {
                                        text: qsTr("%1 px").arg(Number(root.currentProps.tile_size || 64).toFixed(0))
                                        font.pixelSize: Theme.dp(Theme.fontSm)
                                        font.weight: Font.Bold
                                        color: "#06b6d4"
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }

                            IconButton {
                                iconName: "plus"
                                buttonSize: Theme.dp(32)
                                iconSize: Theme.dp(14)
                                onClicked: root.adjustTileSize(16)
                            }
                        }
                    }

                    // Texture Name
                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.dp(2)

                        FieldLabel { text: qsTr("Material Texture") }
                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.dp(32)
                            radius: Theme.radiusSm
                            color: Theme.surface2
                            border.color: Theme.borderSubtle
                            border.width: 1

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.dp(10)
                                spacing: Theme.dp(6)
                                Icon {
                                    name: "file"
                                    size: Theme.dp(13)
                                    color: Theme.textMuted
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: root.currentProps.texture || "water.png"
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }

            // 3. Fluid Color Dynamics
            SectionCard {
                width: parent.width
                label: qsTr("Color Dynamics")
                accentIcon: "palette"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingMd)

                        // Deep Body Color
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(4)

                            FieldLabel { text: qsTr("Deep Body Color") }
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.dp(38)
                                radius: Theme.radiusSm
                                color: Qt.rgba(root.currentProps.color_r || 0,
                                               root.currentProps.color_g || 0.3,
                                               root.currentProps.color_b || 0.7,
                                               root.currentProps.color_a || 0.8)
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: Qt.rgba(root.currentProps.color_r || 0,
                                                  root.currentProps.color_g || 0.3,
                                                  root.currentProps.color_b || 0.7, 1.0).toString()
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.family: "Monospace"
                                    color: "#ffffff"
                                }
                            }
                        }

                        // Surface Crest Color
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(4)

                            FieldLabel { text: qsTr("Surface Crest") }
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.dp(38)
                                radius: Theme.radiusSm
                                color: Qt.rgba(root.currentProps.surface_r || 0.2,
                                               root.currentProps.surface_g || 0.8,
                                               root.currentProps.surface_b || 1.0,
                                               root.currentProps.surface_a || 0.9)
                                border.color: Theme.borderSubtle
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: Qt.rgba(root.currentProps.surface_r || 0.2,
                                                  root.currentProps.surface_g || 0.8,
                                                  root.currentProps.surface_b || 1.0, 1.0).toString()
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.family: "Monospace"
                                    color: "#000000"
                                }
                            }
                        }
                    }
                }
            }

            // 4. Actions
            SectionCard {
                width: parent.width
                label: qsTr("Actions")
                accentIcon: "bolt"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Focus Fluid")
                            highlighted: false
                            onClicked: {
                                if (root.host) {
                                    root.host.frameObject(root.host.selectedObject)
                                }
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Delete Pool")
                            onClicked: {
                                if (root.host) {
                                    root.host.deleteCurrentObject()
                                    root.closeRequested()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function adjustDimension(prop, delta) {
        if (!root.host || !root.host.isSelectedWater) return
        var p = root.host.getSelectedWaterProperties()
        var current = Number(p[prop] || 0)
        var updated = current + delta
        if (prop === "width" && updated < 20) updated = 20
        if (prop === "height" && updated < 20) updated = 20
        p[prop] = updated
        root.host.setSelectedWaterProperties(p)
        root.refreshProperties()
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(15)
    }

    function adjustTileSize(delta) {
        if (!root.host || !root.host.isSelectedWater) return
        var p = root.host.getSelectedWaterProperties()
        var current = Number(p["tile_size"] || 64)
        var updated = Math.max(16, Math.min(256, current + delta))
        p["tile_size"] = updated
        root.host.setSelectedWaterProperties(p)
        root.refreshProperties()
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(15)
    }
}
