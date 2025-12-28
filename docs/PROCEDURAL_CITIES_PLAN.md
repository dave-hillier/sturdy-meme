# Procedural Cities & Settlements - Comprehensive Implementation Plan

## Executive Summary

This document outlines a comprehensive plan for implementing procedural city and settlement generation in the game engine. The system will generate contextually appropriate settlements ranging from small hamlets to larger towns, integrated with the existing terrain, biome, and road systems.

**Target Quality**: AAA-standard procedural generation with artist-controllable parameters
**Scale**: Settlements from 5-building hamlets to 200+ building towns
**Setting**: High Medieval (c. 1100-1300 AD) South Coast of England
**Region**: Inspired by Sussex, Hampshire, and Dorset coastal landscapes

---

## Design Decisions

The following key decisions have been made:

| Decision | Choice | Implication |
|----------|--------|-------------|
| **Scalability** | Algorithm must scale from hamlet to town | No "minimum viable" - single system handles all sizes |
| **Generation Pipeline** | Fully procedural first, then artist modifiers | Build-time generation with post-generation tweaking |
| **Building Interiors** | Required | Full interior spaces, not solid volumes |
| **Performance Budget** | Not constrained initially | Optimize after functionality complete |
| **Dynamic Settlements** | No | Settlements are static once generated |
| **Terrain Integration** | Buildings adapt to terrain | Cellars, tunnels, wells cut into terrain; existing terrain cut system (no physics yet) |
| **Street Networks** | Hybrid: Space colonization + A* terrain-following | L-systems alone insufficient; driven by POIs and terrain constraints |
| **Building Styles** | Multiple period styles per building type | Norman, Early English Gothic, regional material variations |
| **Asset Pipeline** | Procedural blockout → hand-crafted replacement | Start procedural, swap in artist assets as available |

### Street Network Algorithm Rationale

Pure L-systems are insufficient because streets must be:
- **Driven by Points of Interest**: Connections between settlements, key buildings
- **Terrain-Aware**: Follow contours, avoid steep slopes, cross water at fords
- **Hierarchical**: Main roads connect towns, lanes branch to serve lots

Selected approach combines:
1. **Space Colonization**: Organic growth from seed points toward attractors
2. **A* Pathfinding**: Terrain cost evaluation (existing `RoadPathfinder`)
3. **Constraint Satisfaction**: Ensure connectivity, avoid obstacles

### Building Style Variations

Each building type has period/style variants:

| Style Period | Date Range | Characteristics |
|-------------|-----------|-----------------|
| **Saxon Survival** | Pre-1100 | Simple timber, small windows, steep roofs |
| **Norman** | 1066-1200 | Round arches, thick walls, herringbone masonry |
| **Transitional** | 1150-1200 | Mix of round and pointed arches |
| **Early English** | 1180-1275 | Pointed arches, lancet windows, buttresses |

Buildings select style based on:
- Settlement wealth parameter
- Building age (when was it "built")
- Regional material availability

### Blockout-to-Handcrafted Pipeline

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   PROCEDURAL    │────▶│    BLOCKOUT     │────▶│   HAND-CRAFTED  │
│   GENERATION    │     │     ASSET       │     │   REPLACEMENT   │
└─────────────────┘     └─────────────────┘     └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
  - Dimensions            - Simplified mesh      - Artist-modeled
  - Footprint             - Material slots       - Full detail
  - Room layout           - LOD-ready            - Unique variants
  - Style params          - Collision proxy      - Baked lighting
```

Each generated building outputs:
1. **Blockout mesh**: Low-poly placeholder with correct dimensions
2. **Metadata JSON**: All generation parameters for artist reference
3. **Asset ID**: Unique identifier for hand-crafted replacement

Artists can then:
- Model detailed replacements referencing blockout dimensions
- Register replacement in asset manifest
- System swaps in hand-crafted version at runtime

### Implications of Interior Requirement

Requiring interiors significantly expands scope:

- **Room Layout Generation**: Need floor plan algorithms
- **Furniture Placement**: Interior prop system required
- **Lighting**: Interior light sources (hearth, candles, windows)
- **Occlusion**: Cannot use simple box collision
- **LOD Complexity**: Interior/exterior LOD transitions
- **Navigation**: AI pathfinding inside buildings

### Terrain Integration Details

The existing terrain cut system will be extended for:

- **Cellars**: Below-ground rooms in stone houses, churches
- **Tunnels**: Access passages, crypts under churches
- **Wells**: Vertical shafts with water table integration
- **Foundations**: Leveled areas for building footprints
- **Terracing**: Stepped foundations on slopes

*Note: Physics integration for terrain cuts is pending*

---

## Historical & Regional Context

### The High Medieval Period (c. 1100-1300 AD)

This era represents the peak of medieval English civilization before the Black Death. Key characteristics:

- **Population Growth**: England's population peaked at ~5-6 million
- **Agricultural Expansion**: New lands cleared, villages founded
- **Norman Influence**: Post-Conquest architectural styles, stone churches
- **Trade Networks**: Growing market towns, coastal trade with Continent
- **Church Dominance**: Every settlement has religious buildings

### South Coast Regional Architecture

#### Building Materials (by availability)

| Material | Regions | Usage |
|----------|---------|-------|
| **Flint** | Sussex, Hampshire, Dorset chalk downland | Walls, church facades |
| **Chalk Clunch** | Downland areas | Interior walls, carved details |
| **Timber Frame** | Weald (inland Sussex/Kent) | Houses, barns, half-timbered buildings |
| **Thatch** | Throughout | Roofing (wheat or reed) |
| **Clay Tile** | Near clay deposits | Roofing in wealthier areas |
| **Wealden Sandstone** | Inland areas | Church foundations, quoins |
| **Purbeck Marble** | Dorset | Church decoration |
| **Wattle and Daub** | Throughout | Wall infill between timbers |

#### Settlement Types by Landscape

| Landscape | Settlement Pattern | Typical Features |
|-----------|-------------------|------------------|
| **Chalk Downs** | Nucleated villages in valleys | Spring-line settlements, sheep farming |
| **Coastal Plain** | Linear villages along roads | Fishing, salt production |
| **Weald** | Dispersed hamlets | Iron working, woodland industry |
| **River Valleys** | Mill towns | Water-powered mills, fords/bridges |
| **Harbours** | Fishing villages | Quays, net lofts, fish markets |

#### Architectural Styles

**Norman/Romanesque (1066-1200)**
- Round arches in churches and important buildings
- Thick walls with small windows
- Square towers on churches
- Herringbone masonry patterns

**Early English Gothic (1180-1275)**
- Pointed arches
- Lancet windows (tall, narrow)
- Ribbed vaulting
- More elaborate church architecture

**Decorated Gothic (1275-1380)** - Later period
- Larger windows with tracery
- More ornate decoration
- Beginning of the period's end

### Building Types by Settlement

| Building Type | Hamlet | Village | Town | Fishing Village |
|--------------|--------|---------|------|-----------------|
| Peasant Cottage | ✓ | ✓ | ✓ | ✓ |
| Longhouse | ✓ | ✓ | | |
| Cruck Hall | | ✓ | ✓ | |
| Stone House | | ✓ | ✓ | |
| Parish Church | | ✓ | ✓ | ✓ |
| Chapel | ✓ | | | ✓ |
| Manor House | ✓ | ✓ | | |
| Tithe Barn | | ✓ | ✓ | |
| Dovecote | | ✓ | ✓ | |
| Mill (water/wind) | | ✓ | ✓ | |
| Smithy | | ✓ | ✓ | |
| Inn/Tavern | | ✓ | ✓ | ✓ |
| Market Cross | | ✓ | ✓ | |
| Guild Hall | | | ✓ | |
| Town Wall/Gate | | | ✓ | |
| Warehouse | | | ✓ | ✓ |
| Quay/Jetty | | | | ✓ |
| Net Loft | | | | ✓ |
| Fish Market | | | | ✓ |

---

## Table of Contents

1. [Plan Overview](#1-plan-overview)
2. [Architecture & Design Principles](#2-architecture--design-principles)
3. [Phase 1: Foundation & Data Structures](#3-phase-1-foundation--data-structures)
4. [Phase 2: Settlement Layout Generation](#4-phase-2-settlement-layout-generation)
5. [Phase 3: Building Exterior Generation](#5-phase-3-building-exterior-generation)
6. [Phase 3b: Building Interior Generation](#6-phase-3b-building-interior-generation)
7. [Phase 4: Street & Infrastructure Networks](#7-phase-4-street--infrastructure-networks)
8. [Phase 5: Props & Detail Population](#8-phase-5-props--detail-population)
9. [Phase 6: Terrain Integration & Subterranean](#9-phase-6-terrain-integration--subterranean)
10. [Phase 7: LOD & Streaming System](#10-phase-7-lod--streaming-system)
11. [Phase 8: Integration & Polish](#11-phase-8-integration--polish)
12. [Quality Assurance & Testing](#12-quality-assurance--testing)
13. [Research References](#13-research-references)

---

## 1. Plan Overview

### 1.1 Goals

- **Contextual Generation**: Settlements that respond to terrain, biomes, and historical constraints
- **Visual Coherence**: Buildings and layouts that feel planned yet organic
- **Performance**: Efficient runtime representation with aggressive LOD
- **Scalability**: Support for multiple simultaneous settlements in view
- **Artistic Control**: Designer-tunable parameters at every level
- **Incremental Development**: Each phase produces visible, working results

### 1.2 Integration Points with Existing Systems

| Existing System | Integration Method |
|----------------|-------------------|
| `BiomeGenerator` | Read settlement locations, types, and zone data |
| `RoadPathfinder` | Extend internal roads from external network |
| `TerrainSystem` | Height queries, terrain modification for building foundations |
| `VirtualTextureSystem` | Potential for settlement-aware virtual textures |
| `TreeSystem` | Exclude trees from settlement areas, orchard placement |
| `SceneManager` | Building and prop registration |
| `ShadowSystem` | Shadow-casting buildings |
| `PhysicsSystem` | Collision geometry for buildings |

### 1.3 Settlement Type Specifications

Based on existing `SettlementType` enum:

| Type | Building Count | Features | Population Density |
|------|---------------|----------|-------------------|
| Hamlet | 3-8 | Farmhouses, barns, small chapel | Very Low |
| Village | 15-40 | Church, inn, smithy, market square | Low |
| Town | 80-200+ | Market hall, multiple churches, guildhalls, walls | Medium |
| FishingVillage | 8-25 | Quay, fish market, net sheds, cottages | Low |

---

## 2. Architecture & Design Principles

### 2.1 Generation Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        BUILD-TIME GENERATION                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                   │
│  │   Biome &    │───▶│  Settlement  │───▶│   Building   │                   │
│  │  Settlement  │    │    Layout    │    │  Prototypes  │                   │
│  │  Detection   │    │  Generation  │    │  Generation  │                   │
│  └──────────────┘    └──────────────┘    └──────────────┘                   │
│         │                   │                   │                           │
│         ▼                   ▼                   ▼                           │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                   │
│  │ settlements  │    │  layout.json │    │  building    │                   │
│  │    .json     │    │  per settle- │    │  meshes &    │                   │
│  │              │    │     ment     │    │  textures    │                   │
│  └──────────────┘    └──────────────┘    └──────────────┘                   │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                        RUNTIME STREAMING                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                   │
│  │  Settlement  │───▶│   LOD &      │───▶│  Rendering   │                   │
│  │   Loader     │    │  Streaming   │    │  Subsystem   │                   │
│  └──────────────┘    └──────────────┘    └──────────────┘                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Design Principles

1. **Composition Over Inheritance**: Building types are assembled from components, not class hierarchies
2. **Data-Driven Configuration**: All magic numbers in JSON/config files
3. **Deterministic Generation**: Same seed produces identical results
4. **Graceful Degradation**: LOD system maintains silhouette at any distance
5. **Separation of Concerns**: Layout, buildings, props as independent systems

### 2.3 Algorithm Selection Rationale

| Problem Domain | Selected Algorithm | Rationale |
|---------------|-------------------|-----------|
| Settlement Layout | POI-Driven + Template Hybrid | Key buildings drive layout, templates for consistency |
| Street Networks | Space Colonization + A* Terrain | Organic growth toward POIs, terrain-aware pathfinding |
| Building Placement | Wave Function Collapse (WFC) | Respects adjacency constraints |
| Building Geometry | CGA Shape Grammar → Blockout | Procedural first, artist replacement later |
| Roof Generation | Straight Skeleton Algorithm | Handles complex footprints |
| Facade Detail | Hierarchical WFC | Local coherence with global patterns |
| Floor Plans | BSP/Treemap Partition | Room subdivision with adjacency constraints |
| Terrain Integration | Height modification layers | Cellars, foundations via terrain cut system |

---

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

## 4. Phase 2: Settlement Layout Generation

### 4.1 Layout Algorithm Overview

The layout generation uses a hybrid approach combining:

1. **Template-Based Seeding**: Initial placement of key landmarks
2. **Agent-Based Growth**: Organic street/lot expansion
3. **Constraint Satisfaction**: Ensuring accessibility and coherence

```
┌────────────────────────────────────────────────────────────────┐
│                    LAYOUT GENERATION PIPELINE                   │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  1. TERRAIN ANALYSIS                                           │
│     ├── Sample height grid around center                       │
│     ├── Compute slope map                                      │
│     ├── Identify buildable areas (slope < threshold)           │
│     └── Mark water, cliffs, existing roads                     │
│                                                                │
│  2. SEED PLACEMENT                                             │
│     ├── Place settlement core (church, green, market)          │
│     ├── Connect to external road network                       │
│     └── Establish main axis orientation                        │
│                                                                │
│  3. STREET NETWORK GENERATION                                  │
│     ├── Grow main street from external connection              │
│     ├── Branch secondary streets using L-system rules          │
│     ├── Add lanes and alleys for access                        │
│     └── Smooth and snap to terrain                             │
│                                                                │
│  4. LOT SUBDIVISION                                            │
│     ├── Identify blocks between streets                        │
│     ├── Subdivide blocks into lots                             │
│     ├── Assign zones based on template                         │
│     └── Validate street frontage                               │
│                                                                │
│  5. BUILDING ASSIGNMENT                                        │
│     ├── Place required buildings (church, inn, etc.)           │
│     ├── Fill remaining lots with appropriate types             │
│     └── Resolve conflicts and adjust                           │
│                                                                │
│  6. OUTPUT                                                     │
│     ├── Serialize layout to JSON                               │
│     ├── Generate debug visualization                           │
│     └── Compute terrain modification mask                      │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### 4.2 Terrain Integration

