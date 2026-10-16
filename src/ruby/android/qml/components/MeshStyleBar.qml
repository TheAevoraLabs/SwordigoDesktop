import QtQuick
import QtQuick.Controls
import ".."

// ─────────────────────────────────────────────────────────────────────────────
// MeshStyleBar.qml — the compact MESH STYLE bottom bar (Zypher ground objects).
//
// REPLACES ZypherLookPanel. The old panel was a 272x430dp side window with two
// stacked walls of style tiles plus three TEXT FIELDS for frequency / depth /
// min-collision, so it ate a quarter of a landscape phone and the tuning GUI was
// a keyboard. This is one thin bar the same height as the viewport console: a
// single horizontally-scrollable row holding both style groups (TOP deck and
// SIDE shoulder) and a close button. Numeric tuning moved out to the floating
// BubbleStepper pads on the right (see MeshStyleBar's host view), which is a
// press-and-hold +/- control instead of a text field.
//
// The line diagrams are generated from the SAME profile maths the generator
// uses (build_hat_profile / top_pattern_disp in src/tools/roller.cpp), including
// the closing return that ties the shoulder back onto the front face. Keep them
// in step if either changes.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: root

    /// The RubyQuickViewport: zypherActive() / setZypherLook() / getZypherLook().
    property var host: null
    /// REACTIVE generator state, bound by the host view (a function call like
    /// host.zypherActive() has no notify signal, so its binding would freeze at
    /// whatever the generator was when this bar was created). False hides the
    /// fallback hint below.
    property bool modeActive: true
    /// The in-scene editor puts an explicit close button here; the Ground Mesh
    /// Studio hosts the same bar inline in its dock, where closing is meaningless
    /// and the button would be a dead control, so it turns it off.
    property bool showClose: true

    signal closeRequested()
    /// Emitted whenever a style chip is picked (after the host has been told).
    signal zypherStylesChanged()

    // ── Style geometry mirrors (see roller.cpp) ────────────────────────────
    // A hat profile in normalized (lateral, drop) space, exactly as
    // build_hat_profile builds it — including the final return to lat 0 that
    // closes the lip onto the front plane.
    function hatProfile(style, steps) {
        var p = [], i, t, a, n;
        if (style === "none") {
            return [[0, 0]];
        } else if (style === "vanilla") {
            p = [[0, 0], [0.5, 0], [1, 0.4], [1, 1]];
        } else if (style === "plain") {
            for (i = 0; i <= steps; ++i) { t = i / steps; p.push([t, t]); }
        } else if (style === "zigzag") {
            n = steps;
            for (i = 0; i < n; ++i) {
                p.push([(i + 0.5) / n, 0.5]);
                p.push([(i + 1) / n, 0.0]);
            }
            p.push([1, 1]);
        } else { // curvy
            for (i = 0; i <= steps; ++i) {
                t = i / steps;
                a = t * Math.PI / 2;
                p.push([Math.sin(a), 1 - Math.cos(a)]);
            }
        }
        if (p.length > 0 && Math.abs(p[p.length - 1][0]) > 1e-6)
            p.push([0, p[p.length - 1][1]]);
        return p;
    }

    // Deck relief silhouette in normalized space (x along the deck, y up).
    // Mirrors top_pattern_disp: spikey rises as a cone, bubbly as a dome,
    // porous sinks as a pit, plain is flat.
    function topProfile(style, n) {
        var p = [], i, g, y;
        for (i = 0; i <= n; ++i) {
            var t = i / n;
            if (style === "spikey") {
                g = (t * 4) % 1;
                y = Math.max(0, 1 - Math.abs(g - 0.5) * 2);
            } else if (style === "porous") {
                g = (t * 4) % 1;
                y = -(1 - Math.abs(g - 0.5) * 2) * 0.85;
            } else if (style === "bubbly") {
                g = (t * 3) % 1;
                y = Math.sin(g * Math.PI);
            } else {
                y = 0;
            }
            p.push([t, y]);
        }
        return p;
    }

    /// Draws one style diagram. `isSide` profiles drop downward from a rim
    /// baseline (the deck edge); otherwise the relief rises/sinks off a flat
    /// deck baseline.
    function paintDiagram(ctx, style, isSide, w, h) {
        var pad = Theme.dp(3);
        var iw = w - pad * 2, ih = h - pad * 2;
        ctx.reset();
        ctx.lineJoin = "round";
        ctx.lineCap = "round";

        // Baseline = the deck surface the diagram hangs from.
        var baseY = isSide ? pad + Theme.dp(3) : Math.round(h * 0.58);
        ctx.strokeStyle = Theme.alpha(Theme.textMuted, 0.45);
        ctx.lineWidth = Theme.dp(0.8);
        ctx.beginPath();
        ctx.moveTo(pad, baseY);
        ctx.lineTo(w - pad, baseY);
        ctx.stroke();

        var pts = isSide ? hatProfile(style, 10) : topProfile(style, 72);
        if (pts.length === 0) return;

        // Uniform scale keeps every diagram the same weight instead of each
        // style filling its tile differently.
        var sx = iw, sy = ih;
        function mapPt(p) {
            return [pad + p[0] * sx, baseY - p[1] * sy];
        }

        ctx.beginPath();
        var q = mapPt(pts[0]);
        ctx.moveTo(q[0], q[1]);
        for (var i = 1; i < pts.length; ++i) {
            q = mapPt(pts[i]);
            ctx.lineTo(q[0], q[1]);
        }
        // Close the silhouette back along the baseline so it reads as a section.
        ctx.stroke();

        ctx.lineTo(w - pad, baseY);
        ctx.lineTo(pad, baseY);
        ctx.closePath();
        ctx.fillStyle = Theme.alpha(Theme.accentInk, 0.13);
        ctx.fill();
    }

    readonly property var sideStyles: [
        { id: "vanilla", label: qsTr("Vanilla") },
        { id: "curvy",   label: qsTr("Curvy") },
        { id: "plain",   label: qsTr("Chamfer") },
        { id: "zigzag",  label: qsTr("ZigZag") },
        { id: "none",    label: qsTr("Bare") }
    ]
    readonly property var topStyles: [
        { id: "plain",   label: qsTr("Flat") },
        { id: "spikey",  label: qsTr("Spikey") },
        { id: "porous",  label: qsTr("Porous") },
        { id: "bubbly",  label: qsTr("Bubbly") }
    ]

    property string hatStyle: "vanilla"
    property string topStyle: "plain"

    function push() {
        if (!host || typeof host.setZypherLook !== "function") return
        host.setZypherLook(hatStyle, topStyle, -1, -1, -1)
        zypherStylesChanged()
    }

    function refresh() {
        if (!host || typeof host.getZypherLook !== "function") return
        var s = host.getZypherLook()
        if (!s) return
        hatStyle = s.hatStyle || hatStyle
        topStyle = s.topStyle || topStyle
    }

    Component.onCompleted: refresh()
    Connections {
        target: root.host
        ignoreUnknownSignals: true
        function onMeshEditActiveChanged() { root.refresh() }
    }

    color: Theme.alpha(Theme.surface1, 0.96)
    radius: Theme.radiusCard
    border.color: Theme.borderStrong
    border.width: 1

    // One selectable style chip: a line diagram over its name.
    component StyleChip: Rectangle {
        id: chip
        property string styleId: ""
        property string styleLabel: ""
        property bool isSide: true
        readonly property bool picked: (isSide ? root.hatStyle : root.topStyle) === styleId

        width: Theme.dp(48)
        height: Theme.dp(40)
        radius: Theme.radiusSm
        color: picked ? Theme.alpha(Theme.accentInk, 0.20) : Theme.alpha(Theme.surface3, 0.80)
        border.color: picked ? Theme.accentInk : Theme.borderSubtle
        border.width: picked ? 1.5 : 1

        Canvas {
            id: diagram
            anchors.top: parent.top
            anchors.topMargin: Theme.dp(2)
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - Theme.dp(8)
            height: Theme.dp(24)
            onPaint: root.paintDiagram(getContext("2d"), chip.styleId, chip.isSide, width, height)
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.dp(2)
            anchors.horizontalCenter: parent.horizontalCenter
            text: chip.styleLabel
            font.pixelSize: Theme.dp(8)
            font.weight: chip.picked ? Font.Bold : Font.Medium
            color: chip.picked ? Theme.accentInk : Theme.textSecondary
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof screenOrientation !== "undefined" && screenOrientation)
                    screenOrientation.vibrateTouch(15)
                if (chip.isSide) root.hatStyle = chip.styleId
                else             root.topStyle = chip.styleId
                root.push()
                diagram.requestPaint()
            }
        }
    }

    // ── Content: one thin scrollable row ────────────────────────────────────
    Item {
        anchors.fill: parent
        anchors.leftMargin: Theme.dp(Theme.spacingSm)
        anchors.rightMargin: Theme.dp(Theme.spacingSm)
        clip: true

        Flickable {
            id: strip
            anchors.left: parent.left
            anchors.right: closeBtn.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: Theme.dp(Theme.spacingSm)
            height: root.height - Theme.dp(10)
            contentWidth: stripRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Row {
                id: stripRow
                height: strip.height
                spacing: Theme.dp(Theme.spacingSm)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("TOP")
                    font.pixelSize: Theme.dp(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                    color: Theme.textMuted
                }

                Repeater {
                    model: root.topStyles
                    delegate: StyleChip {
                        anchors.verticalCenter: parent.verticalCenter
                        styleId: modelData.id
                        styleLabel: modelData.label
                        isSide: false
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: Theme.dp(26)
                    color: Theme.borderSubtle
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("SIDE")
                    font.pixelSize: Theme.dp(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                    color: Theme.textMuted
                }

                Repeater {
                    model: root.sideStyles
                    delegate: StyleChip {
                        anchors.verticalCenter: parent.verticalCenter
                        styleId: modelData.id
                        styleLabel: modelData.label
                        isSide: true
                    }
                }
            }
        }

        Rectangle {
            id: closeBtn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showClose
            width: root.showClose ? Theme.dp(30) : 0
            height: Theme.dp(30)
            radius: Theme.radiusSm
            color: Theme.alpha(Theme.surface3, 0.9)
            border.color: Theme.borderSubtle
            border.width: 1
            Icon {
                anchors.centerIn: parent
                name: "close"
                size: Theme.dp(13)
                color: Theme.textSecondary
            }
            MouseArea {
                anchors.fill: parent
                onClicked: root.closeRequested()
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.modeActive
        text: qsTr("Zypher look is available on Zypher ground objects")
        font.pixelSize: Theme.dp(9)
        color: Theme.textMuted
    }
}
