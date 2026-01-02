#pragma once

#include "DwellingHouse.h"
#include <string>

namespace dwelling {

// Rendering options
struct RenderOptions {
    float cellSize = 30.0f;        // Size of one cell in pixels
    float wallThickness = 3.0f;    // Wall line thickness
    float padding = 20.0f;         // Padding around the drawing

    // Colors
    const char* backgroundColor = "#fdf5e6";  // Old lace
    const char* floorColor = "#f5f5dc";       // Beige
    const char* wallColor = "#2c2c2c";        // Dark gray
    const char* doorColor = "#8b4513";        // Saddle brown
    const char* windowColor = "#87ceeb";      // Sky blue
    const char* roomLabelColor = "#666666";   // Gray

    bool showRoomLabels = true;
    bool showDoors = true;
    bool showWindows = true;
    bool showGrid = false;         // Debug grid lines
};

// Write floor plan SVG
void writeFloorPlanSVG(
    const std::string& filename,
    const House& house,
    int floorIndex,
    const RenderOptions& options = RenderOptions{}
);

// Write all floors as multi-page SVG
void writeAllFloorsSVG(
    const std::string& filename,
    const House& house,
    const RenderOptions& options = RenderOptions{}
);

// Write isometric/orthographic 3D view
void writeOrthoViewSVG(
    const std::string& filename,
    const House& house,
    const RenderOptions& options = RenderOptions{}
);

} // namespace dwelling
