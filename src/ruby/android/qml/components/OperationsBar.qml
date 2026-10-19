import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// OperationsBar.qml — floating contextual selection dock for Ruby Touch.
//
// The compact bar is a single RowLayout: close, an expanding elided selection
// label, then icon-only actions. Because the label is the only child with
// Layout.fillWidth, the "N selected · size" text can never run underneath the
// action buttons. The expanded drawer keeps select-all/invert controls and a
// horizontal carousel of the selected items.
Rectangle {
    id: root

    property bool active: selectedCount > 0 || isBusy
    property int selectedCount: 0
    property string targetPath: ""
    property string targetFileName: ""
    property string totalSizeStr: ""
    property bool isBusy: false
    property string statusText: ""
    property bool isExpanded: false

    signal closeRequested()
    signal zipRequested(string path)
    signal copyRequested(string path)
    signal cutRequested(string path)
    signal deleteRequested(string path)
    signal selectAllRequested()
    signal clearSelectionRequested()
    signal invertSelectionRequested()
    signal shareRequested(string path)
    signal sanitizeAudioRequested()
    signal removeSelectedRequested(string path)

    function hasSize(value) {
        return value !== undefined && value !== null && value.length > 0
               && value !== "0 B" && value !== "0.0 B"
    }

    readonly property string selectionLabel: {
        if (root.isBusy) {
            return root.statusText.length > 0 ? root.statusText : qsTr("Working…")
        }
        var text = qsTr("%1 selected").arg(root.selectedCount)
        if (root.hasSize(root.totalSizeStr)) {
            text += "  ·  " + root.totalSizeStr
        }
        return text
    }

    function triggerAction(action) {
        if (action === "copy") root.copyRequested(root.targetPath)
        else if (action === "cut") root.cutRequested(root.targetPath)
        else if (action === "zip") root.zipRequested(root.targetPath)
        else if (action === "share") root.shareRequested(root.targetPath)
        else if (action === "delete") root.deleteRequested(root.targetPath)
    }

    height: !active ? 0 : (isExpanded ? Math.min(Theme.dp(236), parent.height * 0.48) : Theme.dp(56))
    visible: height > 0
    color: Theme.surface2
    border.color: Theme.borderSubtle
    border.width: 1
    radius: isExpanded ? Theme.radiusLg : height / 2
    clip: true

    Behavior on height {
        NumberAnimation {
            duration: Theme.durMed
            easing.type: Easing.OutCubic
        }
    }

    onActiveChanged: {
        if (!active) {
            isExpanded = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.dp(Theme.spacingSm)
        spacing: Theme.dp(Theme.spacingSm)
        visible: root.active

        // ── COMPACT DOCK: close | selection label | actions ────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.dp(40)
            spacing: Theme.dp(4)

            IconButton {
                Layout.preferredWidth: Theme.dp(34)
                Layout.preferredHeight: Theme.dp(34)
                iconName: "close"
                tint: Theme.textSecondary
                plate: "transparent"
                plateEdge: "transparent"
                buttonSize: Theme.dp(34)
                iconSize: Theme.dp(16)
                onClicked: {
                    root.isExpanded = false
                    root.closeRequested()
                }
            }

            // The only flexible child in the row. Everything to its right has
            // a fixed intrinsic width, so the text elides instead of colliding.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(36)
                radius: Theme.radiusSm
                color: labelMouse.pressed ? Theme.alpha(Theme.accentInk, 0.10) : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(4)
                    anchors.rightMargin: Theme.dp(2)
                    spacing: Theme.dp(6)

                    Icon {
                        Layout.alignment: Qt.AlignVCenter
                        name: "check-square"
                        size: Theme.dp(14)
                        color: Theme.accentInk
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.selectionLabel
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Icon {
                        Layout.alignment: Qt.AlignVCenter
                        visible: !root.isBusy && root.selectedCount > 0
                        name: root.isExpanded ? "chevron-down" : "chevron-up"
                        size: Theme.dp(12)
                        color: Theme.accentInk
                    }
                }

                MouseArea {
                    id: labelMouse
                    anchors.fill: parent
                    enabled: !root.isBusy && root.selectedCount > 0
                    onClicked: root.isExpanded = !root.isExpanded
                }
            }

            // Collapsed: primary contextual actions (icon only, no boxes).
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
                visible: !root.isExpanded

                IconButton {
                    iconName: "share"
                    tint: Theme.textPrimary
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    onClicked: root.shareRequested(root.targetPath)
                }

                IconButton {
                    iconName: "archive"
                    tint: Theme.colorWarning
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    onClicked: root.zipRequested(root.targetPath)
                }

                IconButton {
                    iconName: "copy"
                    tint: Theme.textPrimary
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    onClicked: root.copyRequested(root.targetPath)
                }

                IconButton {
                    iconName: "cut"
                    tint: Theme.textPrimary
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    onClicked: root.cutRequested(root.targetPath)
                }

                IconButton {
                    iconName: "trash"
                    tint: Theme.colorErrorBright
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(34)
                    iconSize: Theme.dp(16)
                    onClicked: root.deleteRequested(root.targetPath)
                }
            }

            // Expanded: batch selection controls.
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.dp(4)
                visible: root.isExpanded

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.dp(30)
                    width: allText.implicitWidth + Theme.dp(16)
                    radius: Theme.radiusSm
                    color: allMouse.pressed ? Theme.surface3 : Theme.surface1
                    border.width: 0

                    Text {
                        id: allText
                        anchors.centerIn: parent
                        text: qsTr("Select all")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: allMouse
                        anchors.fill: parent
                        onClicked: root.selectAllRequested()
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    height: Theme.dp(30)
                    width: invText.implicitWidth + Theme.dp(16)
                    radius: Theme.radiusSm
                    color: invMouse.pressed ? Theme.surface3 : Theme.surface1
                    border.width: 0

                    Text {
                        id: invText
                        anchors.centerIn: parent
                        text: qsTr("Invert")
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    MouseArea {
                        id: invMouse
                        anchors.fill: parent
                        onClicked: root.invertSelectionRequested()
                    }
                }

                IconButton {
                    iconName: "chevron-down"
                    tint: Theme.accentInk
                    plate: "transparent"
                    plateEdge: "transparent"
                    buttonSize: Theme.dp(32)
                    iconSize: Theme.dp(16)
                    onClicked: root.isExpanded = false
                }
            }
        }

        // ── EXPANDED DRAWER: horizontal carousel of selected files ─────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.dp(120)
            visible: root.isExpanded
            clip: true

            ListView {
                id: selectedCarousel
                anchors.fill: parent
                orientation: ListView.Horizontal
                spacing: Theme.dp(8)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                model: (root.isExpanded && typeof rubyFileModel !== "undefined" && rubyFileModel)
                       ? rubyFileModel.getSelectedItems()
                       : []

                delegate: Rectangle {
                    width: Theme.dp(130)
                    height: Theme.dp(112)
                    radius: Theme.radiusMd
                    color: Theme.surface1
                    border.width: 0

                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Theme.dp(4)
                        width: Theme.dp(22)
                        height: Theme.dp(22)
                        radius: Theme.dp(11)
                        color: remMouse.pressed ? Theme.colorErrorBright : Theme.surface2
                        z: 2

                        Icon {
                            anchors.centerIn: parent
                            name: "close"
                            size: Theme.dp(11)
                            color: remMouse.pressed ? Theme.onAccent : Theme.textMuted
                        }

                        MouseArea {
                            id: remMouse
                            anchors.fill: parent
                            onClicked: {
                                if (modelData && modelData.path) {
                                    root.removeSelectedRequested(modelData.path)
                                }
                            }
                        }
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.dp(8)
                        spacing: Theme.dp(4)

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Theme.dp(36)
                            height: Theme.dp(36)
                            radius: Theme.radiusSm
                            color: Theme.alpha(Theme.colorForType(modelData.fileType), 0.16)

                            Icon {
                                anchors.centerIn: parent
                                name: modelData.isDir ? "folder" : Theme.iconForType(modelData.fileType)
                                size: Theme.dp(18)
                                color: Theme.colorForType(modelData.fileType)
                            }
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.name || ""
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                            elide: Text.ElideMiddle
                            maximumLineCount: 2
                            wrapMode: Text.WrapAnywhere
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.isDir ? qsTr("Folder") : (modelData.sizeStr || "")
                            font.pixelSize: Theme.dp(Theme.fontXs - 1)
                            color: Theme.textMuted
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: selectedCarousel.count === 0
                    text: qsTr("No items selected")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textMuted
                }
            }
        }

        // ── EXPANDED DRAWER: borderless action strip ───────────────────────
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.dp(44)
            visible: root.isExpanded

            RowLayout {
                anchors.centerIn: parent
                spacing: Theme.dp(6)

                Repeater {
                    model: [
                        { label: qsTr("Copy"),   icon: "copy",    tint: Theme.textPrimary,       action: "copy"   },
                        { label: qsTr("Cut"),    icon: "cut",     tint: Theme.textPrimary,       action: "cut"    },
                        { label: qsTr("Zip"),    icon: "archive", tint: Theme.colorWarning,      action: "zip"    },
                        { label: qsTr("Share"),  icon: "share",   tint: Theme.accentInk,         action: "share"  },
                        { label: qsTr("Delete"), icon: "trash",   tint: Theme.colorErrorBright,  action: "delete" }
                    ]

                    delegate: Rectangle {
                        Layout.preferredHeight: Theme.dp(36)
                        Layout.preferredWidth: actionContent.implicitWidth + Theme.dp(24)
                        radius: Theme.radiusSm
                        color: actionMouse.pressed ? Theme.surface3 : Theme.surface1
                        border.width: 0

                        Row {
                            id: actionContent
                            anchors.centerIn: parent
                            spacing: Theme.dp(5)

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.icon
                                size: Theme.dp(14)
                                color: modelData.tint
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font.pixelSize: Theme.dp(Theme.fontXs)
                                font.weight: Font.DemiBold
                                color: modelData.tint
                            }
                        }

                        MouseArea {
                            id: actionMouse
                            anchors.fill: parent
                            onClicked: root.triggerAction(modelData.action)
                        }
                    }
                }
            }
        }
    }
}
