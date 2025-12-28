# Procedural Cities - Reference & Appendices

[← Back to Index](README.md)

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

### D.7 Defensive Structures

#### Town Wall
- **Era**: 12th-14th century
- **Height**: 4-6m typically (up to 10m for major ports)
- **Thickness**: 2-3m
- **Construction**:
  - Wealthy towns: Ashlar stone facing, rubble core
  - Regional: Flint with stone dressings (south coast typical)
  - Early/temporary: Earth bank with timber palisade
- **Features**: Wall walk (1.5m wide), merlons/crenellations, arrow loops
- **Towers**: Interval towers every 30-50m
- **Examples**: Southampton, Rye, Winchelsea

#### Interval Tower
- **Era**: With town walls
- **Size**: 5-8m across, projecting 3-4m from wall
- **Height**: 2-3m above wall walk
- **Shape**: D-shaped (most common), rectangular, round
- **Construction**: Same as wall
- **Features**: 2-3 floors, arrow loops on each level, roof platform

#### Corner Tower
- **Era**: With town walls
- **Size**: Larger than interval towers (7-10m)
- **Shape**: Round (most defensible), octagonal
- **Features**: Command wider field of fire, often stronger construction

#### Town Gate
- **Era**: With town wall
- **Size**: Passage 3-5m wide, 4-5m high
- **Construction**: Stone with timber gates
- **Arch type**: Round (Norman) or pointed (Gothic)
- **Features**:
  - Flanking towers
  - Portcullis groove
  - Murder holes in passage ceiling
  - Guard chambers in towers
  - Room for customs collection
- **Examples**: Bargate (Southampton), Landgate (Rye)

#### Barbican
- **Era**: 13th century onwards
- **Description**: Extended outer gateway defense
- **Size**: 10-20m long passage
- **Features**: Outer gate, inner gate, overlooked by walls on both sides
- **Purpose**: Trap attackers between two gates

#### Water Gate
- **Era**: With harbor walls
- **Description**: Gate providing direct access to harbor
- **Features**: Lower arch for tide levels, boom attachment points

#### Castle Types

##### Motte and Bailey
- **Era**: 1066-1150 (Norman Conquest period)
- **Description**: Earth mound (motte) with timber/stone tower, enclosed yard (bailey)
- **Motte Size**: 5-10m high, 30-50m base diameter
- **Bailey**: 0.5-2 acres enclosed
- **Construction**: Initially timber, often rebuilt in stone
- **Features**: Steep sides, ditch around base, timber palisade

##### Rectangular Keep (Tower Keep)
- **Era**: 1080-1200 (Norman)
- **Size**: 15-30m square, 20-30m tall
- **Walls**: 3-4m thick at base
- **Construction**: Massive stone
- **Floors**: 3-4 (basement, great hall, private chambers, battlements)
- **Features**:
  - Corner pilaster buttresses
  - Forebuilding protecting entrance (raised first floor)
  - Spiral stairs in corners
  - Chapel
- **Examples**: Tower of London, Rochester, Dover

##### Shell Keep
- **Era**: 12th century
- **Description**: Stone wall replacing timber palisade on motte top
- **Size**: 15-25m diameter
- **Construction**: Stone wall 2-3m thick following motte edge
- **Features**: Buildings against inside of wall, open courtyard

##### Round Keep
- **Era**: Late 12th-13th century
- **Description**: Cylindrical tower without corners (harder to mine)
- **Size**: 10-15m diameter, 20-25m tall
- **Construction**: Stone
- **Examples**: Conisbrough, Pembroke

##### Concentric Castle
- **Era**: Late 13th century (Edwardian)
- **Description**: Multiple rings of walls, each higher than the last
- **Features**: Inner and outer bailey, flanking towers, multiple gates
- **Examples**: Caernarfon, Beaumaris, Harlech

#### Coastal Defensive Structures

##### Coastal Castle
- **Era**: Throughout medieval period
- **Location**: Overlooking harbor or coastal approach
- **Features**:
  - Commands harbor entrance
  - Often on headland or cliff
  - May integrate with town walls
- **Examples**: Pevensey, Hastings, Arundel

##### Harbor Tower
- **Era**: 13th century onwards
- **Size**: 8-12m diameter, 15-20m tall
- **Location**: On harbor wall or mole
- **Features**: Guards harbor entrance, chain boom attachment

##### Chain Boom
- **Era**: 13th century onwards
- **Description**: Heavy chain stretched across harbor entrance
- **Components**: Two anchor towers with windlass mechanism
- **Features**: Raised/lowered to permit friendly shipping

##### Watchtower
- **Era**: Throughout medieval period
- **Size**: 4-6m square, 10-15m tall
- **Location**: High ground with sea views
- **Features**: Beacon platform at top, basic accommodation

##### Beacon
- **Era**: Throughout medieval period
- **Description**: Warning fire platform on high point
- **Construction**: Stone platform with iron fire basket
- **Purpose**: Part of coastal warning network

#### Defensive Earthworks

##### Town Ditch
- **Era**: With town walls
- **Size**: 6-10m wide, 3-5m deep
- **Profile**: V-shaped or flat bottom
- **Types**: Dry ditch, wet moat, tidal (coastal)
- **Features**: Often with bank on inside

##### Castle Moat
- **Era**: Throughout medieval period
- **Size**: 10-20m wide, 3-6m deep
- **Construction**: Dug from bedrock or clay-lined
- **Water source**: Diverted stream, spring, or tidal

##### Earthwork Bank
- **Era**: Throughout medieval period
- **Description**: Earth rampart behind ditch
- **Height**: 2-4m
- **Features**: May have palisade on top (early) or stone wall (later)

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
