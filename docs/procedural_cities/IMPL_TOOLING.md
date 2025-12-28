# Implementation - 2D Preview & Browser Tooling

[← Back to Index](README.md)

---

## 9. 2D Preview Layer & Browser Tooling

The foundation and layout systems are fundamentally **2D operations**. Before generating any 3D meshes, we should be able to visualize and iterate on layouts using SVG preview. This enables:

- Rapid iteration on layout algorithms without 3D rebuild
- Interactive parameter tweaking in browser
- Visual debugging of path networks, lot subdivision, zone assignment
- Sharing previews without requiring the full engine

### 9.1 Architecture: Dual Implementation

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           BROWSER PREVIEW LAYER                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                │
│    │   Browser    │    │     SVG      │    │  Interactive │                │
│    │   UI (HTML)  │───▶│   Renderer   │───▶│   Controls   │                │
│    └──────────────┘    └──────────────┘    └──────────────┘                │
│           │                                        │                        │
│           ▼                                        ▼                        │
│    ┌──────────────────────────────────────────────────────────┐            │
│    │              TypeScript Layout Library                    │            │
│    │  (Spatial utils, Path network, Layout, Subdivision)       │            │
│    └──────────────────────────────────────────────────────────┘            │
│                              │                                              │
├──────────────────────────────┼──────────────────────────────────────────────┤
│                              │   SHARED ALGORITHM SPEC                      │
├──────────────────────────────┼──────────────────────────────────────────────┤
│                              ▼                                              │
│    ┌──────────────────────────────────────────────────────────┐            │
│    │                C++ Layout Library                         │            │
│    │  (Spatial utils, Path network, Layout, Subdivision)       │            │
│    └──────────────────────────────────────────────────────────┘            │
│                              │                                              │
│                              ▼                                              │
│    ┌──────────────────────────────────────────────────────────┐            │
│    │              3D Mesh Generation (C++ only)                │            │
│    └──────────────────────────────────────────────────────────┘            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Strategy**: Implement 2D algorithms in both TypeScript (for browser preview) and C++ (for build-time generation). The algorithms are simple enough that maintaining both is feasible, and the TypeScript version serves as executable specification.

### 9.2 Browser Tool Features

```
┌─────────────────────────────────────────────────────────────────────┐
│  Settlement Layout Preview                              [Export SVG] │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────┐  ┌──────────────────────┐ │
│  │                                     │  │ Parameters           │ │
│  │                                     │  │ ─────────────────    │ │
│  │          [SVG Canvas]               │  │ Seed: [12345    ]    │ │
│  │                                     │  │ Type: [Village  ▼]   │ │
│  │     Roads ═══                       │  │ Density: [0.7═══]    │ │
│  │     Streets ───                     │  │ Organicness: [══0.6] │ │
│  │     Lots █ (colored by zone)        │  │                      │ │
│  │     Buildings ▢ (footprints)        │  │ Show Layers          │ │
│  │     Walls ▓▓▓                       │  │ ☑ Roads              │ │
│  │                                     │  │ ☑ Streets            │ │
│  │                                     │  │ ☑ Lots               │ │
│  │                                     │  │ ☑ Buildings          │ │
│  │                                     │  │ ☐ Zone colors        │ │
│  │                                     │  │ ☐ Walls              │ │
│  │                                     │  │ ☐ Fields             │ │
│  └─────────────────────────────────────┘  │                      │ │
│                                           │ [Regenerate]         │ │
│  Lot #47: Residential, 8.2m × 32m        │ [Export JSON]         │ │
│  Street frontage: Main Street             └──────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**Interactive features**:
- Pan/zoom SVG canvas
- Click elements to inspect (lot dimensions, zone, street frontage)
- Slider controls for all parameters with live regeneration
- Layer visibility toggles
- Seed input for reproducibility
- Export to SVG (for documentation) or JSON (for C++ import)

### 9.3 TypeScript Library Structure

```typescript
// web-tools/src/spatial/

// ─────────────────────────────────────────────────────────────────────
// CORE TYPES (matching C++ structs)
// ─────────────────────────────────────────────────────────────────────

interface Vec2 {
  x: number;
  y: number;
}

interface Polygon {
  vertices: Vec2[];

