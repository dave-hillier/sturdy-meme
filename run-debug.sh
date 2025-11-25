#!/bin/bash
# Run the Vulkan game outside the bundle with console logging
# This sets up the MoltenVK ICD path that would normally come from Info.plist

# Set up Vulkan ICD path for MoltenVK
export VK_ICD_FILENAMES="/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json:/usr/local/share/vulkan/icd.d/MoltenVK_icd.json"

# Enable Vulkan validation layers for debugging (optional - comment out if too noisy)
# export VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"

# Run from the bundle directory so resource paths work
cd "$(dirname "$0")/build/debug/vulkan-game.app/Contents/MacOS"
./vulkan-game "$@"
