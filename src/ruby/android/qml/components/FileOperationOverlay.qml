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
        height: progressColumn.implicitHeight + Theme.dp(Theme.spacingLg * 2)
        radius: Theme.radiusLg
        color: Theme.surface1
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            id: progressColumn
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            spacing: Theme.dp(Theme.spacingMd)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.dp(Theme.spacingMd)

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: Theme.dp(36)
                    height: width
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)

                    Text {
                        Layout.fillWidth: true
                        text: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                              ? rubyFileModel.operationTitle : ""
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                              ? rubyFileModel.operationDetail : ""
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                        visible: text.length > 0
                    }
                }

                IconButton {
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "close"
                    iconSize: Theme.dp(16)
                    buttonSize: Theme.dp(32)
                    onClicked: rubyFileModel.cancelFileOperation()
                }
            }

            Rectangle {
                id: progressTrack
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(6)
                radius: height / 2
                color: Theme.surface3

                Rectangle {
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
                    visible: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                             && rubyFileModel.operationProgress < 0
                    width: progressTrack.width * 0.3
                    height: parent.height
                    radius: height / 2
                    color: Theme.accentStart
                    opacity: 0.85

                    SequentialAnimation on x {
                        running: visible
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.dp(Theme.spacingSm)

                Text {
                    text: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                          ? rubyFileModel.operationItemsStr : ""
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textSecondary
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: (typeof rubyFileModel !== "undefined" && rubyFileModel)
                          ? rubyFileModel.operationBytesStr : ""
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.textMuted
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
        height: conflictColumn.implicitHeight + Theme.dp(Theme.spacingLg * 2)
        radius: Theme.radiusSheet
        color: Theme.surface1
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            id: conflictColumn
            anchors.fill: parent
            anchors.margins: Theme.dp(Theme.spacingLg)
            spacing: Theme.dp(Theme.spacingSm)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.dp(Theme.spacingMd)

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    width: Theme.dp(36)
                    height: width
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.dp(2)

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("“%1” already exists").arg(
                                  (typeof rubyFileModel !== "undefined" && rubyFileModel)
                                  ? rubyFileModel.conflictTargetName : "")
                        font.pixelSize: Theme.dp(Theme.fontLg)
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Choose what to do with the existing item in %1.")
                              .arg((typeof rubyFileModel !== "undefined" && rubyFileModel)
                                   ? rubyFileModel.conflictTargetDir : "")
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                }
            }

            MenuRow {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(48)
                Layout.topMargin: Theme.dp(Theme.spacingSm)
                iconName: "refresh"
                accent: true
                text: qsTr("Replace the existing item")
                onClicked: root.choose(0)
            }

            MenuRow {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(48)
                iconName: "copy"
                text: qsTr("Keep both (add “ (1)” to the new name)")
                onClicked: root.choose(2)
            }

            MenuRow {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(48)
                iconName: "arrow-right"
                text: qsTr("Skip this item")
                onClicked: root.choose(1)
            }

            MenuRow {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dp(48)
                iconName: "close"
                danger: true
                text: qsTr("Cancel the whole operation")
                onClicked: root.choose(3)
            }

            // Apply-to-all toggle
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.dp(Theme.spacingXs)

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Remember this choice for the rest of the operation")
                    font.pixelSize: Theme.dp(Theme.fontSm)
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    id: applyToggle
                    width: Theme.dp(40)
                    height: Theme.dp(22)
                    radius: height / 2
                    color: root.applyAll ? Theme.accentStart : Theme.surface3
                    border.color: root.applyAll ? Theme.accentEnd : Theme.borderStrong
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

                    Rectangle {
                        width: Theme.dp(18)
                        height: width
                        radius: width / 2
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
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: Theme.durMed } }

        property string message: ""

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
