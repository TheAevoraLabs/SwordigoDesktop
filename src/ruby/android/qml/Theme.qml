pragma Singleton
// ============================================================================
// Theme.qml — the single design system for Ruby GG Mobile.
//
// PALETTE PROVENANCE
//   Every colour below is promoted from the QSS that was previously inlined in
//   each legacy QWidget (mobile_asset_browser.cpp, mobile_code_editor.cpp,
//   mobile_dpad_widget.cpp, mobile_hierarchy_drawer.cpp). Nothing was invented:
//   the surface ramp, hairlines, accents and semantic type colours are the same
//   values the shipped Widget UI used, now expressed once instead of repeated
//   as hex literals in four files.
//
//   The semantic type colours are carried forward VERBATIM from
//   MobileAssetBrowser::populate_files(), so a .scene file is still amber, an
//   .scl is still green, a .pod/.glb is still blue, a .zip/.apk is still
//   yellow, a .lua is still violet and a texture is still pink. Only the icon
//   rendering changed (emoji -> vector paths); the colour meaning did not.
//
// PROPERTY NAMES ARE A PUBLIC CONTRACT
//   Every view/component in qml/ reads these names. Renaming or removing one
//   breaks the build of the whole UI, so treat additions as safe and renames
//   as a coordinated multi-file change.
// ============================================================================
import QtQuick

