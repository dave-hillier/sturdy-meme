#!/bin/bash
# Full terrain preprocessing pipeline
# Runs all preprocessing tools in the correct order with progress reporting
# Uses parallel processing for significant speedup on multi-core systems

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/claude"
HEIGHTMAP="${SCRIPT_DIR}/assets/terrain/isleofwight-0m-200m.png"
OUTPUT_DIR="${SCRIPT_DIR}/generated/terrain_data"

# Default parameters
MIN_ALTITUDE=-15
MAX_ALTITUDE=200
METERS_PER_PIXEL=1.0
TILE_RESOLUTION=128
NUM_LODS=6

# Parse command line arguments
SKIP_BUILD=false
CLEAN=false
VERBOSE=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Full terrain preprocessing pipeline with parallel processing"
    echo ""
    echo "Options:"
    echo "  --heightmap PATH     Path to 16-bit heightmap PNG (default: assets/terrain/isleofwight-0m-200m.png)"
    echo "  --output DIR         Output directory (default: generated/terrain_data)"
    echo "  --build-dir DIR      Build directory (default: build/claude)"
    echo "  --min-alt VALUE      Minimum altitude in meters (default: -15)"
    echo "  --max-alt VALUE      Maximum altitude in meters (default: 200)"
    echo "  --mpp VALUE          Meters per pixel (default: 1.0)"
    echo "  --tile-res VALUE     Tile resolution (default: 128)"
    echo "  --lods VALUE         Number of LOD levels (default: 6)"
    echo "  --skip-build         Skip building tools (assume already built)"
    echo "  --clean              Clean output directory before processing"
    echo "  --verbose            Show verbose output from tools"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "The pipeline runs these stages in order:"
    echo "  1. terrain_preprocess - Generate terrain tiles with LOD levels"
    echo "  2. watershed          - D8 flow direction and river extraction (parallel with 1)"
    echo "  3. biome_preprocess   - Biome classification and settlements"
    echo "  4. settlement_generator - Optional: regenerate settlements"
    echo "  5. road_generator     - Generate road network between settlements"
    echo "  6. vegetation_generator - Place trees, rocks, and vegetation"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Run full pipeline with defaults"
    echo "  $0 --heightmap my_terrain.png         # Use custom heightmap"
    echo "  $0 --skip-build --verbose             # Skip build, show detailed output"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --heightmap)
            HEIGHTMAP="$2"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --min-alt)
            MIN_ALTITUDE="$2"
            shift 2
            ;;
        --max-alt)
            MAX_ALTITUDE="$2"
            shift 2
            ;;
        --mpp)
            METERS_PER_PIXEL="$2"
            shift 2
            ;;
        --tile-res)
            TILE_RESOLUTION="$2"
            shift 2
            ;;
        --lods)
            NUM_LODS="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_usage
            exit 1
            ;;
    esac
done

# Validate heightmap exists
if [[ ! -f "$HEIGHTMAP" ]]; then
    echo -e "${RED}Error: Heightmap not found: $HEIGHTMAP${NC}"
    exit 1
fi

# Get heightmap dimensions
HEIGHTMAP_INFO=$(file "$HEIGHTMAP")
echo -e "${CYAN}Heightmap: $HEIGHTMAP${NC}"
echo -e "${CYAN}$HEIGHTMAP_INFO${NC}"

# Create output directories
mkdir -p "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/watershed"
mkdir -p "$OUTPUT_DIR/biome"

# Track timing
START_TIME=$(date +%s)

stage_header() {
    local stage=$1
    local desc=$2
    echo ""
    echo -e "${BLUE}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  Stage $stage: $desc${NC}"
    echo -e "${BLUE}════════════════════════════════════════════════════════════════${NC}"
}

run_tool() {
    local tool=$1
    shift
    local tool_path="$BUILD_DIR/$tool"

    if [[ ! -x "$tool_path" ]]; then
        echo -e "${RED}Error: Tool not found or not executable: $tool_path${NC}"
        echo -e "${YELLOW}Run with --skip-build=false to build tools first${NC}"
        exit 1
    fi

    echo -e "${GREEN}Running: $tool $@${NC}"
    local start=$(date +%s)

    if $VERBOSE; then
        "$tool_path" "$@"
    else
        "$tool_path" "$@" 2>&1 | grep -E "(Progress|Generated|Loaded|threads|LOD|tiles|Complete|Success|Error)" || true
    fi

    local end=$(date +%s)
    local elapsed=$((end - start))
    echo -e "${GREEN}✓ Completed in ${elapsed}s${NC}"
}

