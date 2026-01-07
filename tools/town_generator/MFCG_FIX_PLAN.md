# MFCG Faithfulness Fix Plan

## Priority 1: Bisector.cpp makeCut Algorithm (Critical)

The `makeCut` function is completely different from MFCG. This affects both alley generation and lot subdivision.

### MFCG Algorithm Analysis (lines 19513-19619)

The MFCG `makeCut` has two distinct paths:

#### Path A: Perpendicular Cut (lines 19559-19568)
When the cut line is nearly perpendicular to the long axis (`0.99 < D` where D is squared cross product ratio):
- Creates simple 2-point cut line `[f, m]`
- Checks area ratio against `2 * variance`
- Applies gap using `PolyCreate.stripe` + `PolyBool.and`

#### Path B: Angled Cut with minOffset (lines 19570-19609)
When cut is not perpendicular:
- Computes `minOffset / h` ratio
- Uses random offset: `0.5 < ratio ? 0.5 : ratio + (1 - 2*ratio) * normal3`
- Creates intermediate point `p` at offset distance
- Searches for third point `q` on a different edge using perpendicular ray
- Creates 3-point cut line `[f, p, q]`
- Calls `processCut` (which is `detectStraight` by default)
- Checks if middle points are inside polygon

### Implementation Steps

1. **Rewrite `makeCut` with two-path structure**
   - Add perpendicularity check: `D = (cross^2) / (|g|^2 * |p|^2)`
   - Implement Path A for perpendicular case
   - Implement Path B for angled case

2. **Fix `detectStraight` to use minTurnOffset**
   ```cpp
   if (minTurnOffset > 0 && pts.size() >= 3) {
       double area = std::abs(triangleArea(pts[0], pts[1], pts[2]));
       double dist = Point::distance(pts[0], pts[2]);
       if (area / dist < minTurnOffset) {
           return {pts[0], pts[2]};  // Simplify to 2 points
       }
   }
   return pts;
   ```

3. **Fix gap application to use stripe + boolean intersection**
   - Implement `PolyCreate::stripe(cutLine, gapWidth)` - creates a stripe polygon
   - Use `PolyBool::and(half, revert(stripe))` for gap application
   - Remove current `peel()` approach

4. **Fix split function to match MFCG index-based approach**
   - MFCG inserts cut points into vertex list at specific indices
   - Then slices by index ranges to create halves

---

## Priority 2: Block.cpp filterInner Order

### Current (Wrong)
```cpp
// In Block::createLots()
lots = TwistedBlock::createLots(this, params);
filterInner();  // Called after return
```

### MFCG (Correct)
```javascript
// In TwistedBlock.createLots
var d = c.partition();
d = a.filterInner(d);  // Called BEFORE valid-lot filter
// Then applies valid-lot filter
```

### Fix
Move `filterInner` call inside `TwistedBlock::createLots`:
```cpp
std::vector<geom::Polygon> TwistedBlock::createLots(Block* block, const AlleyParams& params) {
    // ... bisector partition ...
    auto partitioned = bisector.partition();

    // Filter inner lots FIRST (like MFCG)
    block->lots = partitioned;
    auto courtyard = block->filterInner();
    partitioned = block->lots;

    // THEN apply valid-lot filter
    // ...
}
```

---

## Priority 3: Move grid() to Cutter (Organizational)

### Current
`Building::grid()` - works but in wrong class

### Fix
1. Move `grid()` implementation to `Cutter.h`
2. Update `Building::create()` to call `Cutter::grid()`
3. Remove `Building::grid()`

---

## Testing Plan

After each fix:
1. Compile: `cmake --preset debug && cmake --build build/debug`
2. Run: `./run-debug.sh`
3. Compare output SVG visually against MFCG reference
4. Check for:
   - Alley gaps present and correct width (1.2)
   - Lot subdivision creating varied shapes
   - Buildings not overlapping alleys
   - Smooth corner transitions where appropriate

---

## Files to Modify

1. `tools/town_generator/src/building/Bisector.cpp` - Major rewrite of makeCut
2. `tools/town_generator/include/town_generator/building/Bisector.h` - Add helper methods
3. `tools/town_generator/src/building/Block.cpp` - Fix filterInner order
4. `tools/town_generator/include/town_generator/building/Cutter.h` - Add grid()
5. `tools/town_generator/src/building/Building.cpp` - Use Cutter::grid()