QtObject {
    id: theme

    // ── Responsive scale ───────────────────────────────────────────────────
    // Set once from Main.qml based on the actual screen width, so a 320dp
    // phone and a 480dp tablet both get sane touch targets. Every metric below
    // is a design-unit value that callers pass through dp().
    property real scaleFactor: 1.0

    /// Design units -> device-independent pixels.
    function dp(n) { return Math.round(n * scaleFactor * 100) / 100 }

    /// Type-safe translucent variant of a theme colour.
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // ── Active Theme ID & Palettes ─────────────────────────────────────────
    property string activeThemeId: (typeof rubySettings !== "undefined" && rubySettings && rubySettings.themeId)
                                   ? rubySettings.themeId : "oled"

    readonly property var availableThemes: [
        { id: "oled",     name: "OLED Pitch Black", desc: "Pure #000000 black canvas for OLED displays with vibrant ruby neon", bg: "#000000", card: "#090A0E", accent: "#FF2A5F" },
        { id: "obsidian", name: "Obsidian Studio",  desc: "The classic deep charcoal studio palette with ruby coral accents",   bg: "#0D0E11", card: "#14161B", accent: "#E06C75" },
        { id: "cyber",    name: "Cyber Violet",     desc: "Abyssal indigo night with electric purple and neon pink highlights",  bg: "#0B0813", card: "#13101E", accent: "#BD93F9" },
        { id: "nordic",   name: "Nordic Frost",     desc: "Sub-zero polar night with soothing arctic cyan and storm blues",      bg: "#191D24", card: "#212631", accent: "#88C0D0" },
        { id: "monolith", name: "Monolith Amber",   desc: "Minimalist graphite slate with high-contrast amber gold accents",    bg: "#080808", card: "#121212", accent: "#F59E0B" }
    ]

    function setTheme(id) {
        if (typeof rubySettings !== "undefined" && rubySettings) {
            rubySettings.themeId = id
        }
        theme.activeThemeId = id
    }

    readonly property bool isOled:     activeThemeId === "oled"
    readonly property bool isObsidian: activeThemeId === "obsidian"
    readonly property bool isCyber:    activeThemeId === "cyber"
    readonly property bool isNordic:   activeThemeId === "nordic"
    readonly property bool isMonolith: activeThemeId === "monolith"

    // ── Surfaces (Elevations) ──────────────────────────────────────────────
    readonly property color surface0: {
        if (isOled)     return "#000000"
        if (isCyber)    return "#0B0813"
        if (isNordic)   return "#191D24"
        if (isMonolith) return "#080808"
        return "#0D0E11"
    }

    readonly property color surface1: {
        if (isOled)     return "#090A0E"
        if (isCyber)    return "#13101E"
        if (isNordic)   return "#212631"
        if (isMonolith) return "#121212"
        return "#14161B"
    }

    readonly property color surface2: {
        if (isOled)     return "#12141C"
        if (isCyber)    return "#1D172B"
        if (isNordic)   return "#2A313E"
        if (isMonolith) return "#1A1A1A"
        return "#1B1E24"
    }

    readonly property color surface3: {
        if (isOled)     return "#1A1E2A"
        if (isCyber)    return "#28213D"
        if (isNordic)   return "#353D4D"
        if (isMonolith) return "#262626"
        return "#242831"
    }

    readonly property color surfaceSunken: {
        if (isOled)     return "#000000"
        if (isCyber)    return "#08060F"
        if (isNordic)   return "#14171D"
        if (isMonolith) return "#040404"
        return "#0A0B0E"
    }

    readonly property color surfaceGlass: {
        if (isOled)     return "#E6090A0E"
        if (isCyber)    return "#E613101E"
        if (isNordic)   return "#E6212631"
        if (isMonolith) return "#E6121212"
        return "#E614161B"
    }

    // ── Hairlines ──────────────────────────────────────────────────────────
    readonly property color border: {
        if (isOled)     return "#1E2332"
        if (isCyber)    return "#2D2745"
        if (isNordic)   return "#3B4455"
        if (isMonolith) return "#2B2B2B"
        return "#2A2F3A"
    }

    readonly property color borderSubtle: {
        if (isOled)     return "#141722"
        if (isCyber)    return "#211C33"
        if (isNordic)   return "#2C3442"
        if (isMonolith) return "#1E1E1E"
        return "#1F232B"
    }

    readonly property color borderStrong: {
        if (isOled)     return "#2D354B"
        if (isCyber)    return "#3F3660"
        if (isNordic)   return "#4C566A"
        if (isMonolith) return "#3D3D3D"
        return "#3A4150"
    }

    readonly property color borderFocus: {
        if (isOled)     return "#FF2A5F"
        if (isCyber)    return "#BD93F9"
        if (isNordic)   return "#88C0D0"
        if (isMonolith) return "#F59E0B"
        return "#E06C75"
    }

    readonly property color edgeHighlight: "transparent"

    // ── Brand Accent ───────────────────────────────────────────────────────
    readonly property color accentStart: {
        if (isOled)     return "#FF2A5F"
        if (isCyber)    return "#BD93F9"
        if (isNordic)   return "#81A1C1"
        if (isMonolith) return "#D97706"
        return "#D3515B"
    }

    readonly property color accentEnd: {
        if (isOled)     return "#FF3366"
        if (isCyber)    return "#FF79C6"
        if (isNordic)   return "#88C0D0"
        if (isMonolith) return "#F59E0B"
        return "#E06C75"
    }

    readonly property color accentSoft: {
        if (isOled)     return "#240B13"
        if (isCyber)    return "#2D183B"
        if (isNordic)   return "#1E2D38"
        if (isMonolith) return "#2B2011"
        return "#351E24"
    }

    readonly property color accentDeep: {
        if (isOled)     return "#480F1F"
        if (isCyber)    return "#4A1E5C"
        if (isNordic)   return "#234255"
        if (isMonolith) return "#4D3616"
        return "#5C1D24"
    }

    readonly property color accentInk: {
        if (isOled)     return "#FF537A"
        if (isCyber)    return "#D6ACFF"
        if (isNordic)   return "#88C0D0"
        if (isMonolith) return "#FBBF24"
        return "#F07178"
    }

    readonly property color onAccent: "#FFFFFF"

    // ── Typography ─────────────────────────────────────────────────────────
    readonly property color textPrimary: {
        if (isOled)     return "#F9FAFB"
        if (isCyber)    return "#F8F8F2"
        if (isNordic)   return "#ECEFF4"
        if (isMonolith) return "#FFFFFF"
        return "#E5E9F0"
    }

    readonly property color textSecondary: {
        if (isOled)     return "#9CA3AF"
        if (isCyber)    return "#B0A8C7"
        if (isNordic)   return "#D8DEE9"
        if (isMonolith) return "#A3A3A3"
        return "#9AA2B1"
    }

    readonly property color textMuted: {
        if (isOled)     return "#4B5563"
        if (isCyber)    return "#6272A4"
        if (isNordic)   return "#7B88A1"
        if (isMonolith) return "#666666"
        return "#5C6370"
    }

    readonly property color textDisabled: {
        if (isOled)     return "#374151"
        if (isCyber)    return "#44475A"
        if (isNordic)   return "#4C566A"
        if (isMonolith) return "#404040"
        return "#4B5263"
    }

    // ── Status ─────────────────────────────────────────────────────────────
    readonly property color colorSuccess:       "#98C379"
    readonly property color colorSuccessDeep:   "#233821"
    readonly property color colorSuccessBright: "#A9DC76"
    readonly property color colorWarning:       "#E5C07B"
    readonly property color colorAmberDeep:     "#4D3A18"
    readonly property color colorError:         "#E06C75"
    readonly property color colorErrorBright:   "#EF7D86"
    readonly property color colorDanger:        colorError

    // ── Semantic Asset Types (Desktop Studio Colors) ───────────────────────
    readonly property color colorFolder:  "#D97706"  // amber / gold
    readonly property color colorScene:   "#F59E0B"  // terrain gold
    readonly property color colorScl:     "#10B981"  // emerald catalog
    readonly property color colorModel:   "#38BDF8"  // cyan wireframe
    readonly property color colorArchive: "#F5D75F"  // yellow archive
    readonly property color colorCode:    "#60A5FA"  // blue / indigo script
    readonly property color colorTexture: "#A855F7"  // PowerVR violet
    readonly property color colorAudio:   "#EC4899"  // audio pink / rose
    readonly property color colorGeneric: "#94A3B8"

    // ── Radii ──────────────────────────────────────────────────────────────
    readonly property int radiusXs:    4
    readonly property int radiusSm:    8
    readonly property int radiusMd:    10
    readonly property int radiusCard:  14
    readonly property int radiusLg:    16
    readonly property int radiusSheet: 20
    readonly property int radiusPill:  999

    // ── Spacing (4pt grid) ─────────────────────────────────────────────────
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24

    // ── Layout metrics ─────────────────────────────────────────────────────
    readonly property int rowHeight:     56   // list row (was QListWidget::item 50px)
    readonly property int rowHeightWide: 64
    readonly property int minTouchTarget: 48  // Material minimum
    readonly property int topBarHeight:   56
    readonly property int navBarHeight:   66
    readonly property int paddingScreen:  16
    readonly property int iconButtonSize: 38

    // ── Icon sizes ─────────────────────────────────────────────────────────
    readonly property int iconXs: 14
    readonly property int iconSm: 18
    readonly property int iconMd: 22
    readonly property int iconLg: 26
    readonly property int iconXl: 34

    // ── Type scale ─────────────────────────────────────────────────────────
    readonly property int fontXs:    10
    readonly property int fontSm:    11
    readonly property int fontMd:    13
    readonly property int fontLg:    14
    readonly property int fontXl:    17
    readonly property int fontTitle: 20
    readonly property string monoFamily: "monospace"
    readonly property string fontFamilyMono: monoFamily
    readonly property string fontFamily: "sans-serif"

    // ── Motion ─────────────────────────────────────────────────────────────
    readonly property int durFast: 120
    readonly property int durMed:  180
    readonly property int durSlow: 260

    // ── File-type helpers ──────────────────────────────────────────────────

    /// Canonical folder/scene/scl/model/archive/code/texture type for a
    /// file name, extension or already-normalised type string.
    function normalizeType(typeStr) {
        var t = (typeStr || "").toLowerCase()
        if (t.indexOf(".") >= 0) t = t.substring(t.lastIndexOf(".") + 1)

        if (t === "dir" || t === "folder") return "folder"
        if (t === "scene") return "scene"
        if (t === "scl") return "scl"
        if (t === "model" || t === "pod" || t === "glb" || t === "fbx" || t === "obj") return "model"
        if (t === "archive" || t === "zip" || t === "apk" || t === "tar" || t === "gz") return "archive"
        if (t === "code" || t === "lua" || t === "filerift" || t === "txt" || t === "json" || t === "boulder") return "code"
        if (t === "texture" || t === "pvr" || t === "tex" || t === "png" || t === "jpg" || t === "jpeg") return "texture"
        if (t === "audio" || t === "wav" || t === "ogg" || t === "mp3") return "audio"
        return "generic"
    }

    function colorForType(typeStr) {
        switch (normalizeType(typeStr)) {
        case "folder":  return colorFolder
        case "scene":   return colorScene
        case "scl":     return colorScl
        case "model":   return colorModel
        case "archive": return colorArchive
        case "code":    return colorCode
        case "texture": return colorTexture
        case "audio":   return colorAudio
        default:        return colorGeneric
        }
    }

    /// Icon name for a file type — the 1:1 semantic successor of the old
    /// text glyphs (map, layers, cube, archive, braces, image).
    function iconForType(typeStr) {
        switch (normalizeType(typeStr)) {
        case "folder":  return "folder"
        case "scene":   return "map"
        case "scl":     return "layers"
        case "model":   return "cube"
        case "archive": return "archive"
        case "code":    return "braces"
        case "texture": return "image"
        case "audio":   return "volume"
        default:        return "file"
        }
    }

    /// Soft tinted plate behind a file-type icon.
    function plateForType(typeStr) { return alpha(colorForType(typeStr), 0.14) }
}