  area(): number;
  centroid(): Vec2;
  contains(point: Vec2): boolean;
  bounds(): AABB2D;
}

interface Polyline {
  points: Vec2[];
  closed: boolean;

  length(): number;
  pointAt(t: number): Vec2;
  tangentAt(t: number): Vec2;
}

// ─────────────────────────────────────────────────────────────────────
// SUBDIVISION
// ─────────────────────────────────────────────────────────────────────

interface SubdivisionResult {
  cells: Polygon[];
  parentEdges: number[];  // Which original edge each cell touches
}

function frontageSubdivide(
  block: Polygon,
  frontageEdge: Vec2[],
  minLotWidth: number,
  maxLotWidth: number,
  targetDepth: number,
  seed: number
): SubdivisionResult;

function bspSubdivide(
  region: Polygon,
  minCellArea: number,
  minCellWidth: number,
  seed: number
): SubdivisionResult;

// ─────────────────────────────────────────────────────────────────────
// PATH NETWORK
// ─────────────────────────────────────────────────────────────────────

interface PathNode {
  id: number;
  position: Vec2;
  connections: number[];
  type: 'endpoint' | 'junction' | 'gate' | 'entry';
}

interface PathSegment {
  id: number;
  startNode: number;
  endNode: number;
  type: PathType;
  controlPoints: Vec2[];
  geometry?: Polyline;  // Computed after finalize
}

class PathNetwork {
  addNode(position: Vec2, type?: PathNode['type']): number;
  addSegment(startNode: number, endNode: number, type: PathType): number;
  finalize(): void;

  // Queries
  getNode(id: number): PathNode;
  getSegment(id: number): PathSegment;
  findNearestNode(position: Vec2): number;
}

// ─────────────────────────────────────────────────────────────────────
// SPACE COLONIZATION
// ─────────────────────────────────────────────────────────────────────

interface Attractor {
  position: Vec2;
  weight: number;
  type: 'keyBuilding' | 'boundary' | 'lotFrontage' | 'external';
}

interface SpaceColonizationConfig {
  killDistance: number;
  influenceRadius: number;
  segmentLength: number;
  branchAngleLimit: number;
  maxIterations: number;
}

function spaceColonization(
  attractors: Attractor[],
  seedPosition: Vec2,
  seedDirection: Vec2,
  config: SpaceColonizationConfig
): PathNetwork;

// ─────────────────────────────────────────────────────────────────────
// LAYOUT GENERATION
// ─────────────────────────────────────────────────────────────────────

interface Lot {
  id: number;
  boundary: Polygon;
  centroid: Vec2;
  area: number;

  frontageSegmentId: number;
  frontageEdge: Vec2[];
  frontageWidth: number;

  zone: SettlementZone;
  buildingTypeId?: string;
}

interface SettlementLayout {
  center: Vec2;
  radius: number;
  paths: PathNetwork;
  lots: Lot[];
  keyLocations: Map<string, Vec2>;
}

function generateLayout(
  settlement: SettlementDefinition,
  template: SettlementTemplateConfig,
  terrainSampler: (pos: Vec2) => { height: number; slope: number },
  seed: number
): SettlementLayout;
```

### 9.4 SVG Renderer

```typescript
// web-tools/src/renderer/SVGRenderer.ts

interface RenderOptions {
  showRoads: boolean;
  showStreets: boolean;
  showLots: boolean;
  showBuildings: boolean;
  showZoneColors: boolean;
  showWalls: boolean;
  showFields: boolean;
  showLabels: boolean;

  roadColor: string;
  streetColor: string;
  lotStrokeColor: string;
  buildingColor: string;

  zoneColors: Record<SettlementZone, string>;
}

class SVGRenderer {
  constructor(private svg: SVGSVGElement) {}

  render(layout: SettlementLayout, options: RenderOptions): void {
    this.clear();

    if (options.showRoads) {
      this.renderPaths(layout.paths, 'MainRoad', options.roadColor, 8);
    }
    if (options.showStreets) {
      this.renderPaths(layout.paths, 'Street', options.streetColor, 4);
      this.renderPaths(layout.paths, 'Lane', options.streetColor, 2);
    }
    if (options.showLots) {
      this.renderLots(layout.lots, options);
    }
    if (options.showBuildings) {
      this.renderBuildingFootprints(layout.lots, options.buildingColor);
    }
  }

