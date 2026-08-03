#!/bin/bash
#
# PhrozenOrca RESIN (mixed resin/FDM) build script for macOS
#
# Same as build_release_macos.sh but passes -DPHROZEN_ORCA_ENABLE_RESIN=ON so
# resin-specific features/params are compiled in for this build only.
# Uses a separate build directory (build-resin) and deps directory
# (deps/build-resin) so it never shares CMake cache or output with the regular
# (non-resin) build -- mirroring build_resin_release_vs2022.bat on Windows.
#
# 為什麼要獨立成一支腳本，而不是在 build_release_macos.sh 加旗標:
#   主線（phrozen-custom-dev）的共用檔案不得夾帶任何 resin 判斷。共用檔案一旦
#   分歧，主線每次更新 build_release_macos.sh 都會在 merge 進 resin 支線時衝突，
#   而主線的 FDM 更新是常態、resin 回主線則是一兩年後的事，維護成本完全不對稱。
#   獨立檔案主線不會使用到，因此可以自由演進。
#
# 維護提醒:
#   本檔案是 build_release_macos.sh 的變體副本。主線修改 build_release_macos.sh
#   時本檔案不會自動跟進，須人工比對。此漂移由 .github/education-ci-parity.lock
#   的機制偵測（見 openspec/changes/phrozen-education-cicd）。

set -e
set -o pipefail

# --- resin/education 變體的固定設定 ------------------------------------------
#
# APP_NAME 必須與 CMake 的 SLIC3R_APP_KEY 一致: PHROZEN_ORCA_ENABLE_RESIN=ON 時
# CMakeLists.txt 會把它改成 "PhrozenOrca-Education"，而 macOS 的 .app 套件名稱與
# Contents/MacOS/ 底下的執行檔名稱都由它決定（見 src/CMakeLists.txt 的 OUTPUT_NAME
# 與 Info.plist.in 的 CFBundleExecutable）。這兩者不一致的話 app 會無法啟動。
APP_NAME="PhrozenOrca-Education"

# 與主線的 build/ 及 deps/build/ 完全分開，本機同時建兩個變體時不會互相覆蓋。
BUILD_DIR_NAME="build-resin"
DEPS_BUILD_DIR_NAME="build-resin"
# ----------------------------------------------------------------------------

while getopts ":dpa:snt:xbc:h" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    n )
        export NIGHTLY_BUILD="1"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    h ) echo "Usage: ./build_resin_release_macos.sh [-d]"
        echo "   (RESIN/education variant -- always builds with PHROZEN_ORCA_ENABLE_RESIN=ON)"
        echo "   -d: Build deps only"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -n: Nightly build"
        echo "   -t: Specify minimum version of the target platform, default is 11.3"
        echo "   -x: Use Ninja CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -1: Use single job for building"
        exit 0
        ;;
    * )
        ;;
  esac
done

# Set defaults

if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="11.3"
fi

echo "Build params:"
echo " - RESIN build: PHROZEN_ORCA_ENABLE_RESIN=ON (app bundle: $APP_NAME.app)"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo " - BUILD DIRS: $BUILD_DIR_NAME/ and deps/$DEPS_BUILD_DIR_NAME/"
echo

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_BUILD_DIR="$PROJECT_DIR/$BUILD_DIR_NAME/$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"
DEPS_BUILD_DIR="$DEPS_DIR/$DEPS_BUILD_DIR_NAME/$ARCH"
DEPS="$DEPS_BUILD_DIR/PhrozenOrca_deps"

# Fix for Multi-config generators
if [ "$SLICER_CMAKE_GENERATOR" == "Xcode" ]; then
    export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"
else
    export BUILD_DIR_CONFIG_SUBDIR=""
fi

