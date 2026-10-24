import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// FileOperationOverlay.qml — GUI for the worker-thread file engine.
//
// The engine never touches the GUI thread, so this overlay is the only thing
// the user sees while a copy/move/delete/zip/extract runs: a determinate
// progress card with a cancel button, a collision prompt that pauses the
// worker, and a transient result banner. Modelled on KIO's job progress +
// overwrite dialog that Index FM gets from MauiKit.
//
// Every card uses FIXED heights and plain Rows/Columns. Deriving a card's
// height from ColumnLayout.implicitHeight while the layout fills the card is a
// QML binding loop (and a 100% Qt thread spin) on Qt 6.6, so the metrics below
// are explicit numbers.
Item {
    id: root
    anchors.fill: parent

    property bool busy: typeof rubyFileModel !== "undefined" && rubyFileModel
                        && rubyFileModel.operationBusy
    property bool conflict: typeof rubyFileModel !== "undefined" && rubyFileModel
                            && rubyFileModel.conflictActive
    property bool applyAll: true
    property real safeBottom: (typeof androidContext !== "undefined" && androidContext)
                              ? androidContext.safeInsetBottom : 0

    visible: busy || conflict || resultBanner.opacity > 0
    z: 400

    // ── Scrim ──────────────────────────────────────────────────────────────
    Rectangle {
        id: scrim
        anchors.fill: parent
        visible: root.busy || root.conflict
        color: "#B3000000"

        MouseArea {
            anchors.fill: parent
            // Consume touches; the operation must be dismissed explicitly.
            onClicked: {}
        }
    }

    // ── Progress card ──────────────────────────────────────────────────────
    Rectangle {
        id: progressCard
        visible: root.busy && !root.conflict
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.dp(24) + root.safeBottom
        width: parent.width - Theme.dp(Theme.paddingScreen * 2)
        height: progressColumn.height + Theme.dp(Theme.spacingLg * 2)
        radius: Theme.radiusLg
        color: Theme.surface1
        border.color: Theme.border
        border.width: 1

        Column {
            id: progressColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.dp(Theme.spacingLg)
            height: Theme.dp(112)
            spacing: Theme.dp(Theme.spacingMd)

            Row {
                id: progressHeader
                width: parent.width
                height: Theme.dp(36)
                spacing: Theme.dp(Theme.spacingMd)

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.dp(36)
                    height: Theme.dp(36)
                    radius: Theme.radiusMd
                    color: Theme.alpha(Theme.accentInk, 0.14)
                    border.color: Theme.alpha(Theme.accentInk, 0.28)
                    border.width: 1

                    Icon {
                        anchors.centerIn: parent
                        name: "refresh"
                        size: Theme.dp(Theme.iconSm)
                        color: Theme.accentInk
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: progressHeader.width - Theme.dp(36) - Theme.dp(32)
                            - Theme.dp(Theme.spacingMd)
                    spacing: Theme.dp(2)

                    Text {
                        width: parent.width
                        text: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                              ? rubyFileModel.operationTitle : ""
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        height: Theme.dp(18)
                    }

                    Text {
                        width: parent.width
                        text: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                              ? rubyFileModel.operationDetail : ""
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                        height: visible ? Theme.dp(15) : 0
                        visible: text.length > 0
                    }
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    iconName: "close"
                    iconSize: Theme.dp(16)
                    buttonSize: Theme.dp(32)
                    onClicked: rubyFileModel.cancelFileOperation()
                }
            }

            Rectangle {
                id: progressTrack
                width: progressColumn.width
                height: Theme.dp(6)
                radius: height / 2
                color: Theme.surface3

                Rectangle {
                    id: progressFill
                    visible: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                             && rubyFileModel.operationProgress >= 0
                    width: Math.max(0, Math.min(1,
                        (typeof rubyFileModel !== "undefined" && rubyFileModel)
                        ? rubyFileModel.operationProgress : 0)) * progressTrack.width
                    height: parent.height
                    radius: height / 2
                    color: Theme.accentStart

                    Behavior on width {
                        NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic }
                    }
                }

                // Indeterminate shimmer while the worker is still planning.
                Rectangle {
                    id: progressSweep
                    visible: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                             && rubyFileModel.operationProgress < 0
                    width: progressTrack.width * 0.3
                    height: parent.height
                    radius: height / 2
                    color: Theme.accentStart
                    opacity: 0.85

                    SequentialAnimation on x {
                        running: progressSweep.visible
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: 0
                            to: progressTrack.width * 0.7
                            duration: 900
                            easing.type: Easing.InOutSine
                        }
                        NumberAnimation {
                            from: progressTrack.width * 0.7
                            to: 0
                            duration: 900
                            easing.type: Easing.InOutSine
                        }
                    }
                }
            }

            Row {
                width: progressColumn.width
                height: Theme.dp(14)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        if (typeof rubyFileModel === "undefined" || !rubyFileModel) return ""
                        var items = rubyFileModel.operationItemsStr
                        var bytes = rubyFileModel.operationBytesStr
                        if (items.length > 0 && bytes.length > 0) return items + "  ·  " + bytes
                        return items.length > 0 ? items : bytes
                    }
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }
    }

    // ── Conflict prompt (pauses the worker) ────────────────────────────────
    Rectangle {
        id: conflictCard
        visible: root.conflict
        anchors.centerIn: parent
        width: parent.width - Theme.dp(Theme.paddingScreen * 2)
        height: conflictColumn.height + Theme.dp(Theme.spacingLg * 2)
        radius: Theme.radiusSheet
        color: Theme.surface1
        border.color: Theme.border
        border.width: 1

        Column {
            id: conflictColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.dp(Theme.spacingLg)
            height: Theme.dp(76) + Theme.dp(Theme.spacingSm)
                    + 4 * Theme.dp(48) + 2 * Theme.dp(Theme.spacingSm)
                    + Theme.dp(Theme.spacingXs) + Theme.dp(40)
            spacing: Theme.dp(Theme.spacingSm)

            Row {
                id: conflictHeader
                width: conflictColumn.width
                height: Theme.dp(76)
                spacing: Theme.dp(Theme.spacingMd)

                Rectangle {
                    anchors.top: parent.top
                    width: Theme.dp(36)
                    height: Theme.dp(36)
                    radius: Theme.radiusMd
                    color: Theme.alpha(Theme.colorWarning, 0.18)
                    border.color: Theme.alpha(Theme.colorWarning, 0.4)
                    border.width: 1

                    Icon {
                        anchors.centerIn: parent
                        name: "alert"
                        size: Theme.dp(Theme.iconMd)
                        color: Theme.colorWarning
                    }
                }

                Column {
                    width: conflictHeader.width - Theme.dp(36) - Theme.dp(Theme.spacingMd)
                    spacing: Theme.dp(4)

                    Text {
                        width: parent.width
                        text: qsTr("“%1” already exists").arg(
                                  (typeof rubyFileModel !== "undefined" && rubyFileModel)
                                  ? rubyFileModel.conflictTargetName : "")
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        height: Theme.dp(18)
                    }

                    Text {
                        width: parent.width
                        text: qsTr("In %1. Choose what to do with the existing item.")
                              .arg((typeof rubyFileModel !== "undefined" && rubyFileModel)
                                   ? rubyFileModel.conflictTargetDir : "")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                        height: Theme.dp(15)
                    }
                }
            }

            MenuRow {
                width: conflictColumn.width
                height: Theme.dp(48)
                iconName: "refresh"
                accent: true
                text: qsTr("Replace the existing item")
                onClicked: root.choose(0)
            }

            MenuRow {
                width: conflictColumn.width
                height: Theme.dp(48)
                iconName: "copy"
                text: qsTr("Keep both (add “ (1)” to the new name)")
                onClicked: root.choose(2)
            }

            MenuRow {
                width: conflictColumn.width
                height: Theme.dp(48)
                iconName: "arrow-right"
                text: qsTr("Skip this item")
                onClicked: root.choose(1)
            }

            MenuRow {
                width: conflictColumn.width
                height: Theme.dp(48)
                iconName: "close"
                danger: true
                text: qsTr("Cancel the whole operation")
                onClicked: root.choose(3)
            }

            // Apply-to-all toggle
            Row {
                width: conflictColumn.width
                height: Theme.dp(40)
                spacing: Theme.dp(Theme.spacingMd)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.dp(40) - Theme.dp(Theme.spacingMd)
                    text: qsTr("Remember this choice for the rest of the operation")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }

                Rectangle {
                    id: applyToggle
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.dp(40)
                    height: Theme.dp(22)
                    radius: height / 2
                    color: root.applyAll ? Theme.accentStart : Theme.surface3
                    border.color: root.applyAll ? Theme.accentEnd : Theme.borderStrong
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

                    Rectangle {
                        width: Theme.dp(18)
                        height: Theme.dp(18)
                        radius: Theme.dp(9)
                        y: Theme.dp(2)
                        x: root.applyAll ? applyToggle.width - width - Theme.dp(2) : Theme.dp(2)
                        color: root.applyAll ? Theme.onAccent : Theme.textMuted

                        Behavior on x {
                            NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.applyAll = !root.applyAll
                    }
                }
            }
        }
    }

    // ── Result banner ──────────────────────────────────────────────────────
    Rectangle {
        id: resultBanner
        property string message: ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.dp(24) + root.safeBottom
        width: Math.min(parent.width - Theme.dp(Theme.paddingScreen * 2), Theme.dp(420))
        height: Theme.dp(44)
        radius: Theme.radiusPill
        color: Theme.surface2
        border.color: Theme.borderStrong
        border.width: 1
        opacity: 0
        visible: opacity > 0.01

        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dp(Theme.spacingLg)
            anchors.right: parent.right
            anchors.rightMargin: Theme.dp(Theme.spacingLg)
            text: resultBanner.message
            font.pixelSize: Theme.dp(Theme.fontSm)
            color: Theme.textPrimary
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: bannerTimer
            interval: 3200
            onTriggered: resultBanner.opacity = 0
        }
    }

    Connections {
        target: (typeof rubyFileModel !== "undefined") ? rubyFileModel : null

        function onOperationFinished(kind, ok, cancelled, summary) {
            resultBanner.message = summary
            resultBanner.opacity = 1
            bannerTimer.restart()
        }
    }

    function choose(decision) {
        if (typeof rubyFileModel !== "undefined" && rubyFileModel) {
            rubyFileModel.resolveFileConflict(decision, root.applyAll)
        }
    }
}