#### 4.2.1 Buildable Area Analysis

```cpp
struct TerrainAnalysis {
    std::vector<float> heightGrid;      // Sampled heights
    std::vector<float> slopeGrid;       // Computed slopes
    std::vector<uint8_t> buildableGrid; // 0=unbuildable, 255=ideal

    uint32_t gridSize;
    float cellSize;                     // Meters per cell
    glm::vec2 origin;                   // World coords of grid corner

    // Sample buildability at world position
    float getBuildability(glm::vec2 worldPos) const;

    // Get terrain height at world position
    float getHeight(glm::vec2 worldPos) const;

    // Get slope at world position
    float getSlope(glm::vec2 worldPos) const;
};

class TerrainAnalyzer {
public:
    TerrainAnalysis analyze(
        const SettlementDefinition& settlement,
        float analysisRadius,           // How far to analyze
        float cellSize = 2.0f           // Analysis resolution
    );

private:
    // Integration with existing systems
    float sampleTerrainHeight(glm::vec2 pos) const;
    BiomeZone sampleBiome(glm::vec2 pos) const;
    bool isWater(glm::vec2 pos) const;
    bool isCliff(glm::vec2 pos) const;
};
```

#### 4.2.2 Terrain Modification

Buildings need flat foundations. Rather than flattening terrain globally, we compute per-lot modifications:

```cpp
struct TerrainModification {
    glm::vec2 center;
    glm::vec2 extents;
    float targetHeight;
    float blendRadius;          // How far to blend back to natural

    enum class Type {
        Flatten,                // Level to target height
        Cut,                    // Only remove material
        Fill,                   // Only add material
        Terrace                 // Create terraced steps
    } type;
};

class TerrainModifier {
public:
    // Compute modifications needed for a lot
    TerrainModification computeForLot(
        const BuildingLot& lot,
        const TerrainAnalysis& terrain
    );

    // Generate height modification texture
    void bakeModificationMap(
        const std::vector<TerrainModification>& mods,
        const std::string& outputPath
    );
};
```

### 4.3 Street Network Algorithm

Hybrid approach combining space colonization for organic growth with A* for terrain-aware pathfinding.

#### 4.3.1 Algorithm Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                 STREET NETWORK GENERATION                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. ATTRACTOR PLACEMENT                                         │
│     ├── Key buildings (church, inn, market)                     │
│     ├── External road connections                               │
│     ├── Settlement boundary points                              │
│     └── Lot frontage requirements                               │
│                                                                 │
│  2. SPACE COLONIZATION                                          │
│     ├── Grow street tree from seed (main road entry)            │
│     ├── Branches compete for nearby attractors                  │
│     ├── Kill attractors when reached                            │
│     └── Natural organic branching pattern                       │
│                                                                 │
│  3. A* TERRAIN REFINEMENT                                       │
│     ├── For each street segment: find terrain-optimal path      │
│     ├── Cost function: slope + water crossing + cliff penalty   │
│     ├── Reuse existing RoadPathfinder infrastructure            │
│     └── Smooth paths with spline interpolation                  │
│                                                                 │
│  4. HIERARCHY ASSIGNMENT                                        │
│     ├── Main street: connects to external roads                 │
│     ├── Streets: branch from main, serve multiple lots          │
│     ├── Lanes: serve individual lots                            │
│     └── Alleys: rear access                                     │
│                                                                 │
│  5. INTERSECTION RESOLUTION                                     │
│     ├── Merge nearby endpoints                                  │
│     ├── Validate connectivity                                   │
│     └── Generate intersection geometry                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 4.3.2 Space Colonization Algorithm

Based on Runions et al. (2007) "Modeling Trees with a Space Colonization Algorithm":

```cpp
struct Attractor {
    glm::vec2 position;
    float weight;               // Importance (key buildings = high)
    bool reached = false;

    enum class Type {
        ExternalRoad,           // Connection to other settlements
        KeyBuilding,            // Church, inn, market - must be accessible
        LotFrontage,            // Lots need street access
        BoundaryPoint           // Settlement perimeter coverage
    } type;
};

struct StreetNode {
    glm::vec2 position;
    StreetNode* parent = nullptr;
    std::vector<StreetNode*> children;

    float width;                // Street width at this node
    int depth;                  // Distance from root in tree
};

class SpaceColonizationStreets {
public:
    struct Config {
        float attractorKillDistance = 10.0f;    // Remove attractor when this close
        float attractorInfluenceRadius = 50.0f; // Attractors affect nodes within this
        float segmentLength = 15.0f;            // Growth step size
        float branchAngleLimit = 60.0f;         // Max deviation from parent direction
        int maxIterations = 200;
    };

    StreetNetwork generate(
        const std::vector<Attractor>& attractors,
        glm::vec2 seedPosition,                 // Main road entry point
        glm::vec2 seedDirection,                // Initial direction
        const Config& config
    );

private:
    // Find attractors influencing a node
    std::vector<const Attractor*> findInfluencingAttractors(
        const StreetNode& node,
        const std::vector<Attractor>& attractors
    );

    // Calculate growth direction from attractors
    glm::vec2 calculateGrowthDirection(
        const StreetNode& node,
        const std::vector<const Attractor*>& influencers
    );

    // Check if node can branch (enough attractors in cone)
    bool shouldBranch(
        const StreetNode& node,
        const std::vector<Attractor>& attractors
    );
};
```

#### 4.3.3 Terrain-Aware Path Refinement

```cpp
class TerrainAwareStreetRefiner {
public:
    // Refine street segment using A* on terrain
    std::vector<glm::vec2> refinePath(
        glm::vec2 start,
        glm::vec2 end,
        const TerrainAnalysis& terrain,
        const RoadPathfinder& pathfinder     // Reuse existing infrastructure
    );

    // Cost function for street placement
    float calculateCost(glm::vec2 from, glm::vec2 to, const TerrainAnalysis& terrain) {
        float baseCost = glm::length(to - from);

        // Slope penalty (prefer following contours)
        float slope = terrain.getSlope((from + to) * 0.5f);
        float slopeCost = slope * slopePenaltyMultiplier;

        // Water crossing penalty
        bool crossesWater = terrain.isWater(to);
        float waterCost = crossesWater ? waterPenalty : 0.0f;

        // Cliff penalty (impassable)
        bool isCliff = slope > cliffThreshold;
        float cliffCost = isCliff ? INFINITY : 0.0f;

        // Prefer following existing paths/desire lines
        float existingPathBonus = terrain.hasExistingPath(to) ? -0.5f : 0.0f;

        return baseCost + slopeCost + waterCost + cliffCost + existingPathBonus;
    }

private:
    float slopePenaltyMultiplier = 5.0f;
    float waterPenalty = 100.0f;
    float cliffThreshold = 0.7f;
};
```

#### 4.3.4 Street Hierarchy Assignment

```cpp
class StreetHierarchyAssigner {
public:
    void assignHierarchy(StreetNetwork& network) {
        // Main street: connects settlement to external road network
        markMainStreet(network);

        // Streets: branches from main serving multiple buildings
        markStreets(network);

        // Lanes: short segments serving 1-3 buildings
        markLanes(network);

        // Alleys: rear access, very narrow
        markAlleys(network);
    }

private:
    void markMainStreet(StreetNetwork& network) {
        // Path from external connection through settlement center
        // Widest street type (8m for main road)
    }

    void markStreets(StreetNetwork& network) {
        // Branches serving 4+ lots
        // Medium width (5m)
    }

    void markLanes(StreetNetwork& network) {
        // Segments serving 1-3 lots
        // Narrow (3.5m)
    }

    void markAlleys(StreetNetwork& network) {
        // Rear access only
        // Very narrow (2m)
    }
};
```

#### 4.3.5 Complete Street Generator

