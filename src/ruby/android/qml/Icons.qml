pragma Singleton
// ============================================================================
// Icons.qml — single source of truth for every vector icon in Ruby GG Mobile.
//
// WHY THIS EXISTS
//   The previous UI drew its icons as emoji / stray text glyphs.
//   Android ships no font covering those codepoints,
//   so every one of them rendered as "?" / tofu. This singleton replaces them
//   with real vector paths on a 24x24 grid.
//
// DESIGN CONTRACT
//   - Every path is authored for a 24x24 viewBox and drawn with a stroked
//     ShapePath (round cap + round join), so icons inherit colour and scale
//     cleanly. Icons that must be solid (play, dot, star-filled) are closed
//     paths and are drawn with `filled: true`.
//   - No font files, no image assets, no network. Just geometry.
//   - Paths are composed from primitives (_rr / _c / _line) rather than typed
//     out by hand, so they cannot drift or contain a malformed command.
//
// TO ADD AN ICON: add a `case "name": return ...;` below. Never inline an
// icon anywhere else in the app — always use components/Icon.qml.
// ============================================================================
import QtQuick

QtObject {
    id: icons

    // Authored grid size. Icon.qml scales this to the requested pixel size.
    readonly property real grid: 24
    // Default stroke weight in grid units.
    readonly property real stroke: 1.7

    // ── Path primitives ────────────────────────────────────────────────────
    function _m(x, y) { return "M" + x + " " + y + " " }
    function _l(x, y) { return "L" + x + " " + y + " " }
    function _line(x1, y1, x2, y2) { return _m(x1, y1) + _l(x2, y2) }

    // Rounded rectangle, clockwise from the top-left corner.
    function _rr(x, y, w, h, r) {
        r = Math.min(r, w / 2, h / 2)
        return _m(x + r, y) + _l(x + w - r, y)
             + "A" + r + " " + r + " 0 0 1 " + (x + w) + " " + (y + r) + " "
             + _l(x + w, y + h - r)
             + "A" + r + " " + r + " 0 0 1 " + (x + w - r) + " " + (y + h) + " "
             + _l(x + r, y + h)
             + "A" + r + " " + r + " 0 0 1 " + x + " " + (y + h - r) + " "
             + _l(x, y + r)
             + "A" + r + " " + r + " 0 0 1 " + (x + r) + " " + y + " Z"
    }

    function _c(cx, cy, r) {
        return _m(cx - r, cy)
             + "A" + r + " " + r + " 0 1 0 " + (cx + r) + " " + cy + " "
             + "A" + r + " " + r + " 0 1 0 " + (cx - r) + " " + cy + " Z"
    }

    // Circle outline with a wedge removed, starting at angle a0 (degrees,
    // 0 = east, growing clockwise) and sweeping to a1.
    function _wedge(cx, cy, r, a0, a1) {
        var s0 = a0 * Math.PI / 180
        var s1 = a1 * Math.PI / 180
        var large = (a1 - a0) > 180 ? 1 : 0
        return _m(cx + r * Math.cos(s0), cy + r * Math.sin(s0))
             + "A" + r + " " + r + " 0 " + large + " 1 "
             + (cx + r * Math.cos(s1)) + " " + (cy + r * Math.sin(s1))
    }

    // ── Icon table ─────────────────────────────────────────────────────────
    function path(name) {
        switch (name) {

        // ── Navigation / chrome ────────────────────────────────────────────
        case "home":
            return _m(3.6, 10.6) + _l(12, 3.8) + _l(20.4, 10.6)
                 + _line(6.2, 9.0, 6.2, 19.8) + _line(6.2, 19.8, 17.8, 19.8) + _line(17.8, 19.8, 17.8, 9.0)
                 + _line(10.1, 19.8, 10.1, 14.2) + _line(10.1, 14.2, 13.9, 14.2) + _line(13.9, 14.2, 13.9, 19.8)

        case "folder":
            // Tab + body, straight segments only (an arc-based tab is fussy at
            // 14px and the angled tab reads better).
            return _line(3.4, 7.4, 3.4, 18.6)
                 + _line(3.4, 7.4, 9.6, 7.4)
                 + _line(9.6, 7.4, 11.6, 9.6)
                 + _line(11.6, 9.6, 20.6, 9.6)
                 + _line(20.6, 9.6, 20.6, 18.6)
                 + _line(20.6, 18.6, 3.4, 18.6)
                 + "Z"

        case "folder-open":
            return _line(3.4, 7.4, 3.4, 18.4)
                 + _line(3.4, 7.4, 9.6, 7.4)
                 + _line(9.6, 7.4, 11.6, 9.6)
                 + _line(11.6, 9.6, 19.6, 9.6)
                 + _line(19.6, 9.6, 19.6, 11.8)
                 + _m(3.4, 18.4) + _l(6.2, 11.8) + _l(21.6, 11.8)
                 + _line(21.6, 11.8, 18.6, 18.4)
                 + _line(18.6, 18.4, 3.4, 18.4)

        case "file":
            return _m(6.2, 3.8) + _l(13.4, 3.8) + _l(18.4, 8.8) + _l(18.4, 20.2) + _l(6.2, 20.2) + _l(6.2, 3.8) + "Z"
                 + _m(13.2, 3.9) + _l(13.2, 9.0) + _l(18.3, 9.0)

        case "file-plus":
            return path("file") + _line(12.3, 13.6, 12.3, 18.6) + _line(9.8, 16.1, 14.8, 16.1)

        case "file-text":
            return path("file") + _line(9.0, 13.4, 15.6, 13.4) + _line(9.0, 16.4, 15.6, 16.4)

        case "search":
            return _c(10.6, 10.6, 6.0) + _line(15.0, 15.0, 20.4, 20.4)

        case "arrow-up":    return _line(12, 20, 12, 4.6) + _m(5.6, 11) + _l(12, 4.6) + _l(18.4, 11)
        case "arrow-down":  return _line(12, 4, 12, 19.4) + _m(5.6, 13) + _l(12, 19.4) + _l(18.4, 13)
        case "arrow-left":  return _line(20, 12, 4.6, 12) + _m(11, 5.6) + _l(4.6, 12) + _l(11, 18.4)
        case "arrow-right": return _line(4, 12, 19.4, 12) + _m(13, 5.6) + _l(19.4, 12) + _l(13, 18.4)
        case "chevron-right": return _line(9.6, 5.4, 16.2, 12) + _line(16.2, 12, 9.6, 18.6)
        case "chevron-left":  return _line(14.4, 5.4, 7.8, 12) + _line(7.8, 12, 14.4, 18.6)
        case "chevron-down":  return _line(5.4, 9.4, 12, 16) + _line(12, 16, 18.6, 9.4)
        case "chevron-up":    return _line(5.4, 14.6, 12, 8) + _line(12, 8, 18.6, 14.6)

        case "close":  return _line(6.2, 6.2, 17.8, 17.8) + _line(17.8, 6.2, 6.2, 17.8)
        case "plus":   return _line(12, 5.0, 12, 19.0) + _line(5.0, 12, 19.0, 12)
        case "minus":  return _line(5.0, 12, 19.0, 12)
        case "check":  return _line(5.2, 12.6, 9.8, 17.2) + _line(9.8, 17.2, 18.8, 6.8)

        case "more-vertical":
            return _c(12, 5.6, 1.5) + _c(12, 12, 1.5) + _c(12, 18.4, 1.5)
        case "more-horizontal":
            return _c(5.6, 12, 1.5) + _c(12, 12, 1.5) + _c(18.4, 12, 1.5)

        case "menu":       return _line(4.4, 7.2, 19.6, 7.2) + _line(4.4, 12, 19.6, 12) + _line(4.4, 16.8, 19.6, 16.8)
        case "drag":       return _line(8.6, 9.4, 15.4, 9.4) + _line(8.6, 14.6, 15.4, 14.6)

        case "indent":
            return _line(3.6, 5.4, 20.4, 5.4) + _line(3.6, 12.0, 13.4, 12.0) + _line(3.6, 18.6, 20.4, 18.6)
                 + _line(16.0, 9.4, 18.6, 12.0) + _line(18.6, 12.0, 16.0, 14.6)

        case "outdent":
            return _line(3.6, 5.4, 20.4, 5.4) + _line(3.6, 12.0, 13.4, 12.0) + _line(3.6, 18.6, 20.4, 18.6)
                 + _line(18.6, 9.4, 16.0, 12.0) + _line(16.0, 12.0, 18.6, 14.6)

        case "sort":
            return _line(7.2, 19.2, 7.2, 5.0) + _m(3.8, 8.4) + _l(7.2, 5.0) + _l(10.6, 8.4)
                 + _line(16.8, 4.8, 16.8, 19.0) + _m(13.4, 15.6) + _l(16.8, 19.0) + _l(20.2, 15.6)

        case "filter":     return _m(3.8, 5.4) + _l(20.2, 5.4) + _l(14.0, 12.6) + _l(14.0, 19.2) + _l(10.0, 16.6) + _l(10.0, 12.6) + "Z"

        case "refresh":
            return _wedge(12, 12, 7.4, 40, 310)
                 + _m(15.4, 3.4) + _l(19.6, 5.0) + _l(17.6, 9.0)

        case "undo":       return _wedge(12, 12.4, 7.2, 150, 340) + _m(4.6, 8.0) + _l(4.8, 12.6) + _l(9.4, 12.4)
        case "redo":       return _wedge(12, 12.4, 7.2, 200, 390) + _m(19.4, 8.0) + _l(19.2, 12.6) + _l(14.6, 12.4)

        case "external":   return _line(13.4, 4.0, 20.2, 4.0) + _line(20.2, 4.0, 20.2, 10.8)
                                 + _line(20.2, 4.0, 11.6, 12.6)
                                 + _m(17.0, 14.2) + _line(17.0, 14.2, 17.0, 20.0)
                                 + _line(17.0, 20.0, 4.0, 20.0) + _line(4.0, 20.0, 4.0, 7.0)
                                 + _line(4.0, 7.0, 9.8, 7.0)

        case "link":       return _m(10.2, 13.8) + _l(13.8, 10.2)
                                 + _m(9.0, 7.4) + _l(11.0, 5.4) + "A4.0 4.0 0 0 1 16.6 11.0" + _l(14.6, 13.0)
                                 + _m(15.0, 16.6) + _l(13.0, 18.6) + "A4.0 4.0 0 0 1 7.4 13.0" + _l(9.4, 11.0)

        // ── File actions ───────────────────────────────────────────────────
        case "save":
            return _m(5.2, 4.8) + _l(15.6, 4.8) + _l(19.2, 8.4) + _l(19.2, 19.2) + _l(4.8, 19.2) + _l(4.8, 5.2)
                 + "A0.4 0.4 0 0 1 5.2 4.8" + "Z"
                 + _m(8.4, 19.2) + _l(8.4, 13.4) + _l(15.6, 13.4) + _l(15.6, 19.2)
                 + _m(8.4, 4.8) + _l(8.4, 9.4) + _l(14.4, 9.4)

        case "download":   return _line(12, 3.8, 12, 15.0) + _m(7.4, 10.4) + _l(12, 15.0) + _l(16.6, 10.4)
                                 + _m(4.6, 16.6) + _l(4.6, 19.6) + _l(19.4, 19.6) + _l(19.4, 16.6)

        case "upload":     return _line(12, 20.2, 12, 9.0) + _m(7.4, 13.6) + _l(12, 9.0) + _l(16.6, 13.6)
                                 + _m(4.6, 6.4) + _l(4.6, 3.4) + _l(19.4, 3.4) + _l(19.4, 6.4)

        case "trash":
            return _line(4.8, 7.2, 19.2, 7.2)
                 + _m(9.4, 7.0) + _l(9.4, 4.6) + _l(14.6, 4.6) + _l(14.6, 7.0)
                 + _m(6.8, 7.4) + _l(7.6, 19.6) + _line(7.6, 19.6, 16.4, 19.6)
                 + _line(16.4, 19.6, 17.2, 7.4)
                 + _line(10.4, 10.8, 10.8, 16.6) + _line(13.6, 10.8, 13.2, 16.6)

        case "pencil":
            return _m(15.4, 4.6) + _l(19.4, 8.6) + _l(9.2, 18.8) + _l(4.4, 19.6) + _l(5.2, 14.8) + "Z"
                 + _line(13.4, 6.6, 17.4, 10.6)

        case "copy":
            return _rr(8.4, 3.8, 11.8, 11.8, 2.0) + _rr(3.8, 8.4, 11.8, 11.8, 2.0)

        case "cut":
            return _c(6.4, 18.4, 2.6) + _c(17.6, 18.4, 2.6)
                 + _line(7.6, 16.4, 17.2, 4.0) + _line(16.4, 16.4, 6.8, 4.0)

        case "clipboard":
            return _line(9.0, 4.0, 15.0, 4.0)
                 + _m(9.0, 4.0) + _l(9.0, 6.0) + _line(9.0, 6.0, 15.0, 6.0) + _line(15.0, 6.0, 15.0, 4.0)
                 + _m(15.0, 5.4) + _line(15.0, 5.4, 18.0, 5.4) + _line(18.0, 5.4, 18.0, 20.0)
                 + _line(18.0, 20.0, 6.0, 20.0) + _line(6.0, 20.0, 6.0, 5.4)
                 + _line(6.0, 5.4, 9.0, 5.4)

        case "archive":
            return _m(3.8, 5.2) + _l(20.2, 5.2) + _l(20.2, 8.6) + _l(3.8, 8.6) + "Z"
                 + _m(5.4, 8.8) + _l(5.4, 19.4) + _l(18.6, 19.4) + _l(18.6, 8.8)
                 + _line(10.0, 12.8, 14.0, 12.8)

        case "star":
            return _m(12, 3.6) + _l(14.65, 9.05) + _l(20.6, 9.9) + _l(16.3, 14.1)
                 + _l(17.35, 20.0) + _l(12, 17.15) + _l(6.65, 20.0) + _l(7.7, 14.1)
                 + _l(3.4, 9.9) + _l(9.35, 9.05) + "Z"

        case "pin":
            return _m(7.2, 3.9) + _l(16.8, 3.9) + _l(16.8, 20.1) + _l(12.0, 16.0) + _l(7.2, 20.1) + "Z"

        case "play":
            return _m(8.2, 5.0) + _l(19.2, 12.0) + _l(8.2, 19.0) + "Z"

        case "pause":
            return _line(8.5, 5.0, 8.5, 19.0) + _line(15.5, 5.0, 15.5, 19.0)

        case "dot":
            return _c(12, 12, 3.4)

        case "check-circle":
            return _c(12, 12, 8.6) + _line(8.0, 12.3, 10.9, 15.2) + _line(10.9, 15.2, 16.2, 9.0)

        case "alert":
            return _c(12, 12, 8.6) + _line(12, 7.4, 12, 12.9) + _c(12, 16.3, 0.9)

        case "info":
            return _c(12, 12, 8.6) + _line(12, 11.0, 12, 16.4) + _c(12, 7.9, 0.9)

        case "volume":
            return _m(11, 5) + _l(6, 9) + _l(2, 9) + _l(2, 15) + _l(6, 15) + _l(11, 19) + "Z"
                 + _wedge(11, 12, 5, -45, 45)
                 + _wedge(11, 12, 9, -45, 45)

        case "music":
            return _m(9, 18) + _c(7, 18, 2.5) + _line(9.5, 18, 9.5, 6)
                 + _line(9.5, 6, 18.5, 3.5) + _line(18.5, 3.5, 18.5, 15.5) + _c(16, 15.5, 2.5)

        // ── Content types (semantic parity with the legacy widget colours) ──
        case "map":
            return _m(9.1, 4.2) + _l(4.4, 6.3) + _l(4.4, 19.7) + _l(9.1, 17.6) + _l(14.9, 19.7)
                 + _l(19.6, 17.6) + _l(19.6, 4.2) + _l(14.9, 6.3) + "Z"
                 + _line(9.1, 4.2, 9.1, 17.6) + _line(14.9, 6.3, 14.9, 19.7)

        case "layers":
            return _m(12, 3.6) + _l(20.6, 8.0) + _l(12, 12.4) + _l(3.4, 8.0) + "Z"
                 + _m(3.4, 12.2) + _l(12, 16.6) + _l(20.6, 12.2)
                 + _m(3.4, 16.2) + _l(12, 20.6) + _l(20.6, 16.2)

        case "water":
            return _m(12, 3.5) + " C 12,3.5 6.5,10.5 6.5,15.5 A 5.5,5.5 0 0 0 17.5,15.5 C 17.5,10.5 12,3.5 12,3.5 Z"

        case "ruby":
        case "gem":
        case "diamond":
            return _m(7.5, 4.5) + _l(16.5, 4.5) + _l(21.0, 9.5) + _l(12.0, 20.5) + _l(3.0, 9.5) + "Z"
                 + _line(3.0, 9.5, 21.0, 9.5)
                 + _line(7.5, 4.5, 9.5, 9.5)
                 + _line(16.5, 4.5, 14.5, 9.5)
                 + _line(9.5, 9.5, 12.0, 20.5)
                 + _line(14.5, 9.5, 12.0, 20.5)

        case "cube":
            return _m(12.0, 3.5) + _l(20.0, 8.0) + _l(20.0, 16.5) + _l(12.0, 21.0) + _l(4.0, 16.5) + _l(4.0, 8.0) + "Z"
                 + _line(12.0, 12.5, 4.0, 8.0)
                 + _line(12.0, 12.5, 20.0, 8.0)
                 + _line(12.0, 12.5, 12.0, 21.0)

        case "image":
            return _rr(3.8, 5.2, 16.4, 13.6, 2.2)
                 + _c(9.0, 10.0, 1.7)
                 + _m(5.2, 18.8) + _l(10.4, 13.4) + _l(14.2, 17.2) + _line(14.2, 17.2, 17.4, 13.2) + _line(17.4, 13.2, 20.0, 15.9)

        case "image-plus":
            return path("image") + _line(18.2, 2.4, 18.2, 6.6) + _line(16.1, 4.5, 20.3, 4.5)

        case "braces":
            return _m(9.6, 3.9) + "A2.0 2.0 0 0 0 7.6 5.9 " + _line(7.6, 5.9, 7.6, 9.6)
                 + "A2.4 2.4 0 0 1 5.2 12.0 " + "A2.4 2.4 0 0 1 7.6 14.4 "
                 + _line(7.6, 14.4, 7.6, 18.1)
                 + "A2.0 2.0 0 0 0 9.6 20.1"
                 + _m(14.4, 3.9) + "A2.0 2.0 0 0 1 16.4 5.9 " + _line(16.4, 5.9, 16.4, 9.6)
                 + "A2.4 2.4 0 0 0 18.8 12.0 " + "A2.4 2.4 0 0 0 16.4 14.4 "
                 + _line(16.4, 14.4, 16.4, 18.1)
                 + "A2.0 2.0 0 0 1 14.4 20.1"

        case "terminal":
            return _rr(3.4, 4.6, 17.2, 14.8, 2.2)
                 + _m(7.4, 9.4) + _l(10.0, 12.0) + _l(7.4, 14.6)
                 + _line(12.4, 14.8, 16.6, 14.8)

        case "grid":
            return _rr(3.8, 3.8, 7.0, 7.0, 1.6) + _rr(13.2, 3.8, 7.0, 7.0, 1.6)
                 + _rr(3.8, 13.2, 7.0, 7.0, 1.6) + _rr(13.2, 13.2, 7.0, 7.0, 1.6)

        case "drive":
            return _rr(3.4, 8.6, 17.2, 7.4, 2.0) + _rr(3.4, 16.4, 17.2, 4.4, 2.0)
                 + _c(17.0, 18.6, 0.9)

        case "chip":
            return _rr(6.4, 6.4, 11.2, 11.2, 2.0)
                 + _line(9.6, 2.8, 9.6, 6.4) + _line(14.4, 2.8, 14.4, 6.4)
                 + _line(9.6, 17.6, 9.6, 21.2) + _line(14.4, 17.6, 14.4, 21.2)
                 + _line(2.8, 9.6, 6.4, 9.6) + _line(2.8, 14.4, 6.4, 14.4)
                 + _line(17.6, 9.6, 21.2, 9.6) + _line(17.6, 14.4, 21.2, 14.4)

        case "palette":
            // Palette reads as a disc with three paint wells.
            return _c(12, 12, 8.4)
                 + _c(8.6, 9.4, 1.3) + _c(15.4, 9.4, 1.3) + _c(12.0, 15.6, 1.3)

        case "tune":
            return _line(4.0, 8.0, 20.0, 8.0) + _c(14.2, 8.0, 2.2)
                 + _line(4.0, 16.0, 20.0, 16.0) + _c(9.8, 16.0, 2.2)

        case "gear":
            return _c(12, 12, 3.4) + _c(12, 12, 8.0)
                 + _line(12, 1.8, 12, 4.0) + _line(12, 20.0, 12, 22.2)
                 + _line(1.8, 12, 4.0, 12) + _line(20.0, 12, 22.2, 12)
                 + _line(4.8, 4.8, 6.3, 6.3) + _line(17.7, 17.7, 19.2, 19.2)
                 + _line(19.2, 4.8, 17.7, 6.3) + _line(6.3, 17.7, 4.8, 19.2)

        case "sliders-vertical":
            return _line(8.0, 4.0, 8.0, 20.0) + _c(8.0, 9.8, 2.2)
                 + _line(16.0, 4.0, 16.0, 20.0) + _c(16.0, 14.2, 2.2)

        case "rule":
            return _rr(4.2, 3.8, 15.6, 16.4, 2.0)
                 + _line(7.6, 8.4, 16.4, 8.4) + _line(7.6, 12.0, 16.4, 12.0) + _line(7.6, 15.6, 12.8, 15.6)

        case "sparkle":
            return _m(11.0, 3.4) + _l(12.9, 8.6) + _l(18.1, 10.5) + _l(12.9, 12.4)
                 + _l(11.0, 17.6) + _l(9.1, 12.4) + _l(3.9, 10.5) + _l(9.1, 8.6) + "Z"
                 + _m(18.2, 15.4) + _l(19.0, 17.6) + _l(21.2, 18.4) + _l(19.0, 19.2)
                 + _l(18.2, 21.4) + _l(17.4, 19.2) + _l(15.2, 18.4) + _l(17.4, 17.6) + "Z"

        case "camera":
            return _rr(3.4, 7.4, 17.2, 11.6, 2.2)
                 + _line(8.6, 7.4, 9.8, 4.8) + _line(9.8, 4.8, 14.2, 4.8)
                 + _line(14.2, 4.8, 15.4, 7.4)
                 + _c(12, 13.2, 3.4)

        case "view3d":
            return _rr(3.6, 6.2, 16.8, 12.6, 2.2) + _c(12, 12.5, 2.6)
                 + _line(4.8, 6.2, 8.0, 3.4) + _line(16.0, 3.4, 19.2, 6.2)

        case "eye":
            return _m(2.6, 12.0) + "C6.4 5.9 17.6 5.9 21.4 12.0 " + "C17.6 18.1 6.4 18.1 2.6 12.0 Z"
                 + _c(12, 12, 3.3)

        case "eye-off":
            return _m(2.6, 12.0) + "C6.4 5.9 17.6 5.9 21.4 12.0 " + "C17.6 18.1 6.4 18.1 2.6 12.0 Z"
                 + _c(12, 12, 3.3) + _line(4.4, 4.0, 19.6, 20.0)

        case "target":
            return _c(12, 12, 8.2) + _c(12, 12, 3.2) + _line(12, 1.6, 12, 5.0) + _line(12, 19.0, 12, 22.4)
                 + _line(1.6, 12, 5.0, 12) + _line(19.0, 12, 22.4, 12)

        case "zoom-in":
            return _c(10.6, 10.6, 6.0) + _line(15.0, 15.0, 20.4, 20.4)
                 + _line(10.6, 7.8, 10.6, 13.4) + _line(7.8, 10.6, 13.4, 10.6)

        case "zoom-out":
            return _c(10.6, 10.6, 6.0) + _line(15.0, 15.0, 20.4, 20.4) + _line(7.8, 10.6, 13.4, 10.6)

        case "move":
            return _line(12, 3.4, 12, 20.6) + _line(3.4, 12, 20.6, 12)
                 + _line(12, 3.4, 9.6, 5.8) + _line(12, 3.4, 14.4, 5.8)
                 + _line(12, 20.6, 9.6, 18.2) + _line(12, 20.6, 14.4, 18.2)
                 + _line(3.4, 12, 5.8, 9.6) + _line(3.4, 12, 5.8, 14.4)
                 + _line(20.6, 12, 18.2, 9.6) + _line(20.6, 12, 18.2, 14.4)

        case "rotate":
            return _wedge(12, 12, 7.4, 200, 470) + _m(8.4, 3.6) + _l(4.6, 5.0) + _l(6.4, 8.8)

        case "rotate-ccw":
            return _wedge(12, 12, 7.4, 160, 430) + _m(15.6, 3.6) + _l(19.4, 5.0) + _l(17.6, 8.8)

        case "flip-horizontal":
            return _line(12, 3.4, 12, 20.6)
                 + _m(9.3, 6.8) + _l(4.4, 12.0) + _l(9.3, 17.2) + "Z"
                 + _m(14.7, 6.8) + _l(19.6, 12.0) + _l(14.7, 17.2) + "Z"

        case "flip-vertical":
            return _line(3.4, 12, 20.6, 12)
                 + _m(6.8, 9.3) + _l(12.0, 4.4) + _l(17.2, 9.3) + "Z"
                 + _m(6.8, 14.7) + _l(12.0, 19.6) + _l(17.2, 14.7) + "Z"

        case "bolt":
            return _m(13.4, 2.6) + _l(5.4, 13.6) + _l(11.2, 13.6) + _l(10.6, 21.4)
                 + _l(18.6, 10.4) + _l(12.8, 10.4) + "Z"

        case "mountain":
            return _m(2.4, 19.6) + _l(9.0, 6.0) + _l(13.2, 14.4) + _l(15.6, 10.8) + _l(21.6, 19.6) + "Z"
                 + _line(6.6, 11.4, 11.4, 11.4)

        case "globe":
            return _c(12, 12, 8.4) + _line(3.6, 12.0, 20.4, 12.0)
                 + _m(12, 3.6) + "C7.9 7.6 7.9 16.4 12 20.4"
                 + _m(12, 3.6) + "C16.1 7.6 16.1 16.4 12 20.4"

        case "folder-plus":
            return path("folder") + _line(12.0, 11.6, 12.0, 16.6) + _line(9.5, 14.1, 14.5, 14.1)

        case "scale":
            return _line(3.8, 20.2, 20.2, 3.8)
                 + _line(3.8, 20.2, 12.6, 20.2) + _line(3.8, 20.2, 3.8, 11.4)
                 + _line(20.2, 3.8, 11.4, 3.8) + _line(20.2, 3.8, 20.2, 12.6)

        case "lock":
            return _rr(5.2, 10.4, 13.6, 9.8, 2.0)
                 + _m(8.6, 10.2) + _l(8.6, 7.6) + "A3.4 3.4 0 0 1 15.4 7.6 "
                 + _line(15.4, 7.6, 15.4, 10.2)

        case "unlock":
            return _rr(5.2, 10.4, 13.6, 9.8, 2.0)
                 + _m(8.6, 10.2) + _l(8.6, 7.6) + "A3.4 3.4 0 0 1 15.4 7.6 "
                 + _line(15.4, 7.6, 17.2, 6.0)

        case "key":
            return _c(8.4, 15.6, 4.0)
                 + _line(11.2, 12.8, 19.4, 4.6) + _line(16.8, 4.6, 19.8, 7.6) + _line(14.6, 6.8, 17.6, 9.8)

        case "sun":
            return _c(12, 12, 4.0)
                 + _line(12, 1.8, 12, 4.2) + _line(12, 19.8, 12, 22.2)
                 + _line(1.8, 12, 4.2, 12) + _line(19.8, 12, 22.2, 12)
                 + _line(4.8, 4.8, 6.5, 6.5) + _line(17.5, 17.5, 19.2, 19.2)
                 + _line(19.2, 4.8, 17.5, 6.5) + _line(6.5, 17.5, 4.8, 19.2)

        case "moon":
            return _m(19.4, 15.0) + "A8.4 8.4 0 1 1 9.4 4.6 " + "A6.8 6.8 0 0 0 19.4 15.0 Z"

        default:
            // Unknown icon: draw a visible placeholder frame instead of a bare
            // "?" so a typo is obvious in QA but never looks like broken text.
            return _rr(3.6, 3.6, 16.8, 16.8, 3.0) + _line(8.4, 15.6, 15.6, 8.4)
        }
    }
}
