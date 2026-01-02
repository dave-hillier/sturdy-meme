# Medieval Fantasy City Generator

A C++ port of [watabou's TownGeneratorOS](https://github.com/watabou/TownGeneratorOS) for procedural medieval city layout generation.

## Features

- **Voronoi tessellation** for district (ward) patches
- **12 ward types**: castle, cathedral, market, patriciate, craftsmen, merchants, administration, military, slum, farm, park, gate
- **City walls** with gates and towers
- **Optional citadel** (inner fortress)
- **Street network** connecting gates to center via A* pathfinding
- **Building footprints** via recursive polygon subdivision
- **Rivers** flowing through cities with automatic bridge placement
- **Coastal cities** with piers
- **Ponds and fountains**
- **Trees** in parks and farms

## Usage

```bash
city_generator <output_dir> [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--seed <value>` | Random seed (0 = random) | 0 |
| `--radius <value>` | City radius in units | 100.0 |
| `--patches <value>` | Number of ward patches | 30 |
| `--no-walls` | Disable city walls | walls enabled |
| `--citadel` | Add inner citadel | disabled |
| `--no-plaza` | Disable central plaza | plaza enabled |
| `--no-temple` | Disable cathedral/temple | temple enabled |
| `--no-castle` | Disable castle | castle enabled |
| `--river` | Add river flowing through city | disabled |
| `--coastal` | Make city coastal with piers | disabled |
| `--coast-dir <degrees>` | Direction to coast (0=east, 90=north) | 0 |
| `--river-width <value>` | River width | 5.0 |
| `--piers <value>` | Number of piers for coastal cities | 3 |
| `--tree-density <value>` | Tree density multiplier | 1.0 |
| `--svg-width <value>` | SVG output width | 1024 |
| `--svg-height <value>` | SVG output height | 1024 |

## Examples

### Basic city with default settings
```bash
city_generator ./output --seed 42
```

### Large city with many districts
```bash
city_generator ./output --seed 123 --patches 50 --radius 150
```

### Fortified city with citadel
```bash
city_generator ./output --seed 456 --citadel --patches 40
```

### River city with bridges
```bash
city_generator ./output --seed 789 --river --patches 35
```

### Coastal port city
```bash
city_generator ./output --seed 101 --coastal --coast-dir 90 --piers 5
```

### River city on the coast
```bash
city_generator ./output --seed 202 --river --coastal --coast-dir 45
```

### Unwalled village
```bash
city_generator ./output --seed 303 --no-walls --patches 15 --radius 60
```

### Dense urban center
```bash
city_generator ./output --seed 404 --patches 60 --tree-density 0.5
```

## Output Files

### city.geojson

GeoJSON FeatureCollection with the following layers:

| Layer | Type | Description |
|-------|------|-------------|
| `boundary` | Polygon | City border |
| `wards` | Polygon | Ward boundaries with type, label, color properties |
| `buildings` | Polygon | Building footprints |
| `walls` | Polygon | City wall perimeter |
| `towers` | Point | Wall towers |
| `gates` | Point | City gates |
| `streets` | LineString | Streets and roads |
| `plaza` | Polygon | Central plaza |
| `trees` | Point | Trees |
| `water` | Polygon | Rivers, coast, ponds |
| `bridges` | Polygon | Bridges over water |
| `piers` | Polygon | Piers extending into water |

### city.svg

SVG preview image with proper styling and colors for all features.

## Ward Types

| Ward | Description | Location Preference |
|------|-------------|---------------------|
| Castle | Central fortification | Central, large area |
| Cathedral | Major church/temple | Central, large area |
| Market | Central marketplace | Near plaza |
| Patriciate | Wealthy residential | Central, away from slums |
| Craftsmen | Artisan workshops | Middle to outer |
| Merchants | Shops and commerce | Near market |
| Administration | Government buildings | Central |
| Military | Barracks | Near gates/walls |
| Slum | Poor housing | Outer areas |
| Farm | Agricultural | Outside walls |
| Park | Green space | Anywhere (rare) |
| Gate | City entrance | At wall gates |

## Algorithm Overview

1. **Seed Point Generation**: Fermat's spiral with jitter for even distribution
2. **Voronoi Tessellation**: Incremental Delaunay triangulation
3. **Lloyd Relaxation**: 3 iterations for better patch shapes
4. **Water Generation**: Rivers, coast, ponds with bridge detection
5. **Wall Construction**: Identify inner patches, build wall perimeter
6. **Gate Placement**: At wall vertices bordering multiple districts
7. **Street Network**: A* pathfinding from gates to city center
8. **Ward Assignment**: Greedy assignment based on location ratings
9. **Building Generation**: Recursive polygon subdivision per ward type

## Integration

The GeoJSON output can be used with:
- GIS tools (QGIS, Mapbox, etc.)
- Game engines (Unity, Unreal, Godot)
- Web mapping libraries (Leaflet, OpenLayers)
- The project's tile_generator for virtual texturing
