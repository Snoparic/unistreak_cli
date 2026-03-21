#!/bin/bash
# scripts/build-all.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

echo "=== UniStreak CLI Client Build Script ==="
echo "Project root: $PROJECT_ROOT"

build_target() {
    local target_name=$1
    local toolchain=$2
    local build_subdir=$3
    
    echo ""
    echo ">>> Building: $target_name"
    
    local target_build_dir="$BUILD_DIR/$build_subdir"
    mkdir -p "$target_build_dir"
    
    cmake -S "$PROJECT_ROOT" -B "$target_build_dir" \
        -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/$toolchain" \
        -DCMAKE_BUILD_TYPE=Release
    
    cmake --build "$target_build_dir" --config Release --parallel
    
    echo "✓ Built: $target_name"
}

mkdir -p "$BUILD_DIR"

build_target "Linux x86_64 (native)" "toolchain-linux-x86_64.cmake" "linux-x86_64"
build_target "Linux x86" "toolchain-linux-x86.cmake" "linux-x86"
build_target "Windows x64" "toolchain-windows-x64.cmake" "windows-x64"
build_target "Windows x86" "toolchain-win32-x86.cmake" "windows-x86"

echo ""
echo "=== Build Complete ==="
echo "Executables location: $BUILD_DIR"
find "$BUILD_DIR" -name "unistreak_cli*" -type f -executable