```cpp
class StreetNetworkGenerator {
public:
    struct Config {
        // Street widths
        float mainStreetWidth = 8.0f;
        float streetWidth = 5.0f;
        float laneWidth = 3.5f;
        float alleyWidth = 2.0f;

        // Space colonization params
        float attractorKillDistance = 10.0f;
        float attractorInfluenceRadius = 50.0f;
        float segmentLength = 15.0f;

        // Terrain params
        float maxStreetSlope = 0.15f;       // 15% grade max for streets
        float maxLaneSlope = 0.25f;         // Lanes can be steeper
    };

    StreetNetwork generate(
        const SettlementDefinition& settlement,
        const TerrainAnalysis& terrain,
        const std::vector<BuildingPlacement>& keyBuildings,
        const Config& config
    );

private:
    SpaceColonizationStreets colonization;
    TerrainAwareStreetRefiner refiner;
    StreetHierarchyAssigner hierarchy;

    // Generate attractors from key buildings and settlement bounds
    std::vector<Attractor> generateAttractors(
        const SettlementDefinition& settlement,
        const std::vector<BuildingPlacement>& keyBuildings
    );

    // Connect to external road network
    glm::vec2 findExternalConnection(
        const SettlementDefinition& settlement,
        const RoadNetwork& externalRoads
    );

    // Detect and merge street intersections
    void mergeIntersections(StreetNetwork& network, float threshold = 5.0f);

    // Validate all key buildings are accessible
    bool validateConnectivity(
        const StreetNetwork& network,
        const std::vector<BuildingPlacement>& keyBuildings
    );
};
```

### 4.4 Lot Subdivision Algorithm

#### 4.4.1 Block Identification

```cpp
class BlockIdentifier {
public:
    // Find closed polygons formed by streets
    std::vector<Polygon> findBlocks(const StreetNetwork& network);

private:
    // Use Boost.Polygon or similar for polygon operations
    std::vector<Polygon> computeVoronoi(const std::vector<glm::vec2>& sites);
    Polygon clipToSettlementBounds(const Polygon& poly, float radius);
};
```

#### 4.4.2 Lot Subdivision

Using OBB (Oriented Bounding Box) recursive subdivision:

```cpp
class LotSubdivider {
public:
    struct Config {
        glm::vec2 minLotSize = {10, 15};
        glm::vec2 maxLotSize = {25, 40};
        float streetSetback = 2.0f;
        float rearSetback = 3.0f;
        float sideSetback = 1.5f;
    };

    std::vector<BuildingLot> subdivide(
        const Polygon& block,
        const StreetNetwork& streets,
        const Config& config
    );

private:
    // Recursive OBB split
    void splitBlock(
        const Polygon& block,
        std::vector<BuildingLot>& lots,
        int depth = 0
    );

    // Determine split axis (parallel to longest street frontage)
    glm::vec2 getSplitAxis(const Polygon& block, const StreetNetwork& streets);

    // Check if lot has valid street access
    bool validateStreetAccess(const BuildingLot& lot, const StreetNetwork& streets);
};
```

### 4.5 Zone Assignment

#### 4.5.1 Zone Placement Strategy

```cpp
class ZonePlanner {
public:
    void assignZones(
        std::vector<BuildingLot>& lots,
        const SettlementDefinition& settlement,
        const StreetNetwork& streets,
        const SettlementTemplate& tmpl
    );

private:
    // Place required features first
    void placeRequiredFeatures(std::vector<BuildingLot>& lots);

    // Score each lot for each zone type
    float scoreLotForZone(const BuildingLot& lot, SettlementZone zone);

    // Use Hungarian algorithm for optimal assignment
    void optimizeAssignment(std::vector<BuildingLot>& lots);
};
```

### 4.6 Deliverables - Phase 2

- [ ] TerrainAnalyzer implementation
- [ ] StreetNetworkGenerator with L-system rules
- [ ] LotSubdivider with OBB algorithm
- [ ] ZonePlanner with constraint satisfaction
- [ ] Layout serialization to JSON
- [ ] Debug visualization output (SVG/PNG)
- [ ] Integration tests with existing biome system

**Testing**: Generate layout for each settlement type, verify:
- All lots have street access
- No overlapping lots
- Streets connect to external road network
- Zones distributed according to template

---

## 5. Phase 3: Building Exterior Generation

### 5.1 Building Grammar (CGA-Style)

The building generation uses a shape grammar inspired by CityEngine's CGA, but simplified for our needs.

#### 5.1.1 Grammar Overview

```
Building → Footprint → Mass → Floors → Roof
                              ↓
                           Facade → Walls → Windows/Doors
                                          → Trim/Details
```

#### 5.1.2 Shape Operations

```cpp
// Core shape operations
enum class ShapeOp {
    Extrude,        // Extrude 2D shape to 3D
    Split,          // Divide shape along axis
    Repeat,         // Repeat element along axis
    ComponentSplit, // Split by face type (front, side, roof, etc.)
    Offset,         // Inset/outset shape boundary
    Roof,           // Generate roof from footprint
    Texture,        // Apply texture coordinates
    SetMaterial,    // Assign material
    Insert,         // Insert component at point
    Scope,          // Transform scope
    NIL             // Delete shape
};

struct ShapeNode {
    std::string name;
    glm::vec3 position;         // Local origin
    glm::vec3 size;             // Bounding dimensions
    glm::mat4 transform;        // Full transform

    enum class Type {
        Volume,                 // 3D volume
        Face,                   // 2D face
        Edge,                   // 1D edge
        Point                   // 0D point
    } type;

    std::vector<Vertex> geometry;
    std::string materialId;
};
```

#### 5.1.3 Example Building Rules

```cpp
// rules/cottage.grammar

// Start with lot, produce building
Rule("Lot") {
    setback(front: 2, sides: 1.5, rear: 3)
    Footprint
}

Rule("Footprint") {
    // Choose footprint shape based on lot
    case lot.aspect > 1.5: LShape(0.3)    // L-shaped for deep lots
    case lot.aspect > 1.2: Rectangle
    else: Rectangle
}

Rule("Rectangle") {
    extrude(floors * floorHeight) Mass
}

Rule("LShape", wingRatio) {
    split(x) {
        wingRatio: WingFootprint
        ~1: MainFootprint
    }
}

Rule("Mass") {
    split(y) {
        groundFloorHeight: GroundFloor
        repeat(upperFloorHeight): UpperFloor
        ~1: Attic
    }
}

Rule("GroundFloor") {
    componentSplit {
        front: GroundFrontFacade
        side: GroundSideFacade
        rear: RearFacade
        top: FloorPlate
    }
}

Rule("GroundFrontFacade") {
    split(x) {
        ~1: Wall
        doorWidth: Door
        ~1: Wall | Window(groundWindowStyle)
    }
}

Rule("Roof") {
    case roofType == "gable": GableRoof(pitch)
    case roofType == "hipped": HippedRoof(pitch)
    case roofType == "half_hipped": HalfHippedRoof(pitch)
}
```

### 5.2 Footprint Generation

#### 5.2.1 Footprint Shapes

```cpp
class FootprintGenerator {
public:
    enum class Shape {
        Rectangle,
        LShape,
        TShape,
        UShape,
        Irregular
    };

    struct Config {
        Shape shape;
        glm::vec2 dimensions;
        float wingWidth = 0.3f;         // For L/T/U shapes
        float irregularity = 0.0f;      // Random vertex displacement
    };

    Polygon generate(const Config& config, uint64_t seed);

private:
    Polygon generateRectangle(const Config& config);
    Polygon generateLShape(const Config& config, uint64_t seed);
    Polygon generateTShape(const Config& config, uint64_t seed);
    Polygon generateIrregular(const Config& config, uint64_t seed);
};
```

#### 5.2.2 Footprint Fitting

```cpp
class FootprintFitter {
public:
    // Fit footprint to lot constraints
    Polygon fitToLot(
        const Polygon& footprint,
        const BuildingLot& lot,
        float frontSetback,
        float sideSetback,
        float rearSetback
    );

    // Rotate footprint to face street
    Polygon alignToStreet(const Polygon& footprint, const BuildingLot& lot);
};
```

### 5.3 Roof Generation

#### 5.3.1 Straight Skeleton Algorithm

For complex footprints, we use the straight skeleton to generate proper roof geometry:

```cpp
class RoofGenerator {
public:
    enum class Type {
        Flat,
        Shed,
        Gable,
        Hipped,
        HalfHipped,         // Jerkinhead
        Gambrel,
        Mansard,
        Cross               // Multiple gables
    };

    struct Config {
        Type type;
        float pitch;                // Roof angle in degrees
        float overhang;             // Eave overhang distance
        bool dormers = false;
        int dormerCount = 0;
        std::string materialId;
    };

    Mesh generate(const Polygon& footprint, const Config& config);

private:
    // Straight skeleton computation
    std::vector<glm::vec2> computeStraightSkeleton(const Polygon& footprint);

    // Generate gable from skeleton
    Mesh generateGable(const Polygon& footprint, float pitch);

    // Generate hipped from skeleton
    Mesh generateHipped(const Polygon& footprint, float pitch);

    // Add dormers to roof surface
    void addDormers(Mesh& roof, const Config& config);
};
```

### 5.4 Facade Generation

#### 5.4.1 Facade Grammar

```cpp
class FacadeGenerator {
public:
    struct Style {
        std::string wallMaterial;
        std::string trimMaterial;

        struct WindowStyle {
            float width, height;
            float sillHeight;
            std::string frameType;
            std::string glassType;
            bool shutters;
        } windowStyle;

        struct DoorStyle {
            float width, height;
            std::string type;           // "plank", "battened", "paneled"
            bool porch;
        } doorStyle;

        float beamExposure;             // Timber frame exposure (0-1)
    };

    Mesh generate(
        const ShapeNode& facadeShape,
        const Style& style,
        uint64_t seed
    );

private:
    void subdivideFacade(ShapeNode& shape, const Style& style);
    void placeWindows(ShapeNode& wallSection, const Style& style);
    void placeDoor(ShapeNode& wallSection, const Style& style);
    void addTimberFrame(Mesh& facade, float exposure);
};
```

#### 5.4.2 Wave Function Collapse for Details

For coherent detail placement (window arrangements, decorative elements):

```cpp
class WFCFacadeDetail {
public:
    struct Tile {
        std::string id;
        Mesh geometry;
        std::array<std::string, 4> sockets;  // top, right, bottom, left
        float weight;
    };

    struct Config {
        std::vector<Tile> tileset;
        int gridWidth, gridHeight;
    };

    // Generate detail placement using WFC
    std::vector<std::string> generate(const Config& config, uint64_t seed);

private:
    // Standard WFC implementation
    void propagate();
    int findLowestEntropy();
    void collapse(int cell);
    bool backtrack();
};
```

### 5.5 Building Component Library

#### 5.5.1 Architectural Components

```cpp
// Pre-modeled components loaded at build time
struct ComponentLibrary {
    // Structural
    std::map<std::string, Mesh> chimneys;
    std::map<std::string, Mesh> dormers;
    std::map<std::string, Mesh> porches;

    // Openings
    std::map<std::string, Mesh> windows;
    std::map<std::string, Mesh> doors;
    std::map<std::string, Mesh> shutters;

    // Decorative
    std::map<std::string, Mesh> cornices;
    std::map<std::string, Mesh> brackets;
    std::map<std::string, Mesh> bargeboards;

    // Materials
    std::map<std::string, Material> materials;
};
```

### 5.6 Material Generation

#### 5.6.1 Procedural Materials

```cpp
class BuildingMaterialGenerator {
public:
    // Generate tiling wall textures
    void generateWallTexture(
        const std::string& type,        // "wattle_daub", "flint", "brick", etc.
        const std::string& outputPath,
        uint32_t size = 1024,
        uint64_t seed = 0
    );

    // Generate roof tile textures
    void generateRoofTexture(
        const std::string& type,        // "thatch", "slate", "clay_tile"
        const std::string& outputPath,
        uint32_t size = 1024,
        uint64_t seed = 0
    );

private:
    // Substance-style node graph evaluation
    void evaluateMaterialGraph(const MaterialGraph& graph);
};
```

