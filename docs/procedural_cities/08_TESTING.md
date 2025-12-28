# Procedural Cities - Testing & QA

[← Back to Index](README.md)

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
