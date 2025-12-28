# Procedural Cities - Phase 1: Foundation & Data Structures

[← Back to Index](README.md)

## 3. Phase 1: Foundation & Data Structures

### 3.1 Core Data Types

#### 3.1.1 Settlement Definition

```cpp
// tools/settlement_generator/SettlementTypes.h

enum class SettlementZone : uint8_t {
    Residential,        // Housing
    Commercial,         // Shops, inns, markets
    Industrial,         // Smithy, tannery, mill
    Religious,          // Church, chapel, cemetery
    Administrative,     // Town hall, guildhalls
    Agricultural,       // Barns, silos, fields
    Maritime,           // Quays, warehouses, net sheds
    Defensive,          // Walls, gates, towers
    Open,               // Squares, greens, commons

    Count
};

struct SettlementDefinition {
    uint32_t id;
    SettlementType type;
    glm::vec2 center;           // World coordinates
    float radius;               // Approximate extent
    uint64_t seed;              // Deterministic generation

    // Constraints from terrain/biome
    float groundLevel;          // Average terrain height
    float maxSlope;             // Steepest allowable building site
    BiomeZone primaryBiome;

    // Generation parameters (from config)
    std::string templateId;     // Optional layout template
    float density;              // Building density multiplier
    float organicness;          // 0=grid, 1=organic
    float wealth;               // Affects building quality
    float age;                  // Settlement age (affects style)
};
```

#### 3.1.2 Building Lot

```cpp
struct BuildingLot {
    uint32_t id;
    glm::vec2 position;         // Center in world coords
    float rotation;             // Facing direction (radians)
    glm::vec2 dimensions;       // Lot width x depth
    SettlementZone zone;

    // Constraints
    float groundLevel;          // After leveling
    bool frontsStreet;          // Must have street access
    uint32_t streetEdge;        // Which edge faces street (0-3)

    // Building assignment (filled during generation)
    std::string buildingTypeId;
    uint64_t buildingSeed;
};
```

#### 3.1.3 Street Segment

```cpp
struct StreetSegment {
    uint32_t id;
    glm::vec2 start;
    glm::vec2 end;
    float width;

    enum class Type : uint8_t {
        MainStreet,     // Primary through-road
        Street,         // Secondary streets
        Lane,           // Narrow residential
        Alley,          // Very narrow service access
        Path            // Pedestrian only
    } type;

    // Connections
    std::vector<uint32_t> connectedSegments;
    std::vector<uint32_t> adjacentLots;    // Left and right
};
```

### 3.2 Configuration System

#### 3.2.1 Settlement Templates

```json
// assets/settlements/templates/english_village.json
{
    "id": "english_village",
    "name": "English Village",
    "applicable_types": ["Village", "Hamlet"],

    "layout": {
        "pattern": "linear_organic",
        "main_street_width": 8.0,
        "secondary_street_width": 4.0,
        "min_lot_size": [10, 15],
        "max_lot_size": [25, 40],
        "lot_setback": 2.0
    },

    "zones": {
        "church": {
            "count": 1,
            "placement": "prominent",
            "min_distance_from_center": 0,
            "max_distance_from_center": 50
        },
        "inn": {
            "count": [1, 2],
            "placement": "main_street",
            "near_intersection": true
        },
        "residential": {
            "density": 0.7,
            "fill_remaining": true
        }
    },

    "features": {
        "village_green": {
            "probability": 0.8,
            "min_size": 400,
            "max_size": 1200
        },
        "pond": {
            "probability": 0.4,
            "requires": ["low_ground", "water_table"]
        }
    }
}
```

#### 3.2.2 Building Type Registry

```json
// assets/buildings/types/peasant_cottage.json
{
    "id": "peasant_cottage",
    "name": "Peasant Cottage",
    "category": "residential",
    "applicable_zones": ["Residential"],
    "era": "high_medieval",
    "region": "south_coast_england",

    "footprint": {
        "min_width": 4,
        "max_width": 7,
        "min_depth": 8,
        "max_depth": 12,
        "shapes": ["rectangle"]
    },

    "floors": {
        "min": 1,
        "max": 1,
        "ground_floor_height": 2.2,
        "has_loft": true,
        "loft_height": 1.6
    },

    "structure": {
        "type": "cruck_frame",
        "cruck_bays": [2, 3],
        "post_and_beam_visible": true
    },

    "roof": {
        "types": ["gable"],
        "pitch_range": [45, 55],
        "overhang": 0.3,
        "materials": ["thatch", "thatch"],
        "thatch_thickness": 0.3
    },

    "facade": {
        "wall_materials": ["wattle_daub"],
        "wall_materials_by_biome": {
            "ChalkCliff": ["flint_rubble", "flint_with_clunch"],
            "Grassland": ["wattle_daub", "cob"],
            "Agricultural": ["wattle_daub", "timber_frame"]
        },
        "window_styles": ["unglazed_shutter", "oiled_linen"],
        "door_styles": ["plank_batten"],
        "door_count": 1
    },

    "interior": {
        "central_hearth": true,
        "smoke_hole": true,
        "chimney": false
    },

    "props": {
        "croft": { "probability": 0.9, "placement": "rear" },
        "wattle_fence": { "probability": 0.7 },
        "midden": { "probability": 0.6, "placement": "rear_corner" },
        "woodpile": { "probability": 0.8 }
    }
}
```

