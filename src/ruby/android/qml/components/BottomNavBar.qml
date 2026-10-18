import QtQuick
import QtQuick.Controls
import ".."

// Material 3 Native Edge-to-Edge Navigation Bar for Ruby Touch
// Contract preserved: `currentIndex` + tabSelected(int).
Rectangle {
    id: root

    readonly property real safeBottom: (typeof androidContext !== "undefined" && androidContext) ? androidContext.safeInsetBottom : 0
    height: Theme.dp(72) + safeBottom
    color: Theme.surface1
    z: 20

    property int currentIndex: 0 // Default to Home

    signal tabSelected(int index)

    readonly property var tabs: [
        { name: qsTr("Home"),     icon: "home"   },
        { name: qsTr("Files"),    icon: "folder" },
        { name: qsTr("Tools"),    icon: "tune"   },
        { name: qsTr("Settings"), icon: "gear"   }
    ]

    // Subtle 1px top divider line (no boxy borders around the bar)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.borderSubtle
    }

    Row {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.dp(72)

        Repeater {
            model: root.tabs

            Item {
                id: tabItem
                width: root.width / root.tabs.length
                height: parent.height

                readonly property bool active: root.currentIndex === index

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.dp(4)

                    // M3 Active Pill: Wraps ONLY the icon (56dp x 30dp)
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.dp(56)
                        height: Theme.dp(30)
                        radius: Theme.dp(15)
                        color: tabItem.active ? Theme.alpha(Theme.accentInk, 0.18) : "transparent"

                        Behavior on color { ColorAnimation { duration: Theme.durMed } }

                        Icon {
                            anchors.centerIn: parent
                            name: modelData.icon
                            size: Theme.dp(Theme.iconMd)
                            weight: tabItem.active ? 2.2 : Icons.stroke
                            color: tabItem.active ? Theme.accentInk : Theme.textMuted
                            scale: tabMouse.pressed ? 0.92 : 1.0

                            Behavior on color { ColorAnimation { duration: Theme.durMed } }
                            Behavior on scale { NumberAnimation { duration: 100 } }
                        }
                    }

                    // Tab Text Label: Cleanly sits below the active pill
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.name
                        font.pixelSize: Theme.dp(11)
                        font.weight: tabItem.active ? Font.DemiBold : Font.Normal
                        color: tabItem.active ? Theme.textPrimary : Theme.textMuted

                        Behavior on color { ColorAnimation { duration: Theme.durMed } }
                    }
                }

                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    onClicked: {
                        if (root.currentIndex !== index) {
                            if (typeof rubySettings !== "undefined" && rubySettings && rubySettings.hapticsEnabled) {
                                if (typeof androidContext !== "undefined" && androidContext && typeof androidContext.vibrate === "function") {
                                    androidContext.vibrate(15)
                                }
                            }
                            root.currentIndex = index
                            root.tabSelected(index)
                        }
                    }
                }
            }
        }
    }
}
