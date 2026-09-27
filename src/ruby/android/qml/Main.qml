import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import "."
import "components"
import "views"

ApplicationWindow {
    id: window

    visible: true
    width: 412
    height: 915
    title: qsTr("Ruby Touch (Ruby Mobile)")
    color: Theme.surface0

    // ── Material 3 dark, keyed to the promoted QSS palette ─────────────────
    // The whole app is Qt Quick Controls Material; without this the framework
    // falls back to its own default purple-blue and the M3 look fights the
    // theme. Every colour here comes from Theme, so there is one source.
    Material.theme: Material.Dark
    Material.primary: Theme.accentStart
    Material.accent: Theme.accentEnd
    Material.background: Theme.surface0
    Material.foreground: Theme.textPrimary

    // ── Responsive design units ────────────────────────────────────────────
    // Android density is already handled by Qt's high-DPI scaling; this only
    // adapts the layout rhythm to the physical width so a 360dp and a 480dp
    // device both get comfortable touch targets.
    //
    // The SHORTER dimension is the reference, not the width: the scene editor
    // forces the window into landscape, and keying off the width there would
    // scale every metric by the aspect ratio (a 915x412 landscape window would
    // ask for 2.2x) and blow up the whole studio. The short side is ~412dp in
    // both orientations on a phone, so the rhythm is continuous across a
    // rotation instead of jumping.
    function updateScale() {
        var reference = Math.min(width, height)
        Theme.scaleFactor = Math.max(0.86, Math.min(1.30, reference / 412.0))
    }

    Component.onCompleted: updateScale()
    onWidthChanged: updateScale()
    onHeightChanged: updateScale()

    // ── Global Android Back Button Handling ────────────────────────────────
    function switchTab(index) {
        bottomBar.currentIndex = index
        tabLayout.currentIndex = index
    }

    Item {
        focus: true
        Keys.onBackPressed: (event) => {
            if (mainStack.depth > 1) {
                var current = mainStack.currentItem
                if (current && typeof current.handleBack === "function") {
                    current.handleBack()
                    event.accepted = true
                    return
                }
                mainStack.pop()
                event.accepted = true
            } else {
                var currentTab = (tabLayout && typeof tabLayout.itemAt === "function")
                                 ? tabLayout.itemAt(tabLayout.currentIndex)
                                 : (tabLayout.children ? tabLayout.children[tabLayout.currentIndex] : null)
                if (currentTab && typeof currentTab.handleBack === "function") {
                    if (currentTab.handleBack()) {
                        event.accepted = true
                        return
                    }
                }
                if (tabLayout.currentIndex !== 0) {
                    window.switchTab(0)
                    event.accepted = true
                    return
                }

                // Gracefully move app to background without destroying RAM state
                if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.moveToBack === "function") {
                    androidContext.moveToBack()
                    event.accepted = true
                    return
                }
            }
        }
    }

    // ── Global Fullscreen StackView (deep views) ───────────────────────────
    StackView {
        id: mainStack
        anchors.fill: parent
        initialItem: mainTabsView
        focus: true

        pushEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durMed }
            NumberAnimation { property: "x"; from: mainStack.width * 0.12; to: 0; duration: Theme.durSlow; easing.type: Easing.OutCubic }
        }
        pushExit: Transition {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durMed }
        }
        popEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durMed }
        }
        popExit: Transition {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durMed }
            NumberAnimation { property: "x"; from: 0; to: mainStack.width * 0.12; duration: Theme.durSlow; easing.type: Easing.OutCubic }
        }

        onDepthChanged: {
            bottomBar.visible = (mainStack.depth === 1)
        }
    }

    // ── Main 4-tab surface ─────────────────────────────────────────────────
    Item {
        id: mainTabsView
        anchors.fill: parent
        anchors.topMargin: (typeof androidContext !== "undefined" && androidContext) ? androidContext.safeInsetTop : 0

        StackLayout {
            id: tabLayout
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: bottomBar.top
            currentIndex: bottomBar.currentIndex

            HomeView {
                id: homeView
                onOpenFilesRequested: window.switchTab(1)
                onOpenFolderRequested: (folderPath) => {
                    if (typeof rubyFileModel !== "undefined" && rubyFileModel && folderPath) {
                        rubyFileModel.navigateTo(folderPath)
                    }
                    window.switchTab(1)
                }
                onOpenPathRequested: (path) => {
                    window.openFileByExtension(path)
                }
                onOpenToolsRequested: (toolName) => {
                    window.switchTab(2)
                    if (toolName && toolName.length > 0) {
                        toolsView.activeTool = toolName
                    }
                }
                onOpenSettingsRequested: window.switchTab(3)
                onNewFileWizardRequested: {
                    window.switchTab(1)
                    filesView.openNewFileWizard()
                }
            }

            FilesView {
                id: filesView
                onFileOpened: (path, fileType) => {
                    window.openFileByType(path, fileType)
                }
                onModelConvertRequested: (path) => {
                    window.switchTab(2)
                    toolsView.activeTool = "converter"
                }
            }

            ToolsView {
                id: toolsView
                onOpenMeshStudioRequested: {
                    mainStack.push(groundMeshStudioComponent)
                }
            }

            SettingsView {
                id: settingsView
            }
        }

        BottomNavBar {
            id: bottomBar
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 0
            anchors.left: parent.left
            anchors.right: parent.right
            currentIndex: 0 // Default to Home dashboard

            onTabSelected: (index) => {
                window.switchTab(index)
            }
        }
    }

    // ── Deep navigation helpers (contract unchanged) ───────────────────────
    function openFileByType(path, fileType) {
        if (typeof rubyFileModel !== "undefined" && rubyFileModel && path) {
            rubyFileModel.addRecentFile(path)
        }
        var t = (fileType || "").toLowerCase()
        if (t === "scene") {
            mainStack.push(sceneViewComponent, { scenePath: path, isModel: false })
        } else if (t === "scl" || t === "code") {
            if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                androidContext.openCodeEditor(path)
            } else {
                mainStack.push(codeViewComponent, { filePath: path })
            }
        } else if (t === "texture") {
            mainStack.push(textureViewComponent, { filePath: path })
        } else if (t === "audio") {
            mainStack.push(audioViewComponent, { filePath: path })
        } else if (t === "model") {
            mainStack.push(sceneViewComponent, { scenePath: path, isModel: true })
        } else {
            if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                androidContext.openCodeEditor(path)
            } else {
                mainStack.push(codeViewComponent, { filePath: path })
            }
        }
    }

    function openFileByExtension(path) {
        var low = path.toLowerCase()
        if (low.endsWith(".scene")) {
            openFileByType(path, "scene")
        } else if (low.endsWith(".scl")) {
            openFileByType(path, "scl")
        } else if (low.endsWith(".pvr") || low.endsWith(".tex") || low.endsWith(".png") || low.endsWith(".jpg")) {
            openFileByType(path, "texture")
        } else if (low.endsWith(".wav") || low.endsWith(".ogg") || low.endsWith(".mp3")) {
            openFileByType(path, "audio")
        } else if (low.endsWith(".pod") || low.endsWith(".glb") || low.endsWith(".fbx") || low.endsWith(".obj")) {
            openFileByType(path, "model")
        } else {
            openFileByType(path, "code")
        }
    }

    Connections {
        target: (typeof screenOrientation !== "undefined" && screenOrientation) ? screenOrientation : null
        function onFileOpenRequested(path) {
            window.openFileByExtension(path)
        }
    }

    Component {
        id: sceneViewComponent
        SceneViewportView {
            onBackRequested: mainStack.pop()
            onOpenScriptRequested: (path) => {
                if (typeof androidContext !== "undefined" && androidContext && androidContext.supported) {
                    androidContext.openCodeEditor(path)
                } else {
                    mainStack.push(codeViewComponent, { filePath: path, fromVisual: true })
                }
            }
        }
    }

    Component {
        id: textureViewComponent
        TextureViewerView {
            onBackRequested: mainStack.pop()
        }
    }

    Component {
        id: audioViewComponent
        AudioViewerView {
            onBackRequested: mainStack.pop()
        }
    }

    Component {
        id: codeViewComponent
        CodeEditorView {
            onBackRequested: mainStack.pop()
            onSwitchToVisualRequested: (path) => {
                if (fromVisual) {
                    mainStack.pop()
                } else {
                    mainStack.push(sceneViewComponent, { scenePath: path, isModel: false })
                }
            }
        }
    }

    Component {
        id: groundMeshStudioComponent
        GroundMeshStudioView {
            onBackRequested: mainStack.pop()
        }
    }
}
