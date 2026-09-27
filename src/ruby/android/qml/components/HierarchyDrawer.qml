import QtQuick
import QtQuick.Controls
import ".."

// ============================================================================
// HierarchyDrawer.qml — QML port of MobileHierarchyDrawer.
//
// HOST CONTRACT (all optional-safe, checked before use):
//   host.objectsSnapshot()                -> [{index, name, subtitle, hidden}]
//   host.selectObject(int)
//   host.toggleObjectVisibility(int)
//   host.focusObject(int)
//   host.deleteObject(int)
//   host.duplicateObject(int)
//   host.addObject()
//   host signals: sceneLoaded, selectedObjectChanged, objectListChanged
//   RubyQuickViewport satisfies all of the above.
//
// Behaviour preserved from the QWidget version:
//   - fixed 300dp off-canvas drawer over the 3D viewport
//   - live, case-insensitive "contains" filter over BOTH the object name and
//     its subtitle
//   - subtitle falls back template_name -> mesh_name
//   - row text is "<scene index>. <name>  [<subtitle>]" — the index shown is
//     the ORIGINAL scene index, not the filtered row number
//   - hidden objects render muted
//   - action row Focus / Duplicate / Delete / + Add, selection-gated except Add
//   - selectObject(i) re-syncs internal selection, list selection and scroll
//
// ADDED (was missing): the per-row visibility toggle. The legacy header
// declared objectVisibilityToggled(int,bool) and included <QCheckBox>, but no
// code path ever emitted it and no widget ever offered it — see the sync
// notes. The QML port wires it to host.toggleObjectVisibility().
//
// LAYOUT
//   Three anchored regions — a fixed header block, a tree that absorbs all the
//   leftover height, and a fixed action bar. The tree used to be sized as
//   `parent.height - 160`, a portrait-derived magic number that in landscape
//   (412dp tall) left the outliner showing about five rows. Anchoring instead
//   means the scene tree gets every pixel the chrome does not need.
// ============================================================================
Item {
    id: root

    property var host: null
    property int drawerWidth: Theme.dp(300)
    property string filterText: ""
    property bool open: false
    property int selectedIndex: -1

    /// Full snapshot of the scene objects.
    property var rows: []
    /// rows after the search filter is applied.
    property var visibleRows: []

    signal objectSelected(int index)
    signal objectVisibilityToggled(int index, bool visible)
    signal objectFocusRequested(int index)
    signal objectDeleteRequested(int index)
    signal objectDuplicateRequested(int index)
    signal addObjectRequested()
    signal closeRequested()

    width: drawerWidth
    height: parent ? parent.height : 0

    Timer {
        id: filterDebounceTimer
        interval: 150
        repeat: false
        onTriggered: root.applyFilter()
    }

    // ── Data ──────────────────────────────────────────────────────────────
    function refresh() {
        root.rows = root.host ? root.host.objectsSnapshot() : []
        root.applyFilter()
        root.syncSelection()
    }

    function applyFilter() {
        var q = root.filterText ? root.filterText.trim().toLowerCase() : ""
        if (q.length === 0) {
            root.visibleRows = root.rows || []
            return
        }
        var src = root.rows || []
        var out = []
        for (var i = 0; i < src.length; ++i) {
            var r = src[i]
            if (!r) continue
            var name = (r.name || "").toLowerCase()
            var sub = (r.subtitle || "").toLowerCase()
            if (name.indexOf(q) >= 0 || sub.indexOf(q) >= 0) {
                out.push(r)
            }
        }
        root.visibleRows = out
    }

    /// Re-syncs the internal selection index and the list's visible row +
    /// scroll position after the filter has changed.
    function selectObject(index) {
        root.selectedIndex = index
        root.syncSelection()
    }

    function syncSelection() {
        if (root.selectedIndex < 0 || !root.visibleRows) {
            listView.currentIndex = -1
            return
        }
        for (var i = 0; i < root.visibleRows.length; ++i) {
            var row = root.visibleRows[i]
            if (row && row.index === root.selectedIndex) {
                listView.currentIndex = i
                var targetIdx = i
                Qt.callLater(function() {
                    if (listView && targetIdx >= 0 && targetIdx < listView.count) {
                        listView.positionViewAtIndex(targetIdx, ListView.Contain)
                    }
                })
                return
            }
        }
        listView.currentIndex = -1
    }

    onVisibleRowsChanged: syncSelection()

    Component.onCompleted: refresh()

    Connections {
        target: root.host
        enabled: root.host !== null

        function onSceneLoaded() { root.refresh() }
        function onObjectListChanged() { root.refresh() }
        function onSelectedObjectChanged() {
            if (root.host) root.selectObject(root.host.selectedObject)
        }
    }

    // ── Presentation ──────────────────────────────────────────────────────
    Rectangle {
        id: panel
        anchors.fill: parent
        color: Theme.surfaceGlass
        border.width: 0

        // Left accent edge, mirroring the legacy paintEvent accent line
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            color: Theme.alpha(Theme.accentInk, 0.35)
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.border
        }

        // ── Region 1: header + filter (fixed height) ──────────────────────
        Column {
            id: topBlock
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: Theme.dp(Theme.spacingMd)
            anchors.bottomMargin: 0
            anchors.leftMargin: Theme.dp(Theme.spacingMd)
            anchors.rightMargin: Theme.dp(Theme.spacingMd)
            spacing: Theme.dp(Theme.spacingMd)

            Row {
                id: headerRow
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "layers"
                    size: Theme.dp(Theme.iconSm)
                    color: Theme.accentInk
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.dp(76)
                    spacing: 0

                    Text {
                        text: qsTr("Scene Hierarchy")
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                    }

                    Text {
                        text: root.rows.length === 1 ? qsTr("1 object")
                                                     : qsTr("%1 objects").arg(root.rows.length)
                        font.pixelSize: Theme.dp(Theme.fontXs)
                        color: Theme.textMuted
                    }
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    iconSize: Theme.dp(16)
                    buttonSize: Theme.dp(32)
                    onClicked: root.closeRequested()
                }
            }

            Rectangle {
                id: searchBox
                width: parent.width
                height: Theme.dp(38)
                radius: Theme.radiusSm
                color: Theme.surface2
                border.color: filterField.activeFocus ? Theme.borderFocus : Theme.borderStrong
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                    anchors.rightMargin: Theme.dp(Theme.spacingSm)
                    spacing: Theme.dp(Theme.spacingSm)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        size: Theme.dp(Theme.iconSm)
                        color: Theme.textMuted
                    }

                    TextInput {
                        id: filterField
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(18) - Theme.dp(Theme.spacingSm)
                        color: Theme.textPrimary
                        font.pixelSize: Theme.dp(Theme.fontMd)
                        clip: true
                        selectByMouse: true

                        Text {
                            text: qsTr("Filter objects…")
                            color: Theme.textMuted
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            visible: !filterField.text && !filterField.activeFocus
                        }

                        onTextChanged: {
                            root.filterText = text
                            if (text.trim().length === 0) {
                                filterDebounceTimer.stop()
                                root.applyFilter()
                            } else {
                                filterDebounceTimer.restart()
                            }
                        }
                    }
                }
            }
        }

        // ── Region 2: the tree (absorbs all leftover height) ──────────────
        ListView {
            id: listView
            anchors.top: topBlock.bottom
            anchors.topMargin: Theme.dp(Theme.spacingMd)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: actionRow.top
            anchors.bottomMargin: Theme.dp(Theme.spacingMd)
            anchors.leftMargin: Theme.dp(Theme.spacingMd)
            anchors.rightMargin: Theme.dp(Theme.spacingMd)
            clip: true
            model: root.visibleRows
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.dp(2)

            delegate: Rectangle {
                id: rowItem
                width: listView.width
                height: Theme.dp(48)
                radius: Theme.radiusSm
                color: rowArea.pressed ? Theme.surface2
                                       : ((modelData && root.selectedIndex === modelData.index)
                                          ? Theme.alpha(Theme.accentInk, 0.16)
                                          : "transparent")
                border.color: (modelData && root.selectedIndex === modelData.index)
                              ? Theme.alpha(Theme.accentInk, 0.40) : "transparent"
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.dp(Theme.spacingMd)
                    anchors.rightMargin: Theme.dp(Theme.spacingSm)
                    spacing: Theme.dp(Theme.spacingSm)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.dp(24)
                        text: (modelData && modelData.index !== undefined ? modelData.index : "") + "."
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.family: Theme.monoFamily
                        color: Theme.textMuted
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - Theme.dp(24) - Theme.dp(34) - Theme.dp(Theme.spacingSm) * 2
                        spacing: Theme.dp(1)

                        Text {
                            width: parent.width
                            text: modelData ? (modelData.name || "") : ""
                            font.pixelSize: Theme.dp(Theme.fontMd)
                            font.weight: (modelData && root.selectedIndex === modelData.index) ? Font.DemiBold : Font.Medium
                            color: (modelData && modelData.hidden) ? Theme.textMuted : Theme.textPrimary
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            visible: Boolean(modelData && modelData.subtitle && modelData.subtitle.length > 0)
                            text: (modelData && modelData.subtitle) ? ("[" + modelData.subtitle + "]") : ""
                            font.pixelSize: Theme.dp(Theme.fontXs)
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }

                    // Visibility toggle.
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: (modelData && modelData.hidden) ? "eye-off" : "eye"
                        size: Theme.dp(Theme.iconSm)
                        color: (modelData && modelData.hidden) ? Theme.textDisabled : Theme.accentInk

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.dp(10)
                            onClicked: {
                                if (modelData && modelData.index !== undefined) {
                                    root.objectVisibilityToggled(modelData.index, !modelData.hidden)
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    anchors.rightMargin: Theme.dp(40) // keep the eye hit area usable
                    onClicked: {
                        if (modelData && modelData.index !== undefined) {
                            root.selectObject(modelData.index)
                            root.objectSelected(modelData.index)
                        }
                    }
                }
            }
        }

        // Empty state, overlaid on the tree rather than taking a row from it.
        Text {
            anchors.centerIn: listView
            width: Math.min(listView.width - Theme.dp(Theme.spacingLg) * 2, Theme.dp(240))
            visible: root.visibleRows.length === 0
            text: root.rows.length === 0
                  ? qsTr("No scene loaded")
                  : qsTr("No objects match the filter")
            font.pixelSize: Theme.dp(Theme.fontSm)
            color: Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        // ── Region 3: action bar (fixed height) ───────────────────────────
        Row {
            id: actionRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.dp(Theme.spacingMd)
            anchors.rightMargin: Theme.dp(Theme.spacingMd)
            anchors.bottomMargin: Theme.dp(Theme.spacingMd)
            height: Theme.dp(40)
            spacing: Theme.dp(Theme.spacingSm)

            readonly property bool hasSelection: root.selectedIndex >= 0

            Rectangle {
                width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                height: parent.height
                radius: Theme.radiusSm
                color: focusArea.pressed ? Theme.accentStart : Theme.accentDeep
                opacity: parent.hasSelection ? 1.0 : 0.4

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "target"; size: Theme.dp(Theme.iconSm); color: Theme.onAccent }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Focus"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.onAccent; font.weight: Font.DemiBold }
                }

                MouseArea {
                    id: focusArea
                    anchors.fill: parent
                    enabled: actionRow.hasSelection
                    onClicked: root.objectFocusRequested(root.selectedIndex)
                }
            }

            Rectangle {
                width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                height: parent.height
                radius: Theme.radiusSm
                color: dupArea.pressed ? Theme.surface2 : "transparent"
                border.color: Theme.borderStrong
                border.width: 1
                opacity: parent.hasSelection ? 1.0 : 0.4

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "copy"; size: Theme.dp(Theme.iconSm); color: Theme.textSecondary }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Copy"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.textSecondary; font.weight: Font.DemiBold }
                }

                MouseArea {
                    id: dupArea
                    anchors.fill: parent
                    enabled: actionRow.hasSelection
                    onClicked: root.objectDuplicateRequested(root.selectedIndex)
                }
            }

            Rectangle {
                width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                height: parent.height
                radius: Theme.radiusSm
                color: delArea.pressed ? Theme.colorError : Theme.alpha(Theme.colorError, 0.28)
                border.color: Theme.alpha(Theme.colorErrorBright, 0.35)
                border.width: 1
                opacity: parent.hasSelection ? 1.0 : 0.4

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "trash"; size: Theme.dp(Theme.iconSm); color: Theme.colorErrorBright }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Delete"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.colorErrorBright; font.weight: Font.DemiBold }
                }

                MouseArea {
                    id: delArea
                    anchors.fill: parent
                    enabled: actionRow.hasSelection
                    onClicked: root.objectDeleteRequested(root.selectedIndex)
                }
            }

            Rectangle {
                width: (parent.width - Theme.dp(Theme.spacingSm) * 3) / 4
                height: parent.height
                radius: Theme.radiusSm
                color: addArea.pressed ? Theme.colorSuccess : Theme.colorSuccessDeep
                border.color: Theme.alpha(Theme.colorSuccessBright, 0.35)
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: Theme.dp(Theme.iconSm); color: Theme.colorSuccessBright }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Add"); font.pixelSize: Theme.dp(Theme.fontXs); color: Theme.colorSuccessBright; font.weight: Font.DemiBold }
                }

                MouseArea {
                    id: addArea
                    anchors.fill: parent
                    onClicked: root.addObjectRequested()
                }
            }
        }
    }
}
