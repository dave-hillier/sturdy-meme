#!/bin/bash
# Run the Vulkan game outside the bundle with console logging
# This sets up the MoltenVK ICD path that would normally come from Info.plist

# Codex Desktop runs shell commands inside a macOS seatbelt sandbox by default.
# That sandbox cannot register Cocoa apps, and SDL's Cocoa backend aborts during SDL_Init.
# Bail out early with a clear message instead of crashing.
if [[ "${CODEX_SANDBOX:-}" == "seatbelt" ]]; then
  echo "ERROR: Detected Codex sandbox (CODEX_SANDBOX=seatbelt)."
  echo "SDL video init uses Cocoa/AppKit and will abort in this sandbox."
  echo ""
  echo "Run this script from a normal Terminal session, or in Codex approve running it outside the sandbox."
  exit 1
fi

# Set up Vulkan ICD path for MoltenVK
export VK_ICD_FILENAMES="/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json:/usr/local/share/vulkan/icd.d/MoltenVK_icd.json"

# Enable Vulkan validation layers for debugging (optional - comment out if too noisy)
# export VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"

# Run from the bundle directory so resource paths work
cd "$(dirname "$0")/build/debug/vulkan-game.app/Contents/MacOS"
./vulkan-game "$@"