### 5.7 Mesh Output

#### 5.7.1 Combined Building Mesh

```cpp
struct BuildingMesh {
    // Geometry
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Submeshes by material
    struct Submesh {
        uint32_t indexOffset;
        uint32_t indexCount;
        std::string materialId;
    };
    std::vector<Submesh> submeshes;

    // LOD variants
    std::vector<BuildingMesh> lodLevels;    // Lower detail versions

    // Collision
    std::vector<glm::vec3> collisionHull;   // Simplified collision shape

    // Metadata
    std::string buildingTypeId;
    glm::vec3 boundingBoxMin;
    glm::vec3 boundingBoxMax;
};
```

### 5.8 Deliverables - Phase 3

- [ ] FootprintGenerator with all shape types
- [ ] RoofGenerator with straight skeleton
- [ ] FacadeGenerator with grammar system
- [ ] WFCFacadeDetail for coherent details
- [ ] ComponentLibrary loader
- [ ] BuildingMaterialGenerator for procedural textures
- [ ] Mesh export to GLB format
- [ ] Building type configs for all required types

**Testing**: Generate sample buildings of each type, render in engine:
- Cottage, farmhouse, barn (hamlet)
- Church, inn, smithy, market stall (village)
- Guildhall, townhouse, warehouse (town)
- Net shed, quay building, fish market (fishing village)

---

## 5b. Phase 3b: Building Interior Generation

### 5b.1 Floor Plan Generation

Interior layout generation using space partitioning algorithms.

#### 5b.1.1 Room Types by Building

| Building Type | Rooms | Notes |
|--------------|-------|-------|
| Peasant Cottage | Single hall, loft | Open plan, central hearth |
| Longhouse | Hall, cross-passage, byre | Animals separated by passage |
| Cruck Hall | Hall, solar, buttery, pantry | High-status open hall |
| Stone House | Ground floor (storage/workshop), upper hall, chamber | Vertical separation |
| Parish Church | Nave, chancel, porch, tower base | Ritual sequence |
| Inn | Hall/drinking room, kitchen, chambers, stable | Multiple functions |
| Smithy | Forge area, storage | Open work area |
| Mill | Grinding floor, storage, machinery space | Functional layout |

#### 5b.1.2 Space Partitioning Algorithm

```cpp
class FloorPlanGenerator {
public:
    enum class PartitionMethod {
        None,               // Single room (cottages)
        CrossPassage,       // Longhouse style - single bisection
        Medieval,           // Hall + services + chambers
        Ecclesiastical,     // Nave/chancel/aisles
        Commercial          // Shop front + back rooms
    };

    struct RoomSpec {
        std::string name;
        float minArea;
        float maxArea;
        float aspectRatioMin;
        float aspectRatioMax;
        bool requiresExteriorWall;  // Needs window
        bool requiresHearth;
        std::vector<std::string> adjacentTo;  // Required adjacencies
    };

    struct FloorPlan {
        std::vector<Room> rooms;
        std::vector<Wall> interiorWalls;
        std::vector<Opening> doorways;
        std::vector<Opening> windows;
        glm::vec2 hearthPosition;
    };

    FloorPlan generate(
        const Polygon& footprint,
        const std::vector<RoomSpec>& requiredRooms,
        PartitionMethod method,
        uint64_t seed
    );

private:
    // Binary Space Partition for rectangular division
    void bspPartition(const Polygon& space, std::vector<Room>& rooms);

    // Squarified treemap for more regular rooms
    void treemapPartition(const Polygon& space,
                          const std::vector<float>& areas,
                          std::vector<Room>& rooms);

    // Validate room adjacencies
    bool validateAdjacencies(const FloorPlan& plan,
                            const std::vector<RoomSpec>& specs);
};
```

#### 5b.1.3 Room Definition

```cpp
struct Room {
    std::string typeId;             // "hall", "solar", "byre", etc.
    Polygon boundary;               // 2D floor polygon
    float floorHeight;              // Y offset from building base
    float ceilingHeight;            // Room height

    enum class FloorType {
        Earth,          // Beaten earth (common)
        Stone,          // Stone flags (churches, wealthy)
        Timber,         // Timber boards (upper floors)
        Rush            // Rush-strewn earth
    } floorType;

    bool hasHearth;
    glm::vec2 hearthPosition;

    bool hasLoft;                   // Storage loft above
    float loftHeight;

    std::vector<uint32_t> connectedRooms;  // Doorway connections
};
```

### 5b.2 Interior Architectural Elements

#### 5b.2.1 Structural Elements

```cpp
struct InteriorStructure {
    // Cruck frames (visible A-frames)
    struct CruckBlade {
        glm::vec2 basePosition;
        float height;
        float curve;                // Curvature of blade
    };
    std::vector<CruckBlade> cruckBlades;

    // Timber posts and beams
    struct TimberFrame {
        glm::vec3 start;
        glm::vec3 end;
        float width;
        float height;
    };
    std::vector<TimberFrame> beams;

    // Stone pillars (churches)
    struct Pillar {
        glm::vec2 position;
        float radius;
        float height;
        enum class Style { Round, Octagonal, Clustered } style;
    };
    std::vector<Pillar> pillars;
};
```

#### 5b.2.2 Central Hearth

Medieval buildings typically had central hearths (chimneys were rare before 1300):

```cpp
struct Hearth {
    glm::vec2 position;
    float width;
    float depth;

    enum class Type {
        Open,           // Simple fire on floor
        Raised,         // Stone platform
        Brazier         // Portable metal container
    } type;

    bool hasSmokeHole;          // Hole in roof above
    bool hasHoodLouvre;         // Timber smoke hood
};

class HearthGenerator {
public:
    Mesh generateHearth(const Hearth& hearth);
    Mesh generateSmokeHood(const Hearth& hearth, float ceilingHeight);

    // Smoke staining effect on roof interior
    void generateSmokeStaining(
        Mesh& roofInterior,
        const Hearth& hearth,
        float intensity
    );
};
```

### 5b.3 Interior Wall Surfaces

#### 5b.3.1 Wall Treatments

```cpp
enum class WallTreatment {
    ExposeTimber,       // Visible timber frame + infill
    Limewash,           // White lime wash (common)
    Painted,            // Decorative painting (churches)
    Hung,               // Textile hangings (wealthy)
    Bare                // Exposed stone/daub
};

class InteriorWallGenerator {
public:
    Mesh generate(
        const Room& room,
        WallTreatment treatment,
        bool hasTimberFrame
    );

private:
    void addTimberFrame(Mesh& wall, const std::vector<TimberFrame>& frames);
    void addWattleTexture(Mesh& wall);  // Visible between timbers
    void addLimewash(Mesh& wall);
};
```

### 5b.4 Interior Props and Furniture

#### 5b.4.1 Furniture by Room Type

| Room Type | Essential Furniture | Optional Furniture |
|-----------|--------------------|--------------------|
| Hall | Trestle table, benches, hearth | Chest, candlesticks |
| Solar | Bed, chest | Chair, prie-dieu |
| Kitchen | Work table, cauldron, spit | Barrels, shelves |
| Byre | Stalls, mangers | Hay racks |
| Church Nave | Benches/none, font | Lectern |
| Church Chancel | Altar, sedilia | Reredos, piscina |
| Inn Hall | Tables, benches, barrels | Fireplace, tap |
| Smithy | Anvil, forge, bellows, water trough | Tool racks |

#### 5b.4.2 Furniture Placement Algorithm

```cpp
class InteriorPropPlacer {
public:
    struct FurnitureRule {
        std::string propId;
        std::string roomType;

        enum class Placement {
            Center,             // Center of room
            Wall,               // Against wall
            Corner,             // In corner
            NearHearth,         // Close to fire
            NearWindow,         // By window for light
            NearDoor,           // By entrance
            Custom              // Specific position logic
        } placement;

        float minClearance;     // Space around item
        bool required;
        float probability;
    };

    std::vector<PropPlacement> place(
        const Room& room,
        const std::vector<FurnitureRule>& rules,
        uint64_t seed
    );

private:
    // Place furniture avoiding collisions
    bool tryPlace(
        const FurnitureRule& rule,
        const Room& room,
        std::vector<PropPlacement>& placed
    );

    // Find valid wall positions
    std::vector<glm::vec2> findWallPositions(
        const Room& room,
        float itemWidth,
        float clearance
    );
};
```

#### 5b.4.3 Furniture Models

```json
// assets/props/furniture/trestle_table.json
{
    "id": "trestle_table",
    "category": "furniture",
    "applicable_rooms": ["hall", "inn_hall", "kitchen"],

    "dimensions": {
        "width": [1.5, 3.0],
        "depth": 0.8,
        "height": 0.75
    },

    "construction": {
        "material": "oak",
        "style": "trestle"
    },

    "placement": {
        "preferred": "center",
        "orientation": "long_axis_to_door",
        "clearance": 0.8
    },

    "lod": {
        "lod0_triangles": 500,
        "lod1_triangles": 100,
        "impostor_distance": 15
    }
}
```

### 5b.5 Interior Lighting

#### 5b.5.1 Light Sources

```cpp
struct InteriorLightSource {
    enum class Type {
        Hearth,             // Central fire
        Candle,             // Single candle
        Candlestick,        // Multi-candle holder
        Rushlight,          // Rush dipped in fat
        Cresset,            // Oil lamp
        Window              // Daylight through window
    } type;

    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
    float flicker;          // Flicker intensity for flames
};

class InteriorLightingSystem {
public:
    std::vector<InteriorLightSource> generate(
        const Room& room,
        float timeOfDay,        // 0-24 hours
        float wealthLevel       // Affects number of candles
    );

    // Bake ambient occlusion for room corners
    void bakeAO(Mesh& roomMesh);
};
```

#### 5b.5.2 Window Light

```cpp
struct WindowLight {
    glm::vec3 position;
    glm::vec3 direction;        // Light direction
    glm::vec2 size;             // Window dimensions

    bool hasGlass;              // Rare in medieval period
    bool hasOiledLinen;         // Translucent covering
    bool hasShutter;            // Solid shutter

    // Light shaft visualization
    bool renderLightShaft;
    float dustParticleDensity;
};
```

### 5b.6 Interior/Exterior Transition

#### 5b.6.1 Portal System

```cpp
struct Portal {
    glm::vec3 position;
    glm::vec2 size;
    glm::quat orientation;

    enum class State {
        Open,
        Closed,
        Broken
    } state;

    // Visibility determination
    uint32_t interiorZoneId;
    uint32_t exteriorZoneId;

    // For rendering
    bool renderDoorMesh;
    std::string doorMeshId;
};

class PortalSystem {
public:
    // Determine visible zones from camera position
    std::vector<uint32_t> getVisibleZones(
        const Camera& camera,
        const std::vector<Portal>& portals
    );

    // Cull interior when door closed and outside
    bool shouldRenderInterior(
        const Building& building,
        const Camera& camera
    );
};
```

### 5b.7 Deliverables - Phase 3b

- [ ] FloorPlanGenerator with partition methods
- [ ] Room type definitions for all building types
- [ ] InteriorStructure generation (crucks, beams, pillars)
- [ ] HearthGenerator with smoke hood
- [ ] InteriorWallGenerator with treatments
- [ ] InteriorPropPlacer with furniture rules
- [ ] Furniture model library
- [ ] InteriorLightingSystem
- [ ] PortalSystem for visibility