# Clean if requested
if $CLEAN; then
    echo -e "${YELLOW}Cleaning output directory: $OUTPUT_DIR${NC}"
    rm -rf "$OUTPUT_DIR"/*
fi

# Build tools if needed
if ! $SKIP_BUILD; then
    stage_header "0" "Building preprocessing tools"

    if [[ -f "$SCRIPT_DIR/build-claude.sh" ]]; then
        echo -e "${GREEN}Building with build-claude.sh...${NC}"
        "$SCRIPT_DIR/build-claude.sh" --shaders 2>&1 | tail -20
    else
        echo -e "${GREEN}Building with cmake...${NC}"
        cmake --preset claude && cmake --build "$BUILD_DIR" --target terrain_preprocess watershed biome_preprocess road_generator vegetation_generator -j
    fi
fi

# Stage 1 & 2: Terrain tiles and watershed (can run in parallel)
stage_header "1+2" "Terrain tiles + Watershed analysis (parallel)"

# Run terrain_preprocess and watershed in parallel
echo -e "${CYAN}Starting parallel processing...${NC}"

(
    run_tool terrain_preprocess \
        "$HEIGHTMAP" \
        "$OUTPUT_DIR" \
        --min-altitude "$MIN_ALTITUDE" \
        --max-altitude "$MAX_ALTITUDE" \
        --meters-per-pixel "$METERS_PER_PIXEL" \
        --tile-resolution "$TILE_RESOLUTION" \
        --lod-levels "$NUM_LODS"
) &
TERRAIN_PID=$!

(
    run_tool watershed \
        --heightmap "$HEIGHTMAP" \
        --output "$OUTPUT_DIR/watershed" \
        --min-altitude "$MIN_ALTITUDE" \
        --max-altitude "$MAX_ALTITUDE" \
        --flow-threshold 0.001
) &
WATERSHED_PID=$!

# Wait for both to complete
echo -e "${CYAN}Waiting for terrain tiles (PID $TERRAIN_PID) and watershed (PID $WATERSHED_PID)...${NC}"
wait $TERRAIN_PID
TERRAIN_STATUS=$?
wait $WATERSHED_PID
WATERSHED_STATUS=$?

if [[ $TERRAIN_STATUS -ne 0 ]]; then
    echo -e "${RED}Error: terrain_preprocess failed${NC}"
    exit 1
fi

if [[ $WATERSHED_STATUS -ne 0 ]]; then
    echo -e "${RED}Error: watershed failed${NC}"
    exit 1
fi

# Stage 3: Biome preprocessing (depends on watershed)
stage_header "3" "Biome classification and settlements"

run_tool biome_preprocess \
    "$HEIGHTMAP" \
    "$OUTPUT_DIR/watershed" \
    "$OUTPUT_DIR/biome" \
    --min-altitude "$MIN_ALTITUDE" \
    --max-altitude "$MAX_ALTITUDE"

# Stage 4: Road generation (depends on settlements)
stage_header "4" "Road network generation"

SETTLEMENTS_JSON="$OUTPUT_DIR/biome/settlements.json"
if [[ -f "$SETTLEMENTS_JSON" ]]; then
    run_tool road_generator \
        --heightmap "$HEIGHTMAP" \
        --settlements "$SETTLEMENTS_JSON" \
        --output "$OUTPUT_DIR/roads" \
        --biome "$OUTPUT_DIR/biome/biome_map.png" \
        --min-altitude "$MIN_ALTITUDE" \
        --max-altitude "$MAX_ALTITUDE"
else
    echo -e "${YELLOW}Warning: No settlements found, skipping road generation${NC}"
fi

# Stage 5: Vegetation placement (depends on biome)
stage_header "5" "Vegetation placement"

BIOME_MAP="$OUTPUT_DIR/biome/biome_map.png"
if [[ -f "$BIOME_MAP" ]]; then
    run_tool vegetation_generator \
        --heightmap "$HEIGHTMAP" \
        --biome "$BIOME_MAP" \
        --output "$OUTPUT_DIR/vegetation" \
        --min-altitude "$MIN_ALTITUDE" \
        --max-altitude "$MAX_ALTITUDE"
else
    echo -e "${YELLOW}Warning: No biome map found, skipping vegetation${NC}"
fi

# Summary
END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))

echo ""
echo -e "${BLUE}════════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Preprocessing complete!${NC}"
echo -e "${BLUE}════════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "${CYAN}Output directory: $OUTPUT_DIR${NC}"
echo ""
echo "Generated data:"
ls -lh "$OUTPUT_DIR"/ 2>/dev/null || true
echo ""
echo "Terrain tiles:"
ls "$OUTPUT_DIR"/*.meta 2>/dev/null && cat "$OUTPUT_DIR"/*.meta 2>/dev/null | head -20 || echo "  (none found)"
echo ""
echo "Watershed data:"
ls -lh "$OUTPUT_DIR/watershed"/ 2>/dev/null || echo "  (none found)"
echo ""
echo "Biome data:"
ls -lh "$OUTPUT_DIR/biome"/ 2>/dev/null || echo "  (none found)"
echo ""
echo -e "${GREEN}Total time: ${TOTAL_TIME}s${NC}"
