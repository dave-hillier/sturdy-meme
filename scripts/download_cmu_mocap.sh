#!/usr/bin/env bash
# Download CMU Motion Capture Database in BVH format.
#
# Uses the GitHub mirror (una-dinosauria/cmu-mocap) which provides
# all 2,548 CMU mocap sequences pre-converted to BVH format.
#
# Usage:
#   ./scripts/download_cmu_mocap.sh                  # Download all subjects
#   ./scripts/download_cmu_mocap.sh --subjects 1,2,5 # Download specific subjects
#   ./scripts/download_cmu_mocap.sh --list            # List available subjects
#   ./scripts/download_cmu_mocap.sh --output data/mocap/cmu  # Custom output dir
#
# The BVH files can then be converted to training format using:
#   python tools/convert_mocap_to_training.py data/mocap/cmu/ data/calm/motions/

set -euo pipefail

REPO_URL="https://github.com/una-dinosauria/cmu-mocap.git"
DEFAULT_OUTPUT="data/mocap/cmu"
SUBJECTS=""
LIST_ONLY=false

usage() {
    cat <<'USAGE'
Usage: download_cmu_mocap.sh [OPTIONS]

Download CMU Motion Capture Database (BVH format) from GitHub mirror.

Options:
  --output DIR         Output directory (default: data/mocap/cmu)
  --subjects LIST      Comma-separated subject numbers (e.g. 1,2,5,35)
  --list               List available subject categories and exit
  --help               Show this help

Subject categories (partial list):
   1-6    Human interaction, walking variations
   7-12   Walking, running, various locomotion
  13-15   Exercising, stretching, sports
  16-23   Human interaction, playground activities
  35-45   Walking, running, locomotion styles
  49-56   Jumping, acrobatics
  60-69   Walking with objects, daily activities
  85-94   Dance, expressive movement
 105-115  Walking, running, turning
 131-144  Martial arts, combat, action

USAGE
}

log() {
    echo "[download_cmu_mocap] $*"
}

# Parse arguments
OUTPUT="$DEFAULT_OUTPUT"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            OUTPUT="$2"
            shift 2
            ;;
        --subjects)
            SUBJECTS="$2"
            shift 2
            ;;
        --list)
            LIST_ONLY=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if $LIST_ONLY; then
    cat <<'LIST'
CMU Motion Capture Subject Categories:
=======================================
  01     - Walk, turn, climb stairs
  02     - Walk, sway, stretch, kick
  05     - Walk, run, jump, cartwheel
  07     - Walk (various speeds and styles)
  08     - Walk, run (various speeds)
  09     - Run, walk, dance
  10     - Walk, run, sit
  12     - Walk, run, jump
  13     - Exercise, stretch
  14     - Dance (ballet, modern)
  15     - Sports (baseball, basketball)
  16     - Walk, run, jump, sit, stand
  17     - Walk, stretch, reach
  18     - Walk, run, balance
  35     - Walk (many variations)
  36     - Walk, run (many variations)
  38     - Walk, run, dance
  39     - Walk, run (many variations)
  40     - Walk (various styles)
  45     - Walk, run
  49     - Jump, leap
  55     - Walk, grab, push
  56     - Walk, pick up, carry
  60     - Walk, sit, stand
  69     - Walk, jog, run
  75     - Walk, dance, swing
  85     - Walk, run, jump (athletic)
  86     - Dance, jump, spin
  87     - Dance (various styles)
  88     - Dance (various styles)
  89     - Dance, jump
  90     - Dance (modern)
  91     - Dance (jazz, modern)
  93     - Punch, kick, martial arts
  94     - Fighting, martial arts
 105     - Walk, run
 106     - Walk, run, jump
 111     - Walk, run
 118     - Walk, dance
 131     - Walk, run (variations)
 132     - Walk, run (athletic)
 135     - Walk, run, dance
 138     - Walk, run
 141     - Walk, run (various)
 143     - Walk, run, jump
 144     - Walk, run, acrobatics

See http://mocap.cs.cmu.edu/motcat.php for the full catalog.
LIST
    exit 0
fi

# Create output directory
mkdir -p "$OUTPUT"

# Check for git
if ! command -v git &>/dev/null; then
    echo "Error: git is required but not found."
    exit 1
fi

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

# Clone the repository (shallow clone to save bandwidth)
log "Cloning CMU mocap BVH repository (shallow)..."
git clone --depth 1 "$REPO_URL" "$TEMP_DIR/cmu-mocap"

# Count available files
TOTAL_BVH=$(find "$TEMP_DIR/cmu-mocap" -name '*.bvh' | wc -l)
log "Repository contains $TOTAL_BVH BVH files"

if [[ -n "$SUBJECTS" ]]; then
    # Copy only requested subjects
    IFS=',' read -ra SUBJECT_LIST <<< "$SUBJECTS"
    COPIED=0
    for subj in "${SUBJECT_LIST[@]}"; do
        # Subject directories are zero-padded to 2 digits in the repo
        subj_padded=$(printf "%02d" "$subj")
        src_dir="$TEMP_DIR/cmu-mocap/$subj_padded"
        if [[ -d "$src_dir" ]]; then
            mkdir -p "$OUTPUT/$subj_padded"
            count=$(find "$src_dir" -name '*.bvh' | wc -l)
            log "  Subject $subj_padded: $count files"
            cp "$src_dir"/*.bvh "$OUTPUT/$subj_padded/" 2>/dev/null || true
            COPIED=$((COPIED + count))
        else
            log "  Subject $subj_padded: not found (skipping)"
        fi
    done
    log "Copied $COPIED BVH files to $OUTPUT"
else
    # Copy all subjects
    log "Copying all BVH files to $OUTPUT..."
    find "$TEMP_DIR/cmu-mocap" -mindepth 1 -maxdepth 1 -type d | while read -r dir; do
        dirname=$(basename "$dir")
        # Skip non-subject directories
        if [[ "$dirname" =~ ^[0-9]+$ ]]; then
            mkdir -p "$OUTPUT/$dirname"
            cp "$dir"/*.bvh "$OUTPUT/$dirname/" 2>/dev/null || true
        fi
    done
    FINAL_COUNT=$(find "$OUTPUT" -name '*.bvh' | wc -l)
    log "Copied $FINAL_COUNT BVH files to $OUTPUT"
fi

log "Done. BVH files are in: $OUTPUT"
log ""
log "Next steps:"
log "  1. Convert to training format:"
log "     python tools/convert_mocap_to_training.py $OUTPUT data/calm/motions/"
log "  2. Encode into latent library:"
log "     python tools/calm_encode_library.py --motions data/calm/motions/ --output data/calm/models/latent_library.json"
