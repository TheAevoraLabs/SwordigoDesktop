import QtQuick
import QtQuick.Controls
import ".."

// Settings list row: label + optional description on the left, and either a
// Switch, a trailing value, or a chevron on the right.
//
// A row may instead offer `choiceOptions` — a segmented pick between
// alternatives, used by the ground-mesh generator picker. That control renders
// on its own line beneath the text rather than beside it: a phone row is not
// wide enough to hold a two-chip control next to a description that wraps to
// two lines without the two overlapping.
//
// Choices are { value, label } pairs so the row reports a stable value rather
// than a display string (which would be translated) or an index (which would
// reorder the meaning if the list is ever sorted).
Item {
    id: root

    property string title: ""
    property string description: ""
    property string leadingIcon: ""
    property bool hasSwitch: false
    property bool checked: false
    property bool hasChevron: false
    property string trailingText: ""

    /// Segmented alternatives; empty means this row has no choice control.
    property var choiceOptions: []
    /// Currently selected value, compared against each entry's `value`.
    property string choiceValue: ""

    signal toggled(bool checked)
    signal clicked()
    signal choiceSelected(string value)

    readonly property bool hasChoice: choiceOptions.length > 0

    implicitHeight: hasChoice
                    ? Theme.dp(58) + Theme.dp(38)
                    : (description.length > 0 ? Theme.dp(58) : Theme.dp(50))
    width: parent ? parent.width : implicitWidth

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSm
        color: rowArea.pressed ? Theme.alpha(Theme.textPrimary, 0.045) : "transparent"
    }

    Row {
        id: textRow
        anchors.left: parent.left
        anchors.leftMargin: Theme.dp(Theme.spacingSm)
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        // A choice row pushes its text to the top so the chips have the bottom
        // line to themselves; every other row stays vertically centred.
        anchors.top: root.hasChoice ? parent.top : undefined
        anchors.topMargin: Theme.dp(Theme.spacingMd)
        anchors.verticalCenter: root.hasChoice ? undefined : parent.verticalCenter
        spacing: Theme.dp(Theme.spacingMd)

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.leadingIcon.length > 0
            name: root.leadingIcon
            size: Theme.dp(Theme.iconSm)
            color: Theme.textMuted
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            // Reserve room for the switch only when one shares the line.
            width: (root.hasSwitch && !root.hasChoice)
                   ? parent.width - Theme.dp(52) : parent.width
            spacing: Theme.dp(1)

            Text {
                width: parent.width
                text: root.title
                font.pixelSize: Theme.dp(Theme.fontMd)
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.description.length > 0
                text: root.description
                font.pixelSize: Theme.dp(Theme.fontSm)
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    // A dropdown rather than a segmented row: the option set is expected to grow
    // (more generators), and a row of chips silently runs off the edge once there
    // are more than two. The combo scrolls instead.
    ComboBox {
        id: choiceBox
        visible: root.hasChoice
        anchors.left: parent.left
        anchors.leftMargin: Theme.dp(Theme.spacingSm)
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.dp(Theme.spacingSm)
        height: Theme.dp(34)
        font.pixelSize: Theme.dp(Theme.fontSm)

        // `value` is the stable identity that gets persisted; `label` is what the
        // user reads. Selecting by index alone would change meaning if the list
        // were ever reordered, which is the whole reason the pairs are separate.
        readonly property var optionValues: {
            let v = []
            for (let i = 0; i < root.choiceOptions.length; ++i)
                v.push(root.choiceOptions[i].value)
            return v
        }

        model: {
            let labels = []
            for (let i = 0; i < root.choiceOptions.length; ++i)
                labels.push(root.choiceOptions[i].label)
            return labels
        }

        currentIndex: Math.max(0, optionValues.indexOf(root.choiceValue))

        onActivated: (index) => {
            if (index >= 0 && index < optionValues.length)
                root.choiceSelected(optionValues[index])
        }
    }

    Text {
        anchors.right: trailing.left
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.verticalCenter: parent.verticalCenter
        visible: root.trailingText.length > 0
        text: root.trailingText
        font.pixelSize: Theme.dp(Theme.fontSm)
        color: Theme.textMuted
    }

    Icon {
        id: trailing
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingMd)
        anchors.verticalCenter: parent.verticalCenter
        visible: root.hasChevron
        name: "chevron-right"
        size: Theme.dp(Theme.iconSm)
        color: Theme.textMuted
    }

    Switch {
        id: switchControl
        visible: root.hasSwitch
        checked: root.checked
        anchors.right: parent.right
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        anchors.verticalCenter: parent.verticalCenter
        onToggled: root.toggled(checked)
    }

    MouseArea {
        id: rowArea
        anchors.fill: parent
        anchors.rightMargin: root.hasSwitch ? Theme.dp(60) : 0
        // Keep clear of the choice chips: this MouseArea is last in the file and
        // therefore on top, so without the inset it would swallow every tap
        // meant for a chip.
        anchors.bottomMargin: root.hasChoice ? Theme.dp(44) : 0
        onClicked: root.clicked()
    }
}