**Testing**: For each building type:
- Walk through interior and verify room layout
- Check furniture placement is sensible
- Verify lighting responds to time of day
- Test portal visibility culling

---

## 6. Phase 4: Street & Infrastructure Networks

### 6.1 Street Mesh Generation

#### 6.1.1 Street Surface

```cpp
class StreetMeshGenerator {
public:
    struct Config {
        float curb_height = 0.15f;
        float camber = 0.02f;           // Cross-slope for drainage
        bool cobblestones = true;
        float tessellation = 2.0f;      // Vertices per meter
    };

    Mesh generate(
        const StreetNetwork& network,
        const TerrainAnalysis& terrain,
        const Config& config
    );

private:
    // Generate road surface following terrain
    void generateSurface(const StreetSegment& segment, Mesh& mesh);

    // Generate curbs along edges
    void generateCurbs(const StreetSegment& segment, Mesh& mesh);

    // Generate intersection geometry
    void generateIntersection(
        const std::vector<const StreetSegment*>& segments,
        Mesh& mesh
    );
};
```

#### 6.1.2 Street Materials

```json
// assets/materials/streets/cobblestone.json
{
    "id": "cobblestone",
    "albedo": "textures/streets/cobblestone_albedo.png",
    "normal": "textures/streets/cobblestone_normal.png",
    "roughness": 0.8,
    "ao": "textures/streets/cobblestone_ao.png",
    "displacement": "textures/streets/cobblestone_height.png",
    "displacement_scale": 0.05,
    "tiling": [4, 4]
}
```

### 6.2 Infrastructure Elements

#### 6.2.1 Wells and Pumps

```cpp
struct WellPlacement {
    glm::vec2 position;
    float rotation;
    enum class Type { Well, Pump, Trough } type;
    bool isShared;                      // Shared between lots
};

class WellPlacer {
public:
    std::vector<WellPlacement> place(
        const std::vector<BuildingLot>& lots,
        const StreetNetwork& streets,
        float maxDistanceBetweenWells = 60.0f
    );
};
```

#### 6.2.2 Fences and Walls

```cpp
class FenceGenerator {
public:
    enum class Type {
        PicketFence,
        PostAndRail,
        DryStoneWall,
        HedgeRow,
        WattleFence
    };

    Mesh generate(
        const std::vector<glm::vec2>& path,
        Type type,
        float height
    );
};
```

#### 6.2.3 Street Furniture

```cpp
struct StreetFurniture {
    enum class Type {
        Signpost,
        Hitching_Post,
        Mounting_Block,
        Market_Cross,
        Stocks,             // Medieval punishment device
        Lamp_Post,
        Bench
    };

    glm::vec3 position;
    float rotation;
    Type type;
};

class StreetFurniturePlacer {
public:
    std::vector<StreetFurniture> place(
        const SettlementDefinition& settlement,
        const StreetNetwork& streets,
        const std::vector<BuildingLot>& lots
    );
};
```

### 6.3 Special Structures

#### 6.3.1 Bridges

```cpp
class BridgeGenerator {
public:
    enum class Type {
        Stone_Arch,
        Timber_Beam,
        Clapper                 // Simple stone slab
    };

    Mesh generate(
        glm::vec2 start,
        glm::vec2 end,
        float waterLevel,
        Type type
    );
};
```

#### 6.3.2 Town Walls (for Towns)

```cpp
class TownWallGenerator {
public:
    struct Config {
        float wallHeight = 6.0f;
        float wallThickness = 2.0f;
        float towerSpacing = 50.0f;
        float towerHeight = 10.0f;
        int gateCount = 2;
    };

    struct WallSegment {
        std::vector<glm::vec2> path;
        bool hasTower;
        bool isGate;
    };

    std::vector<WallSegment> generate(
        const SettlementDefinition& settlement,
        const StreetNetwork& streets,
        const Config& config
    );

    Mesh generateMesh(const std::vector<WallSegment>& segments);
};
```

### 6.4 Deliverables - Phase 4

- [ ] StreetMeshGenerator with intersections
- [ ] Street texture generation (cobblestone, dirt, gravel)
- [ ] WellPlacer and well/pump models
- [ ] FenceGenerator with all types
- [ ] StreetFurniturePlacer
- [ ] BridgeGenerator
- [ ] TownWallGenerator (for town settlements)

**Testing**: Generate complete village layout with:
- Connected street network rendered
- Fences around properties
- Wells distributed appropriately
- Street furniture at key locations

---

## 7. Phase 5: Props & Detail Population

### 7.1 Vegetation Integration

#### 7.1.1 Settlement Vegetation

```cpp
class SettlementVegetation {
public:
    struct Config {
        float gardenTreeDensity = 0.02f;
        float orchardTreeSpacing = 8.0f;
        float hedgeHeight = 1.5f;
    };

    struct TreePlacement {
        glm::vec2 position;
        std::string treeType;       // "apple", "pear", "oak", etc.
        float scale;
    };

    // Generate garden trees
    std::vector<TreePlacement> placeGardenTrees(
        const std::vector<BuildingLot>& lots,
        const Config& config
    );

    // Generate orchard if settlement has agricultural zone
    std::vector<TreePlacement> generateOrchard(
        const Polygon& area,
        const Config& config
    );

    // Generate hedge boundaries
    std::vector<std::vector<glm::vec2>> generateHedges(
        const std::vector<BuildingLot>& lots
    );
};
```

#### 7.1.2 Tree Exclusion Zones

```cpp
class VegetationExclusion {
public:
    // Generate mask for TreeSystem to exclude trees
    void generateExclusionMask(
        const SettlementDefinition& settlement,
        const std::string& outputPath,
        uint32_t resolution = 512
    );
};
```

### 7.2 Ground Cover

#### 7.2.1 Ground Detail Decals

```cpp
class GroundDetailPlacer {
public:
    enum class DetailType {
        Mud_Puddle,
        Hay_Scatter,
        Fallen_Leaves,
        Manure,
        Dirt_Patch,
        Grass_Tuft,
        Moss_Patch
    };

    struct Placement {
        glm::vec2 position;
        float rotation;
        float scale;
        DetailType type;
    };

    std::vector<Placement> place(
        const SettlementDefinition& settlement,
        const std::vector<BuildingLot>& lots,
        const StreetNetwork& streets
    );
};
```

### 7.3 Props and Clutter

#### 7.3.1 Yard Props

```cpp
class YardPropPlacer {
public:
    enum class PropType {
        // Agricultural
        Hay_Bale,
        Cart,
        Barrel,
        Crate,
        Sack,
        Pitchfork,
        Scythe,

        // Domestic
        Bucket,
        Wash_Tub,
        Clothesline,
        Firewood_Stack,

        // Commercial
        Market_Stall,
        Cask,
        Anvil,
        Bellows,

        // Maritime
        Net_Rack,
        Lobster_Pot,
        Boat,
        Oar_Rack
    };

    struct Placement {
        glm::vec3 position;
        float rotation;
        PropType type;
        float scale = 1.0f;
    };

    std::vector<Placement> place(
        const BuildingLot& lot,
        const std::string& buildingType
    );
};
```

#### 7.3.2 Prop Models

Store props as GLB files with multiple LOD levels:

```
assets/props/
├── agricultural/
│   ├── hay_bale.glb
│   ├── cart.glb
│   └── barrel.glb
├── domestic/
│   ├── bucket.glb
│   └── firewood_stack.glb
├── commercial/
│   └── market_stall.glb
└── maritime/
    ├── net_rack.glb
    └── lobster_pot.glb
```

### 7.4 Ambient Life (Optional/Future)

```cpp
// For future implementation
struct AmbientCreature {
    enum class Type {
        Chicken,
        Goose,
        Pig,
        Sheep,
        Dog,
        Cat
    };

    glm::vec3 position;
    Type type;
    uint32_t wanderAreaId;      // Constrained to yard/pen
};
```

### 7.5 Deliverables - Phase 5

- [ ] SettlementVegetation integration with TreeSystem
- [ ] VegetationExclusion mask generation
- [ ] GroundDetailPlacer
- [ ] YardPropPlacer with zone-aware placement
- [ ] Prop model library (GLB)
- [ ] Prop LOD system integration

**Testing**: Full settlement with:
- Garden trees and orchards visible
- Ground clutter distributed appropriately
- Props matching building types (farm props near barns, maritime props near quays)

---

## 7b. Phase 5b: Terrain Integration & Subterranean

### 7b.1 Terrain Modification System

Integration with the existing terrain cut system for building foundations and subterranean spaces.

#### 7b.1.1 Foundation Types

```cpp
enum class FoundationType {
    Surface,            // Building sits directly on terrain
    Leveled,            // Terrain flattened under footprint
    Cut,                // Cut into hillside
    CutAndFill,         // Combination for sloped sites
    Terraced,           // Multiple levels on steep slopes
    Raised,             // Platform above terrain (waterlogged areas)
    Piled               // Posts driven into soft ground
};

struct BuildingFoundation {
    FoundationType type;
    Polygon footprint;
    float targetElevation;

    // Cut parameters
    float cutDepth;
    float cutSlope;             // Angle of cut walls

    // Fill parameters
    float fillHeight;
    bool retainingWall;
    std::string wallMaterial;   // "flint", "chalk", "timber"

    // Blending
    float blendRadius;          // Distance to blend to natural terrain
};
```

#### 7b.1.2 Terrain Cut Integration

```cpp
class SettlementTerrainModifier {
public:
    // Generate all terrain modifications for a settlement
    std::vector<TerrainModification> generate(
        const SettlementLayout& layout,
        const TerrainAnalysis& terrain
    );

    // Export as heightmap modification layer
    void exportHeightModification(
        const std::vector<TerrainModification>& mods,
        const std::string& outputPath,
        uint32_t resolution
    );

    // Export as terrain material mask (where to blend settlement ground)
    void exportMaterialMask(
        const std::vector<TerrainModification>& mods,
        const std::string& outputPath,
        uint32_t resolution
    );

private:
    // Calculate optimal foundation for building on slope
    FoundationType selectFoundationType(
        const BuildingLot& lot,
        float maxSlope,
        float averageSlope
    );

    // Generate cut geometry
    Mesh generateCutWalls(const TerrainModification& mod);
};
```

### 7b.2 Cellar Generation

#### 7b.2.1 Cellar Types

| Building Type | Cellar Probability | Cellar Type |
|--------------|-------------------|-------------|
| Peasant Cottage | 0.1 | Storage pit |
| Stone House | 0.7 | Full cellar |
| Inn | 0.9 | Barrel cellar |
| Church | 0.4 | Crypt |
| Manor House | 0.8 | Wine cellar |
| Warehouse | 0.6 | Storage cellar |

```cpp
struct Cellar {
    Polygon footprint;          // May be smaller than building
    float depth;                // Below ground level
    float ceilingHeight;

    enum class Type {
        StoragePit,     // Simple pit with ladder
        VaultedCellar,  // Stone vaulted ceiling
        TimberCellar,   // Timber-lined
        Crypt,          // Church undercroft
        Undercroft      // Ground floor below hall
    } type;

    // Access
    enum class Access {
        InternalStair,      // Stair inside building
        ExternalStair,      // Steps from outside
        Trapdoor,           // Hatch in floor
        Ramp                // Cart access (warehouses)
    } access;
    glm::vec2 accessPosition;

    // Contents
    std::vector<std::string> propTypes;  // Barrels, crates, etc.
};
```

#### 7b.2.2 Cellar Generator

