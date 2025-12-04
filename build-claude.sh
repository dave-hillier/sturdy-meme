#!/bin/bash
# Build script for Claude environment
# Bootstraps vcpkg if needed and builds the project

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VCPKG_DIR="$SCRIPT_DIR/vcpkg"

# Run setup if vcpkg not present
if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "Running setup..."
    "$SCRIPT_DIR/setup-claude.sh"
fi

export VCPKG_ROOT="$VCPKG_DIR"
# Force vcpkg to use system cmake/ninja instead of downloading its own
export VCPKG_FORCE_SYSTEM_BINARIES=1

echo "Configuring with cmake --preset claude..."
cmake --preset claude

echo "Building..."
cmake --build build/claude

echo "Build complete!"
