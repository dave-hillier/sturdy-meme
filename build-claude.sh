#!/bin/bash
# Build script for Claude environment (Linux only)
# Bootstraps vcpkg if needed and builds the project
#
# Usage:
#   ./build-claude.sh              - Build shaders, headers, and C++ (skip terrain tools)
#   ./build-claude.sh --full       - Full build including terrain preprocessing tools
#   ./build-claude.sh --shaders    - Shaders and generated headers only

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VCPKG_DIR="$SCRIPT_DIR/vcpkg"
BUILD_TYPE="default"

# Parse arguments
if [ "$1" == "--full" ]; then
    BUILD_TYPE="full"
elif [ "$1" == "--shaders" ]; then
    BUILD_TYPE="shaders"
fi

# This script is for Linux environments without vcpkg pre-installed
# On macOS, use the standard cmake commands with your system vcpkg
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Error: build-claude.sh is for Linux environments only."
    echo ""
    echo "On macOS, use the standard build commands:"
    echo "  cmake --preset debug && cmake --build build/debug"
    echo "  ./run-debug.sh"
    echo ""
    echo "Make sure VCPKG_ROOT is set to your vcpkg installation."
    exit 1
fi

# Run setup if vcpkg not present
if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "Running setup..."
    "$SCRIPT_DIR/setup-claude.sh"
fi

export VCPKG_ROOT="$VCPKG_DIR"
# Force vcpkg to use system cmake/ninja instead of downloading its own
export VCPKG_FORCE_SYSTEM_BINARIES=1

case "$BUILD_TYPE" in
    full)
        echo "Configuring with cmake --preset claude (full build)..."
        cmake --preset claude
        echo "Building everything..."
        cmake --build build/claude
        ;;
    shaders)
        echo "Configuring with cmake --preset claude..."
        cmake --preset claude -DSKIP_TERRAIN_PREPROCESSING=ON
        echo "Building shaders and generated headers only..."
        cmake --build build/claude --target shader_reflect
        cmake --build build/claude --target shaders
        cmake --build build/claude --target generated_headers
        ;;
    *)
        echo "Configuring with cmake --preset claude (skip terrain preprocessing)..."
        cmake --preset claude -DSKIP_TERRAIN_PREPROCESSING=ON
        echo "Building shaders, headers, and C++..."
        cmake --build build/claude
        ;;
esac

echo "Build complete!"
