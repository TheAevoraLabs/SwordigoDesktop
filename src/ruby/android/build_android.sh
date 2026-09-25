#!/usr/bin/env bash
# ============================================================================
# build_android.sh — Build script for Ruby GG Mobile Android Port
#   Exclusively utilizes Android build tools directly from the TVPG drive:
#   /run/media/quantumcreeper/TVPG/linuxFiles/Applications/AndroidBuildTools
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -d "${SCRIPT_DIR}/src/ruby/android" ]; then
    ANDROID_SRC_DIR="${SCRIPT_DIR}/src/ruby/android"
    PROJECT_ROOT="${SCRIPT_DIR}"
elif [ -f "${SCRIPT_DIR}/../../../CMakeLists.txt" ]; then
    ANDROID_SRC_DIR="${SCRIPT_DIR}"
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
elif [ -f "${SCRIPT_DIR}/../../CMakeLists.txt" ]; then
    ANDROID_SRC_DIR="${SCRIPT_DIR}"
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
elif [ -f "${SCRIPT_DIR}/CMakeLists.txt" ]; then
    ANDROID_SRC_DIR="${SCRIPT_DIR}"
    PROJECT_ROOT="${SCRIPT_DIR}"
else
    ANDROID_SRC_DIR="${SCRIPT_DIR}"
    PROJECT_ROOT="${SCRIPT_DIR}"
fi

# Android Build Tools & SDK resolution (respects environment or falls back to workstation)
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/run/media/quantumcreeper/TVPG/linuxFiles/Applications/AndroidBuildTools}}"

if [ -z "${ANDROID_NDK_ROOT:-}" ]; then
    if [ -d "${ANDROID_SDK_ROOT}/ndk" ]; then
        ANDROID_NDK_ROOT="$(find "${ANDROID_SDK_ROOT}/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
    else
        ANDROID_NDK_ROOT="${ANDROID_SDK_ROOT}/ndk/28.2.13676358"
    fi
fi

if [ -z "${BUILD_TOOLS_DIR:-}" ]; then
    if [ -d "${ANDROID_SDK_ROOT}/build-tools" ]; then
        BUILD_TOOLS_DIR="$(find "${ANDROID_SDK_ROOT}/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
    else
        BUILD_TOOLS_DIR="${ANDROID_SDK_ROOT}/build-tools/35.0.0"
    fi
fi

if [ -z "${PLATFORM_DIR:-}" ]; then
    if [ -d "${ANDROID_SDK_ROOT}/platforms" ]; then
        PLATFORM_DIR="$(find "${ANDROID_SDK_ROOT}/platforms" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
    else
        PLATFORM_DIR="${ANDROID_SDK_ROOT}/platforms/android-36"
    fi
fi

ANDROID_JAR="${PLATFORM_DIR}/android.jar"
TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"

AAPT2="${BUILD_TOOLS_DIR}/aapt2"
D8="${BUILD_TOOLS_DIR}/d8"
ZIPALIGN="$(which zipalign 2>/dev/null || echo "${BUILD_TOOLS_DIR}/zipalign")"
APKSIGNER="$(which apksigner 2>/dev/null || echo "${BUILD_TOOLS_DIR}/apksigner")"
ADB="$(which adb 2>/dev/null || echo "${ANDROID_SDK_ROOT}/platform-tools/adb")"
STRIP="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

echo "=== Ruby GG Mobile Android Build System ==="
echo "SDK:        ${ANDROID_SDK_ROOT}"
echo "NDK:        ${ANDROID_NDK_ROOT}"
echo "BuildTools: ${BUILD_TOOLS_DIR}"
echo "Platform:   android-36"

# Supported ABIs: arm64-v8a (default), armeabi-v7a (arm32), x86_64, x86
TARGET_ABI="${RUBY_TARGET_ABI:-}"
ABIS=("arm64-v8a")

while [[ $# -gt 0 ]]; do
    case "$1" in
        --abi)
            TARGET_ABI="$2"
            shift 2
            ;;
        --all-abis)
            ABIS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86")
            shift
            ;;
        *)
            shift
            ;;
    esac
done

if [ -n "${TARGET_ABI}" ]; then
    ABIS=("${TARGET_ABI}")
fi

BUILD_ROOT="${PROJECT_ROOT}/build-android"
mkdir -p "${BUILD_ROOT}"

READELF="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"

