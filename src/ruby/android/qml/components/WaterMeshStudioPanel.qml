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
    property string activeColorTarget: "body" // "body" or "surface"
    property real curH: 0.58
    property real curS: 0.75
    property real curV: 0.85
    property real curA: 0.80
    property bool internalColorSync: false

    readonly property var fluidSwatches: [
        {
            name: qsTr("Crystal Water"),
            body: [0.08, 0.35, 0.70, 0.75],
            crest: [0.25, 0.65, 0.95, 0.85],
            deadly: false
        },
        {
            name: qsTr("Molten Lava"),
            body: [0.85, 0.18, 0.05, 0.95],
            crest: [1.00, 0.58, 0.08, 0.98],
            deadly: true,
            minDmg: 4,
            maxDmg: 4,
            dmgType: 0,
            ignoreImmunity: true
        },
        {
            name: qsTr("Toxic Acid"),
            body: [0.12, 0.55, 0.12, 0.85],
            crest: [0.45, 0.95, 0.18, 0.90],
            deadly: true,
            minDmg: 2,
            maxDmg: 3,
            dmgType: 1,
            ignoreImmunity: false
        },
        {
            name: qsTr("Murky Swamp"),
            body: [0.22, 0.28, 0.15, 0.85],
            crest: [0.38, 0.45, 0.22, 0.88],
            deadly: false
        },
        {
            name: qsTr("Void Aether"),
            body: [0.45, 0.12, 0.75, 0.80],
            crest: [0.75, 0.35, 0.98, 0.85],
            deadly: false
        },
        {
            name: qsTr("Glacial Ice"),
            body: [0.00, 0.52, 0.60, 0.70],
            crest: [0.50, 0.88, 0.95, 0.85],
            deadly: false
        },
        {
            name: qsTr("Golden Nectar"),
            body: [0.75, 0.50, 0.02, 0.80],
            crest: [1.00, 0.82, 0.20, 0.90],
            deadly: false
        },
        {
            name: qsTr("Abyssal Dark"),
            body: [0.03, 0.06, 0.12, 0.95],
            crest: [0.12, 0.22, 0.38, 0.90],
            deadly: false
        }
    ]

    function hsvToRgb(h, s, v) {
        var r = 0, g = 0, b = 0;
        var i = Math.floor(h * 6);
        var f = h * 6 - i;
        var p = v * (1 - s);
        var q = v * (1 - f * s);
        var t = v * (1 - (1 - f) * s);
        switch (i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            case 5: r = v; g = p; b = q; break;
        }
        return { r: r, g: g, b: b };
    }

    function rgbToHsv(r, g, b) {
        var max = Math.max(r, g, b), min = Math.min(r, g, b);
        var d = max - min;
        var h = 0, s = (max === 0 ? 0 : d / max), v = max;
        if (max !== min) {
            if (max === r) h = (g - b) / d + (g < b ? 6 : 0);
            else if (max === g) h = (b - r) / d + 2;
            else if (max === b) h = (r - g) / d + 4;
            h /= 6;
        }
        return { h: h, s: s, v: v };
    }

    function toHex2(val) {
        var hex = Math.round(Math.max(0, Math.min(1, val)) * 255).toString(16);
        return hex.length === 1 ? "0" + hex : hex;
    }

    function rgbaToHex(r, g, b) {
        return "#" + toHex2(r) + toHex2(g) + toHex2(b);
    }

    function parseHex(hexStr) {
        var str = hexStr.trim();
        if (str.indexOf("#") === 0) str = str.substring(1);
        if (str.length === 6) {
            var r = parseInt(str.substring(0, 2), 16) / 255.0;
            var g = parseInt(str.substring(2, 4), 16) / 255.0;
            var b = parseInt(str.substring(4, 6), 16) / 255.0;
            if (!isNaN(r) && !isNaN(g) && !isNaN(b)) {
                return { r: r, g: g, b: b, a: -1, valid: true };
            }
        } else if (str.length === 8) {
            var r8 = parseInt(str.substring(0, 2), 16) / 255.0;
            var g8 = parseInt(str.substring(2, 4), 16) / 255.0;
            var b8 = parseInt(str.substring(4, 6), 16) / 255.0;
            var a8 = parseInt(str.substring(6, 8), 16) / 255.0;
            if (!isNaN(r8) && !isNaN(g8) && !isNaN(b8) && !isNaN(a8)) {
                return { r: r8, g: g8, b: b8, a: a8, valid: true };
            }
        }
        return { valid: false };
    }

    function syncHsvFromCurrentProps() {
        if (!root.currentProps) return;
        var r = 0.20, g = 0.50, b = 0.85, a = 0.78;
        if (root.activeColorTarget === "body") {
            r = Number(root.currentProps.frontR !== undefined ? root.currentProps.frontR : (root.currentProps.color_r !== undefined ? root.currentProps.color_r : 0.20));
            g = Number(root.currentProps.frontG !== undefined ? root.currentProps.frontG : (root.currentProps.color_g !== undefined ? root.currentProps.color_g : 0.50));
            b = Number(root.currentProps.frontB !== undefined ? root.currentProps.frontB : (root.currentProps.color_b !== undefined ? root.currentProps.color_b : 0.85));
            a = Number(root.currentProps.frontA !== undefined ? root.currentProps.frontA : (root.currentProps.color_a !== undefined ? root.currentProps.color_a : 0.78));
        } else {
            r = Number(root.currentProps.surfaceR !== undefined ? root.currentProps.surfaceR : (root.currentProps.surface_r !== undefined ? root.currentProps.surface_r : 0.40));
            g = Number(root.currentProps.surfaceG !== undefined ? root.currentProps.surfaceG : (root.currentProps.surface_g !== undefined ? root.currentProps.surface_g : 0.75));
            b = Number(root.currentProps.surfaceB !== undefined ? root.currentProps.surfaceB : (root.currentProps.surface_b !== undefined ? root.currentProps.surface_b : 1.00));
            a = Number(root.currentProps.surfaceA !== undefined ? root.currentProps.surfaceA : (root.currentProps.surface_a !== undefined ? root.currentProps.surface_a : 0.82));
        }
        var hsv = rgbToHsv(r, g, b);
        root.curH = hsv.h;
        root.curS = hsv.s;
        root.curV = hsv.v;
        root.curA = a;
    }

    function applyActiveHsv() {
        if (!root.host || !root.host.isSelectedWater) return;
        var rgb = hsvToRgb(root.curH, root.curS, root.curV);
        var p = root.host.getSelectedWaterProperties();
        if (root.activeColorTarget === "body") {
            p["frontR"] = rgb.r; p["frontG"] = rgb.g; p["frontB"] = rgb.b; p["frontA"] = root.curA;
            p["color_r"] = rgb.r; p["color_g"] = rgb.g; p["color_b"] = rgb.b; p["color_a"] = root.curA;
        } else {
            p["surfaceR"] = rgb.r; p["surfaceG"] = rgb.g; p["surfaceB"] = rgb.b; p["surfaceA"] = root.curA;
            p["surface_r"] = rgb.r; p["surface_g"] = rgb.g; p["surface_b"] = rgb.b; p["surface_a"] = root.curA;
        }
        root.internalColorSync = true;
        root.host.setSelectedWaterProperties(p);
        root.currentProps = p;
        root.internalColorSync = false;
    }

    function setHazardProperties(propsMap) {
        if (!root.host || !root.host.isSelectedWater) return;
        var p = root.host.getSelectedWaterProperties();
        for (var k in propsMap) {
            p[k] = propsMap[k];
        }
        root.host.setSelectedWaterProperties(p);
        root.refreshProperties();
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(15);
    }

    function setHazardProperty(prop, value) {
        var m = {};
        m[prop] = value;
        setHazardProperties(m);
    }

    function applyFluidSwatch(swatch) {
        if (!root.host || !root.host.isSelectedWater) return;
        var p = root.host.getSelectedWaterProperties();
        p["frontR"] = swatch.body[0]; p["frontG"] = swatch.body[1]; p["frontB"] = swatch.body[2]; p["frontA"] = swatch.body[3];
        p["color_r"] = swatch.body[0]; p["color_g"] = swatch.body[1]; p["color_b"] = swatch.body[2]; p["color_a"] = swatch.body[3];
        p["surfaceR"] = swatch.crest[0]; p["surfaceG"] = swatch.crest[1]; p["surfaceB"] = swatch.crest[2]; p["surfaceA"] = swatch.crest[3];
        p["surface_r"] = swatch.crest[0]; p["surface_g"] = swatch.crest[1]; p["surface_b"] = swatch.crest[2]; p["surface_a"] = swatch.crest[3];
        p["isDeadly"] = swatch.deadly;
        if (swatch.deadly) {
            p["minDamage"] = swatch.minDmg !== undefined ? swatch.minDmg : 4;
            p["maxDamage"] = swatch.maxDmg !== undefined ? swatch.maxDmg : 4;
            p["damageType"] = swatch.dmgType !== undefined ? swatch.dmgType : 0;
            p["ignoreImmunity"] = swatch.ignoreImmunity !== undefined ? swatch.ignoreImmunity : true;
        }
        root.host.setSelectedWaterProperties(p);
        root.refreshProperties();
        syncHsvFromCurrentProps();
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(25);
    }

    function refreshProperties() {
        if (host && host.isSelectedWater) {
            root.currentProps = host.getSelectedWaterProperties();
            if (!internalColorSync) {
                syncHsvFromCurrentProps();
            }
        } else {
            root.currentProps = ({});
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
        if (open) {
            refreshProperties()
            syncHsvFromCurrentProps()
        }
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
                label: qsTr("Mesh Dimensions & Extrusion")
                accentIcon: "cube"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    // 1. Primary Mesh Dimensions: Width, Height, and Mesh Z Depth
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

                        // Height (Extents)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Height") }
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

                    // 2. Object World Position (Pos X, Pos Y, and World Layer Z)
                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        // Pos X
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("World Pos X") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(28)
                                    iconSize: Theme.dp(11)
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
                                    buttonSize: Theme.dp(28)
                                    iconSize: Theme.dp(11)
                                    onClicked: root.adjustDimension("x", 20)
                                }
                            }
                        }

                        // Pos Y
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("World Pos Y") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(28)
                                    iconSize: Theme.dp(11)
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
                                    buttonSize: Theme.dp(28)
                                    iconSize: Theme.dp(11)
                                    onClicked: root.adjustDimension("y", 20)
                                }
                            }
                        }

                        // Layer Z (World Translation)
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("World Layer Z") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(28)
                                    iconSize: Theme.dp(11)
                                    onClicked: root.adjustDimension("posZ", -5)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: Number(root.currentProps.posZ !== undefined ? root.currentProps.posZ : (root.currentProps.z || 0)).toFixed(0)
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Bold
                                    color: "#a855f7"
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(28)
                                    iconSize: Theme.dp(11)
                                    onClicked: root.adjustDimension("posZ", 5)
                                }
                            }
                        }
                    }
                }
            }

            // 3D Fluid Extension (Recursive Sheeting)
            SectionCard {
                width: parent.width
                label: qsTr("3D Fluid Extension (Sheets)")
                accentIcon: "layers"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    // Total fluid depth badge
                    Rectangle {
                        width: parent.width
                        height: Theme.dp(28)
                        radius: Theme.radiusSm
                        color: Theme.alpha("#06b6d4", 0.15)
                        border.color: Theme.alpha("#06b6d4", 0.4)
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.dp(10)
                            anchors.rightMargin: Theme.dp(10)

                            Text {
                                text: qsTr("TOTAL FLUID DEPTH")
                                font.pixelSize: Theme.dp(9)
                                font.weight: Font.Bold
                                color: Theme.alpha("#06b6d4", 0.8)
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: {
                                    var sf = Number(root.currentProps.sheetsFront || 0);
                                    var sb = Number(root.currentProps.sheetsBack || 0);
                                    var tot = 1 + sf + sb;
                                    return qsTr("%1 Sheets (%2 u)").arg(tot).arg(tot * 60);
                                }
                                font.pixelSize: Theme.dp(11)
                                font.weight: Font.Bold
                                color: "#06b6d4"
                            }
                        }
                    }

                    // Front & Back Sheets Steppers
                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)

                        // Front Extension (+Z) Stepper
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Front Sheets (+Z)") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: {
                                        if (root.host && root.host.removeWaterSheetFront) {
                                            root.host.removeWaterSheetFront();
                                            root.refreshProperties();
                                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                                screenOrientation.vibrateTouch(15);
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("+%1").arg(Number(root.currentProps.sheetsFront || 0))
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Bold
                                    color: "#06b6d4"
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: {
                                        if (root.host && root.host.addWaterSheetFront) {
                                            root.host.addWaterSheetFront();
                                            root.refreshProperties();
                                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                                screenOrientation.vibrateTouch(15);
                                        }
                                    }
                                }
                            }
                        }

                        // Back Extension (-Z) Stepper
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Back Sheets (-Z)") }
                            RowLayout {
                                spacing: Theme.dp(4)
                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: {
                                        if (root.host && root.host.removeWaterSheetBack) {
                                            root.host.removeWaterSheetBack();
                                            root.refreshProperties();
                                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                                screenOrientation.vibrateTouch(15);
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("+%1").arg(Number(root.currentProps.sheetsBack || 0))
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.weight: Font.Bold
                                    color: "#06b6d4"
                                }
                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(30)
                                    iconSize: Theme.dp(12)
                                    onClicked: {
                                        if (root.host && root.host.addWaterSheetBack) {
                                            root.host.addWaterSheetBack();
                                            root.refreshProperties();
                                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                                screenOrientation.vibrateTouch(15);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Extent & World Readouts
                    Rectangle {
                        width: parent.width
                        height: Theme.dp(48)
                        radius: Theme.radiusSm
                        color: Theme.alpha(Theme.surface2, 0.6)
                        border.color: Theme.alpha(Theme.border, 0.4)
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.dp(6)
                            spacing: Theme.dp(2)

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Front Extent: +%1 u").arg(Number(root.currentProps.frontZ !== undefined ? root.currentProps.frontZ : 40).toFixed(0))
                                    font.pixelSize: Theme.dp(10)
                                    font.weight: Font.DemiBold
                                    color: "#06b6d4"
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("Back Extent: %1 u").arg(Number(root.currentProps.backZ !== undefined ? root.currentProps.backZ : -20).toFixed(0))
                                    font.pixelSize: Theme.dp(10)
                                    font.weight: Font.DemiBold
                                    color: "#06b6d4"
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("World Z Span: [%1 .. %2]").arg(Number(root.currentProps.worldBackZ !== undefined ? root.currentProps.worldBackZ : 0).toFixed(0)).arg(Number(root.currentProps.worldFrontZ !== undefined ? root.currentProps.worldFrontZ : 0).toFixed(0))
                                font.pixelSize: Theme.dp(10)
                                font.weight: Font.Medium
                                color: Theme.textSecondary
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

            // 3. Professional Spectrum Fluid Color Studio
            SectionCard {
                width: parent.width
                label: qsTr("Color Spectrum Studio")
                accentIcon: "palette"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    // Target Selector: Body Fluid vs Surface Wave Crest
                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(6)

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: root.activeColorTarget === "body" ? Theme.alpha("#06b6d4", 0.22) : Theme.surface2
                            border.color: root.activeColorTarget === "body" ? "#06b6d4" : Theme.borderSubtle
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)
                                Rectangle {
                                    width: Theme.dp(12); height: Theme.dp(12); radius: Theme.dp(6)
                                    color: Qt.rgba(Number(root.currentProps.color_r !== undefined ? root.currentProps.color_r : 0.2),
                                                   Number(root.currentProps.color_g !== undefined ? root.currentProps.color_g : 0.5),
                                                   Number(root.currentProps.color_b !== undefined ? root.currentProps.color_b : 0.85), 1.0)
                                    border.color: "#ffffff"; border.width: 1
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: qsTr("Deep Body Fluid")
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.weight: root.activeColorTarget === "body" ? Font.Bold : Font.Normal
                                    color: root.activeColorTarget === "body" ? "#06b6d4" : Theme.textPrimary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.activeColorTarget = "body"
                                    root.syncHsvFromCurrentProps()
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(15)
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: root.activeColorTarget === "surface" ? Theme.alpha("#06b6d4", 0.22) : Theme.surface2
                            border.color: root.activeColorTarget === "surface" ? "#06b6d4" : Theme.borderSubtle
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: Theme.dp(6)
                                Rectangle {
                                    width: Theme.dp(12); height: Theme.dp(12); radius: Theme.dp(6)
                                    color: Qt.rgba(Number(root.currentProps.surface_r !== undefined ? root.currentProps.surface_r : 0.4),
                                                   Number(root.currentProps.surface_g !== undefined ? root.currentProps.surface_g : 0.75),
                                                   Number(root.currentProps.surface_b !== undefined ? root.currentProps.surface_b : 1.0), 1.0)
                                    border.color: "#ffffff"; border.width: 1
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: qsTr("Surface Crest")
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    font.weight: root.activeColorTarget === "surface" ? Font.Bold : Font.Normal
                                    color: root.activeColorTarget === "surface" ? "#06b6d4" : Theme.textPrimary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.activeColorTarget = "surface"
                                    root.syncHsvFromCurrentProps()
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(15)
                                }
                            }
                        }
                    }

                    // Interactive Swatch & Hex Code Editor
                    RowLayout {
                        width: parent.width
                        spacing: Theme.dp(8)

                        Rectangle {
                            width: Theme.dp(48)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Qt.hsva(root.curH, root.curS, root.curV, root.curA)
                            border.color: Theme.borderSubtle
                            border.width: 1

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusSm
                                color: "transparent"
                                border.color: "#ffffff"
                                border.width: 1
                                opacity: 0.35
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.surface2
                            border.color: Theme.borderSubtle
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.dp(8)
                                anchors.rightMargin: Theme.dp(8)
                                spacing: Theme.dp(4)

                                Icon {
                                    name: "palette"
                                    size: Theme.dp(14)
                                    color: "#06b6d4"
                                }

                                TextInput {
                                    id: hexInput
                                    Layout.fillWidth: true
                                    text: root.rgbaToHex(root.hsvToRgb(root.curH, root.curS, root.curV).r,
                                                         root.hsvToRgb(root.curH, root.curS, root.curV).g,
                                                         root.hsvToRgb(root.curH, root.curS, root.curV).b)
                                    font.pixelSize: Theme.dp(Theme.fontSm)
                                    font.family: "Monospace"
                                    font.weight: Font.DemiBold
                                    color: Theme.textPrimary
                                    verticalAlignment: TextInput.AlignVCenter
                                    selectByMouse: true

                                    onEditingFinished: {
                                        var parsed = root.parseHex(text)
                                        if (parsed.valid) {
                                            var hsv = root.rgbToHsv(parsed.r, parsed.g, parsed.b)
                                            root.curH = hsv.h
                                            root.curS = hsv.s
                                            root.curV = hsv.v
                                            if (parsed.a >= 0) root.curA = parsed.a
                                            root.applyActiveHsv()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Continuous Hue Spectrum Bar
                    Column {
                        width: parent.width
                        spacing: Theme.dp(3)

                        RowLayout {
                            width: parent.width
                            FieldLabel { text: qsTr("Hue Spectrum") }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("%1°").arg(Math.round(root.curH * 360))
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: "#06b6d4"
                                font.weight: Font.Bold
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.dp(22)
                            radius: Theme.dp(11)
                            border.color: Theme.borderSubtle
                            border.width: 1

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.000; color: "#ff0000" }
                                GradientStop { position: 0.166; color: "#ffff00" }
                                GradientStop { position: 0.333; color: "#00ff00" }
                                GradientStop { position: 0.500; color: "#00ffff" }
                                GradientStop { position: 0.666; color: "#0000ff" }
                                GradientStop { position: 0.833; color: "#ff00ff" }
                                GradientStop { position: 1.000; color: "#ff0000" }
                            }

                            Rectangle {
                                width: Theme.dp(18); height: Theme.dp(18)
                                radius: Theme.dp(9)
                                color: Qt.hsva(root.curH, 1.0, 1.0, 1.0)
                                border.color: "#ffffff"; border.width: 2
                                anchors.verticalCenter: parent.verticalCenter
                                x: Math.max(0, Math.min(parent.width - width, root.curH * (parent.width - width)))
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                function updateHue(mouse) {
                                    var frac = Math.max(0.0, Math.min(1.0, (mouse.x - Theme.dp(9)) / (width - Theme.dp(18))))
                                    root.curH = frac
                                    root.applyActiveHsv()
                                }
                                onPressed: (mouse) => updateHue(mouse)
                                onPositionChanged: (mouse) => { if (pressed) updateHue(mouse) }
                                onReleased: {
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(10)
                                }
                            }
                        }
                    }

                    // Saturation Slider
                    Column {
                        width: parent.width
                        spacing: Theme.dp(3)

                        RowLayout {
                            width: parent.width
                            FieldLabel { text: qsTr("Saturation") }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("%1%").arg(Math.round(root.curS * 100))
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                                font.weight: Font.Medium
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.dp(22)
                            radius: Theme.dp(11)
                            border.color: Theme.borderSubtle
                            border.width: 1

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Qt.hsva(root.curH, 0.0, root.curV, 1.0) }
                                GradientStop { position: 1.0; color: Qt.hsva(root.curH, 1.0, root.curV, 1.0) }
                            }

                            Rectangle {
                                width: Theme.dp(18); height: Theme.dp(18)
                                radius: Theme.dp(9)
                                color: "#ffffff"
                                border.color: "#000000"; border.width: 1.5
                                anchors.verticalCenter: parent.verticalCenter
                                x: Math.max(0, Math.min(parent.width - width, root.curS * (parent.width - width)))
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                function updateSat(mouse) {
                                    var frac = Math.max(0.0, Math.min(1.0, (mouse.x - Theme.dp(9)) / (width - Theme.dp(18))))
                                    root.curS = frac
                                    root.applyActiveHsv()
                                }
                                onPressed: (mouse) => updateSat(mouse)
                                onPositionChanged: (mouse) => { if (pressed) updateSat(mouse) }
                                onReleased: {
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(10)
                                }
                            }
                        }
                    }

                    // Brightness / Value Slider
                    Column {
                        width: parent.width
                        spacing: Theme.dp(3)

                        RowLayout {
                            width: parent.width
                            FieldLabel { text: qsTr("Brightness") }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("%1%").arg(Math.round(root.curV * 100))
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                                font.weight: Font.Medium
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.dp(22)
                            radius: Theme.dp(11)
                            border.color: Theme.borderSubtle
                            border.width: 1

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#000000" }
                                GradientStop { position: 1.0; color: Qt.hsva(root.curH, root.curS, 1.0, 1.0) }
                            }

                            Rectangle {
                                width: Theme.dp(18); height: Theme.dp(18)
                                radius: Theme.dp(9)
                                color: "#ffffff"
                                border.color: "#000000"; border.width: 1.5
                                anchors.verticalCenter: parent.verticalCenter
                                x: Math.max(0, Math.min(parent.width - width, root.curV * (parent.width - width)))
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                function updateVal(mouse) {
                                    var frac = Math.max(0.0, Math.min(1.0, (mouse.x - Theme.dp(9)) / (width - Theme.dp(18))))
                                    root.curV = frac
                                    root.applyActiveHsv()
                                }
                                onPressed: (mouse) => updateVal(mouse)
                                onPositionChanged: (mouse) => { if (pressed) updateVal(mouse) }
                                onReleased: {
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(10)
                                }
                            }
                        }
                    }

                    // Opacity / Alpha Slider
                    Column {
                        width: parent.width
                        spacing: Theme.dp(3)

                        RowLayout {
                            width: parent.width
                            FieldLabel { text: qsTr("Fluid Opacity (Alpha)") }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: qsTr("%1%").arg(Math.round(root.curA * 100))
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                color: Theme.textMuted
                                font.weight: Font.Medium
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: Theme.dp(22)
                            radius: Theme.dp(11)
                            border.color: Theme.borderSubtle
                            border.width: 1

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Theme.alpha(Qt.hsva(root.curH, root.curS, root.curV, 1.0), 0.1) }
                                GradientStop { position: 1.0; color: Qt.hsva(root.curH, root.curS, root.curV, 1.0) }
                            }

                            Rectangle {
                                width: Theme.dp(18); height: Theme.dp(18)
                                radius: Theme.dp(9)
                                color: "#ffffff"
                                border.color: "#000000"; border.width: 1.5
                                anchors.verticalCenter: parent.verticalCenter
                                x: Math.max(0, Math.min(parent.width - width, root.curA * (parent.width - width)))
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                function updateAlpha(mouse) {
                                    var frac = Math.max(0.05, Math.min(1.0, (mouse.x - Theme.dp(9)) / (width - Theme.dp(18))))
                                    root.curA = frac
                                    root.applyActiveHsv()
                                }
                                onPressed: (mouse) => updateAlpha(mouse)
                                onPositionChanged: (mouse) => { if (pressed) updateAlpha(mouse) }
                                onReleased: {
                                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                                        screenOrientation.vibrateTouch(10)
                                }
                            }
                        }
                    }

                    // Curated Fluid Palette Swatches
                    Column {
                        width: parent.width
                        spacing: Theme.dp(6)

                        FieldLabel { text: qsTr("Curated Fluid Palette") }

                        Flow {
                            width: parent.width
                            spacing: Theme.dp(6)

                            Repeater {
                                model: root.fluidSwatches
                                delegate: Rectangle {
                                    width: (parent.width - Theme.dp(6)) / 2
                                    height: Theme.dp(34)
                                    radius: Theme.radiusSm
                                    color: Theme.surface2
                                    border.color: Theme.borderSubtle
                                    border.width: 1

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.dp(6)
                                        anchors.rightMargin: Theme.dp(6)
                                        spacing: Theme.dp(6)

                                        // Dual circle for body & crest
                                        Item {
                                            width: Theme.dp(18); height: Theme.dp(18)
                                            Rectangle {
                                                width: Theme.dp(14); height: Theme.dp(14); radius: Theme.dp(7)
                                                color: Qt.rgba(modelData.body[0], modelData.body[1], modelData.body[2], 1.0)
                                                anchors.left: parent.left
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Rectangle {
                                                width: Theme.dp(10); height: Theme.dp(10); radius: Theme.dp(5)
                                                color: Qt.rgba(modelData.crest[0], modelData.crest[1], modelData.crest[2], 1.0)
                                                border.color: "#ffffff"; border.width: 1
                                                anchors.right: parent.right
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            font.pixelSize: Theme.dp(Theme.fontXs)
                                            font.weight: Font.Medium
                                            color: Theme.textPrimary
                                            elide: Text.ElideRight
                                        }

                                        Icon {
                                            visible: Boolean(modelData.deadly)
                                            name: "alert"
                                            size: Theme.dp(11)
                                            color: "#ef4444"
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.applyFluidSwatch(modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 4. Hazard & Damage Pipeline (Lava / Lethal Fluid Mechanics)
            SectionCard {
                width: parent.width
                label: qsTr("Hazard & Damage Pipeline")
                accentIcon: "alert"

                Column {
                    width: parent.width
                    spacing: Theme.dp(Theme.spacingSm)

                    SettingRow {
                        title: qsTr("Deadly Fluid (Lava / Acid)")
                        description: qsTr("Inflicts contact damage via Swordigo's collision hazard matrix")
                        leadingIcon: "alert"
                        hasSwitch: true
                        checked: Boolean(root.currentProps.isDeadly)
                        onToggled: (checked) => {
                            root.setHazardProperty("isDeadly", checked)
                        }
                    }

                    // Expanded Hazard Details
                    Column {
                        width: parent.width
                        spacing: Theme.dp(Theme.spacingSm)
                        visible: Boolean(root.currentProps.isDeadly)

                        // Base Damage Stepper
                        ColumnLayout {
                            width: parent.width
                            spacing: Theme.dp(2)

                            FieldLabel { text: qsTr("Base Damage (per impact)") }
                            RowLayout {
                                width: parent.width
                                spacing: Theme.dp(8)

                                IconButton {
                                    iconName: "minus"
                                    buttonSize: Theme.dp(32)
                                    iconSize: Theme.dp(14)
                                    onClicked: {
                                        var cur = Number(root.currentProps.minDamage || 4)
                                        var updated = Math.max(1, cur - 1)
                                        root.setHazardProperties({ "minDamage": updated, "maxDamage": updated })
                                    }
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
                                             name: "bolt"
                                             size: Theme.dp(12)
                                             color: "#ef4444"
                                             anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            text: qsTr("%1 Hearts").arg(Number(root.currentProps.minDamage || 4).toFixed(0))
                                            font.pixelSize: Theme.dp(Theme.fontSm)
                                            font.weight: Font.Bold
                                            color: "#ef4444"
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }

                                IconButton {
                                    iconName: "plus"
                                    buttonSize: Theme.dp(32)
                                    iconSize: Theme.dp(14)
                                    onClicked: {
                                        var cur = Number(root.currentProps.minDamage || 4)
                                        var updated = Math.min(20, cur + 1)
                                        root.setHazardProperties({ "minDamage": updated, "maxDamage": updated })
                                    }
                                }
                            }
                        }

                        // Damage Type Segmented Picker
                        ColumnLayout {
                            width: parent.width
                            spacing: Theme.dp(3)

                            FieldLabel { text: qsTr("Damage Element") }

                            RowLayout {
                                width: parent.width
                                spacing: Theme.dp(6)

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: Theme.dp(32)
                                    radius: Theme.radiusSm
                                    color: Number(root.currentProps.damageType || 0) === 0 ? Theme.alpha("#ef4444", 0.20) : Theme.surface2
                                    border.color: Number(root.currentProps.damageType || 0) === 0 ? "#ef4444" : Theme.borderSubtle
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: qsTr("Physical / Heat")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        font.weight: Number(root.currentProps.damageType || 0) === 0 ? Font.Bold : Font.Normal
                                        color: Number(root.currentProps.damageType || 0) === 0 ? "#ef4444" : Theme.textPrimary
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.setHazardProperty("damageType", 0)
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: Theme.dp(32)
                                    radius: Theme.radiusSm
                                    color: Number(root.currentProps.damageType || 0) === 1 ? Theme.alpha("#a855f7", 0.20) : Theme.surface2
                                    border.color: Number(root.currentProps.damageType || 0) === 1 ? "#a855f7" : Theme.borderSubtle
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: qsTr("Magical / Cursed")
                                        font.pixelSize: Theme.dp(Theme.fontXs)
                                        font.weight: Number(root.currentProps.damageType || 0) === 1 ? Font.Bold : Font.Normal
                                        color: Number(root.currentProps.damageType || 0) === 1 ? "#a855f7" : Theme.textPrimary
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.setHazardProperty("damageType", 1)
                                    }
                                }
                            }
                        }

                        // Penetrate Invulnerability Switch
                        SettingRow {
                            title: qsTr("Penetrate Invulnerability")
                            description: qsTr("Inflicts continuous damage bypassing hero i-frames")
                            hasSwitch: true
                            checked: Boolean(root.currentProps.ignoreImmunity !== false)
                            onToggled: (checked) => {
                                root.setHazardProperty("ignoreImmunity", checked)
                            }
                        }

                        // Engine Pipeline Note
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(34)
                            radius: Theme.radiusSm
                            color: Theme.alpha("#ef4444", 0.08)
                            border.color: Theme.alpha("#ef4444", 0.20)
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.dp(8)
                                anchors.rightMargin: Theme.dp(8)
                                spacing: Theme.dp(6)

                                Icon {
                                    name: "info"
                                    size: Theme.dp(12)
                                    color: "#ef4444"
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Wires into CollisionShape (121) + DamageComponent (122)")
                                    font.pixelSize: Theme.dp(Theme.fontXs)
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
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
        var current = Number(p[prop] !== undefined ? p[prop] : 0)
        var updated = current + delta
        if (prop === "width" && updated < 20) updated = 20
        if (prop === "height" && updated < 20) updated = 20
        p[prop] = updated
        if (prop === "posZ" || prop === "z" || prop === "depth") {
            p["posZ"] = updated
            p["z"] = updated
            p["depth"] = updated
        }
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