```cpp
class CellarGenerator {
public:
    Cellar generate(
        const Building& building,
        const Polygon& footprint,
        float groundLevel,
        uint64_t seed
    );

    // Generate cellar geometry
    Mesh generateMesh(const Cellar& cellar);

    // Generate terrain cut for cellar
    TerrainModification generateTerrainCut(
        const Cellar& cellar,
        glm::vec2 buildingPosition
    );

private:
    // Generate vaulted ceiling
    Mesh generateVaultedCeiling(const Polygon& footprint, float height);

    // Generate access stairs
    Mesh generateStairs(
        const Cellar& cellar,
        Cellar::Access accessType
    );
};
```

### 7b.3 Church Crypts and Undercrofts

#### 7b.3.1 Crypt Structure

```cpp
struct ChurchCrypt {
    // Location
    enum class Position {
        UnderChancel,       // Most common
        UnderNave,          // Larger churches
        UnderTower          // Tower base crypt
    } position;

    Polygon footprint;
    float depth;

    // Architecture
    bool hasAisles;
    int numBays;
    float vaultHeight;

    // Features
    bool hasShrineNiche;        // For relics
    bool hasAltarPlatform;
    std::vector<glm::vec2> pillarPositions;

    // Access
    glm::vec2 stairPosition;
    bool externalAccess;        // Can be entered from outside
};

class CryptGenerator {
public:
    ChurchCrypt generate(
        const Building& church,
        uint64_t seed
    );

    Mesh generateMesh(const ChurchCrypt& crypt);
    TerrainModification generateTerrainCut(const ChurchCrypt& crypt);
};
```

### 7b.4 Wells and Water Access

#### 7b.4.1 Well Types

```cpp
struct Well {
    glm::vec2 position;

    enum class Type {
        OpenWell,           // Simple stone-lined shaft
        CoveredWell,        // With roof structure
        DrawWell,           // With winding gear
        Pump,               // Hand pump (later period)
        Spring,             // Natural spring with stone surround
    } type;

    float shaftDepth;           // To water table
    float shaftDiameter;
    float waterLevel;           // Current water depth

    // Structure above ground
    float wallHeight;
    bool hasRoof;
    bool hasWindlass;
};

class WellGenerator {
public:
    Well generate(
        glm::vec2 position,
        float groundLevel,
        float waterTableDepth,
        uint64_t seed
    );

    // Generate visible structure
    Mesh generateAboveGround(const Well& well);

    // Generate shaft (for terrain integration)
    TerrainModification generateShaft(const Well& well);

    // Generate water surface
    Mesh generateWaterSurface(const Well& well);
};
```

### 7b.5 Tunnels and Passages

#### 7b.5.1 Underground Connections

```cpp
struct UndergroundPassage {
    glm::vec2 startPosition;
    glm::vec2 endPosition;

    std::vector<glm::vec2> waypoints;   // Path through terrain

    float width;
    float height;
    float depth;                        // Below ground

    enum class Type {
        ServiceTunnel,      // Connecting buildings
        Escape,             // Secret escape route
        Drainage,           // Water management
        Crypt_Connection    // Between church areas
    } type;

    // Construction
    bool isLined;           // Stone/timber lined
    std::string liningMaterial;
};

class TunnelGenerator {
public:
    UndergroundPassage generate(
        glm::vec2 start,
        glm::vec2 end,
        const TerrainAnalysis& terrain,
        uint64_t seed
    );

    Mesh generateMesh(const UndergroundPassage& passage);
    std::vector<TerrainModification> generateTerrainCuts(
        const UndergroundPassage& passage
    );
};
```

### 7b.6 Terrain Physics Integration

*Note: Physics for terrain cuts is pending implementation*

```cpp
// Future integration point
class TerrainPhysicsModifier {
public:
    // Update physics heightfield for terrain cuts
    void applyModifications(
        PhysicsHeightfield& heightfield,
        const std::vector<TerrainModification>& mods
    );

    // Generate collision geometry for cellar walls
    void generateCellarCollision(
        PhysicsSystem& physics,
        const Cellar& cellar
    );

    // Generate collision for tunnel
    void generateTunnelCollision(
        PhysicsSystem& physics,
        const UndergroundPassage& passage
    );
};
```

### 7b.7 Deliverables - Phase 5b

- [ ] SettlementTerrainModifier integration with existing terrain cut
- [ ] Foundation type selection algorithm
- [ ] CellarGenerator for all building types
- [ ] CryptGenerator for churches
- [ ] WellGenerator with shaft terrain integration
- [ ] TunnelGenerator for underground passages
- [ ] Terrain modification export (heightmap layer)
- [ ] Material mask export for settlement ground

**Testing**:
- Buildings on slopes have appropriate foundations
- Cellars are accessible and don't clip terrain
- Wells connect to water table correctly
- Church crypts render correctly below ground
- Terrain cuts blend smoothly to natural terrain

*Physics TODO*: Integrate terrain cuts with Jolt Physics heightfield

---

## 8. Phase 6: LOD & Streaming System

### 8.1 LOD Strategy

#### 8.1.1 Building LOD Levels

| LOD Level | Distance | Description |
|-----------|----------|-------------|
| LOD0 | 0-50m | Full detail with all geometry |
| LOD1 | 50-150m | Simplified geometry, full textures |
| LOD2 | 150-400m | Billboard impostor |
| LOD3 | 400m+ | Merged settlement silhouette |

#### 8.1.2 Building LOD Generation

```cpp
class BuildingLODGenerator {
public:
    // Generate simplified mesh
    Mesh generateLOD1(const Mesh& fullDetail, float targetTriangleReduction = 0.5f);

    // Generate impostor billboard
    Impostor generateLOD2(const Mesh& fullDetail, uint32_t atlasSize = 256);

    // Generate settlement silhouette mesh
    Mesh generateSettlementSilhouette(
        const std::vector<BuildingMesh>& buildings,
        float simplification = 0.1f
    );
};

struct Impostor {
    glm::vec4 atlasRect;            // UV rect in impostor atlas
    glm::vec3 boundingBoxMin;
    glm::vec3 boundingBoxMax;
    float groundOffset;
};
```

### 8.2 Impostor Atlas

#### 8.2.1 Atlas Generation

```cpp
class BuildingImpostorAtlas {
public:
    struct Config {
        uint32_t atlasSize = 4096;
        uint32_t tileSize = 256;
        int viewsPerBuilding = 8;       // Octagonal views
    };

    void generate(
        const std::vector<BuildingMesh>& buildings,
        const Config& config
    );

    void save(const std::string& atlasPath, const std::string& metadataPath);

private:
    // Render building from multiple angles
    std::vector<Image> renderViews(const BuildingMesh& building, int viewCount);

    // Pack into atlas
    void packAtlas(const std::vector<std::vector<Image>>& allViews);
};
```

### 8.3 Streaming System

#### 8.3.1 Settlement Streaming

```cpp
class SettlementStreamingSystem {
public:
    struct Config {
        float loadRadius = 500.0f;
        float unloadRadius = 700.0f;
        float lodTransitionDistance = 50.0f;
        uint32_t maxConcurrentLoads = 2;
    };

    void update(const Camera& camera);

private:
    // Currently loaded settlements
    std::map<uint32_t, LoadedSettlement> loadedSettlements;

    // Async loading queue
    std::queue<uint32_t> loadQueue;

    void startLoad(uint32_t settlementId);
    void finishLoad(uint32_t settlementId, SettlementData&& data);
    void unload(uint32_t settlementId);
};

struct LoadedSettlement {
    SettlementDefinition definition;

    // Per-LOD data
    std::vector<RenderableHandle> lod0Renderables;
    std::vector<RenderableHandle> lod1Renderables;
    RenderableHandle lod2Impostor;
    RenderableHandle lod3Silhouette;

    // Current LOD state
    int currentLOD;
    float lodBlendFactor;
};
```

### 8.4 Culling Integration

#### 8.4.1 Settlement Culling

```cpp
class SettlementCullingSystem {
public:
    void update(
        const Camera& camera,
        const std::vector<LoadedSettlement>& settlements
    );

    // Frustum culling for individual buildings
    std::vector<uint32_t> getVisibleBuildings(uint32_t settlementId) const;

    // Occlusion culling using Hi-Z
    void cullOccluded(const HiZPyramid& hiZ);

private:
    // Per-settlement visibility data
    std::map<uint32_t, std::vector<bool>> buildingVisibility;
};
```

### 8.5 Deliverables - Phase 6

- [ ] BuildingLODGenerator (mesh simplification)
- [ ] BuildingImpostorAtlas generation
- [ ] SettlementStreamingSystem
- [ ] SettlementCullingSystem with Hi-Z integration
- [ ] LOD transition shader (dithered cross-fade)
- [ ] Streaming configuration and tuning

**Testing**: Performance validation:
- 60 FPS with 5+ settlements in view
- Smooth LOD transitions (no popping)
- Memory budget adherence (<200MB per settlement at LOD0)

---

## 9. Phase 7: Integration & Polish

### 9.1 Runtime Integration

#### 9.1.1 SettlementSystem

```cpp
// src/settlement/SettlementSystem.h

class SettlementSystem : public RenderSystem {
public:
    static std::unique_ptr<SettlementSystem> create(const InitContext& ctx);

    void init(const InitContext& ctx) override;
    void update(const UpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void cleanup() override;

    // Settlement queries
    const Settlement* getNearestSettlement(glm::vec3 position) const;
    bool isInsideSettlement(glm::vec3 position) const;
    float getSettlementDensity(glm::vec3 position) const;

private:
    std::unique_ptr<SettlementStreamingSystem> streaming;
    std::unique_ptr<SettlementCullingSystem> culling;
    std::unique_ptr<BuildingRenderer> buildingRenderer;

    // All settlement definitions (loaded at startup)
    std::vector<SettlementDefinition> allSettlements;
};
```

### 9.2 Shader Integration

#### 9.2.1 Building Shader

```glsl
// shaders/building.vert
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    // ...
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint materialFlags;
    float lodBlend;
} pc;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out mat3 fragTBN;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(pc.model) * inNormal;
    fragTexCoord = inTexCoord;

    vec3 T = normalize(mat3(pc.model) * inTangent.xyz);
    vec3 N = normalize(fragNormal);
    vec3 B = cross(N, T) * inTangent.w;
    fragTBN = mat3(T, B, N);

    gl_Position = scene.proj * scene.view * worldPos;
}
```

```glsl
// shaders/building.frag
#version 450

#include "lighting_common.glsl"
#include "pbr_common.glsl"
#include "dither_common.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in mat3 fragTBN;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D roughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint materialFlags;
    float lodBlend;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // LOD dithering for smooth transitions
    if (pc.lodBlend < 1.0 && shouldDiscardForLOD(pc.lodBlend)) {
        discard;
    }

    // Sample textures
    vec4 albedo = texture(albedoMap, fragTexCoord);
    vec3 normal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    float roughness = texture(roughnessMap, fragTexCoord).r;
    float ao = texture(aoMap, fragTexCoord).r;

    // Transform normal to world space
    vec3 N = normalize(fragTBN * normal);

    // PBR lighting
    vec3 V = normalize(cameraPos - fragWorldPos);
    vec3 color = calculatePBRLighting(albedo.rgb, N, V, roughness, 0.0, ao);

    outColor = vec4(color, albedo.a);
}
```

#### 9.2.2 Impostor Shader