  private renderPaths(
    network: PathNetwork,
    type: PathType,
    color: string,
    width: number
  ): void {
    for (const segment of network.getSegmentsOfType(type)) {
      const path = document.createElementNS(SVG_NS, 'path');
      path.setAttribute('d', this.polylineToPath(segment.geometry));
      path.setAttribute('stroke', color);
      path.setAttribute('stroke-width', String(width));
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke-linecap', 'round');
      path.setAttribute('stroke-linejoin', 'round');
      this.svg.appendChild(path);
    }
  }

  private renderLots(lots: Lot[], options: RenderOptions): void {
    for (const lot of lots) {
      const polygon = document.createElementNS(SVG_NS, 'polygon');
      polygon.setAttribute('points', this.polygonToPoints(lot.boundary));

      if (options.showZoneColors) {
        polygon.setAttribute('fill', options.zoneColors[lot.zone]);
        polygon.setAttribute('fill-opacity', '0.3');
      } else {
        polygon.setAttribute('fill', 'none');
      }

      polygon.setAttribute('stroke', options.lotStrokeColor);
      polygon.setAttribute('stroke-width', '1');
      polygon.dataset.lotId = String(lot.id);
      polygon.dataset.zone = lot.zone;

      this.svg.appendChild(polygon);
    }
  }
}
```

### 9.5 Integration with C++ Build

The browser tool generates JSON that the C++ pipeline can consume:

```typescript
// Export format
interface LayoutExport {
  version: 1;
  seed: number;
  settlement: SettlementDefinition;
  template: string;  // Template ID

  // Generated data (can be imported by C++ to skip regeneration)
  paths: {
    nodes: PathNode[];
    segments: PathSegment[];
  };
  lots: Lot[];
  keyLocations: Record<string, Vec2>;
}

function exportLayout(layout: SettlementLayout): LayoutExport;
```

C++ can either:
1. **Regenerate from seed**: Use same algorithms, verify output matches
2. **Import layout**: Skip 2D generation, just do 3D mesh generation

```cpp
// tools/settlement_generator/LayoutImporter.h

class LayoutImporter {
public:
    // Import layout from browser tool JSON
    SettlementLayout importFromJSON(const std::filesystem::path& jsonPath);

    // Verify our C++ generation matches browser output
    bool verifyAgainstJSON(
        const SettlementLayout& generated,
        const std::filesystem::path& jsonPath,
        float tolerance = 0.01f
    );
};
```

### 9.6 C++ SVG Export (for debugging)

The C++ tools should also be able to export SVG for debugging:

```cpp
// tools/common/SVGExporter.h

class SVGExporter {
public:
    void begin(float width, float height);

    // Primitives
    void drawPolygon(
        const Polygon& poly,
        const std::string& fill = "none",
        const std::string& stroke = "black",
        float strokeWidth = 1.0f
    );

    void drawPolyline(
        const Polyline& line,
        const std::string& stroke = "black",
        float strokeWidth = 1.0f
    );

    void drawCircle(
        glm::vec2 center,
        float radius,
        const std::string& fill = "none",
        const std::string& stroke = "black"
    );

    void drawText(
        glm::vec2 position,
        const std::string& text,
        float fontSize = 12.0f
    );

    // High-level
    void drawPathNetwork(const PathNetwork& network, const RenderOptions& options);
    void drawLots(const std::vector<Lot>& lots, bool colorByZone);
    void drawSettlement(const SettlementLayout& layout, const RenderOptions& options);

