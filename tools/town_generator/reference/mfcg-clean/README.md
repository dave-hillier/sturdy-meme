# MFCG Clean - Medieval Fantasy City Generator

A clean ES6 refactoring of the [Medieval Fantasy City Generator](https://watabou.itch.io/medieval-fantasy-city-generator) by Oleg Dolya (watabou).

## Overview

This is a standalone JavaScript library that procedurally generates medieval fantasy city layouts. The code has been refactored from the original Haxe-compiled JavaScript into clean, readable ES6 modules with one class per file.

## Structure

```
mfcg-clean/
├── core/           # Core utilities
│   ├── Point.js    # 2D point/vector
│   ├── Random.js   # Seeded RNG
│   └── MathUtils.js
├── geometry/       # Geometry classes
│   ├── Circle.js
│   ├── Segment.js
│   ├── DCEL.js     # Doubly-connected edge list
│   ├── Voronoi.js  # Voronoi diagram
│   ├── Chaikin.js  # Curve smoothing
│   ├── GeomUtils.js
│   ├── PolygonUtils.js
│   ├── PoissonPattern.js  # Poisson disk sampling
│   ├── Graph.js    # Graph with A* pathfinding
│   └── SkeletonBuilder.js # Straight skeleton for roofs
├── model/          # City model
│   ├── City.js     # Main generator
│   ├── Blueprint.js # Generation parameters
│   ├── Building.js
│   ├── Cell.js     # Voronoi cell
│   ├── CurtainWall.js
│   ├── Canal.js
│   ├── District.js
│   ├── Forester.js # Tree placement
│   └── Topology.js # Road network pathfinding
├── wards/          # Ward types
│   ├── Ward.js     # Base class
│   ├── Alleys.js   # Urban housing
│   ├── Castle.js
│   ├── Cathedral.js
│   ├── Farm.js
│   ├── Harbour.js
│   ├── Market.js
│   ├── Park.js
│   └── Wilderness.js
├── utils/
│   ├── Noise.js    # Perlin/fractal noise
│   └── Bisector.js # Polygon subdivision
├── export/
│   └── JsonExporter.js # GeoJSON export
└── index.js        # Main entry point
```

## Usage

```javascript
import { generateCity, City, Blueprint } from './mfcg-clean/index.js';

// Simple usage
const city = generateCity({
    size: 20,        // Number of patches (6-60)
    seed: 12345,     // Random seed
    walls: true,     // City walls
    citadel: true,   // Castle
    plaza: true,     // Central plaza
    temple: true,    // Cathedral
    coast: true,     // Coastline
    river: true      // River
});

console.log(`Generated: ${city.name}`);
console.log(`Buildings: ${city.countBuildings()}`);
console.log(`Districts: ${city.districts.length}`);

// Advanced usage
const blueprint = new Blueprint(25, Date.now());
blueprint.walls = true;
blueprint.citadel = true;
blueprint.coast = false;  // Inland city

const city2 = new City(blueprint);
city2.build();
```

## Class Overview

### Core

- **Point** - 2D point/vector with common operations
- **Random** - Deterministic seeded random number generator
- **MathUtils** - Math utilities (clamp, lerp, etc.)

### Geometry

- **DCEL** - Doubly-connected edge list for planar subdivision
- **Voronoi** - Voronoi diagram generator
- **Chaikin** - Curve smoothing algorithm
- **PolygonUtils** - Polygon operations (area, centroid, inset, etc.)
- **PoissonPattern** - Poisson disk sampling for natural distribution
- **Graph/Node** - Graph data structure with A* pathfinding
- **SkeletonBuilder** - Straight skeleton algorithm for roof generation

### Model

- **City** - Main generator class
- **Blueprint** - Generation parameters
- **Cell** - Single Voronoi cell/patch
- **CurtainWall** - City/castle walls with towers and gates
- **Canal** - Rivers and water features
- **District** - Named city districts
- **Forester** - Tree placement using Poisson sampling
- **Topology** - Road network pathfinding

### Utilities

- **Noise/Perlin** - Fractal noise generation
- **Bisector** - Polygon subdivision for lot generation

### Export

- **JsonExporter** - Export city to GeoJSON format
- **GeoJSON** - GeoJSON geometry helpers

### Wards

Ward types determine how each cell is developed:

- **Alleys** - Dense urban housing with buildings
- **Castle** - Fortified citadel
- **Cathedral** - Religious district
- **Farm** - Agricultural area with fields
- **Harbour** - Waterfront with docks
- **Market** - Central plaza
- **Park** - Green spaces
- **Wilderness** - Undeveloped natural area

## Algorithm

1. **Patch Generation** - Create Voronoi diagram from spiral of points
2. **Coast/Water** - Mark cells as water based on noise
3. **Optimization** - Collapse short edges for better junctions
4. **Walls** - Build city wall around inner patches
5. **Streets** - Connect gates to center via streets
6. **Canals** - Add river through city
7. **Wards** - Assign ward types to each cell
8. **Geometry** - Generate buildings and details

## Credits

Original generator by [Oleg Dolya (watabou)](https://watabou.itch.io/)

This refactoring extracts the core algorithm into clean, readable ES6 modules for educational purposes and integration into other projects.