function build_deps() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/$BUILD_DIR_NAME/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/$DEPS_BUILD_DIR_NAME/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/PhrozenOrca_dep"

            echo "Building deps..."
            (
                set -x
                mkdir -p "$DEPS"
                cd "$DEPS_BUILD_DIR"
                if [ "1." != "$BUILD_ONLY". ]; then
                    cmake "${DEPS_DIR}" \
                        -G "${DEPS_CMAKE_GENERATOR}" \
                        -DDESTDIR="$DEPS" \
                        -DOPENSSL_ARCH="darwin64-${_ARCH}-cc" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
                fi
                cmake --build . --config "$BUILD_CONFIG" --target deps
            )
        fi
    done
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        cd "$DEPS_DIR"
        tar -zcvf "PhrozenOrca-Education_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "$DEPS_BUILD_DIR_NAME"
    )
}

function build_slicer() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/$BUILD_DIR_NAME/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/$DEPS_BUILD_DIR_NAME/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/PhrozenOrca_dep"

            echo "Building slicer for $_ARCH..."
            (
                set -x
            mkdir -p "$PROJECT_BUILD_DIR"
            cd "$PROJECT_BUILD_DIR"
            if [ "1." != "$BUILD_ONLY". ]; then
                cmake "${PROJECT_DIR}" \
                    -G "${SLICER_CMAKE_GENERATOR}" \
                    -DBBL_RELEASE_TO_PUBLIC=1 \
                    -DPHROZEN_ORCA_ENABLE_RESIN=ON \
                    -DCMAKE_PREFIX_PATH="$DEPS/usr/local" \
                    -DCMAKE_INSTALL_PREFIX="$PWD/$APP_NAME" \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_MACOSX_RPATH=ON \
                    -DCMAKE_INSTALL_RPATH="${DEPS}/usr/local" \
                    -DCMAKE_MACOSX_BUNDLE=ON \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
        )

        echo "Verify localization with gettext..."
        (
            cd "$PROJECT_DIR"
            ./scripts/run_gettext.sh
        )

        echo "Fix macOS app package..."
        (
            cd "$PROJECT_BUILD_DIR"
            mkdir -p "$APP_NAME"
            cd "$APP_NAME"
            # remove previously built app
            rm -rf "./$APP_NAME.app"
            # fully copy newly built app
            cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/$APP_NAME.app" "./$APP_NAME.app"
            # fix resources
            resources_path=$(readlink "./$APP_NAME.app/Contents/Resources")
            rm "./$APP_NAME.app/Contents/Resources"
            cp -R "$resources_path" "./$APP_NAME.app/Contents/Resources"
            # delete .DS_Store file
            find "./$APP_NAME.app/" -name '.DS_Store' -delete
        )

    fi
    done
}

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/$BUILD_DIR_NAME/$ARCH"

    # Create universal binary
    echo "Creating universal binary..."
    mkdir -p "$PROJECT_BUILD_DIR/$APP_NAME"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/$APP_NAME/$APP_NAME.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$PROJECT_DIR/$BUILD_DIR_NAME/arm64/$APP_NAME/$APP_NAME.app" "$UNIVERSAL_APP"

    # Get the binary path inside the .app bundle.
    # 這個檔名同樣來自 CMake 的 OUTPUT_NAME（= SLIC3R_APP_KEY），未跟著 APP_NAME
    # 一起變動的話 lipo 會直接找不到輸入檔而失敗。
    BINARY_PATH="Contents/MacOS/$APP_NAME"

    # Create universal binary using lipo
    lipo -create \
        "$PROJECT_DIR/$BUILD_DIR_NAME/x86_64/$APP_NAME/$APP_NAME.app/$BINARY_PATH" \
        "$PROJECT_DIR/$BUILD_DIR_NAME/arm64/$APP_NAME/$APP_NAME.app/$BINARY_PATH" \
        -output "$UNIVERSAL_APP/$BINARY_PATH"

    echo "Universal binary created at $UNIVERSAL_APP"
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, all."
        exit 1
        ;;
esac

if [ "$ARCH" = "universal" ] && [ "$BUILD_TARGET" != "deps" ]; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi
