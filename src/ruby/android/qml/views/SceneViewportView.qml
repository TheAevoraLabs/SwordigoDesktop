import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../components"
import Ruby 1.0

// ============================================================================
// SceneViewportView.qml — the LANDSCAPE-ONLY scene editor studio.
//
// WHY LANDSCAPE ONLY
//   The studio is built around four simultaneous touch surfaces: a camera-move
//   pad under the left thumb, a gizmo pad in the middle, a camera-fly pad under
//   the right thumb, plus a full-height outliner and a full-height inspector.
//   None of that fits a portrait phone without every surface becoming too small
//   to hit accurately, so the editor asks SceneOrientation to hold the activity
//   in landscape and, if that cannot be honoured (desktop dev build, or the user
//   has rotation locked), it shows OrientationGate rather than laying itself out
//   wrong. The studio stays mounted underneath, so rotating back is instant and
//   loses no state.
//
// LAYOUT — the viewport is the base layer and is NEVER squeezed
//   ┌─────────────────────────────────────────────────────────────┐
//   │ [←  scene.scene  24 objects •]        [↶ ↷ ▤ ⚙ ⋮]           │  ← two glass HUD pills
//   │                                                             │
//   │            RubyQuickViewport  (full bleed)                  │
//   │                                                             │
//   │ ┌───────────────────────── console ──────────────────────┐  │
//   │ │ ( MOVE PAD )   [ gizmo 3x3 + mode dots ]   ( FLY PAD ) │  │  ← one thumb-reachable deck
//   │ └────────────────────────────────────────────────────────┘  │
//   └─────────────────────────────────────────────────────────────┘
//   The outliner (left) and inspector (right) slide in ON TOP of the viewport as
//   full-height docks, so the 3D scene keeps rendering at full size behind them
//   and nothing has to be torn down or re-laid-out when they open.
//
//        ┌──────────┬───────────────────────┬──────────────┐
//        │ outliner │      viewport         │  inspector   │
//        │  (tree)  │   (always full-bleed) │ (full height)│
//        └──────────┴───────────────────────┴──────────────┘
//
// SIGNAL CONTRACT (unchanged from the portrait build):
//   backRequested()
// ============================================================================
Item {
    id: root

    property string scenePath: ""
    property string sceneName: ""
    property bool isModel: false
    property bool outlinerOpen: false
    property bool inspectorOpen: false
    /// Set by the inspector's maximise button; widens the dock to show the
    /// number pad without ever covering the whole viewport.
    property bool inspectorMaximized: false
    /// Collapses the console to a thin strip so the whole screen becomes 3D.
    property bool consoleCollapsed: true
    /// Master controls visibility toggle (hide/show all floating HUD and buttons)
    property bool controlsVisible: true
    /// WaterMesh Fluid Studio open state
    property bool waterStudioOpen: false

    signal backRequested()
    signal openScriptRequested(string path)

    // ── Geometry ───────────────────────────────────────────────────────────
    // The legacy QWidget pad was setFixedSize(160, 160) in raw PIXELS, which on
    // a 2.6x phone density is only ~61dp — far below the 48dp minimum touch
    // target once its dead zone is subtracted. The studio sizes it in design
    // units instead and lets NavPad's internal `unit` scale its geometry, so
    // the touch math stays identical while the pad becomes thumb-sized.
    readonly property real padSize: Theme.dp(68)
    readonly property real consoleHeight: Theme.dp(98)

    property bool forceBypassOrientation: true

    /// Landscape (or at least wide enough) to host the four-surface deck.
    readonly property bool studioFits: forceBypassOrientation
                                       || root.width > root.height
                                       || (typeof screenOrientation !== "undefined" && screenOrientation && screenOrientation.isLandscape)

    readonly property var gizmoModes: [
        { name: qsTr("View"),   mode: 0, icon: "eye"    },
        { name: qsTr("Move"),   mode: 1, icon: "move"   },
        { name: qsTr("Rotate"), mode: 2, icon: "rotate" },
        { name: qsTr("Scale"),  mode: 3, icon: "scale"  }
    ]

    property var savedCameraState: null
    property bool viewportLoading: true
    property string lastLoadedPath: ""
    readonly property bool inAnyMeshEdit: (typeof viewportItem !== "undefined" && viewportItem)
                                          ? (viewportItem.meshEditActive || viewportItem.waterEditActive)
                                          : false

    Timer {
        id: loadingDismissTimer
        interval: 320
        onTriggered: root.viewportLoading = false
    }

    function startLoading(status) {
        root.viewportLoading = true
        if (status) loadingOverlay.statusText = status
        loadingDismissTimer.restart()
    }

    function requestExit() {
        if (viewportItem.waterEditActive) {
            viewportItem.discardWaterMeshEdit()
            return true
        }
        if (viewportItem.meshEditActive) {
            viewportItem.discardMeshEdit()
            return true
        }
        if (viewportItem.dirty) {
            unsavedExitDialog.open()
            return true
        }
        doExit()
        return true
    }

    function doExit() {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockPortrait()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockPortrait()
        root.backRequested()
    }

    function handleBack() {
        return requestExit()
    }

    function proceedToCodeEditor() {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockPortrait()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockPortrait()
        root.openScriptRequested(root.scenePath)
    }

    function loadCurrentPath() {
        if (scenePath.length > 0 && scenePath !== lastLoadedPath) {
            lastLoadedPath = scenePath
            root.startLoading(qsTr("Loading 3D Geometry..."))
            var parts = scenePath.split("/")
            root.sceneName = parts[parts.length - 1]
            var low = scenePath.toLowerCase()
            if (root.isModel || low.endsWith(".pod") || low.endsWith(".glb") || low.endsWith(".obj") || low.endsWith(".fbx")) {
                root.isModel = true
                viewportItem.loadModel(scenePath)
            } else {
                root.isModel = false
                viewportItem.loadSceneAsync(scenePath)
            }
            if (root.savedCameraState) {
                viewportItem.setCameraState(root.savedCameraState)
            }
        }
    }

    // ── Orientation policy ─────────────────────────────────────────────────
    Component.onCompleted: {
        root.viewportLoading = true
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockLandscape()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockLandscape()
        loadCurrentPath()
    }

    // Hand the app's normal portrait policy back when the editor is popped, so
    // the rest of the (portrait) app is unaffected.
    Component.onDestruction: {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockPortrait()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockPortrait()
    }

    StackView.onActivated: {
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.lockLandscape()
        if (typeof androidContext !== "undefined" && androidContext)
            androidContext.lockLandscape()
        loadCurrentPath()
    }

    onScenePathChanged: loadCurrentPath()

    // ── Transient toast for save results ──────────────────────────────────
    property string toastText: ""
    property bool toastIsError: false

    function showToast(message, isError) {
        root.toastText = message
        root.toastIsError = isError === true
        toastTimer.restart()
    }

    Timer {
        id: toastTimer
        interval: 2600
        onTriggered: root.toastText = ""
    }

    // ── 3D viewport — the base layer ───────────────────────────────────────
    RubyQuickViewport {
        id: viewportItem
        anchors.fill: parent
        holdToSelect: (typeof rubySettings !== "undefined" && rubySettings) ? rubySettings.holdToSelect : false

        onSceneLoaded: loadingDismissTimer.restart()
        onModelLoaded: loadingDismissTimer.restart()

        // ── Touch selection (desktop single-click parity) ───────────────────
        // Selection and framing are resolved in C++ straight from the raw touch
        // stream (RubyQuickViewport::touchEvent): a tap picks the object under
        // the finger exactly like a desktop left click, a tap on empty space
        // deselects, and a second tap within the double-tap window frames the
        // object. There is deliberately no QML TapHandler here any more — it
        // used to swallow the tap and force selection onto a 0.45 s long-press,
        // which is most of why selecting anything felt unreliable.
        onObjectTapped: (index, doubleTap) => {
            if (index >= 0) {
                var objData = viewportItem.getSelectedObjectData()
                var name = objData.name || objData.template || qsTr("Object #%1").arg(index)
                if (typeof screenOrientation !== "undefined" && screenOrientation)
                    screenOrientation.vibrateTouch(doubleTap ? 30 : 20)
                root.showToast(doubleTap
                               ? qsTr("Framed %1").arg(name)
                               : qsTr("Selected: %1").arg(name), false)
            } else if (!doubleTap) {
                if (typeof screenOrientation !== "undefined" && screenOrientation)
                    screenOrientation.vibrateTouch(12)
                root.showToast(qsTr("Deselected"), false)
            }
        }

        onMeshEditActiveChanged: {
            if (viewportItem.meshEditActive) {
                root.outlinerOpen = false
                root.inspectorOpen = false
                root.waterStudioOpen = false
                // A new edit starts with the style bar closed and pulls the
                // tuning back from the viewport, so the steppers never show a
                // value carried over from the previous object.
                root.styleBarOpen = false
                root.refreshZypherTuning()
            }
        }

        onWaterEditActiveChanged: {
            if (viewportItem.waterEditActive) {
                root.outlinerOpen = false
                root.inspectorOpen = false
                root.waterStudioOpen = false
                root.styleBarOpen = false
            }
        }

        onSceneSaved: (path) => root.showToast(qsTr("Saved %1").arg(root.sceneName), false)
        onLastErrorChanged: {
            if (viewportItem.lastError.length > 0)
                root.showToast(viewportItem.lastError, true)
        }

        function updateExclusionRects() {
            viewportItem.clearTouchExclusionRects()
            var padMargin = Theme.dp(16) // Generous finger boundary around controls

            // Bottom control deck (MovePad, Center mode pill, FlyPad)
            if (controlDeck.visible) {
                var pMove = controlDeck.mapToItem(viewportItem, movePad.x, movePad.y)
                viewportItem.addTouchExclusionRect(pMove.x - padMargin, pMove.y - padMargin,
                                                  movePad.width + padMargin * 2, movePad.height + padMargin * 2)

                var pFly = controlDeck.mapToItem(viewportItem, flyPad.x, flyPad.y)
                viewportItem.addTouchExclusionRect(pFly.x - padMargin, pFly.y - padMargin,
                                                  flyPad.width + padMargin * 2, flyPad.height + padMargin * 2)

                if (centerGizmoBar.visible) {
                    var pGizmoBar = controlDeck.mapToItem(viewportItem, centerGizmoBar.x, centerGizmoBar.y)
                    viewportItem.addTouchExclusionRect(pGizmoBar.x - padMargin, pGizmoBar.y - padMargin,
                                                      centerGizmoBar.width + padMargin * 2, centerGizmoBar.height + padMargin * 2)
                }
            }

            // Floating GizmoPad (when armed)
            if (gizmoPad.visible) {
                var pGizmo = gizmoPad.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pGizmo.x - padMargin, pGizmo.y - padMargin,
                                                  gizmoPad.width + padMargin * 2, gizmoPad.height + padMargin * 2)
            }

            // Right-edge precision controls (D-Pad, Rotate buttons, Z-depth nudge buttons)
            if (rightEdgeControls.visible) {
                var pRight = rightEdgeControls.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pRight.x - padMargin, pRight.y - padMargin,
                                                  rightEdgeControls.width + padMargin * 2, rightEdgeControls.height + padMargin * 2)
            }

            // Left-edge controls (Place, Delete, Back)
            if (leftEdgeControls.visible) {
                var pLeft = leftEdgeControls.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pLeft.x - padMargin, pLeft.y - padMargin,
                                                  leftEdgeControls.width + padMargin * 2, leftEdgeControls.height + padMargin * 2)
            }

            // Top HUD identity & menu pills
            if (leftHud.visible) {
                var pLeftHud = leftHud.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pLeftHud.x - padMargin, pLeftHud.y - padMargin,
                                                  leftHud.width + padMargin * 2, leftHud.height + padMargin * 2)
            }
            if (rightHud.visible) {
                var pRightHud = rightHud.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pRightHud.x - padMargin, pRightHud.y - padMargin,
                                                  rightHud.width + padMargin * 2, rightHud.height + padMargin * 2)
            }
            if (hudTogglePill.visible) {
                var pHudToggle = hudTogglePill.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pHudToggle.x - padMargin, pHudToggle.y - padMargin,
                                                  hudTogglePill.width + padMargin * 2, hudTogglePill.height + padMargin * 2)
            }
            if (meshEditToolbar.visible) {
                var pMeshBar = meshEditToolbar.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pMeshBar.x - padMargin, pMeshBar.y - padMargin,
                                                  meshEditToolbar.width + padMargin * 2, meshEditToolbar.height + padMargin * 2)
            }
            if (waterEditToolbar.visible) {
                var pWaterBar = waterEditToolbar.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pWaterBar.x - padMargin, pWaterBar.y - padMargin,
                                                  waterEditToolbar.width + padMargin * 2, waterEditToolbar.height + padMargin * 2)
            }
            if (animBar.visible) {
                var pAnimBar = animBar.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pAnimBar.x - padMargin, pAnimBar.y - padMargin,
                                                  animBar.width + padMargin * 2, animBar.height + padMargin * 2)
            }
            if (waterStudioPanel.open && waterStudioPanel.visible) {
                var pWaterPanel = waterStudioPanel.mapToItem(viewportItem, 0, 0)
                viewportItem.addTouchExclusionRect(pWaterPanel.x, pWaterPanel.y,
                                                  waterStudioPanel.width, waterStudioPanel.height)
            }
        }

        onWidthChanged: updateExclusionTimer.restart()
        onHeightChanged: updateExclusionTimer.restart()
    }

    Timer {
        id: updateExclusionTimer
        interval: 100
        repeat: false
        onTriggered: viewportItem.updateExclusionRects()
    }

    // ── Mesh Edit Floating Control Bar ─────────────────────────────────────
    GlassmorphicOverlay {
        id: meshEditToolbar
        anchors.top: root.width > root.height ? parent.top : leftHud.bottom
        anchors.right: root.width > root.height ? parent.right : undefined
        anchors.horizontalCenter: root.width > root.height ? undefined : parent.horizontalCenter
        anchors.topMargin: root.width > root.height ? Theme.dp(6) : Theme.dp(8)
        anchors.rightMargin: root.width > root.height ? Theme.dp(6) : 0
        height: Theme.dp(40)
        width: meshToolbarRow.implicitWidth + Theme.dp(20)
        z: 30
        visible: viewportItem.meshEditActive

        Row {
            id: meshToolbarRow
            anchors.centerIn: parent
            spacing: Theme.dp(5)

            // Generator mode: Boulder / Zenith / Zypher. Switching pushes
            // straight into boulderx (rubySettings) and re-runs the open edit.
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: genSegRow.implicitWidth + Theme.dp(8)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.9)
                border.color: Theme.borderSubtle
                border.width: 1

                Row {
                    id: genSegRow
                    anchors.centerIn: parent
                    spacing: 0

                    Repeater {
                        model: [
                            { id: "boulder", label: qsTr("Boulder") },
                            { id: "boulderx", label: qsTr("Zenith") },
                            { id: "zypher",  label: qsTr("Zypher") }
                        ]
                        Rectangle {
                            height: Theme.dp(22)
                            width: genSegLabel.implicitWidth + Theme.dp(14)
                            radius: Theme.radiusSm
                            color: root.genMode === modelData.id
                                   ? Theme.alpha(Theme.accentInk, 0.22) : "transparent"
                            border.color: root.genMode === modelData.id
                                          ? Theme.accentInk : "transparent"
                            border.width: 1
                            Text {
                                id: genSegLabel
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: Theme.dp(10)
                                font.weight: root.genMode === modelData.id ? Font.Bold : Font.Medium
                                color: root.genMode === modelData.id
                                       ? Theme.accentInk : Theme.textSecondary
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.setGenerator(modelData.id)
                            }
                        }
                    }
                }
            }

            // Pencil: toggles the Zypher mesh style bar (bottom). Present only
            // on Zypher ground objects, where the style is generated geometry
            // and therefore actually selectable.
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.zypherMode
                iconName: "pencil"
                buttonSize: Theme.dp(28)
                iconSize: Theme.dp(14)
                square: true
                tint: root.styleBarOpen ? Theme.accentInk : Theme.textPrimary
                plate: root.styleBarOpen ? Theme.alpha(Theme.accentInk, 0.20) : Theme.alpha(Theme.surface2, 0.9)
                plateEdge: root.styleBarOpen ? Theme.accentInk : Theme.borderSubtle
                onClicked: {
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.vibrateTouch(20)
                    root.styleBarOpen = !root.styleBarOpen
                    if (root.styleBarOpen) root.refreshZypherTuning()
                }
            }

            // Discard Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: discardRow.implicitWidth + Theme.dp(12)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.9)
                border.color: Theme.borderSubtle
                border.width: 1
                Row {
                    id: discardRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "x"; size: Theme.dp(13); color: Theme.colorErrorBright }
                    Text { text: qsTr("Discard"); font.pixelSize: Theme.dp(11); font.weight: Font.Medium; color: Theme.textPrimary }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.discardMeshEdit()
                    }
                }
            }

            // Divider
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Theme.dp(18)
                color: Theme.borderSubtle
            }

            // Terrain relief. The sheet is drafted in 2D, so Z cannot be drawn on
            // it — every node would otherwise share one depth and the mesh would
            // read as a flat plate. This gives each node its own depth inside the
            // object's own range, without changing its thickness. Tap again for a
            // different shape; long-press to go back to the object's OWN depths
            // (the Z it shipped with, which the import recovers) — not to flat.
            Rectangle {
                id: reliefButton
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: reliefRow.implicitWidth + Theme.dp(12)
                radius: Theme.radiusSm
                color: Theme.alpha(viewportItem.hasGroundRelief()
                                   ? Theme.accentEnd : Theme.surface2, 0.9)
                border.color: viewportItem.hasGroundRelief()
                              ? Theme.accentEnd : Theme.borderSubtle
                border.width: 1

                Row {
                    id: reliefRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon {
                        name: "layers"
                        size: Theme.dp(13)
                        color: viewportItem.hasGroundRelief()
                               ? Theme.textPrimary : Theme.textSecondary
                    }
                    Text {
                        // "Vanilla Z" is the object's own per-node depth, recovered
                        // from its baked mesh and carried through the edit; the seed
                        // shows only once it has been replaced by a randomisation.
                        // #will be unlocked after: per-node Z is staged off for this
                        // release, so the pill says so rather than offering a tap that
                        // would silently do nothing. Long-press (Flatten) still works.
                        text: viewportItem.groundReliefStaged()
                              ? qsTr("Z — #will be unlocked after")
                              : (viewportItem.hasGroundRelief()
                                 ? (viewportItem.groundReliefSeed() > 0
                                    ? qsTr("Z %1").arg(viewportItem.groundReliefSeed())
                                    : qsTr("Vanilla Z"))
                                 : qsTr("Relief"))
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.Medium
                        color: Theme.textPrimary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressAndHold: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.clearGroundRelief()
                    }
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.randomiseGroundRelief(-1, -1, -1)
                    }
                }
            }

            // Divider
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Theme.dp(18)
                color: Theme.borderSubtle
            }

            // Previous Vertex Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(26)
                height: Theme.dp(28)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.85)
                border.color: Theme.borderSubtle
                border.width: 1
                Icon {
                    anchors.centerIn: parent
                    name: "chevron-left"
                    size: Theme.dp(13)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(15)
                        viewportItem.selectPrevMeshVertex()
                    }
                }
            }

            // Vertex Index Badge
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: vertBadgeText.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.accentInk, 0.15)
                border.color: Theme.alpha(Theme.accentInk, 0.4)
                border.width: 1
                Text {
                    id: vertBadgeText
                    anchors.centerIn: parent
                    text: qsTr("V %1/%2").arg(viewportItem.selectedMeshVertex + 1).arg(viewportItem.meshVertexCount)
                    font.pixelSize: Theme.dp(10)
                    font.weight: Font.Bold
                    color: Theme.accentInk
                }
            }

            // Next Vertex Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(26)
                height: Theme.dp(28)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.85)
                border.color: Theme.borderSubtle
                border.width: 1
                Icon {
                    anchors.centerIn: parent
                    name: "chevron-right"
                    size: Theme.dp(13)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(15)
                        viewportItem.selectNextMeshVertex()
                    }
                }
            }

            // Add Vertex Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: addVertRow.implicitWidth + Theme.dp(12)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface3, 0.9)
                border.color: Theme.accentInk
                border.width: 1
                Row {
                    id: addVertRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "plus"; size: Theme.dp(12); color: Theme.accentInk }
                    Text { text: qsTr("Vertex"); font.pixelSize: Theme.dp(11); font.weight: Font.DemiBold; color: Theme.accentInk }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(15)
                        viewportItem.insertMeshVertex(viewportItem.selectedMeshVertex, 0, 0)
                    }
                }
            }

            // Delete Vertex Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: delVertRow.implicitWidth + Theme.dp(10)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.9)
                border.color: Theme.borderSubtle
                border.width: 1
                enabled: viewportItem.meshVertexCount > 3
                opacity: enabled ? 1.0 : 0.4
                Row {
                    id: delVertRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(2)
                    Icon { name: "trash"; size: Theme.dp(12); color: Theme.colorDanger }
                    Text { text: qsTr("Del"); font.pixelSize: Theme.dp(11); color: Theme.colorDanger }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.deleteMeshVertex(viewportItem.selectedMeshVertex)
                    }
                }
            }

            // Divider
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Theme.dp(18)
                color: Theme.borderSubtle
            }

            // Apply Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: applyRow.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: Theme.colorSuccess
                Row {
                    id: applyRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "check"; size: Theme.dp(13); color: "#ffffff" }
                    Text { text: qsTr("Apply"); font.pixelSize: Theme.dp(11); font.weight: Font.Bold; color: "#ffffff" }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.applyMeshEdit()
                    }
                }
            }
        }
    }

    // ── Water Mesh (W-Mesh) Floating Control Bar ───────────────────────────
    GlassmorphicOverlay {
        id: waterEditToolbar
        anchors.top: root.width > root.height ? parent.top : leftHud.bottom
        anchors.right: root.width > root.height ? parent.right : undefined
        anchors.horizontalCenter: root.width > root.height ? undefined : parent.horizontalCenter
        anchors.topMargin: root.width > root.height ? Theme.dp(6) : Theme.dp(8)
        anchors.rightMargin: root.width > root.height ? Theme.dp(6) : 0
        height: Theme.dp(40)
        width: waterToolbarRow.implicitWidth + Theme.dp(20)
        z: 30
        visible: viewportItem.waterEditActive

        Row {
            id: waterToolbarRow
            anchors.centerIn: parent
            spacing: Theme.dp(5)

            // Identity Badge
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: wBadgeRow.implicitWidth + Theme.dp(12)
                radius: Theme.radiusSm
                color: Theme.alpha("#06b6d4", 0.22)
                border.color: "#06b6d4"
                border.width: 1

                Row {
                    id: wBadgeRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "water"; size: Theme.dp(12); color: "#06b6d4" }
                    Text { text: qsTr("W-Mesh"); font.pixelSize: Theme.dp(11); font.weight: Font.Bold; color: "#06b6d4" }
                }
            }

            // Discard Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: discardWRow.implicitWidth + Theme.dp(12)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.9)
                border.color: Theme.borderSubtle
                border.width: 1
                Row {
                    id: discardWRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "close"; size: Theme.dp(13); color: Theme.colorErrorBright }
                    Text { text: qsTr("Discard"); font.pixelSize: Theme.dp(11); font.weight: Font.Medium; color: Theme.textPrimary }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.discardWaterMeshEdit()
                    }
                }
            }

            // Reset to Rect Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: resetRectRow.implicitWidth + Theme.dp(10)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.9)
                border.color: Theme.borderSubtle
                border.width: 1
                Row {
                    id: resetRectRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "rotate"; size: Theme.dp(12); color: Theme.textSecondary }
                    Text { text: qsTr("Reset Rect"); font.pixelSize: Theme.dp(11); color: Theme.textSecondary }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(20)
                        viewportItem.resetWaterEditToRect()
                    }
                }
            }

            // Divider
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Theme.dp(18)
                color: Theme.borderSubtle
            }

            // Previous Vertex Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(26)
                height: Theme.dp(28)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.85)
                border.color: Theme.borderSubtle
                border.width: 1
                Icon {
                    anchors.centerIn: parent
                    name: "chevron-left"
                    size: Theme.dp(13)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(15)
                        viewportItem.selectPrevWaterVertex()
                    }
                }
            }

            // Vertex Index Badge
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: wVertBadgeText.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: Theme.alpha("#06b6d4", 0.18)
                border.color: Theme.alpha("#06b6d4", 0.50)
                border.width: 1
                Text {
                    id: wVertBadgeText
                    anchors.centerIn: parent
                    text: qsTr("V %1/%2").arg(viewportItem.selectedWaterVertex + 1).arg(viewportItem.waterVertexCount)
                    font.pixelSize: Theme.dp(10)
                    font.weight: Font.Bold
                    color: "#06b6d4"
                }
            }

            // Next Vertex Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(26)
                height: Theme.dp(28)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.85)
                border.color: Theme.borderSubtle
                border.width: 1
                Icon {
                    anchors.centerIn: parent
                    name: "chevron-right"
                    size: Theme.dp(13)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(15)
                        viewportItem.selectNextWaterVertex()
                    }
                }
            }

            // Add Point Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: addPointRow.implicitWidth + Theme.dp(12)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface3, 0.9)
                border.color: "#06b6d4"
                border.width: 1
                Row {
                    id: addPointRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "plus"; size: Theme.dp(12); color: "#06b6d4" }
                    Text { text: qsTr("Point"); font.pixelSize: Theme.dp(11); font.weight: Font.DemiBold; color: "#06b6d4" }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(15)
                        viewportItem.insertWaterVertex(viewportItem.selectedWaterVertex, 0, 0)
                    }
                }
            }

            // Delete Point Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: delPointRow.implicitWidth + Theme.dp(10)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.9)
                border.color: Theme.borderSubtle
                border.width: 1
                enabled: viewportItem.waterVertexCount > 3
                opacity: enabled ? 1.0 : 0.4
                Row {
                    id: delPointRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(2)
                    Icon { name: "trash"; size: Theme.dp(12); color: Theme.colorDanger }
                    Text { text: qsTr("Del"); font.pixelSize: Theme.dp(11); color: Theme.colorDanger }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.deleteWaterVertex(viewportItem.selectedWaterVertex)
                    }
                }
            }

            // Divider
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: Theme.dp(18)
                color: Theme.borderSubtle
            }

            // Apply Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(28)
                width: applyWRow.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: Theme.colorSuccess
                Row {
                    id: applyWRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(3)
                    Icon { name: "check"; size: Theme.dp(13); color: "#ffffff" }
                    Text { text: qsTr("Apply"); font.pixelSize: Theme.dp(11); font.weight: Font.Bold; color: "#ffffff" }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) screenOrientation.vibrateTouch(25)
                        viewportItem.applyWaterMeshEdit()
                    }
                }
            }
        }
    }

    // ── Mesh style bar (Zypher look) ───────────────────────────────────────
    //
    // The old ZypherLookPanel was a 272x430dp side window with text fields for
    // the tuning numbers. It is now a thin bottom bar the same height as the
    // viewport console, opened by the pencil button in the mesh-edit toolbar and
    // smoothly revealed upward. Numeric tuning lives in the floating
    // BubbleStepper pads on the right — press-and-hold +/- instead of a keyboard.
    property bool styleBarOpen: false
    property real zypFreq: 6
    property real zypDepth: 14
    property real zypMinColl: 8

    // ── Ground-mesh generator mode (Boulder / Zenith / Zypher) ─────────────
    // The desktop edition's Generator combo, ported as a touch segmented
    // control for the in-scene editor. rubySettings is the single source of
    // truth: its setter persists AND pushes into boulderx synchronously, so
    // every generator-gated affordance below re-evaluates the moment a segment
    // is tapped (zypherActive() alone has no notify signal and would freeze).
    readonly property string genMode: (typeof rubySettings !== "undefined" && rubySettings)
                                      ? rubySettings.groundGenerator
                                      : (viewportItem.zypherActive() ? "zypher" : "boulder")
    readonly property bool zypherMode: root.genMode === "zypher"

    function setGenerator(id) {
        if (id === root.genMode) return
        if (typeof screenOrientation !== "undefined" && screenOrientation)
            screenOrientation.vibrateTouch(15)
        if (typeof rubySettings !== "undefined" && rubySettings)
            rubySettings.groundGenerator = id
        if (id !== "zypher" && root.styleBarOpen)
            root.styleBarOpen = false
        if (id === "zypher")
            root.refreshZypherTuning()
        // Re-run the open edit's generation path with the new engine so the
        // switch shows new geometry immediately, not at the next commit.
        viewportItem.refreshMeshEditPreview()
    }

    function refreshZypherTuning() {
        if (typeof viewportItem.getZypherLook !== "function") return
        var s = viewportItem.getZypherLook()
        if (!s) return
        root.zypFreq = s.frequency
        root.zypDepth = s.depth
        root.zypMinColl = s.minCollisionRadius
    }

    // Reveal container: animating its height (clipped) is what makes the bar
    // grow smoothly out of the bottom edge instead of popping in.
    Item {
        id: styleDock
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: (root.styleBarOpen && viewportItem.meshEditActive
                 && root.zypherMode) ? root.consoleHeight : 0
        clip: true
        z: 28
        visible: height > 0.5 || root.styleBarOpen

        Behavior on height { NumberAnimation { duration: Theme.durMed; easing.type: Easing.OutCubic } }

        MeshStyleBar {
            id: meshStyleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.dp(6)
            anchors.rightMargin: Theme.dp(6)
            anchors.bottomMargin: Theme.dp(4)
            height: root.consoleHeight - Theme.dp(8)
            host: viewportItem
            modeActive: root.zypherMode
            onCloseRequested: root.styleBarOpen = false
            onZypherStylesChanged: root.refreshZypherTuning()
        }
    }

    // ── Floating numeric bubbles (Zypher tuning, right side) ──────────────
    // Frequency = features per 100 unit, Depth/radius = feature size, Min radius
    // = the size below which a feature casts no collision. Hold + / - to ramp.
    Column {
        id: zypBubbles
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.dp(6)
        spacing: Theme.dp(8)
        z: 29
        visible: root.styleBarOpen && viewportItem.meshEditActive
                 && root.zypherMode
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        BubbleStepper {
            anchors.horizontalCenter: parent.horizontalCenter
            label: qsTr("FREQ")
            step: 0.5; decimals: 1
            value: root.zypFreq
            onStepped: (delta) => {
                var nv = Math.max(0.5, Math.min(30.0, root.zypFreq + delta))
                if (Math.abs(nv - root.zypFreq) < 1e-6) return
                root.zypFreq = nv
                viewportItem.setZypherLook("", "", nv, -1, -1)
            }
        }

        BubbleStepper {
            anchors.horizontalCenter: parent.horizontalCenter
            label: qsTr("DEPTH")
            tone: Theme.colorModel
            step: 1.0; decimals: 0
            value: root.zypDepth
            onStepped: (delta) => {
                var nv = Math.max(0.0, Math.min(120.0, root.zypDepth + delta))
                if (Math.abs(nv - root.zypDepth) < 1e-6) return
                root.zypDepth = nv
                viewportItem.setZypherLook("", "", -1, nv, -1)
            }
        }

        BubbleStepper {
            anchors.horizontalCenter: parent.horizontalCenter
            label: qsTr("MIN R")
            tone: Theme.colorWarning
            step: 1.0; decimals: 0
            value: root.zypMinColl
            onStepped: (delta) => {
                var nv = Math.max(0.0, Math.min(120.0, root.zypMinColl + delta))
                if (Math.abs(nv - root.zypMinColl) < 1e-6) return
                root.zypMinColl = nv
                viewportItem.setZypherLook("", "", -1, -1, nv)
            }
        }
    }

    // ── HUD: left pill (identity) ──────────────────────────────────────────
    GlassmorphicOverlay {
        id: leftHud
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: Theme.dp(6)
        anchors.leftMargin: Theme.dp(6)
        height: Theme.dp(40)
        width: Math.min(Theme.dp(300), root.width * 0.40)
        z: 12
        visible: root.controlsVisible
        opacity: root.controlsVisible ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        Row {
            id: leftHudRow
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(2)
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            spacing: Theme.dp(Theme.spacingXs)

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "arrow-left"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                onClicked: root.requestExit()
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: leftHudRow.width - Theme.dp(32) - Theme.dp(Theme.spacingXs) * 2
                spacing: 0

                Row {
                    width: parent.width
                    spacing: Theme.dp(4)

                    Text {
                        width: parent.width - (viewportItem.dirty ? Theme.dp(12) : 0)
                        text: root.sceneName.length > 0 ? root.sceneName : qsTr("Viewport")
                        font.pixelSize: Theme.dp(Theme.fontMd)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideMiddle
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: viewportItem.dirty
                        width: Theme.dp(7)
                        height: Theme.dp(7)
                        radius: width / 2
                        color: Theme.colorWarning
                    }
                }

                Text {
                    text: root.isModel
                          ? qsTr("%1 meshes · %2 v").arg(viewportItem.meshCount).arg(viewportItem.vertexCount)
                          : (viewportItem.meshEditActive
                             // Name the generator that actually produced the current
                             // mesh. BoulderX declines shapes it cannot reproduce
                             // faithfully and the editor then uses Boulder, so a
                             // fallback shown as plain success is indistinguishable
                             // from BoulderX having worked. The reason is front-loaded
                             // ("declined: …") so elision cannot hide the fact.
                             ? (viewportItem.groundGeneratorStatus.length > 0
                                ? qsTr("Mesh Edit (%1 v) · %2")
                                      .arg(viewportItem.meshVertexCount)
                                      .arg(viewportItem.groundGeneratorStatus)
                                : qsTr("Mesh Edit (%1 v)").arg(viewportItem.meshVertexCount))
                             : (viewportItem.waterEditActive
                                ? qsTr("W-Mesh Edit (%1 v)").arg(viewportItem.waterVertexCount)
                                : qsTr("%1 objects · %2").arg(viewportItem.objectCount)
                                                     .arg(viewportItem.gizmoMode === 0
                                                          ? qsTr("view")
                                                          : root.gizmoModes[viewportItem.gizmoMode].name.toLowerCase())))
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: viewportItem.meshEditActive
                           ? Theme.colorWarning
                           : (viewportItem.waterEditActive ? "#06b6d4" : Theme.textMuted)
                    elide: Text.ElideRight
                }
            }
        }
    }

    // ── Floating Controls / HUD Visibility Toggle chip ─────────────────────
    Rectangle {
        id: hudTogglePill
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.dp(6)
        height: Theme.dp(22)
        width: Theme.dp(56)
        radius: Theme.radiusPill
        color: root.controlsVisible ? Theme.alpha(Theme.surface1, 0.65) : Theme.alpha(Theme.surface3, 0.90)
        border.color: root.controlsVisible ? Theme.borderSubtle : Theme.accentInk
        border.width: 1
        z: 30
        visible: root.controlsVisible && !root.inAnyMeshEdit

        Row {
            anchors.centerIn: parent
            spacing: Theme.dp(3)

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.controlsVisible ? "eye" : "eye-off"
                size: Theme.dp(11)
                color: root.controlsVisible ? Theme.textMuted : Theme.accentInk
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.controlsVisible ? qsTr("HUD") : qsTr("FULL")
                font.pixelSize: Theme.dp(9)
                font.weight: Font.Bold
                color: root.controlsVisible ? Theme.textMuted : Theme.accentInk
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.controlsVisible = !root.controlsVisible
        }
    }

    // ── HUD: right pill (tools) ───────────────────────────────────────────
    GlassmorphicOverlay {
        id: rightHud
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.dp(6)
        anchors.rightMargin: Theme.dp(6)
        height: Theme.dp(40)
        width: rightHudRow.implicitWidth + Theme.dp(16)
        z: 12
        visible: root.controlsVisible && !root.inAnyMeshEdit
        opacity: (root.controlsVisible && !root.inAnyMeshEdit) ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        Row {
            id: rightHudRow
            anchors.centerIn: parent
            spacing: Theme.dp(Theme.spacingXs)

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: true
                iconName: root.isModel ? "file-text" : "braces"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                variant: "soft"
                onClicked: {
                    root.savedCameraState = viewportItem.getCameraState()
                    if (viewportItem.dirty) {
                        unsavedSceneDialog.open()
                    } else {
                        root.proceedToCodeEditor()
                    }
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "undo"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                enabled: viewportItem.canUndo
                onClicked: viewportItem.undo()
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "redo"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                enabled: viewportItem.canRedo
                onClicked: viewportItem.redo()
            }

            // Mesh Edit mode toggle
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: viewportItem.canMeshEdit
                height: Theme.dp(30)
                width: meshEditRow.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: viewportItem.meshEditActive
                       ? Theme.alpha(Theme.colorWarning, 0.35)
                       : Theme.alpha(Theme.surface2, 0.90)
                border.color: viewportItem.meshEditActive ? Theme.colorWarning : Theme.borderSubtle
                border.width: 1

                Row {
                    id: meshEditRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "edit"
                        size: Theme.dp(13)
                        color: viewportItem.meshEditActive ? Theme.colorWarning : Theme.textPrimary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: viewportItem.meshEditActive ? qsTr("Editing") : qsTr("Mesh")
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.DemiBold
                        color: viewportItem.meshEditActive ? Theme.colorWarning : Theme.textPrimary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) {
                            screenOrientation.vibrateTouch(20)
                            screenOrientation.lockLandscape()
                        }
                        if (viewportItem.meshEditActive) {
                            viewportItem.applyMeshEdit()
                        } else {
                            viewportItem.beginMeshEdit()
                        }
                    }
                }
            }

            // Water Mesh Edit mode toggle
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: viewportItem.canWaterMeshEdit
                height: Theme.dp(30)
                width: wmeshEditRow.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: viewportItem.waterEditActive
                       ? Theme.alpha("#06b6d4", 0.35)
                       : Theme.alpha(Theme.surface2, 0.90)
                border.color: viewportItem.waterEditActive ? "#06b6d4" : Theme.borderSubtle
                border.width: 1

                Row {
                    id: wmeshEditRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "water"
                        size: Theme.dp(13)
                        color: viewportItem.waterEditActive ? "#06b6d4" : Theme.textPrimary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: viewportItem.waterEditActive ? qsTr("W-Edit") : qsTr("W-Mesh")
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.DemiBold
                        color: viewportItem.waterEditActive ? "#06b6d4" : Theme.textPrimary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation) {
                            screenOrientation.vibrateTouch(20)
                            screenOrientation.lockLandscape()
                        }
                        if (viewportItem.waterEditActive) {
                            viewportItem.applyWaterMeshEdit()
                        } else {
                            viewportItem.beginWaterMeshEdit()
                        }
                    }
                }
            }

            // Dedicated Fluid Studio button (visible when selected object is water)
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: viewportItem.isSelectedWater
                height: Theme.dp(30)
                width: waterRow.implicitWidth + Theme.dp(14)
                radius: Theme.radiusSm
                color: root.waterStudioOpen
                       ? Theme.alpha("#06b6d4", 0.35)
                       : Theme.alpha(Theme.surface2, 0.90)
                border.color: root.waterStudioOpen ? "#06b6d4" : Theme.borderSubtle
                border.width: 1

                Row {
                    id: waterRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "water"
                        size: Theme.dp(13)
                        color: root.waterStudioOpen ? "#06b6d4" : Theme.textPrimary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Fluid")
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.DemiBold
                        color: root.waterStudioOpen ? "#06b6d4" : Theme.textPrimary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(20)
                        root.waterStudioOpen = !root.waterStudioOpen
                    }
                }
            }

            // Prominent Save Button
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.dp(30)
                width: saveRow.implicitWidth + Theme.dp(16)
                radius: Theme.radiusSm
                color: viewportItem.dirty ? Theme.alpha(Theme.colorWarning, 0.28) : Theme.alpha(Theme.surface2, 0.90)
                border.color: viewportItem.dirty ? Theme.colorWarning : Theme.borderSubtle
                border.width: 1

                Row {
                    id: saveRow
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "save"
                        size: Theme.dp(14)
                        color: viewportItem.dirty ? Theme.colorWarning : Theme.textPrimary
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Save")
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.DemiBold
                        color: viewportItem.dirty ? Theme.colorWarning : Theme.textPrimary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(20)
                        viewportItem.saveScene()
                    }
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: root.controlsVisible ? "eye" : "eye-off"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                tint: root.controlsVisible ? Theme.textSecondary : Theme.accentInk
                plate: !root.controlsVisible ? Theme.alpha(Theme.accentInk, 0.20) : "transparent"
                onClicked: root.controlsVisible = !root.controlsVisible
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "layers"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                tint: root.outlinerOpen ? Theme.accentInk : Theme.textSecondary
                plate: root.outlinerOpen ? Theme.alpha(Theme.accentInk, 0.16) : "transparent"
                onClicked: {
                    root.outlinerOpen = !root.outlinerOpen
                    if (root.outlinerOpen) root.inspectorOpen = false
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "sliders-vertical"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                tint: root.inspectorOpen ? Theme.accentInk : Theme.textSecondary
                plate: root.inspectorOpen ? Theme.alpha(Theme.accentInk, 0.16) : "transparent"
                onClicked: {
                    root.inspectorOpen = !root.inspectorOpen
                    if (root.inspectorOpen) root.outlinerOpen = false
                }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "more-vertical"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                onClicked: viewportMenu.open(false)
            }
        }
    }

    // ── Compact Coordinate & Step Readout HUD (top-left below leftHud) ──────
    GlassmorphicOverlay {
        id: coordsHud
        anchors.top: leftHud.bottom
        anchors.left: parent.left
        anchors.topMargin: Theme.dp(4)
        anchors.leftMargin: Theme.dp(6)
        height: Theme.dp(26)
        width: Math.min(coordsRow.implicitWidth + Theme.dp(12), root.width * 0.45)
        z: 12
        visible: root.controlsVisible && !root.outlinerOpen && !root.inAnyMeshEdit
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        Row {
            id: coordsRow
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dp(6)
            spacing: Theme.dp(5)

            // r & s readout
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: viewportItem.selectedObject >= 0
                      ? "r:" + Math.round(viewportItem.selectedRot) + " s:" + viewportItem.selectedScale.toFixed(1)
                      : (viewportItem.isGameView ? "2.5D" : "3D")
                font.pixelSize: Theme.dp(10)
                font.weight: Font.DemiBold
                color: Theme.accentInk
            }

            // x, y, z readout
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: viewportItem.selectedObject >= 0
                      ? "x:" + viewportItem.selectedX.toFixed(1) + " y:" + viewportItem.selectedY.toFixed(1) + " z:" + viewportItem.selectedZ.toFixed(1)
                      : "x:" + viewportItem.cameraX.toFixed(1) + " y:" + viewportItem.cameraY.toFixed(1)
                font.pixelSize: Theme.dp(10)
                font.family: "monospace"
                color: Theme.textPrimary
            }

            // Step readout and minus/plus
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(1)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Step:" + viewportItem.navStep
                    font.pixelSize: Theme.dp(10)
                    color: Theme.textMuted
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "minus"
                    buttonSize: Theme.dp(30)
                    iconSize: Theme.dp(13)
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.cycleNavStep(-1)
                    }
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "plus"
                    buttonSize: Theme.dp(30)
                    iconSize: Theme.dp(13)
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.cycleNavStep(1)
                    }
                }
            }
        }
    }

    // ── Left Edge Quick Action Controls (Place / Back / Delete) ─────────────
    Column {
        id: leftEdgeControls
        anchors.left: parent.left
        anchors.top: coordsHud.bottom
        anchors.topMargin: Theme.dp(6)
        anchors.leftMargin: Theme.dp(6)
        spacing: Theme.dp(4)
        z: 12
        visible: root.controlsVisible && !root.outlinerOpen && !root.inAnyMeshEdit
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        Rectangle {
            width: Theme.dp(44)
            height: Theme.dp(24)
            radius: Theme.radiusSm
            color: Theme.alpha(Theme.surface2, 0.85)
            border.color: Theme.borderSubtle
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("Place")
                font.pixelSize: Theme.dp(10)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.vibrateTouch(15)
                    placeObjectDialog.openDialog()
                }
            }
        }

        Rectangle {
            visible: viewportItem.selectedObject >= 0
            width: Theme.dp(44)
            height: Theme.dp(24)
            radius: Theme.radiusSm
            color: Theme.alpha(Theme.surface2, 0.85)
            border.color: Theme.borderSubtle
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("Clone")
                font.pixelSize: Theme.dp(10)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.vibrateTouch(15)
                    viewportItem.placeCurrentObject()
                }
            }
        }

        Rectangle {
            width: Theme.dp(44)
            height: Theme.dp(24)
            radius: Theme.radiusSm
            color: Theme.alpha(Theme.surface2, 0.85)
            border.color: Theme.borderSubtle
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("Back")
                font.pixelSize: Theme.dp(10)
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.vibrateTouch(15)
                    viewportItem.deselectObject()
                }
            }
        }

        Rectangle {
            visible: viewportItem.selectedObject >= 0
            width: Theme.dp(44)
            height: Theme.dp(24)
            radius: Theme.radiusSm
            color: Theme.alpha(Theme.colorError, 0.20)
            border.color: Theme.alpha(Theme.colorError, 0.45)
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: qsTr("Del")
                font.pixelSize: Theme.dp(10)
                font.weight: Font.DemiBold
                color: Theme.colorErrorBright
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.vibrateTouch(25)
                    viewportItem.deleteCurrentObject()
                }
            }
        }
    }

    // ── Right Edge Compact Navigation & D-Pad Controls ──────────────────────
    Column {
        id: rightEdgeControls
        anchors.right: parent.right
        anchors.top: rightHud.bottom
        anchors.topMargin: Theme.dp(6)
        anchors.rightMargin: Theme.dp(6)
        spacing: Theme.dp(4)
        z: 12
        visible: root.controlsVisible && !root.inspectorOpen && root.consoleCollapsed && !root.inAnyMeshEdit
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        // Row 1: Camera Mode & Frame
        Row {
            anchors.right: parent.right
            spacing: Theme.dp(4)

            Rectangle {
                width: Theme.dp(52)
                height: Theme.dp(24)
                radius: Theme.radiusPill
                color: viewportItem.isGameView ? Theme.alpha(Theme.accentInk, 0.25) : Theme.alpha(Theme.surface2, 0.85)
                border.color: viewportItem.isGameView ? Theme.accentInk : Theme.borderSubtle
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: viewportItem.isGameView ? "2.5D" : "3D"
                    font.pixelSize: Theme.dp(10)
                    font.weight: Font.Bold
                    color: viewportItem.isGameView ? Theme.accentInk : Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.toggleCameraMode()
                    }
                }
            }

            Rectangle {
                width: Theme.dp(24)
                height: Theme.dp(24)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.85)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "maximize"
                    size: Theme.dp(13)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.frameScene()
                    }
                }
            }
        }

        // Row 2: Rotate CCW / CW (Enlarged 36x34dp touch targets)
        Row {
            anchors.right: parent.right
            spacing: Theme.dp(4)

            Rectangle {
                width: Theme.dp(36)
                height: Theme.dp(34)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.90)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "rotate-ccw"
                    size: Theme.dp(16)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.rotateSelectedZ(-15.0)
                    }
                }
            }

            Rectangle {
                width: Theme.dp(36)
                height: Theme.dp(34)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.90)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "rotate"
                    size: Theme.dp(16)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.rotateSelectedZ(15.0)
                    }
                }
            }
        }

        // Row 3: Z-Depth Nudge (Enlarged 36x34dp touch targets)
        Row {
            anchors.right: parent.right
            spacing: Theme.dp(4)

            Rectangle {
                width: Theme.dp(36)
                height: Theme.dp(34)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.90)
                border.color: Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: 1
                    Text {
                        text: "Z"
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.Bold
                        color: Theme.colorModel
                    }
                    Icon {
                        name: "chevron-up"
                        size: Theme.dp(12)
                        color: Theme.colorModel
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.nudgeSelectedCoord(2, viewportItem.navStep)
                    }
                }
            }

            Rectangle {
                width: Theme.dp(36)
                height: Theme.dp(34)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.90)
                border.color: Theme.borderSubtle
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: 1
                    Text {
                        text: "Z"
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.Bold
                        color: Theme.colorModel
                    }
                    Icon {
                        name: "chevron-down"
                        size: Theme.dp(12)
                        color: Theme.colorModel
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.nudgeSelectedCoord(2, -viewportItem.navStep)
                    }
                }
            }
        }

        // Precision D-Pad Grid (Enlarged 114x114dp comfortable mobile controller)
        Item {
            anchors.right: parent.right
            width: Theme.dp(114)
            height: Theme.dp(114)

            // Up
            Rectangle {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.dp(38)
                height: Theme.dp(36)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.92)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "chevron-up"
                    size: Theme.dp(18)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.nudgeSelectedCoord(1, viewportItem.navStep)
                    }
                }
            }

            // Left
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(36)
                height: Theme.dp(38)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.92)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "chevron-left"
                    size: Theme.dp(18)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.nudgeSelectedCoord(0, -viewportItem.navStep)
                    }
                }
            }

            // Center: Nav Step Indicator & Hybrid Continuous Translation Joystick
            Rectangle {
                id: centerStepBtn
                anchors.centerIn: parent
                width: Theme.dp(36)
                height: Theme.dp(36)
                radius: Theme.radiusSm
                color: isTranslating ? Theme.alpha(Theme.accentInk, 0.45) : Theme.alpha(Theme.surface3, 0.96)
                border.color: isTranslating ? "#ffffff" : Theme.alpha(Theme.accentInk, 0.7)
                border.width: isTranslating ? 2.0 : 1.5

                property bool isTranslating: false

                Column {
                    anchors.centerIn: parent
                    spacing: 0
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "" + viewportItem.navStep
                        font.pixelSize: Theme.dp(11)
                        font.weight: Font.Bold
                        color: centerStepBtn.isTranslating ? "#ffffff" : Theme.accentInk
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: centerStepBtn.isTranslating ? "drag" : "step"
                        font.pixelSize: Theme.dp(7)
                        color: centerStepBtn.isTranslating ? "#ffffff" : Theme.textMuted
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    property real pressStartX: 0
                    property real pressStartY: 0
                    property real totalDist: 0

                    onPressed: (mouse) => {
                        pressStartX = mouse.x
                        pressStartY = mouse.y
                        totalDist = 0
                        centerStepBtn.isTranslating = false
                    }

                    onPositionChanged: (mouse) => {
                        var dx = mouse.x - pressStartX
                        var dy = mouse.y - pressStartY
                        var dist = Math.hypot(dx, dy)
                        totalDist += dist
                        if (dist > 3 || centerStepBtn.isTranslating) {
                            centerStepBtn.isTranslating = true
                            var dragScale = viewportItem.navStep * 0.22
                            viewportItem.dragSelected(dx * dragScale, -dy * dragScale, 0)
                            pressStartX = mouse.x
                            pressStartY = mouse.y
                        }
                    }

                    onReleased: {
                        if (centerStepBtn.isTranslating) {
                            viewportItem.finishDrag()
                            centerStepBtn.isTranslating = false
                        }
                    }

                    onCanceled: {
                        if (centerStepBtn.isTranslating) {
                            viewportItem.finishDrag()
                            centerStepBtn.isTranslating = false
                        }
                    }

                    onClicked: {
                        if (totalDist < 10 && !centerStepBtn.isTranslating) {
                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                screenOrientation.vibrateTouch(20)
                            viewportItem.cycleNavStep(1)
                        }
                    }

                    onPressAndHold: {
                        if (totalDist < 10 && !centerStepBtn.isTranslating) {
                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                screenOrientation.vibrateTouch(35)
                            viewportItem.cycleNavStep(-1)
                        }
                    }
                }
            }

            // Right
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.dp(36)
                height: Theme.dp(38)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.92)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "chevron-right"
                    size: Theme.dp(18)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.nudgeSelectedCoord(0, viewportItem.navStep)
                    }
                }
            }

            // Down
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.dp(38)
                height: Theme.dp(36)
                radius: Theme.radiusSm
                color: Theme.alpha(Theme.surface2, 0.92)
                border.color: Theme.borderSubtle
                border.width: 1

                Icon {
                    anchors.centerIn: parent
                    name: "chevron-down"
                    size: Theme.dp(18)
                    color: Theme.textPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    onClicked: {
                        if (typeof screenOrientation !== "undefined" && screenOrientation)
                            screenOrientation.vibrateTouch(15)
                        viewportItem.nudgeSelectedCoord(1, -viewportItem.navStep)
                    }
                }
            }
        }
    }

    // ── The console: two pads with the gizmo pad between them ─────────────
    // Anchored to the bottom and centred as one unit, so the pads sit exactly
    // where the thumbs already rest in landscape and the middle of the screen
    // stays clear for the scene.
    // NOTE: the id must NOT be `console` — that is a global JavaScript object
    // and QML rejects the file with "ID illegally masks global JavaScript
    // property", taking the whole studio down at load time.
    // ── Floating Bottom Controls (100% full-bleed scene underneath) ─────────
    Item {
        id: controlDeck
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.consoleHeight
        z: 11
        // The style bar takes the deck's place while it is open, so the two
        // never stack up and eat twice the height.
        visible: root.controlsVisible && root.studioFits && !root.styleBarOpen

        // Left Thumbstick (Pan / Move Camera)
        NavPad {
            id: movePad
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.dp(12)
            anchors.bottomMargin: Theme.dp(10)
            width: Theme.dp(84)
            height: Theme.dp(84)
            navMode: 0
            allowModeToggle: false
            showModeHint: false

            onPanRequested: (vx, vy) => viewportItem.panCamera(vx, vy)
            onDollyRequested: (fwd, strafe) => viewportItem.dollyCamera(fwd, strafe)
        }

        // Center Floating Gizmo Mode Pill
        GlassmorphicOverlay {
            id: centerGizmoBar
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.dp(10)
            height: Theme.dp(36)
            width: gizmoRow.implicitWidth + Theme.dp(12)
            visible: !root.inAnyMeshEdit

            Row {
                id: gizmoRow
                anchors.centerIn: parent
                spacing: Theme.dp(4)

                Repeater {
                    model: root.gizmoModes

                    delegate: Rectangle {
                        width: Theme.dp(36)
                        height: Theme.dp(28)
                        radius: Theme.radiusSm
                        color: viewportItem.gizmoMode === modelData.mode
                               ? Theme.alpha(Theme.accentInk, 0.28)
                               : Theme.alpha(Theme.surface2, 0.65)
                        border.color: viewportItem.gizmoMode === modelData.mode
                                      ? Theme.accentInk
                                      : Theme.borderSubtle
                        border.width: 1

                        Icon {
                            anchors.centerIn: parent
                            name: modelData.icon
                            size: Theme.dp(15)
                            color: viewportItem.gizmoMode === modelData.mode
                                   ? Theme.accentInk : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (typeof screenOrientation !== "undefined" && screenOrientation)
                                    screenOrientation.vibrateTouch(15)
                                viewportItem.gizmoMode = modelData.mode
                            }
                        }
                    }
                }
            }
        }

        // Right Thumbstick (Fly / Dolly Camera)
        NavPad {
            id: flyPad
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: Theme.dp(12)
            anchors.bottomMargin: Theme.dp(10)
            width: Theme.dp(84)
            height: Theme.dp(84)
            navMode: 1
            allowModeToggle: false
            showModeHint: false

            onPanRequested: (vx, vy) => viewportItem.panCamera(vx, vy)
            onDollyRequested: (fwd, strafe) => viewportItem.dollyCamera(fwd, strafe)
        }
    }

    // ── Floating gizmo pad for precise object transforms ───────────────────
    // Touch-first replacement for dragging a 3D gizmo handle, which a thumb is
    // always covering. It only appears once a transform mode is armed AND an
    // object is selected, so it never competes with camera navigation, and its
    // axis buttons repeat while held — the touch equivalent of keeping a desktop
    // gizmo drag alive. It pairs with the centre mode pill, which arms the mode.
    GizmoPad {
        id: gizmoPad
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.consoleHeight + Theme.dp(8)
        cellSize: Theme.dp(30)
        gizmoMode: viewportItem.gizmoMode
        hasSelection: viewportItem.selectedObject >= 0
        visible: root.controlsVisible && root.studioFits
                 && viewportItem.gizmoMode > 0
                 && viewportItem.selectedObject >= 0
                 && !root.inAnyMeshEdit
        z: 13
        onNudged: (axis, steps) => viewportItem.nudgeSelected(axis, steps)
        onModeCycleRequested: {
            if (typeof screenOrientation !== "undefined" && screenOrientation)
                screenOrientation.vibrateTouch(15)
            viewportItem.gizmoMode = (viewportItem.gizmoMode + 1) % 4
        }
        onModeRequested: (mode) => viewportItem.gizmoMode = mode
    }

    // ── Outliner (left, full height, over the live viewport) ──────────────
    HierarchyDrawer {
        id: hierarchyDrawer
        y: 0
        height: root.height
        width: Math.min(Theme.dp(300), root.width * 0.38)
        host: viewportItem
        open: root.outlinerOpen
        x: root.outlinerOpen ? 0 : -width - Theme.dp(4)
        z: 22

        Behavior on x { NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic } }

        onCloseRequested: root.outlinerOpen = false
        onObjectSelected: (i) => viewportItem.selectObject(i)
        onObjectVisibilityToggled: (i) => viewportItem.toggleObjectVisibility(i)
        onObjectFocusRequested: (i) => viewportItem.focusObject(i)
        onObjectDeleteRequested: (i) => viewportItem.deleteObject(i)
        onAddObjectRequested: {
            root.outlinerOpen = false
            placeObjectDialog.openDialog()
        }
    }

    // Scrim behind whichever dock is open, so the 3D scene stays visible but
    // visibly inactive, and a tap outside closes the dock.
    Rectangle {
        anchors.fill: parent
        z: 21
        visible: root.outlinerOpen || root.inspectorOpen
        color: "#000000"
        opacity: visible ? 0.30 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.outlinerOpen = false
                root.inspectorOpen = false
                root.inspectorMaximized = false
            }
        }
    }

    // ── Inspector (right, full height, over the live viewport) ────────────
    InspectorPanel {
        id: inspectorPanel
        y: 0
        height: root.height
        host: viewportItem
        maximized: root.inspectorMaximized
        x: root.inspectorOpen ? root.width - width : root.width + Theme.dp(4)
        z: 23

        Behavior on x { NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic } }

        onMaximizeRequested: (value) => root.inspectorMaximized = value

        onOpenWaterStudioRequested: {
            root.inspectorOpen = false
            root.waterStudioOpen = true
        }

        onCloseRequested: {
            root.inspectorOpen = false
            root.inspectorMaximized = false
        }
    }

    // ── WaterMesh / Fluid Studio Panel (right dock over the live viewport) ───
    WaterMeshStudioPanel {
        id: waterStudioPanel
        host: viewportItem
        open: root.waterStudioOpen && viewportItem.isSelectedWater
        dockWidth: Math.min(Theme.dp(340), root.width * 0.85)
        onCloseRequested: root.waterStudioOpen = false
    }

    // ── Overflow menu: the long tail of viewport controls ─────────────────
    BottomSheet {
        id: viewportMenu
        title: qsTr("Viewport")
        peekHeight: Math.min(Theme.dp(320), root.height * 0.78)
        expandedHeight: root.height * 0.9
        z: 40

        Column {
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(Theme.paddingScreen)
            anchors.rightMargin: Theme.dp(Theme.paddingScreen)
            spacing: Theme.dp(Theme.spacingSm)

            FieldLabel { text: qsTr("Camera") }

            Row {
                width: parent.width
                spacing: Theme.dp(Theme.spacingSm)

                Repeater {
                    model: [
                        { name: qsTr("Iso"),   key: "iso"   },
                        { name: qsTr("Top"),   key: "top"   },
                        { name: qsTr("Front"), key: "front" },
                        { name: qsTr("Side"),  key: "side"  }
                    ]

                    delegate: Chip {
                        width: (viewportMenu.width - Theme.dp(Theme.paddingScreen) * 2
                                - Theme.dp(Theme.spacingSm) * 3) / 4
                        text: modelData.name
                        iconName: "camera"
                        onClicked: {
                            viewportItem.setCameraPreset(modelData.key)
                            viewportMenu.close()
                        }
                    }
                }
            }

            MenuRow {
                width: parent.width
                iconName: "zoom-in"
                text: qsTr("Dolly in")
                onClicked: viewportItem.dollyCamera(1.0, 0.0)
            }

            MenuRow {
                width: parent.width
                iconName: "zoom-out"
                text: qsTr("Dolly out")
                onClicked: viewportItem.dollyCamera(-1.0, 0.0)
            }

            MenuRow {
                width: parent.width
                iconName: "refresh"
                text: qsTr("Reset camera")
                onClicked: {
                    viewportItem.resetCamera()
                    viewportMenu.close()
                }
            }

            MenuRow {
                width: parent.width
                iconName: "target"
                text: qsTr("Frame selected object")
                enabled: viewportItem.selectedObject >= 0
                onClicked: {
                    viewportItem.focusObject(viewportItem.selectedObject)
                    viewportMenu.close()
                }
            }

            MenuRow {
                width: parent.width
                visible: !root.isModel
                iconName: "plus"
                text: qsTr("Place / Import object...")
                onClicked: {
                    viewportMenu.close()
                    placeObjectDialog.openDialog()
                }
            }

            MenuRow {
                width: parent.width
                visible: !root.isModel
                iconName: "copy"
                text: qsTr("Duplicate selected object")
                enabled: viewportItem.selectedObject >= 0
                onClicked: {
                    viewportItem.duplicateObject(viewportItem.selectedObject)
                    viewportMenu.close()
                }
            }

            MenuRow {
                width: parent.width
                visible: !root.isModel
                iconName: "trash"
                danger: true
                text: qsTr("Delete selected object")
                enabled: viewportItem.selectedObject >= 0
                onClicked: {
                    viewportItem.deleteObject(viewportItem.selectedObject)
                    viewportMenu.close()
                }
            }

            FieldLabel { text: qsTr("Tools & Scripts") }

            MenuRow {
                width: parent.width
                iconName: "file-text"
                text: qsTr("Open File in Code Editor")
                onClicked: {
                    viewportMenu.close()
                    if (typeof screenOrientation !== "undefined" && screenOrientation)
                        screenOrientation.lockPortrait()
                    if (typeof androidContext !== "undefined" && androidContext)
                        androidContext.lockPortrait()
                    root.openScriptRequested(root.scenePath)
                }
            }

            MenuRow {
                width: parent.width
                visible: !root.isModel && typeof rubyFileModel !== "undefined" && rubyFileModel && rubyFileModel.playInGameAvailable
                iconName: "play"
                text: qsTr("Play Scene in Game")
                onClicked: {
                    viewportMenu.close()
                    if (viewportItem.meshEditActive) {
                        viewportItem.applyMeshEdit()
                    }
                    if (viewportItem.waterEditActive) {
                        viewportItem.applyWaterMeshEdit()
                    }
                    if (viewportItem.dirty) {
                        viewportItem.saveScene()
                    }
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                        rubyFileModel.startSceneInGame(root.scenePath)
                    }
                }
            }

            MenuRow {
                width: parent.width
                visible: !root.isModel
                iconName: "braces"
                text: qsTr("Extract Embedded Lua Script")
                onClicked: {
                    viewportMenu.close()
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
                        var extracted = rubyFileModel.extractLuaFromScene(root.scenePath)
                        if (extracted && extracted.length > 0) {
                            if (typeof screenOrientation !== "undefined" && screenOrientation)
                                screenOrientation.lockPortrait()
                            if (typeof androidContext !== "undefined" && androidContext)
                                androidContext.lockPortrait()
                            root.openScriptRequested(extracted)
                        }
                    }
                }
            }

            Column {
                width: parent.width
                visible: root.isModel
                spacing: Theme.dp(Theme.spacingXs)

                FieldLabel { text: qsTr("Model Information") }

                Text {
                    text: qsTr("File: %1").arg(root.sceneName)
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                }
                Text {
                    text: qsTr("Meshes: %1 · Vertices: %2 · Triangles: %3")
                          .arg(viewportItem.meshCount)
                          .arg(viewportItem.vertexCount)
                          .arg(viewportItem.triangleCount)
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                }
                Text {
                    visible: viewportItem.frameCount > 1
                    text: qsTr("Animation frames: %1").arg(viewportItem.frameCount)
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.accentInk
                }
            }
        }
    }

    // ── Model animation playback bar (only visible when viewing an animated POD) ──
    GlassmorphicOverlay {
        id: animBar
        visible: root.isModel && viewportItem.frameCount > 1
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: controlDeck.top
        anchors.bottomMargin: Theme.dp(Theme.spacingSm)
        height: Theme.dp(40)
        width: Math.min(root.width * 0.65, Theme.dp(380))
        z: 15

        property bool isPlaying: false

        Timer {
            id: animTimer
            interval: 33 // ~30 fps
            running: animBar.visible && animBar.isPlaying
            repeat: true
            onTriggered: {
                var next = viewportItem.frame + 1.0
                if (next >= viewportItem.frameCount) next = 0.0
                viewportItem.frame = next
            }
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.dp(Theme.spacingSm)
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            spacing: Theme.dp(Theme.spacingSm)

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: animBar.isPlaying ? "pause" : "play"
                buttonSize: Theme.dp(30)
                iconSize: Theme.dp(15)
                onClicked: animBar.isPlaying = !animBar.isPlaying
            }

            Slider {
                id: frameSlider
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - Theme.dp(30) - Theme.dp(60) - Theme.dp(Theme.spacingSm) * 2
                from: 0
                to: (viewportItem && typeof viewportItem.frameCount !== "undefined") ? Math.max(1, viewportItem.frameCount - 1) : 1
                value: (viewportItem && typeof viewportItem.frame !== "undefined") ? viewportItem.frame : 0
                onMoved: {
                    if (viewportItem && typeof viewportItem.frame !== "undefined") {
                        viewportItem.frame = value
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: ((viewportItem && typeof viewportItem.frame !== "undefined") ? Math.round(viewportItem.frame) : 0) + "/" + ((viewportItem && typeof viewportItem.frameCount !== "undefined") ? Math.max(0, viewportItem.frameCount - 1) : 0)
                font.pixelSize: Theme.dp(Theme.fontXs)
                color: Theme.textMuted
            }
        }
    }

    // ── Toast ──────────────────────────────────────────────────────────────
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: controlDeck.top
        anchors.bottomMargin: Theme.dp(Theme.spacingMd)
        width: toastRow.implicitWidth + Theme.dp(Theme.spacingLg) * 2
        height: Theme.dp(36)
        radius: Theme.radiusPill
        color: root.toastIsError ? Theme.alpha(Theme.colorError, 0.92) : Theme.alpha(Theme.surface3, 0.94)
        border.color: root.toastIsError ? Theme.alpha(Theme.colorErrorBright, 0.6) : Theme.borderStrong
        border.width: 1
        z: 30
        visible: opacity > 0.0
        opacity: root.toastText.length > 0 ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

        Row {
            id: toastRow
            anchors.centerIn: parent
            spacing: Theme.dp(6)

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.toastIsError ? "alert" : "check-circle"
                size: Theme.dp(Theme.iconSm)
                color: root.toastIsError ? "#FFFFFF" : Theme.colorSuccessBright
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.toastText
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textPrimary
            }
        }
    }

    // ── Unsaved Scene Changes Popup ─────────────────────────────────────────
    Popup {
        id: unsavedSceneDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - Theme.dp(48) : Theme.dp(340), Theme.dp(380))
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: Theme.dp(Theme.spacingLg)

        Overlay.modal: Rectangle { color: "#B3000000" }

        background: Rectangle {
            radius: Theme.radiusCard
            color: Theme.surface1
            border.color: Theme.border
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)

            Row {
                spacing: Theme.dp(Theme.spacingSm)
                Rectangle {
                    width: Theme.dp(32)
                    height: Theme.dp(32)
                    radius: Theme.radiusSm
                    color: Theme.alpha(Theme.colorWarning, 0.25)
                    border.color: Theme.colorWarning
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter
                    Icon {
                        anchors.centerIn: parent
                        name: "alert"
                        size: Theme.dp(16)
                        color: Theme.colorWarning
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Unsaved Changes")
                    font.pixelSize: Theme.dp(Theme.fontLg)
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }
            }

            Text {
                width: parent.width
                text: qsTr("You have unsaved changes in this scene. Would you like to save before opening the FileRift code editor?")
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.dp(Theme.spacingSm)

                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: unsavedSceneDialog.close()
                }

                Button {
                    text: qsTr("Discard")
                    flat: true
                    onClicked: {
                        unsavedSceneDialog.close()
                        root.proceedToCodeEditor()
                    }
                }

                Button {
                    text: qsTr("Save & Open")
                    highlighted: true
                    onClicked: {
                        viewportItem.saveScene()
                        unsavedSceneDialog.close()
                        root.proceedToCodeEditor()
                    }
                }
            }
        }
    }

    // ── Dialog: Unsaved changes on Exit ─────────────────────────────────────
    Dialog {
        id: unsavedExitDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.dp(48), Theme.dp(360))
        modal: true
        title: qsTr("Unsaved Changes")
        standardButtons: Dialog.NoButton

        Column {
            width: parent.width
            spacing: Theme.dp(Theme.spacingMd)

            Text {
                width: parent.width
                text: qsTr("You have unsaved changes in %1. Would you like to save before exiting?").arg(root.sceneName.length > 0 ? root.sceneName : qsTr("this scene"))
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.dp(Theme.spacingSm)

                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: unsavedExitDialog.close()
                }

                Button {
                    text: qsTr("Discard")
                    flat: true
                    onClicked: {
                        unsavedExitDialog.close()
                        root.doExit()
                    }
                }

                Button {
                    text: qsTr("Save & Exit")
                    highlighted: true
                    onClicked: {
                        viewportItem.saveScene()
                        unsavedExitDialog.close()
                        root.doExit()
                    }
                }
            }
        }
    }

    // ── Place / Import Object Dialog ─────────────────────────────────────────
    PlaceObjectDialog {
        id: placeObjectDialog
        viewportItem: viewportItem
        onObjectImported: (name, type) => {
            if (typeof screenOrientation !== "undefined" && screenOrientation)
                screenOrientation.vibrateTouch(25)
            if (viewportItem.selectedObject >= 0) {
                viewportItem.focusObject(viewportItem.selectedObject)
            }
            root.showToast(qsTr("Placed %1 (%2)").arg(name).arg(type), false)
        }
    }

    // ── Smooth loading screen for transitions & initial render ─────────────
    SceneLoadingOverlay {
        id: loadingOverlay
        anchors.fill: parent
        z: 95
        loading: root.viewportLoading || viewportItem.isSceneLoading
        sceneName: root.sceneName
        statusText: viewportItem.isSceneLoading && viewportItem.loadingStatus.length > 0 ? viewportItem.loadingStatus : qsTr("Preparing 3D Viewport...")
    }

    // ── Landscape gate — non-blocking so scenes always open freely ───────
    OrientationGate {
        anchors.fill: parent
        z: 100
        blocking: false
        visible: false
    }
}