```glsl
// shaders/building_impostor.vert
#version 450

layout(location = 0) in vec3 inPosition;     // Billboard corner
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inAtlasRect;    // Per-instance atlas UV rect
layout(location = 3) in mat4 inModel;        // Per-instance transform

layout(set = 0, binding = 0) uniform SceneUBO { /* ... */ } scene;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Camera-facing billboard
    vec3 right = vec3(scene.view[0][0], scene.view[1][0], scene.view[2][0]);
    vec3 up = vec3(0, 1, 0);

    vec3 worldPos = inModel[3].xyz;
    vec2 size = vec2(inModel[0][0], inModel[1][1]);

    worldPos += right * inPosition.x * size.x;
    worldPos += up * inPosition.y * size.y;

    // Map to atlas rect
    fragTexCoord = inAtlasRect.xy + inTexCoord * inAtlasRect.zw;

    gl_Position = scene.proj * scene.view * vec4(worldPos, 1.0);
}
```

### 9.3 Time of Day Integration

```cpp
class SettlementLighting {
public:
    void update(float timeOfDay, const std::vector<LoadedSettlement>& settlements);

private:
    // Enable/disable window glow based on time
    void updateWindowEmission(float timeOfDay);

    // Update street lamp states
    void updateStreetLamps(float timeOfDay);

    // Chimney smoke intensity based on time and weather
    void updateChimneySmokeParams(float timeOfDay, float temperature);
};
```

### 9.4 Weather Integration

```cpp
class SettlementWeatherEffects {
public:
    void update(const WeatherState& weather);

private:
    // Wet surfaces during rain
    void updateWetness(float rainIntensity);

    // Snow accumulation on roofs
    void updateSnowAccumulation(float snowAmount);

    // Puddle placement in settlement streets
    void updatePuddles(float groundWetness);
};
```

### 9.5 Audio Integration

```cpp
struct SettlementAmbience {
    // Distance-based ambient sounds
    std::string ambienceId;             // "village_day", "village_night", etc.
    float radius;
    float volume;

    // Point sound emitters
    struct SoundEmitter {
        glm::vec3 position;
        std::string soundId;            // "blacksmith_hammer", "church_bell", etc.
        float radius;
        float probability;              // Chance to play per interval
    };
    std::vector<SoundEmitter> emitters;
};
```

### 9.6 Deliverables - Phase 7

- [ ] SettlementSystem runtime class
- [ ] Building and impostor shaders
- [ ] Time of day integration (window glow, lamps)
- [ ] Weather effect integration
- [ ] Audio hooks for settlement ambience
- [ ] Full integration with existing render pipeline

**Testing**: Complete end-to-end validation:
- Walk through settlement in real-time
- Day/night cycle with lighting changes
- Weather effects (rain on roofs, snow accumulation)
- Performance profiling and optimization

---

## 10. Quality Assurance & Testing

### 10.1 Visual Quality Validation

#### 10.1.1 Reference Comparisons

- Compare generated buildings against reference photos of English villages
- Validate proportions against architectural standards
- Check material authenticity (correct stone, thatch, timber styles)

#### 10.1.2 Visual Checklist

- [ ] Buildings have correct floor heights (2.4-2.8m)
- [ ] Roof pitches match regional style (35-50° for thatch)
- [ ] Windows are proportioned correctly (height > width)
- [ ] Doors are human scale (1.9-2.1m height)
- [ ] Materials tile without obvious repetition
- [ ] LOD transitions are smooth (no popping)
- [ ] Impostor silhouettes match full geometry

### 10.2 Layout Validation

#### 10.2.1 Accessibility Checks

```cpp
class LayoutValidator {
public:
    struct ValidationResult {
        bool allLotsAccessible;
        bool noOverlappingBuildings;
        bool streetsConnected;
        bool keyBuildingsPresent;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    ValidationResult validate(
        const SettlementLayout& layout,
        const SettlementTemplate& tmpl
    );
};
```

#### 10.2.2 Layout Checklist

- [ ] All lots have street frontage
- [ ] Key buildings (church, inn) are accessible
- [ ] No buildings overlap
- [ ] Street network is fully connected
- [ ] Settlement connects to external road network

### 10.3 Performance Validation

#### 10.3.1 Performance Targets

| Metric | Target | Method |
|--------|--------|--------|
| Draw calls per settlement | <100 | Instancing, batching |
| Triangle count (LOD0) | <500K per settlement | Mesh optimization |
| Texture memory | <50MB per settlement | Atlas packing |
| Load time | <2s | Async streaming |
| Frame time impact | <2ms | Profiling |

#### 10.3.2 Performance Tests

```cpp
class SettlementPerformanceTest {
public:
    struct Results {
        float averageFrameTime;
        float peakFrameTime;
        uint32_t drawCallCount;
        uint32_t triangleCount;
        size_t textureMemory;
        size_t meshMemory;
    };

    Results runBenchmark(
        const std::vector<SettlementDefinition>& settlements,
        const Camera& camera
    );
};
```

### 10.4 Automated Testing

#### 10.4.1 Unit Tests

```cpp
// tests/settlement/test_footprint_generator.cpp
TEST(FootprintGenerator, RectangleFootprint) {
    FootprintGenerator gen;
    auto footprint = gen.generate({
        .shape = Shape::Rectangle,
        .dimensions = {10, 15}
    }, 12345);

    EXPECT_EQ(footprint.vertices.size(), 4);
    EXPECT_FLOAT_EQ(footprint.area(), 150.0f);
}

TEST(RoofGenerator, GableRoof) {
    RoofGenerator gen;
    Polygon footprint = createRectangle(10, 15);
    auto roof = gen.generate(footprint, {
        .type = Type::Gable,
        .pitch = 45.0f
    });

    EXPECT_GT(roof.vertices.size(), 0);
    EXPECT_TRUE(roof.isWatertight());
}
```

#### 10.4.2 Integration Tests

```cpp
// tests/settlement/test_settlement_generation.cpp
TEST(SettlementGeneration, EndToEnd) {
    SettlementDefinition def = {
        .type = SettlementType::Village,
        .center = {1000, 1000},
        .seed = 42
    };

    SettlementGenerator gen;
    auto layout = gen.generateLayout(def);

    EXPECT_GE(layout.lots.size(), 15);
    EXPECT_LE(layout.lots.size(), 40);

    LayoutValidator validator;
    auto result = validator.validate(layout);

    EXPECT_TRUE(result.allLotsAccessible);
    EXPECT_TRUE(result.noOverlappingBuildings);
}
```

### 10.5 Manual Testing Procedure

1. **Generate all settlement types** with different seeds
2. **Walk through each settlement** in-game
3. **Verify visual quality** against reference images
4. **Check performance** on target hardware
5. **Test LOD transitions** at various distances
6. **Validate streaming** with rapid camera movement

---

## 11. Research References

### 11.1 Academic Papers

1. **Parish, Y. I. H., & Müller, P. (2001)**. "Procedural modeling of cities." *SIGGRAPH '01*. [Link](https://cgl.ethz.ch/Downloads/Publications/Papers/2001/p_Par01.pdf)
   - Foundation for procedural road networks (reference, but we use space colonization instead)

2. **Runions, A., Lane, B., & Prusinkiewicz, P. (2007)**. "Modeling Trees with a Space Colonization Algorithm." *Eurographics Workshop on Natural Phenomena*.
   - **Key algorithm for street network generation** - organic growth toward attractors

3. **Müller, P., Wonka, P., Haegler, S., Ulmer, A., & Van Gool, L. (2006)**. "Procedural modeling of buildings." *SIGGRAPH '06*.
   - CGA shape grammar for buildings

4. **Merrell, P., & Manocha, D. (2008)**. "Continuous model synthesis." *ACM TOG*.
   - Early constraint-based generation

