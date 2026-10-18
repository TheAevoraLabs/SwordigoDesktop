import QtQuick
import QtQuick.Controls
import ".."

// OperationsBar.qml — Multi-select batch operations & transfer tracking dock
// Adapted from HyprFM for mobile ergonomics.
Rectangle {
    id: root

    property bool active: selectedCount > 0 || isBusy
    property int selectedCount: 0
    property string targetPath: ""
    property string targetFileName: ""
    property string totalSizeStr: ""
    property bool isBusy: false
    property string statusText: ""

    signal closeRequested()
    signal zipRequested(string path)
    signal copyRequested(string path)
    signal cutRequested(string path)
    signal deleteRequested(string path)
    signal selectAllRequested()
    signal clearSelectionRequested()

    height: active ? Theme.dp(60) : 0
    visible: height > 0
    color: Theme.surface2
    border.color: Theme.borderStrong
    border.width: 1
    radius: Theme.radiusMd
    clip: true

    Behavior on height {
        NumberAnimation {
            duration: Theme.durMed
            easing.type: Easing.OutCubic
        }
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.spacingMd)
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        visible: root.active

        // Left Side: Counter / Busy indicator
        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(Theme.spacingSm)

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                buttonSize: Theme.dp(32)
                iconSize: Theme.dp(16)
                square: true
                onClicked: root.closeRequested()
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.dp(1)

                Text {
                    text: root.isBusy ? (root.statusText.length > 0 ? root.statusText : qsTr("Working…"))
                                      : (root.totalSizeStr.length > 0 ? qsTr("%1 Selected (%2)").arg(root.selectedCount).arg(root.totalSizeStr)
                                                                      : qsTr("%1 Selected").arg(root.selectedCount))
                    font.pixelSize: Theme.dp(Theme.fontMd)
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }

                Text {
                    visible: !root.isBusy && root.targetFileName.length > 0
                    text: root.targetFileName
                    font.pixelSize: Theme.dp(Theme.fontXs)
                    color: Theme.accentInk
                    elide: Text.ElideMiddle
                    width: Theme.dp(110)
                }
            }
        }

        // Right Side: Quick Action Buttons
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.dp(Theme.spacingSm)

            // Compress to ZIP
            IconButton {
                iconName: "archive"
                tint: Theme.colorArchive
                plate: Theme.alpha(Theme.colorArchive, 0.15)
                plateEdge: Theme.alpha(Theme.colorArchive, 0.35)
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: {
                    if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                        if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                            androidContext.vibrate(15)
                        }
                    }
                    root.zipRequested(root.targetPath)
                }
            }

            // Copy
            IconButton {
                iconName: "copy"
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: {
                    if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                        if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                            androidContext.vibrate(15)
                        }
                    }
                    root.copyRequested(root.targetPath)
                }
            }

            // Cut
            IconButton {
                iconName: "scissors"
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: {
                    if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                        if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                            androidContext.vibrate(15)
                        }
                    }
                    root.cutRequested(root.targetPath)
                }
            }

            // Delete
            IconButton {
                iconName: "trash"
                tint: Theme.colorErrorBright
                plate: Theme.alpha(Theme.colorError, 0.15)
                plateEdge: Theme.alpha(Theme.colorError, 0.35)
                buttonSize: Theme.dp(38)
                iconSize: Theme.dp(18)
                square: true
                onClicked: {
                    if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                        if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                            androidContext.vibrate(20)
                        }
                    }
                    root.deleteRequested(root.targetPath)
                }
            }
        }
    }
}