```json
// assets/buildings/types/longhouse.json
{
    "id": "longhouse",
    "name": "Longhouse",
    "category": "residential_agricultural",
    "applicable_zones": ["Residential", "Agricultural"],
    "era": "high_medieval",
    "description": "Combined dwelling and byre under one roof",

    "footprint": {
        "min_width": 5,
        "max_width": 7,
        "min_depth": 15,
        "max_depth": 25,
        "shapes": ["rectangle"]
    },

    "structure": {
        "type": "cruck_frame",
        "cruck_bays": [3, 5],
        "cross_passage": true,
        "byre_end": "downslope"
    },

    "floors": {
        "ground_floor_height": 2.4,
        "has_loft": true,
        "loft_over_dwelling_only": true
    },

    "roof": {
        "types": ["gable"],
        "pitch_range": [48, 55],
        "materials": ["thatch"],
        "ridge_treatment": "plain"
    },

    "facade": {
        "wall_materials_by_biome": {
            "default": ["wattle_daub", "cob"],
            "ChalkCliff": ["flint_rubble"],
            "Woodland": ["timber_frame"]
        },
        "opposing_doors": true,
        "door_styles": ["plank_batten"],
        "window_styles": ["unglazed_shutter"],
        "windows_count": [1, 3]
    }
}
```

```json
// assets/buildings/types/parish_church.json
{
    "id": "parish_church",
    "name": "Parish Church",
    "category": "religious",
    "applicable_zones": ["Religious"],
    "era": "high_medieval",
    "architectural_style": ["norman", "early_english"],

    "footprint": {
        "shapes": ["cruciform", "nave_chancel", "nave_only"],
        "nave_width": [6, 10],
        "nave_length": [15, 25],
        "chancel_width_ratio": 0.7,
        "chancel_length": [6, 12],
        "has_tower": { "probability": 0.7 },
        "tower_position": ["west", "central"]
    },

    "structure": {
        "type": "stone_masonry",
        "wall_thickness": 0.9,
        "buttresses": true,
        "buttress_spacing": 4.0
    },

    "floors": {
        "nave_height": [6, 10],
        "chancel_height_ratio": 0.9,
        "tower_height": [12, 20]
    },

    "roof": {
        "types": ["gable"],
        "pitch_range": [50, 60],
        "materials": ["stone_slate", "clay_tile", "lead"],
        "materials_by_wealth": {
            "poor": ["thatch", "stone_slate"],
            "average": ["stone_slate", "clay_tile"],
            "wealthy": ["lead", "clay_tile"]
        }
    },

    "facade": {
        "wall_materials_by_biome": {
            "ChalkCliff": ["flint_with_stone_dressing"],
            "default": ["coursed_rubble", "ashlar"]
        },
        "window_styles_by_era": {
            "norman": ["round_arch", "small_round"],
            "early_english": ["lancet", "paired_lancet", "triplet_lancet"]
        },
        "door_styles": ["round_arch_norman", "pointed_arch"]
    },

    "features": {
        "porch": { "probability": 0.5, "position": "south" },
        "bell_tower": { "probability": 0.7 },
        "graveyard": { "required": true, "position": "surrounding" },
        "lychgate": { "probability": 0.4 }
    }
}
```

### 3.3 File Structure

```
tools/
├── settlement_generator/
│   ├── SettlementTypes.h           # Core data structures
│   ├── SettlementConfig.h          # JSON config loading
│   ├── SettlementLayout.h/cpp      # Layout algorithm
│   ├── StreetNetwork.h/cpp         # Street generation
│   ├── LotSubdivision.h/cpp        # Parcel division
│   └── settlement_generator.cpp    # CLI tool
│
├── building_generator/
│   ├── BuildingTypes.h             # Building data structures
│   ├── BuildingGrammar.h/cpp       # CGA shape grammar
│   ├── FootprintGenerator.h/cpp    # Building footprints
│   ├── RoofGenerator.h/cpp         # Roof mesh generation
│   ├── FacadeGenerator.h/cpp       # Wall and window placement
│   ├── WFCFacade.h/cpp             # Wave Function Collapse for details
│   └── building_generator.cpp      # CLI tool
│
assets/
├── settlements/
│   ├── templates/                  # Settlement layout templates
│   └── generated/                  # Generated layout data
│
├── buildings/
│   ├── types/                      # Building type definitions
│   ├── components/                 # Reusable building parts
│   ├── materials/                  # Material definitions
│   └── generated/                  # Generated building meshes

src/
├── settlement/
│   ├── SettlementSystem.h/cpp      # Runtime settlement manager
│   ├── SettlementLoader.h/cpp      # Async settlement loading
│   ├── BuildingRenderer.h/cpp      # Building rendering subsystem
│   ├── BuildingLOD.h/cpp           # LOD management
│   └── SettlementCulling.h/cpp     # Visibility culling
```

### 3.4 Deliverables - Phase 1

- [ ] Core data structure headers implemented
- [ ] JSON configuration schema defined and documented
- [ ] Config loader with validation
- [ ] Unit tests for config parsing
- [ ] Example template files for each settlement type

---