# ============================================================================
# Curated Qt allow-lists — keeps the APK lean.
#   Qt ships ~100 native libs per ABI; a Qt Quick/Controls app only needs a
#   dozen. The lists below are the verified load-time closure of libruby plus
#   the plugins the QML engine actually imports (see qml.qrc / *.qml).
#   Anything not listed is intentionally excluded to shrink the package.
# ============================================================================

# Qt shared libraries (matched as "<base>_<abi>.so")
QT_KEEP_LIBS=(
    libQt6Core
    libQt6Gui
    libQt6Network
    libQt6OpenGL
    libQt6Qml
    libQt6QmlCore
    libQt6QmlModels
    libQt6QmlWorkerScript
    libQt6Quick
    libQt6QuickControls2
    libQt6QuickControls2Impl
    libQt6QuickControls2Basic
    libQt6QuickControls2BasicStyleImpl
    libQt6QuickControls2Material
    libQt6QuickControls2MaterialStyleImpl
    libQt6QuickTemplates2
    libQt6QuickLayouts
    libQt6QuickShapes
    libQt6Svg
)

# Qt plugins (paths relative to <qt>/plugins, matched as "<rel>_<abi>.so")
QT_KEEP_PLUGINS=(
    platforms/libplugins_platforms_qtforandroid
    imageformats/libplugins_imageformats_qsvg
    imageformats/libplugins_imageformats_qjpeg
    imageformats/libplugins_imageformats_qgif
    imageformats/libplugins_imageformats_qico
    iconengines/libplugins_iconengines_qsvgicon
    networkinformation/libplugins_networkinformation_qandroidnetworkinformation
    tls/libplugins_tls_qopensslbackend
)

# QML module directories to keep (paths relative to <qt>/qml). Only the modules
# imported by the app: QtQuick, QtQuick.Controls(+Material), Layouts, Shapes,
# Window, Templates, plus the QtQml/QtCore runtime modules they pull in.
qt_qml_dir_keep() {
    case "$1" in
        QtCore|QtQml|QtQml/Base|QtQml/Models|QtQml/WorkerScript|\
        QtQuick|QtQuick/Window|QtQuick/Layouts|QtQuick/Shapes|QtQuick/Templates|\
        QtQuick/Controls|QtQuick/Controls/impl|\
        QtQuick/Controls/Basic|QtQuick/Controls/Basic/impl|\
        QtQuick/Controls/Material|QtQuick/Controls/Material/impl)
            return 0 ;;
        *) return 1 ;;
    esac
}

# Warn if a staged library's DT_NEEDED dependency is absent from the same dir.
check_staged_deps() {
    local dir="$1" f base dep missing=0
    if [ ! -x "${READELF}" ]; then
        echo "  (dependency check skipped: llvm-readelf not found)"
        return 0
    fi
    for f in "${dir}"/*.so; do
        [ -e "${f}" ] || continue
        base="$(basename "${f}")"
        while IFS= read -r dep; do
            [ -z "${dep}" ] && continue
            [ -e "${dir}/${dep}" ] && continue
            case "${dep}" in
                # Android platform/NDK libraries provided by the OS, never bundled.
                libc.so|libm.so|libdl.so|liblog.so|libz.so|libandroid.so|\
                libEGL.so|libGLESv2.so|libGLESv3.so|libjnigraphics.so)
                    continue ;;
            esac
            echo "  [!] ${base} requires missing ${dep}" >&2
            missing=1
        done < <("${READELF}" -d "${f}" 2>/dev/null | sed -nE 's/.*NEEDED.*\[(.*)\].*/\1/p')
    done
    if [ "${missing}" = 0 ]; then
        echo "  [✓] All staged native dependencies satisfied."
    else
        echo "  [!] Unresolved native dependencies detected — the app may fail to launch." >&2
    fi
}