    void save(const std::filesystem::path& path);

private:
    std::stringstream svg_;
    float width_, height_;
};
```

### 9.7 File Structure for Web Tools

```
web-tools/
├── package.json
├── tsconfig.json
├── vite.config.ts              # Or webpack/esbuild config
│
├── src/
│   ├── spatial/
│   │   ├── types.ts            # Vec2, Polygon, Polyline, AABB
│   │   ├── polygon.ts          # Polygon operations
│   │   ├── subdivision.ts      # BSP, grid, frontage subdivision
│   │   └── straightSkeleton.ts # Straight skeleton (simplified)
│   │
│   ├── paths/
│   │   ├── PathNetwork.ts
│   │   ├── spaceColonization.ts
│   │   └── pathSmoothing.ts
│   │
│   ├── layout/
│   │   ├── LayoutGenerator.ts
│   │   ├── blockIdentification.ts
│   │   ├── lotSubdivision.ts
│   │   └── zoneAssignment.ts
│   │
│   ├── renderer/
│   │   ├── SVGRenderer.ts
│   │   └── interactivity.ts    # Pan, zoom, selection
│   │
│   ├── ui/
│   │   ├── App.tsx             # Main React/Preact component
│   │   ├── ParameterPanel.tsx
│   │   ├── LayerControls.tsx
│   │   └── Inspector.tsx
│   │
│   └── export/
│       ├── jsonExport.ts
│       └── svgExport.ts
│
├── public/
│   └── index.html
│
└── tests/
    ├── subdivision.test.ts
    ├── spaceColonization.test.ts
    └── layout.test.ts
```

### 9.8 Development Workflow

```
┌─────────────────────────────────────────────────────────────────────┐
│                      DEVELOPMENT WORKFLOW                            │
└─────────────────────────────────────────────────────────────────────┘

1. DESIGN PHASE (Browser)
   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
   │  Tweak      │────▶│  Preview    │────▶│  Export     │
   │  Parameters │     │  SVG        │     │  JSON       │
   └─────────────┘     └─────────────┘     └─────────────┘
         │                                        │
         │ Fast iteration                         │ Lock in design
         ▼                                        ▼

2. IMPLEMENTATION PHASE (C++)
   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
   │  Import or  │────▶│  Generate   │────▶│  Verify     │
   │  Regenerate │     │  3D Meshes  │     │  In Engine  │
   └─────────────┘     └─────────────┘     └─────────────┘
         │
         │ Verify matches browser output
         ▼

3. TESTING PHASE
   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
   │  C++ SVG    │────▶│  Compare    │────▶│  Fix        │
   │  Export     │     │  Outputs    │     │  Diffs      │
   └─────────────┘     └─────────────┘     └─────────────┘
```

### 9.9 Algorithm Parity Testing

To ensure C++ and TypeScript implementations produce identical results:

```typescript
// web-tools/tests/parity.test.ts

describe('Algorithm Parity', () => {
  const testCases = loadTestCases('fixtures/layout-tests.json');

  for (const testCase of testCases) {
    it(`should match C++ output for ${testCase.name}`, () => {
      const tsResult = generateLayout(
        testCase.settlement,
        testCase.template,
        testCase.seed
      );

      // Compare against pre-generated C++ output
      const cppResult = loadCppOutput(`fixtures/cpp-output/${testCase.name}.json`);

      expect(tsResult.lots.length).toBe(cppResult.lots.length);

      for (let i = 0; i < tsResult.lots.length; i++) {
        expect(tsResult.lots[i].centroid.x).toBeCloseTo(cppResult.lots[i].centroid.x, 2);
        expect(tsResult.lots[i].centroid.y).toBeCloseTo(cppResult.lots[i].centroid.y, 2);
        expect(tsResult.lots[i].area).toBeCloseTo(cppResult.lots[i].area, 1);
      }
    });
  }
});
```

### 9.10 Deliverables - 2D Preview Layer

**TypeScript Library**:
- [ ] Spatial types (Vec2, Polygon, Polyline, AABB)
- [ ] Polygon operations (area, centroid, contains, bounds)
- [ ] Frontage subdivision algorithm
- [ ] BSP subdivision algorithm
- [ ] PathNetwork class
- [ ] Space colonization algorithm
- [ ] Layout generator

**Browser UI**:
- [ ] SVG renderer with pan/zoom
- [ ] Parameter panel with live update
- [ ] Layer visibility toggles
- [ ] Click-to-inspect functionality
- [ ] JSON export
- [ ] SVG export

**C++ Integration**:
- [ ] SVGExporter class
- [ ] LayoutImporter (JSON → SettlementLayout)
- [ ] Parity verification tests

**Testing**:
- Open browser tool at `http://localhost:5173`
- Adjust parameters, see layout update in real-time
- Export JSON, import into C++, verify mesh generation
- Compare C++ SVG export against browser preview

---