5. **Gumin, M. (2016)**. "Wave Function Collapse algorithm."
   - [GitHub](https://github.com/mxgmn/WaveFunctionCollapse)

6. **Alaka, S., & Bidarra, R. (2023)**. "Hierarchical Semantic Wave Function Collapse." *PCG Workshop*.
   - Hierarchical WFC for complex structures

7. **Vanegas, C. A., et al. (2012)**. "Procedural Generation of Parcels in Urban Modeling." *Computer Graphics Forum*.
   - Lot subdivision algorithms

### 11.2 Industry Resources

1. **CityEngine** by ESRI
   - Commercial implementation of procedural city generation

2. **Medieval Fantasy City Generator** by Watabou
   - [itch.io](https://watabou.itch.io/medieval-fantasy-city-generator)
   - Inspiration for layout patterns

3. **Unreal Engine PCG Framework**
   - [Documentation](https://docs.unrealengine.com/5.2/en-US/procedural-content-generation-overview/)

### 11.3 Historical References

1. **Beresford, M. W. (1967)**. *New Towns of the Middle Ages*
   - Historical English settlement patterns

2. **Aston, M., & Bond, J. (1976)**. *The Landscape of Towns*
   - English village morphology

3. **Roberts, B. K. (1987)**. *The Making of the English Village*
   - Village plan analysis

---

## Appendix A: Tool Command Reference

### A.1 Settlement Generator

```bash
# Generate settlement layout
./build/debug/settlement_generator \
    --heightmap assets/terrain/heightmap.png \
    --biomes assets/terrain/biomes.png \
    --settlements assets/terrain/settlements.json \
    --templates assets/settlements/templates/ \
    --output assets/settlements/generated/ \
    --settlement-id 0 \
    --seed 12345

# Generate all settlements
./build/debug/settlement_generator \
    --heightmap assets/terrain/heightmap.png \
    --biomes assets/terrain/biomes.png \
    --settlements assets/terrain/settlements.json \
    --templates assets/settlements/templates/ \
    --output assets/settlements/generated/ \
    --all
```

### A.2 Building Generator

```bash
# Generate building prototypes
./build/debug/building_generator \
    --types assets/buildings/types/ \
    --components assets/buildings/components/ \
    --output assets/buildings/generated/ \
    --type cottage \
    --variations 5 \
    --seed 12345

# Generate LOD variants
./build/debug/building_generator \
    --input assets/buildings/generated/cottage_0.glb \
    --output assets/buildings/generated/cottage_0_lod1.glb \
    --lod 1

# Generate impostor atlas
./build/debug/building_generator \
    --atlas \
    --input assets/buildings/generated/ \
    --output assets/buildings/impostor_atlas.png \
    --atlas-size 4096
```

### A.3 Material Generator

```bash
# Generate wall material
./build/debug/material_generator \
    --type wall \
    --style wattle_daub \
    --output textures/buildings/wattle_daub \
    --size 1024

# Generate roof material
./build/debug/material_generator \
    --type roof \
    --style thatch \
    --output textures/buildings/thatch \
    --size 1024
```

---

## Appendix B: Configuration Schema

### B.1 Settlement Template Schema

```json
{
    "$schema": "http://json-schema.org/draft-07/schema#",
    "type": "object",
    "required": ["id", "name", "applicable_types", "layout", "zones"],
    "properties": {
        "id": { "type": "string" },
        "name": { "type": "string" },
        "applicable_types": {
            "type": "array",
            "items": { "enum": ["Hamlet", "Village", "Town", "FishingVillage"] }
        },
        "layout": {
            "type": "object",
            "properties": {
                "pattern": { "enum": ["linear", "radial", "grid", "organic"] },
                "main_street_width": { "type": "number" },
                "secondary_street_width": { "type": "number" },
                "min_lot_size": { "type": "array", "items": { "type": "number" } },
                "max_lot_size": { "type": "array", "items": { "type": "number" } }
            }
        },
        "zones": { "type": "object" },
        "features": { "type": "object" }
    }
}
```

### B.2 Building Type Schema

```json
{
    "$schema": "http://json-schema.org/draft-07/schema#",
    "type": "object",
    "required": ["id", "name", "category", "footprint", "floors", "roof", "facade"],
    "properties": {
        "id": { "type": "string" },
        "name": { "type": "string" },
        "category": { "enum": ["residential", "commercial", "religious", "industrial", "agricultural", "maritime"] },
        "applicable_zones": { "type": "array" },
        "footprint": {
            "type": "object",
            "properties": {
                "min_width": { "type": "number" },
                "max_width": { "type": "number" },
                "min_depth": { "type": "number" },
                "max_depth": { "type": "number" },
                "shapes": { "type": "array" }
            }
        },
        "floors": { "type": "object" },
        "roof": { "type": "object" },
        "facade": { "type": "object" },
        "attachments": { "type": "object" },
        "props": { "type": "object" }
    }
}
```

---

## Appendix C: Glossary

| Term | Definition |
|------|------------|
| **CGA** | Computer Generated Architecture - shape grammar for buildings |
| **WFC** | Wave Function Collapse - constraint-solving algorithm |
| **L-System** | Lindenmayer System - parallel rewriting grammar |
| **Straight Skeleton** | Algorithm for roof generation from polygon |
| **Impostor** | Billboard texture representing 3D object at distance |
| **LOD** | Level of Detail - simplified geometry for distant objects |
| **CBT** | Concurrent Binary Tree - terrain subdivision structure |

---

## Appendix D: Complete Building Type Catalogue

### D.1 Residential Buildings

#### Peasant Cottage
- **Era**: Throughout medieval period
- **Size**: 4-7m × 8-12m (single bay or two-bay)
- **Construction**: Cruck frame with wattle and daub infill (inland) or flint rubble (coastal/downland)
- **Roof**: Steeply pitched thatch (45-55°), no chimney (central hearth with smoke hole)
- **Features**: Single room with loft storage, earth floor, central hearth
- **Variations**: Single-bay cot (smallest), two-bay cottage

#### Longhouse
- **Era**: Common through 13th century, declining after
- **Size**: 5-7m × 15-25m
- **Construction**: Cruck frame, 3-5 bays
- **Layout**: Cross-passage dividing dwelling from byre, animals at downhill end
- **Roof**: Continuous thatched roof over both sections
- **Features**: Opposing doors at cross-passage, drainage channel in byre

#### Cruck Hall
- **Era**: 12th-14th century
- **Size**: 6-8m × 12-20m
- **Construction**: A-frame cruck blades supporting roof, exposed internally
- **Layout**: Open hall with central hearth, possible solar/chamber at one end
- **Roof**: High-pitched thatch, often with decorative ridge
- **Features**: Status symbol for wealthier peasants and minor gentry

#### Stone House
- **Era**: 13th century onwards (wealthy areas)
- **Size**: 6-10m × 10-15m
- **Construction**: Local stone (flint in chalk areas, limestone where available)
- **Layout**: Ground floor storage/workshop, living quarters above
- **Roof**: Stone slate or clay tile
- **Features**: Stone newel stair, larger windows, early chimneys

### D.2 Agricultural Buildings

#### Tithe Barn
- **Era**: 12th century onwards
- **Size**: 10-15m × 30-50m (very large)
- **Construction**: Heavy timber frame, stone in wealthy areas
- **Layout**: Large open interior with wagon doors on long sides
- **Roof**: Thatch or stone slate, very steeply pitched
- **Features**: Owned by church, stores tithes from parish

#### Granary
- **Era**: Throughout medieval period
- **Size**: 4-6m × 6-10m
- **Construction**: Timber frame on stone or timber staddle stones (mushroom-shaped supports)
- **Features**: Raised floor to prevent rodent access

#### Byre/Cowshed
- **Era**: Throughout medieval period
- **Size**: 5-7m × 10-15m
- **Construction**: Timber frame, open-fronted or enclosed
- **Features**: Drainage channel, tie stalls

#### Dovecote
- **Era**: 12th century onwards (manorial privilege)
- **Size**: 4-6m diameter (circular) or 4-5m square
- **Construction**: Stone (circular) or timber (square)
- **Features**: Internal nesting boxes, lantern roof, reserved for lords of the manor

### D.3 Religious Buildings

#### Parish Church
- **Era**: Saxon origins rebuilt Norman/Early English
- **Size**: Nave 6-10m × 15-25m, chancel smaller
- **Construction**: Stone (even in areas where timber dominates domestic)
- **Style**: Norman (round arches, thick walls) transitioning to Early English (pointed arches, lancet windows)
- **Features**: West tower (common in Sussex), central tower (rarer), porch (usually south)
- **Graveyard**: Always present, low wall or hedge boundary

#### Wayside Chapel
- **Era**: Throughout medieval period
- **Size**: 4-6m × 6-10m
- **Construction**: Stone or timber
- **Features**: Simple single-cell structure, may be at crossroads or on pilgrimage route

#### Preaching Cross
- **Era**: Throughout medieval period
- **Construction**: Stone shaft on stepped base
- **Location**: Market squares, churchyards, crossroads

### D.4 Commercial Buildings

#### Inn/Tavern
- **Era**: 12th century onwards
- **Size**: 6-10m × 12-20m
- **Construction**: As local houses but larger
- **Layout**: Ground floor hall/drinking room, upper chambers
- **Features**: Sign board, larger doorway, stable yard
- **Signs**: Distinctive symbols (bush for ale house, specific inn signs)

#### Smithy
- **Era**: Throughout medieval period
- **Size**: 5-7m × 8-12m
- **Construction**: Stone or timber, open-fronted work area
- **Features**: Forge, bellows, water trough, wide doorway
- **Location**: Edge of settlement (fire risk), near roads

#### Market Cross/Hall
- **Era**: 13th century onwards (chartered markets)
- **Construction**: Stone cross or timber/stone covered hall
- **Features**: Open ground floor for trading, upper floor for storage/guilds
- **Location**: Central, at road junction

#### Baker/Brewhouse
- **Era**: Throughout medieval period
- **Size**: 5-7m × 8-12m
- **Features**: Large oven (stone-built, dome-shaped), chimney or vent

### D.5 Mill Buildings

#### Watermill
- **Era**: Domesday records many
- **Size**: 6-8m × 10-15m (mill building)
- **Construction**: Stone or timber, over or adjacent to millrace
- **Features**: Undershot or overshot wheel, mill pond, sluice gate
- **Location**: River or stream, often at settlement edge

#### Windmill (Post Mill)
- **Era**: Late 12th century onwards
- **Size**: ~5m square body on central post
- **Construction**: Timber body rotating on massive oak post
- **Features**: Fantail for wind direction, sails, steps
- **Location**: Exposed hilltop or open ground

### D.6 Maritime Buildings (Fishing Villages)

#### Net Loft
- **Era**: Throughout medieval period
- **Size**: 4-6m × 8-12m
- **Construction**: Timber frame, upper floor open-sided
- **Features**: Ground floor storage, upper floor for net drying/repair

#### Fish Market/Shambles
- **Era**: 13th century onwards
- **Construction**: Open-sided timber structure
- **Features**: Stone or timber stalls, drainage

#### Quay/Jetty
- **Era**: Throughout medieval period
- **Construction**: Stone quay walls, timber jetties
- **Features**: Mooring rings, steps, slipway

#### Salthouse
- **Era**: Throughout medieval period
- **Size**: 6-8m × 10-15m
- **Construction**: Stone or timber
- **Features**: Salt pans, fire pit for evaporation

### D.7 Defensive Structures (Towns only)

#### Town Wall
- **Era**: 12th-14th century
- **Height**: 4-6m typically
- **Construction**: Stone, sometimes with rubble core
- **Features**: Wall walk, merlons, interval towers

#### Town Gate
- **Era**: With town wall
- **Size**: 4-6m wide passage
- **Features**: Twin towers flanking, portcullis, guard chambers

#### Motte (if pre-existing)
- **Era**: Norman conquest period
- **Construction**: Earth mound with timber or stone keep
- **Features**: May be ruined or replaced by manor house

### D.8 Building Material Palette by Biome

| Biome Zone | Primary Wall | Secondary Wall | Roofing | Decorative |
|------------|-------------|----------------|---------|------------|
| ChalkCliff | Knapped flint | Clunch (chalk block) | Thatch, stone slate | Flint + stone banding |
| Grassland (Downs) | Flint rubble | Cob | Thatch | Clunch dressings |
| Agricultural | Timber frame | Wattle and daub | Thatch | Brick (later period) |
| Woodland | Timber frame | Weatherboard | Thatch, clay tile | Decorative bargeboards |
| SaltMarsh | Flint | Timber frame | Thatch | Tarred timber |
| River Valley | Stone rubble | Timber frame | Thatch, clay tile | Carved stone |
| Coastal | Flint, stone | Tarred timber | Slate, tile | Whitewashed render |

### D.9 Prop and Detail Catalogue

#### Agricultural Props
- Haystacks (conical, thatched top)
- Wattle hurdles (portable fencing)
- Sheep creep (narrow gap in wall)
- Manure pile
- Tethering posts
- Stone troughs
- Wooden feeding racks
- Beehives (skeps - straw domes)

#### Domestic Props
- Well with winding gear
- Rain barrel
- Washing line
- Butter churn
- Chicken coops (woven baskets)
- Pig sty
- Vegetting frame
- Bread oven (detached, dome-shaped)

#### Commercial Props
- Market stalls (trestle tables)
- Hanging signs (iron bracket + timber board)
- Barrels and casks
- Sacks (grain, wool)
- Weighing scales
- Anvil and forge tools
- Drying racks

#### Maritime Props
- Fishing nets (hung to dry)
- Lobster/crab pots
- Boats (small clinker-built)
- Oars and poles
- Fish drying racks
- Salt pans
- Rope coils
- Anchor stones

---

## Appendix E: Historical Settlement Patterns

### E.1 Village Plan Types

#### Nucleated Village (most common in chalk downland)
- Clustered around church/green
- Single main street or crossroads
- Fields in open field strips beyond
- Common grazing on downs

#### Linear Village (common on coastal plain)
- Single street following road/stream
- Properties perpendicular to street
- Long narrow plots (tofts)

#### Green Village
- Houses around rectangular green
- Church prominent position
- Pond or well on green
- Market cross if chartered

#### Polyfocal Village
- Multiple clusters that merged
- May have multiple greens/churches
- Complex street pattern

### E.2 Field Systems

#### Open Field System
- 2 or 3 great fields
- Strip cultivation (selions)
- Communal farming decisions
- Fallow rotation

#### Enclosed Fields (later/upland)
- Hedged or walled
- Individual holdings
- Pastoral focus

### E.3 Social Hierarchy in Building

| Social Class | Building Type | Material Quality | Size | Features |
|-------------|---------------|------------------|------|----------|
| Lord | Manor House | Best local stone | Large | Hall, solar, chapel |
| Priest | Rectory/Vicarage | Good stone/timber | Medium-large | Garden, study |
| Freeman | Cruck Hall | Quality timber | Medium | Open hall |
| Villein | Longhouse | Standard timber | Medium | Combined dwelling/byre |
| Cottar | Cottage | Basic timber | Small | Single bay |

---

*Document Version: 1.0*
*Last Updated: [Generation Date]*
*Setting: High Medieval (c. 1100-1300) South Coast of England*
