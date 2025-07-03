#!/usr/bin/env bash
# build.sh: if "rebuild" is given, performs a clean build; otherwise, just builds
# Cross-platform support (Windows, Linux, macOS)

set -euo pipefail

# Platform detection
detect_platform() {
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
        echo "windows"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    else
        echo "unknown"
    fi
}

PLATFORM=$(detect_platform)
BUILD_DIR="build"

# Platform-specific settings
case $PLATFORM in
    "windows")
        EXE_SUFFIX=".exe"
        RM_CMD="rm -rf"
        MKDIR_CMD="mkdir -p"
        ;;
    "macos"|"linux")
        EXE_SUFFIX=""
        RM_CMD="rm -rf"
        MKDIR_CMD="mkdir -p"
        ;;
    *)
        echo "[ERROR] Unsupported platform: $OSTYPE"
        exit 1
        ;;
esac

echo "[INFO] Platform: $PLATFORM"

# Clean build if "rebuild" argument is given
if [ "${1-}" == "rebuild" ]; then
    if [ -d "$BUILD_DIR" ]; then
        echo "[INFO] Rebuild: removing $BUILD_DIR"
        $RM_CMD "$BUILD_DIR"
    fi
fi

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo "[INFO] Creating: $BUILD_DIR"
    $MKDIR_CMD "$BUILD_DIR"
fi

# Change to build directory
cd "$BUILD_DIR"

# Run CMake configuration if not already done
if [ ! -f "CMakeCache.txt" ]; then
    echo "[INFO] Running CMake configuration"
    cmake ..
fi

# Build project
echo "[INFO] Starting build"
cmake --build . --config Release

# Detect executable
EXECUTABLE=""
if [ -f "server${EXE_SUFFIX}" ]; then
    EXECUTABLE="./server${EXE_SUFFIX}"
elif [ -f "Release/server${EXE_SUFFIX}" ]; then
    EXECUTABLE="./Release/server${EXE_SUFFIX}"
elif [ -f "Debug/server${EXE_SUFFIX}" ]; then
    EXECUTABLE="./Debug/server${EXE_SUFFIX}"
else
    echo "[ERROR] Executable not found (server${EXE_SUFFIX})"
    echo "[INFO] Files in build directory:"
    ls -la
    exit 1
fi

# Run the server
echo "[INFO] Launching server: $EXECUTABLE"
"$EXECUTABLE"