# The app ships res/values/libs.xml, the manifest Qt itself reads to decide which
# libraries to preload ("qt_libs") and dlopen ("load_local_libs"). Anything the
# manifest promises but the package omits causes a startup UnsatisfiedLinkError,
# so verify the staged files against the manifest for this ABI.
LIBS_XML="${ANDROID_SRC_DIR}/res/values/libs.xml"
check_manifest_libs() {
    local abi="$1" dir="$2" line name f missing=0
    if [ ! -f "${LIBS_XML}" ]; then
        echo "  (manifest check skipped: ${LIBS_XML} not found)"
        return 0
    fi
    while IFS= read -r line; do
        name="${line#*;}"
        case "${name}" in
            Qt6*) f="lib${name}.so" ;;
            *.so) f="${name}" ;;
            *) continue ;;
        esac
        if [ ! -e "${dir}/${f}" ]; then
            echo "  [!] res/values/libs.xml declares ${f} for ${abi}, but it is missing from the package" >&2
            missing=1
        fi
    done < <(grep -oE "<item>${abi};[^<]+</item>" "${LIBS_XML}" | sed -E 's#</?item>##g')

    if [ "${missing}" = 0 ]; then
        echo "  [✓] Package matches res/values/libs.xml manifest for ${abi}."
    else
        echo "  [!] Package is missing libraries declared in res/values/libs.xml." >&2
    fi
}

resolve_qt_abi_dir() {
    local target_abi="$1"
    local abi_normalized="${target_abi//-/_}"
    local qt_arch_name="android_${abi_normalized}"
    if [[ "${target_abi}" == "armeabi-v7a" ]]; then
        qt_arch_name="android_armv7"
    fi
    local res=""

    if [ -n "${QT_ROOT_DIR:-}" ] && [ -f "${QT_ROOT_DIR}/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        res="${QT_ROOT_DIR}"
    elif [ -n "${QT_DIR:-}" ] && [ -f "${QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        res="${QT_DIR}"
    elif [ -n "${QT_DIR:-}" ] && [ -d "${QT_DIR}/6.6.3/${qt_arch_name}" ]; then
        res="${QT_DIR}/6.6.3/${qt_arch_name}"
    elif [ -n "${QT_DIR:-}" ] && [ -d "${QT_DIR}/6.6.3/android_${abi_normalized}" ]; then
        res="${QT_DIR}/6.6.3/android_${abi_normalized}"
    elif [ -n "${Qt6_DIR:-}" ] && [ -f "${Qt6_DIR}/Qt6Config.cmake" ]; then
        res="$(cd "${Qt6_DIR}/../../.." && pwd)"
    elif [ -d "${BUILD_ROOT}/qt6/6.6.3/${qt_arch_name}" ]; then
        res="${BUILD_ROOT}/qt6/6.6.3/${qt_arch_name}"
    elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/${qt_arch_name}" ]; then
        res="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/${qt_arch_name}"
    elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_${abi_normalized}" ]; then
        res="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/android_${abi_normalized}"
    fi

    # Dynamic search fallback for CI environments (e.g. GitHub Actions runners)
    if [ -z "${res}" ] || [ ! -f "${res}/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        local search_hit
        search_hit="$(find /home/runner/work "${BUILD_ROOT}" /opt /usr -name "Qt6Config.cmake" 2>/dev/null | grep -E "${qt_arch_name}|android_${abi_normalized}|android" | head -n 1 || true)"
        if [ -n "${search_hit}" ]; then
            res="$(cd "$(dirname "${search_hit}")/../../.." && pwd)"
        fi
    fi

    if [ -z "${res}" ] || [ ! -d "${res}" ]; then
        res="${BUILD_ROOT}/qt6/6.6.3/android_arm64_v8a"
    fi
    echo "${res}"
}

resolve_qt_host_dir() {
    local qt_abi="$1"
    local res=""
    if [ -n "${QT_HOST_PATH:-}" ] && [ -d "${QT_HOST_PATH}" ]; then
        res="${QT_HOST_PATH}"
    elif [ -d "${BUILD_ROOT}/qt6/6.6.3/gcc_64" ]; then
        res="${BUILD_ROOT}/qt6/6.6.3/gcc_64"
    elif [ -d "/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/gcc_64" ]; then
        res="/home/quantumcreeper/SwordigoDesktop/build-android/qt6/6.6.3/gcc_64"
    elif [ -n "${qt_abi}" ] && [ -d "${qt_abi}/../gcc_64" ]; then
        res="$(cd "${qt_abi}/../gcc_64" && pwd)"
    else
        res="/usr"
    fi
    echo "${res}"
}

for ABI in "${ABIS[@]}"; do
    echo ""
    echo "--- Building native shared library for ${ABI} ---"
    ABI_BUILD_DIR="${BUILD_ROOT}/${ABI}"
    mkdir -p "${ABI_BUILD_DIR}"

    QT_ABI_DIR="$(resolve_qt_abi_dir "${ABI}")"
    QT_HOST_DIR="$(resolve_qt_host_dir "${QT_ABI_DIR}")"
    echo "Qt6 Android ABI: ${QT_ABI_DIR}"
    echo "Qt6 Host Path:   ${QT_HOST_DIR}"

    cmake -S "${ANDROID_SRC_DIR}" -B "${ABI_BUILD_DIR}" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DANDROID_ABI="${ABI}" \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_STL=c++_shared \
        -DCMAKE_FIND_ROOT_PATH="${QT_ABI_DIR}" \
        -DCMAKE_PREFIX_PATH="${QT_ABI_DIR}" \
        -DQT_HOST_PATH="${QT_HOST_DIR}" \
        -DQt6_DIR="${QT_ABI_DIR}/lib/cmake/Qt6" \
        -DRUBY_BUILD_VERSION="${RUBY_VERSION_NAME:-v1.4}"

    cmake --build "${ABI_BUILD_DIR}" --config Release -j"$(nproc)"
    LIBRUBY_SO="$(find "${ABI_BUILD_DIR}" -name "libruby.so" | head -n 1)"
    echo "[✓] Native library built: ${LIBRUBY_SO:-${ABI_BUILD_DIR}/libruby.so}"
done

echo ""
echo "=== Packaging APK ==="
PACKAGE_DIR="${BUILD_ROOT}/apk_staging"
rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/lib" "${PACKAGE_DIR}/res" "${PACKAGE_DIR}/assets"

# Copy native libraries for each ABI
for ABI in "${ABIS[@]}"; do
    mkdir -p "${PACKAGE_DIR}/lib/${ABI}"
    
    # 1. Main application library.
    #    QtLoader.java resolves the entry library as
    #        getString("android.app.lib_name") + "_" + preferredAbi
    #    i.e. INSTALLED_ABI/lib<lib_name>_<abi>.so. For the arm64-v8a build that
    #    is exactly "libruby_arm64-v8a.so" — so a plain "libruby.so" copy is
    #    never loaded and only wastes ~15 MB in the APK. Ship the ABI-suffixed
    #    name only.
    LIBRUBY_SO="$(find "${BUILD_ROOT}/${ABI}" -name "libruby.so" | head -n 1)"
    if [ -z "${LIBRUBY_SO}" ] || [ ! -f "${LIBRUBY_SO}" ]; then
        echo "ERROR: libruby.so not found under ${BUILD_ROOT}/${ABI}" >&2
        exit 1
    fi
    cp "${LIBRUBY_SO}" "${PACKAGE_DIR}/lib/${ABI}/libruby_${ABI}.so"

    # 1b. In-Engine Swordigo Runner and GlossHook libraries
    LIBRUBY_RUNNER_SO="$(find "${BUILD_ROOT}/${ABI}" -name "libruby_runner.so" | head -n 1)"
    if [ -n "${LIBRUBY_RUNNER_SO}" ] && [ -f "${LIBRUBY_RUNNER_SO}" ]; then
        cp "${LIBRUBY_RUNNER_SO}" "${PACKAGE_DIR}/lib/${ABI}/libruby_runner.so"
        echo "[✓] Copied libruby_runner.so for ${ABI}"
    fi
    if [ -f "${ANDROID_SRC_DIR}/libs/${ABI}/libGlossHook.so" ]; then
        cp "${ANDROID_SRC_DIR}/libs/${ABI}/libGlossHook.so" "${PACKAGE_DIR}/lib/${ABI}/libGlossHook.so"
        echo "[✓] Copied libGlossHook.so for ${ABI}"
    fi

    # 2. NDK libc++_shared.so
    LIBCXX="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib"
    if [[ "${ABI}" == "arm64-v8a" ]]; then
        cp "${LIBCXX}/aarch64-linux-android/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    elif [[ "${ABI}" == "armeabi-v7a" ]]; then
        cp "${LIBCXX}/arm-linux-androideabi/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    elif [[ "${ABI}" == "x86_64" ]]; then
        cp "${LIBCXX}/x86_64-linux-android/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    elif [[ "${ABI}" == "x86" ]]; then
        cp "${LIBCXX}/i686-linux-android/libc++_shared.so" "${PACKAGE_DIR}/lib/${ABI}/"
    fi

    # 3. Qt shared libraries and plugins for this ABI (curated allow-list)
    QT_ABI_DIR="$(resolve_qt_abi_dir "${ABI}")"

    echo "Copying curated Qt shared libraries from ${QT_ABI_DIR}/lib..."
    for base in "${QT_KEEP_LIBS[@]}"; do
        copied=0
        for f in "${QT_ABI_DIR}/lib/${base}_"*.so; do
            [ -f "${f}" ] || continue
            cp "${f}" "${PACKAGE_DIR}/lib/${ABI}/"
            copied=1
        done
        [ "${copied}" = 1 ] || echo "  (warn) Qt lib not found: ${base}_<abi>.so" >&2
    done

    echo "Copying curated Qt plugins from ${QT_ABI_DIR}/plugins..."
    for rel in "${QT_KEEP_PLUGINS[@]}"; do
        copied=0
        for f in "${QT_ABI_DIR}/plugins/${rel}_"*.so; do
            [ -f "${f}" ] || continue
            cp "${f}" "${PACKAGE_DIR}/lib/${ABI}/"
            copied=1
        done
        [ "${copied}" = 1 ] || echo "  (warn) Qt plugin not found: ${rel}_<abi>.so" >&2
    done

    echo "Copying curated Qt QML plugins from ${QT_ABI_DIR}/qml..."
    if [ -d "${QT_ABI_DIR}/qml" ]; then
        while IFS= read -r d; do
            rel="${d#${QT_ABI_DIR}/qml/}"
            qt_qml_dir_keep "${rel}" || continue
            find "${QT_ABI_DIR}/qml/${rel}" -maxdepth 1 -name "*.so" \
                -exec cp {} "${PACKAGE_DIR}/lib/${ABI}/" \;
        done < <(find "${QT_ABI_DIR}/qml" -mindepth 1 -type d)
    fi

    # 4. Strip unneeded debug symbols from native shared libraries for release
    echo "Stripping debug symbols from ${PACKAGE_DIR}/lib/${ABI}..."
    if [ -x "${STRIP}" ]; then
        find "${PACKAGE_DIR}/lib/${ABI}" -name "*.so" -exec "${STRIP}" --strip-unneeded {} + 2>/dev/null || true
    fi

    # 5. Guards: every staged library's DT_NEEDED dependency must be present,
    #    and every library promised by res/values/libs.xml must be packaged.
    echo "Verifying staged native dependencies for ${ABI}..."
    check_staged_deps "${PACKAGE_DIR}/lib/${ABI}"
    echo "Verifying package against res/values/libs.xml for ${ABI}..."
    check_manifest_libs "${ABI}" "${PACKAGE_DIR}/lib/${ABI}"
done

# Package only the QML modules the app imports into assets/qml.
echo "Packaging curated QML modules into assets/qml..."
QML_ASSET_DIR="${PACKAGE_DIR}/assets/qml"
mkdir -p "${QML_ASSET_DIR}"
QT_QML_DIR="$(resolve_qt_abi_dir "${ABIS[0]}")/qml"
if [ -d "${QT_QML_DIR}" ]; then
    while IFS= read -r d; do
        rel="${d#${QT_QML_DIR}/}"
        qt_qml_dir_keep "${rel}" || continue
        mkdir -p "${QML_ASSET_DIR}/${rel}"
        # Copy only files directly in the module dir (never recurse into
        # sibling/unused style or lab modules).
        find "${QT_QML_DIR}/${rel}" -maxdepth 1 -type f \
            -exec cp {} "${QML_ASSET_DIR}/${rel}/" \;
    done < <(find "${QT_QML_DIR}" -mindepth 1 -type d)

    # Drop tooling-only metadata (Qt Creator / qmllint) and stray native libs.
    find "${QML_ASSET_DIR}" -name "*.so" -delete
    find "${QML_ASSET_DIR}" -name "*.qmltypes" -delete
    find "${QML_ASSET_DIR}" -name "*.metainfo" -delete
    find "${QML_ASSET_DIR}" -type d -name designer -exec rm -rf {} + 2>/dev/null || true
fi

# Compile resources using aapt2
"${AAPT2}" compile --dir "${ANDROID_SRC_DIR}/res" -o "${BUILD_ROOT}/compiled_res.zip"

AAPT2_LINK_CMD=(
    "${AAPT2}" link -o "${BUILD_ROOT}/unaligned.apk"
    -I "${ANDROID_JAR}"
    --manifest "${ANDROID_SRC_DIR}/AndroidManifest.xml"
    -A "${PACKAGE_DIR}/assets"
)
if [ -n "${RUBY_VERSION_CODE:-}" ]; then
    AAPT2_LINK_CMD+=(--version-code "${RUBY_VERSION_CODE}")
fi
if [ -n "${RUBY_VERSION_NAME:-}" ]; then
    AAPT2_LINK_CMD+=(--version-name "${RUBY_VERSION_NAME}")
fi
if [ -n "${RUBY_VERSION_CODE:-}" ] || [ -n "${RUBY_VERSION_NAME:-}" ]; then
    AAPT2_LINK_CMD+=(--replace-version)
fi
AAPT2_LINK_CMD+=(
    "${BUILD_ROOT}/compiled_res.zip"
    --auto-add-overlay
)
"${AAPT2_LINK_CMD[@]}"

# Compile Java sources
JAVA_OUT="${BUILD_ROOT}/java_classes"
rm -rf "${JAVA_OUT}"
mkdir -p "${JAVA_OUT}"

PRIMARY_QT_ABI="$(resolve_qt_abi_dir "arm64-v8a")"
QT_JAR_DIR="${PRIMARY_QT_ABI}/jar"
QT_SRC_DIR="${PRIMARY_QT_ABI}/src/android/java/src"

QT_CP=""
for j in "${QT_JAR_DIR}"/*.jar; do
    if [ -f "${j}" ]; then
        QT_CP="${QT_CP}:${j}"
    fi
done

JAVA_SRCS=()
while IFS= read -r -d '' src; do
    JAVA_SRCS+=("${src}")
done < <(find "${ANDROID_SRC_DIR}/java" -name "*.java" -print0)
while IFS= read -r -d '' src; do
    JAVA_SRCS+=("${src}")
done < <(find "${QT_SRC_DIR}" -name "*.java" -print0)

javac -source 11 -target 11 -cp "${ANDROID_JAR}${QT_CP}" \
    -d "${JAVA_OUT}" \
    "${JAVA_SRCS[@]}"

# Dex classes
DEX_DIR="${BUILD_ROOT}/dex"
mkdir -p "${DEX_DIR}"
CLASS_FILES=()
while IFS= read -r -d '' cls; do
    CLASS_FILES+=("${cls}")
done < <(find "${JAVA_OUT}" -name "*.class" -print0)

"${D8}" --output "${DEX_DIR}" --lib "${ANDROID_JAR}" "${CLASS_FILES[@]}" "${QT_JAR_DIR}"/*.jar

# Add classes.dex to APK
(cd "${DEX_DIR}" && zip -u "${BUILD_ROOT}/unaligned.apk" classes*.dex)

# Add native libraries and assets to APK
(cd "${PACKAGE_DIR}" && zip -r -u "${BUILD_ROOT}/unaligned.apk" lib assets)

# Align APK
FINAL_APK_NAME="${RUBY_APK_NAME:-RubyTouch-${ABIS[0]}.apk}"
FINAL_APK="${PROJECT_ROOT}/bin/${FINAL_APK_NAME}"
mkdir -p "${PROJECT_ROOT}/bin"
"${ZIPALIGN}" -f 4 "${BUILD_ROOT}/unaligned.apk" "${FINAL_APK}"

# Debug signing if keystore available or generate ephemeral debug key
KEYSTORE="${BUILD_ROOT}/debug.keystore"
if [ ! -f "${KEYSTORE}" ]; then
    keytool -genkey -v -keystore "${KEYSTORE}" -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi
"${APKSIGNER}" sign --ks "${KEYSTORE}" --ks-pass pass:android --ks-key-alias androiddebugkey --key-pass pass:android "${FINAL_APK}"

echo "[✓] Successfully built APK: ${FINAL_APK}"

# Check for ADB device
if "${ADB}" devices | grep -q -E "[a-zA-Z0-9_-]+\s+device$"; then
    echo "[*] Connected Android device detected. Installing..."
    "${ADB}" install -r "${FINAL_APK}"
    echo "[*] Launching Ruby Touch (Ruby Mobile)..."
    "${ADB}" shell am start -n "in.aevora.ruby/.RubyActivity"
    echo "[✓] Installed and launched Ruby Touch (Ruby Mobile) on device!"
else
    echo "NOTE: No ADB device in 'device' state currently. Ensure USB debugging is ON."
fi
