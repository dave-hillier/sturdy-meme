#pragma once

class ITerrainControl;
class PhysicsTerrainTileManager;
class Camera;

namespace GuiTileLoaderTab {
    // Tile loader visualization mode
    enum class TileViewMode {
        GPU,      // Active GPU tiles (loaded with GPU resources)
        CPU,      // All tiles with CPU data (includes GPU + CPU-only + base LOD)
        Physics   // Physics collision tiles
    };

    struct State {
        TileViewMode viewMode = TileViewMode::GPU;
    };

    void render(ITerrainControl& terrain, PhysicsTerrainTileManager* physicsTerrainTiles,
                const Camera& camera, State& state);
}
