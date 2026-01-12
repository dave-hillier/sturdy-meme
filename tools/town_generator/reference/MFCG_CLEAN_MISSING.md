# MFCG Clean Implementation - Missing Classes Analysis

This document compares the clean ES6 module implementation (`mfcg-clean/`) with the original Haxe-compiled JavaScript (`mfcg.js` and `mfcg/`).

## Summary

~~The clean implementation covers most core city generation functionality but is missing several utility classes, growth algorithms, and name generation features.~~

**UPDATE (2026-01-12):** All high-priority and medium-priority missing classes have been implemented. See "Implementation Status" section below.

---

## Missing Classes

### Core Model Classes (High Priority)

| Class | File Location (Original) | Purpose |
|-------|-------------------------|---------|
| `Grower` | `mfcg.js:11553` | Base class for district growth algorithms with rate-based expansion |
| `DocksGrower` | `mfcg.js:11613` | Validates patches for dock district expansion (checks landing + Alleys) |
| `ParkGrower` | `mfcg.js:11625` | Validates patches for park district expansion |
| `Landmark` | `mfcg.js:11817` | Places landmarks at positions using barycentric interpolation |

**Impact:** District expansion during city generation may not work correctly without Grower classes.

### Utility Classes (Medium Priority)

| Class | File Location (Original) | Purpose |
|-------|-------------------------|---------|
| `Bloater` | `mfcg.js:19658` | Polygon expansion via edge extrusion (`bloat()`, `extrude()`, `extrudeEx()`) |
| `Cutter` | `mfcg.js:19683` | Grid subdivision of quadrilaterals - `grid()` function used in Building.create() |
| `PathTracker` | `mfcg.js:19745` | Tracks position along polylines for curved labels and path operations |

**Impact:** Building geometry generation and curved label placement.

### Geometry Classes (Medium Priority)

| Class | File Location (Original) | Purpose | Usage |
|-------|-------------------------|---------|-------|
| `PolyBool` | `mfcg.js:7641` | Boolean polygon operations | Used in Ward.inset(), Block.indentFronts(), Bisector |
| `PolyBool.and()` | `mfcg.js:7698` | Polygon intersection | Clipping blocks to shapes |
| `PolyBool.augmentPolygons()` | `mfcg.js:7642` | Augments polygons for boolean ops | Internal helper |

**Impact:** Ward geometry clipping, block boundary adjustment, building placement.

### Linguistics Classes (Lower Priority)

| Class | File Location (Original) | Purpose |
|-------|-------------------------|---------|
| `Namer` | `mfcg.js:8503` | Town, street, and district name generation |
| `Markov` | `mfcg.js:19961` | Markov chain text generation for procedural names |
| `Syllables` | `mfcg.js:20022` | Syllable parsing for name merging |
| `Grammar` | `mfcg.js:20311` | Tracery-based grammar text generation |
| `Tracery` | `mfcg.js:20677` | Text generation engine |

**Impact:** Automatic city and district naming features.

### Other Classes (Low Priority)

| Class | File Location (Original) | Purpose |
|-------|-------------------------|---------|
| `Mansion` | `mfcg.js:12874` | Ward type that links to Dwellings tool for building interior export |
| `UnitSystem` | model | Metric/imperial unit conversion for scale bars |
| `Values` | `mfcg.js:8445` | Default configuration values |

**Impact:** UI features and external tool integration.

---

## What IS Present in Clean Implementation

The clean implementation includes all essential components for basic city generation:

### Core Classes (Present)
- **Model:** Blueprint, Building, Canal, Cell, City, CurtainWall, District, DistrictBuilder, Forester, Topology
- **Wards:** Ward, Alleys, Block, TwistedBlock, Castle, Cathedral, Farm, Harbour, Market, Park, WardGroup, Wilderness
- **Geometry:** DCEL, Vertex, HalfEdge, Face, EdgeChain, Circle, Segment, GeomUtils
- **Algorithms:** Voronoi, Triangulation, Graph, Node, SkeletonBuilder, Rib, Chaikin, PoissonPattern
- **Utilities:** PolygonUtils (combines PolyCore/PolyBounds/PolyCreate/PolyCut/PolyTransform), Bisector, Noise, Perlin
- **Export:** JsonExporter, GeoJSON

### PolygonUtils Coverage
The clean `PolygonUtils.js` combines functionality from:
- PolyCore: area, perimeter, centroid, center, compactness
- PolyBounds: bounds, rect, obb, lira, containsPoint
- PolyCreate: regular, rectangle
- PolyTransform: translate, rotate, scale
- PolyCut: shrink, inset, cut, grid

**Missing from PolygonUtils:** PolyBool operations (and/or/subtract)

---

## Priority Implementation Order

If implementing missing functionality:

1. **PolyBool.and()** - Critical for geometry clipping
2. **Grower/DocksGrower/ParkGrower** - District expansion
3. **Cutter.grid()** (verify against PolygonUtils.grid) - Building layouts
4. **Bloater** - Polygon expansion
5. **Landmark** - Marker placement
6. **Namer/Markov** - Name generation (optional for core functionality)

---

## Notes

- The clean implementation uses modern ES6 class syntax
- Original uses Haxe-compiled function prototypes
- Many UI/rendering classes intentionally omitted (BuildingPainter, FarmPainter, etc.)
- Scene and overlay classes omitted (TownScene, ViewScene, etc.)
- Form and tool classes omitted (ToolForm, StyleForm, etc.)

---

## Implementation Status

The following classes have been implemented and added to the clean implementation:

### Implemented (2026-01-12)

| Class | File | Status |
|-------|------|--------|
| `Grower` | `model/Grower.js` | ✅ Implemented |
| `DocksGrower` | `model/Grower.js` | ✅ Implemented |
| `ParkGrower` | `model/Grower.js` | ✅ Implemented |
| `Landmark` | `model/Landmark.js` | ✅ Implemented |
| `PolyBool` | `geometry/PolyBool.js` | ✅ Implemented |
| `Bloater` | `utils/Bloater.js` | ✅ Implemented |
| `Cutter` | `utils/Cutter.js` | ✅ Implemented |
| `PathTracker` | `utils/PathTracker.js` | ✅ Implemented |
| `Syllables` | `linguistics/Syllables.js` | ✅ Implemented |
| `Markov` | `linguistics/Markov.js` | ✅ Implemented |
| `Namer` | `linguistics/Namer.js` | ✅ Implemented |

### Still Missing (Low Priority)

| Class | Purpose | Notes |
|-------|---------|-------|
| `Mansion` | Links to Dwellings tool | UI/export feature only |
| `UnitSystem` | Metric/imperial conversion | UI feature only |
| `Values` | Default configuration | UI defaults |
| `Grammar`/`Tracery` | Full grammar-based generation | Simplified in Namer |

---

*Analysis performed: 2026-01-12*
*Implementation completed: 2026-01-12*
