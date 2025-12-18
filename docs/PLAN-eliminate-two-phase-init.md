# Plan: Eliminate Two-Phase Initialization & Adopt Smart Pointers

## Status: COMPLETE ✓

All systems have been converted to RAII factory pattern. The two-phase initialization pattern has been eliminated.

### Completed Conversions
All systems now use `static std::unique_ptr<T> create(...)` factory pattern:

**Terrain Systems:**
- TerrainSystem, TerrainPipelines, TerrainHeightMap, TerrainTileCache, TerrainMeshlet

**Virtual Texture Systems:**
- VirtualTextureFeedback, VirtualTexturePageTable, VirtualTextureTileLoader

**Water Systems:**
- OceanFFT, WaterSystem, WaterGBuffer, WaterDisplacement, FoamBuffer

**Subdivision Systems:**
- CatmullClarkSystem, CatmullClarkCBT, CatmullClarkMesh (struct with existing factory methods)

**Scene & Animation:**
- SceneBuilder, SceneManager, AnimatedCharacter

**Core:**
- Renderer, BillboardCapture

**Already Converted (earlier work):**
- WindSystem, PhysicsWorld, ShadowSystem, BloomSystem, SSRSystem, HiZSystem
- PostProcessSystem, SkySystem, AtmosphereLUTSystem, GrassSystem, RockSystem
- LeafSystem, WeatherSystem, SnowMaskSystem, FroxelSystem, DebugLineSystem
- GuiSystem, GpuProfiler, CpuProfiler, TreeEditSystem

---

## Goal
Systematically replace `init()`/`destroy()` two-phase initialization patterns with proper RAII semantics, and adopt smart pointers for clear ownership throughout the engine, following C++17+ best practices.

## Problems with Two-Phase Init

1. **Zombie state**: Objects exist but aren't usable until `init()` called
2. **Defensive guards**: Every method must check validity state
3. **Manual cleanup**: Must remember to call `destroy()`/`shutdown()`
4. **Ordering bugs**: Easy to call methods before init or after destroy
5. **Not exception-safe**: Partial init leaves objects in invalid state
6. **Verbose callsites**: `if (!foo.init(...)) return false;` everywhere

## Problems with Raw Pointer Ownership

1. **Ambiguous ownership**: Is it owned? Borrowed? Shared?
2. **Manual delete**: Easy to leak or double-free
3. **Dangling pointers**: No protection against use-after-free
4. **No self-documentation**: Code doesn't express intent

## Smart Pointer Guidelines (C++17+)

### Ownership Semantics

| Pointer Type | Ownership | Use Case |
|--------------|-----------|----------|
| `std::unique_ptr<T>` | Exclusive | Default for owned resources |
| `std::shared_ptr<T>` | Shared | Multiple owners, rare in game code |
| `std::weak_ptr<T>` | Non-owning observer of shared | Break cycles, optional access |
| `T*` (raw) | Non-owning | Function parameters, observers |
| `T&` (reference) | Non-owning, non-null | Function parameters, always valid |

### Rules

1. **Default to `unique_ptr`** for any owned heap allocation
2. **Use `shared_ptr` sparingly** - usually for caches or cross-system references
3. **Raw pointers are observers only** - never delete through them
4. **Prefer references over pointers** when null is not valid
5. **Use `make_unique`/`make_shared`** - never raw `new`
6. **Document observer semantics** with comments when using raw pointers

### Factory Pattern with Smart Pointers

```cpp
// For polymorphic types or when heap allocation is required
static std::unique_ptr<System> create(const CreateInfo& info);

// For value types (preferred when possible)
static std::optional<System> create(const CreateInfo& info);
```

## Target Pattern

### Factory Function with `std::optional`
```cpp
class System {
public:
    // Factory: returns nullopt on failure
    static std::optional<System> create(const CreateInfo& info);

    // Move-only (RAII handles are non-copyable)
    System(System&&) noexcept;
    System& operator=(System&&) noexcept;
    System(const System&) = delete;
    System& operator=(const System&) = delete;

    // Destructor handles cleanup
    ~System();

    // All methods assume valid state (no guards)
    void doWork();

private:
    System();  // Private: only factory can construct
};
```

### Usage
```cpp
// Before (two-phase)
System sys;
if (!sys.init(info)) return false;
// ... use sys ...
sys.destroy();

// After (RAII)
auto sys = System::create(info);
if (!sys) return false;
// ... use *sys ...
// Automatic cleanup on scope exit
```

## Existing RAII Patterns in Codebase

The codebase already has several RAII patterns to build on:

### 1. `RAIIAdapter<T>` ([src/core/RAIIAdapter.h](src/core/RAIIAdapter.h))
Wraps existing init/destroy classes without modifying them:
```cpp
auto pipelines = RAIIAdapter<TerrainPipelines>::create(
    [&](auto& p) { return p.init(info); },
    [device](auto& p) { p.destroy(device); }
);
```
**Use for**: Transitional wrapping of systems not yet migrated.

### 2. `ManagedBuffer`, `ManagedImage`, etc. ([src/core/VulkanRAII.h](src/core/VulkanRAII.h))
RAII wrappers for Vulkan resources with static factory functions:
```cpp
ManagedBuffer buffer;
if (!ManagedBuffer::create(allocator, bufferInfo, allocInfo, buffer)) {
    return false;
}
```
**Pattern**: Out-parameter factories (migrate to `std::optional` return).

### 3. `ScopeGuard` ([src/core/VulkanRAII.h](src/core/VulkanRAII.h))
Cleanup-on-failure guard for multi-step initialization:
```cpp
auto guard = makeScopeGuard([&]() { partialCleanup(); });
// ... init steps ...
guard.dismiss();  // Success path
```
**Use for**: Complex initialization sequences.

## Raw Pointer Inventory & Smart Pointer Adoption

### Current Raw `new`/`delete` Usage

| Location | Current Code | Proposed Change |
|----------|--------------|-----------------|
| PhysicsSystem.cpp:184 | `JPH::Factory::sInstance = new JPH::Factory()` | `shared_ptr` with ref-counting |
| PhysicsSystem.cpp:237 | `delete JPH::Factory::sInstance` | Handled by shared_ptr destructor |
| PhysicsSystem.cpp:460 | `new JPH::PhysicsMaterial()` | No change (Jolt RefConst manages) |
| PhysicsSystem.cpp:651 | `new JPH::CapsuleShape(...)` | No change (Jolt RefConst manages) |

### Raw Pointer Member Variables (Ownership)

Systems that store raw pointers as members representing ownership should be migrated:

| Class | Member | Current | Proposed |
|-------|--------|---------|----------|
| `Texture` | `VkImage image` | Raw Vulkan handle | Keep raw (Vulkan handles, not C++ pointers) |
| `Texture` | `VmaAllocation allocation` | Raw VMA handle | Keep raw (VMA handles) |
| `Renderer` | Various `Vk*` handles | Raw Vulkan handles | Keep raw (Vulkan API design) |

**Note**: Vulkan handles (VkImage, VkBuffer, etc.) are opaque handles, not C++ pointers. They follow Vulkan's explicit lifecycle model and are correctly managed by the existing `destroy()` methods or RAII wrappers in VulkanRAII.h.

### Raw Pointer Parameters (Non-owning)

These are correct usage - raw pointers as non-owning observers:

```cpp
// Correct: Non-owning parameter
void SceneManager::update(PhysicsWorld& physics);

// Correct: Optional non-owning parameter
void startCharacterJump(..., const PhysicsWorld* physics);

// Correct: Returning non-owning view into container
AnimationLayer* AnimationLayerController::getLayer(const std::string& name);
```

### Smart Pointer Migration Candidates

#### Already Using Smart Pointers (Good)

```cpp
// PhysicsSystem.h - Already correct
std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
std::unique_ptr<JPH::PhysicsSystem> physicsSystem;
std::unique_ptr<JPH::CharacterVirtual> character;

// AnimationLayerController.h - Already correct
std::vector<std::unique_ptr<AnimationLayer>> layers;

// ResizeCoordinator.h - Already correct
std::vector<std::unique_ptr<IResizable>> adapters_;
```

#### Candidates for Migration

| Class | Current | Proposed |
|-------|---------|----------|
| `Application` | `PhysicsWorld physics` (value) | `std::optional<PhysicsWorld>` (RAII factory) |
| `Renderer` | Various subsystem values | Consider `unique_ptr` for large systems |
| `TreeEditorGui` | `std::unique_ptr<BillboardCapture>` | Already correct |

### Texture RAII Pattern (Transitional)

Current pattern in Texture.cpp builds with RAII then releases to raw storage:

```cpp
// Current: Build with RAII, release to raw
ManagedImage managedImage;
ManagedImage::create(..., managedImage);
managedImage.releaseToRaw(image, allocation);  // Store as raw members
```

This is a transitional pattern. Full migration would:

```cpp
// Future: Keep RAII wrapper as member
class Texture {
    ManagedImage image_;      // RAII wrapper owns the resource
    ManagedImageView view_;
    ManagedSampler sampler_;
};
```

**Decision**: Low priority - current pattern works correctly, manual destroy() is called.

## Systems Inventory

### Tier 1: Core Systems (High Priority)
| System | File | Notes |
|--------|------|-------|
| `PhysicsWorld` | physics/PhysicsSystem.h | Raw new/delete for Jolt Factory |
| `VulkanContext` | core/VulkanContext.h | Foundation for all Vulkan resources |
| `Renderer` | core/Renderer.h | Main renderer, owns many subsystems |

### Tier 2: Graphics Subsystems
| System | File | Notes |
|--------|------|-------|
| `TerrainSystem` | terrain/TerrainSystem.h | Large system, many dependencies |
| `TerrainPipelines` | terrain/TerrainPipelines.h | Pipeline management |
| `ShadowSystem` | lighting/ShadowSystem.h | Shadow mapping |
| `BloomSystem` | postprocess/BloomSystem.h | Post-processing |
| `SSRSystem` | postprocess/SSRSystem.h | Screen-space reflections |
| `HiZSystem` | postprocess/HiZSystem.h | Hierarchical-Z |
| `PostProcessSystem` | postprocess/PostProcessSystem.h | Post-process chain |
| `SkySystem` | atmosphere/SkySystem.h | Atmospheric rendering |
| `AtmosphereLUTSystem` | atmosphere/AtmosphereLUTSystem.h | Atmosphere LUTs |

### Tier 3: Water Systems
| System | File | Notes |
|--------|------|-------|
| `WaterSystem` | water/WaterSystem.h | Main water system |
| `OceanFFT` | water/OceanFFT.h | FFT ocean simulation |
| `WaterGBuffer` | water/WaterGBuffer.h | Water G-buffer |
| `WaterDisplacement` | water/WaterDisplacement.h | Displacement maps |
| `FoamBuffer` | water/FoamBuffer.h | Foam rendering |

### Tier 4: Vegetation & Scene
| System | File | Notes |
|--------|------|-------|
| `GrassSystem` | vegetation/GrassSystem.h | Grass rendering |
| `RockSystem` | vegetation/RockSystem.h | Rock placement |
| `LeafSystem` | vegetation/LeafSystem.h | Leaf particles |
| `SceneManager` | scene/SceneManager.h | Scene graph |
| `SceneBuilder` | scene/SceneBuilder.h | Scene construction |

### Tier 5: Animation & Physics
| System | File | Notes |
|--------|------|-------|
| `AnimatedCharacter` | animation/AnimatedCharacter.h | Character animation |
| `SkinnedMeshRenderer` | animation/SkinnedMeshRenderer.h | Skinned mesh rendering |
| `ParticleSystem` | physics/ParticleSystem.h | GPU particles |
| `ClothSimulation` | physics/ClothSimulation.h | (If exists) |

### Tier 6: Debug & GUI
| System | File | Notes |
|--------|------|-------|
| `DebugLineSystem` | debug/DebugLineSystem.h | Debug visualization |
| `GpuProfiler` | debug/GpuProfiler.h | GPU timing |
| `GuiSystem` | gui/GuiSystem.h | ImGui integration |

## Implementation Strategy

### Phase 1: PhysicsWorld (First Example)

**Why start here:**
- Self-contained (minimal dependencies on other systems)
- Has problematic raw `new`/`delete` for Jolt Factory
- Simpler than Vulkan systems (no device/allocator threading)
- Clear ownership model

**Changes:**
1. Create `JoltRuntime` RAII wrapper for global Jolt state
2. Convert `PhysicsWorld` to factory pattern
3. Update `Application` to use `std::optional<PhysicsWorld>`
4. Remove `initialized` guards from all methods

### Phase 2: VulkanRAII Improvements

Before migrating Vulkan systems, improve the base RAII types:
1. Change `ManagedBuffer::create()` to return `std::optional<ManagedBuffer>`
2. Same for `ManagedImage`, `ManagedImageView`, etc.
3. Provide deduction guides for cleaner syntax

### Phase 3: Simple Vulkan Systems

Start with systems that have few dependencies:
1. `DebugLineSystem` - Simple, isolated
2. `GpuProfiler` - Standalone
3. `TerrainPipelines` - Pipeline only, no complex state

### Phase 4: Complex Vulkan Systems

Migrate systems with more dependencies:
1. `TerrainSystem` (owns TerrainPipelines, etc.)
2. `ShadowSystem`
3. `PostProcessSystem` family

### Phase 5: Top-Level Systems

Finally migrate the systems that own others:
1. `VulkanContext`
2. `Renderer`
3. `Application`

## Detailed Design: PhysicsWorld Migration

### Current API
```cpp
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    bool init();
    void shutdown();
    // ... methods with `if (!initialized) return;` guards
private:
    bool initialized = false;
};
```

### New API
```cpp
class PhysicsWorld {
public:
    static std::optional<PhysicsWorld> create();

    ~PhysicsWorld();

    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Public API unchanged
    void update(float deltaTime);
    PhysicsBodyID createTerrainHeightfield(...);
    // ...

private:
    PhysicsWorld();  // Private
    bool initInternal();

    // Jolt runtime (ref-counted for multiple worlds)
    std::shared_ptr<JoltRuntime> joltRuntime_;

    // Existing members (unchanged)
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    std::unique_ptr<JPH::CharacterVirtual> character_;
    // ...
};
```

### JoltRuntime RAII Helper
```cpp
// In PhysicsSystem.cpp (anonymous namespace)
namespace {
    struct JoltRuntime {
        JoltRuntime();   // RegisterDefaultAllocator, Factory, RegisterTypes
        ~JoltRuntime();  // UnregisterTypes, delete Factory
    };

    std::weak_ptr<JoltRuntime> g_joltRuntime;
    std::mutex g_joltMutex;
}
```

### Application Changes
```cpp
// Application.h
class Application {
    std::optional<PhysicsWorld> physics_;
    PhysicsWorld& physics() { return *physics_; }
};

// Application.cpp
bool Application::init(...) {
    physics_ = PhysicsWorld::create();
    if (!physics_) {
        SDL_Log("Failed to initialize physics system");
        return false;
    }
    physics_->createTerrainHeightfield(...);
}

void Application::shutdown() {
    physics_.reset();  // Or let destructor handle it
}
```

## Migration Checklist Template

For each system:

### Two-Phase Init Removal

- [ ] Create factory function `static std::optional<T> create(...)`
- [ ] Make constructor private
- [ ] Add move constructor and assignment
- [ ] Delete copy constructor and assignment
- [ ] Move init logic to private `initInternal()` called by factory
- [ ] Move destroy logic to destructor
- [ ] Remove `bool initialized` or equivalent state flag
- [ ] Remove all `if (!initialized)` guards from methods
- [ ] Update all callsites to use factory and optional

### Smart Pointer Adoption

- [ ] Replace raw `new` with `std::make_unique` or `std::make_shared`
- [ ] Replace raw `delete` with smart pointer reset/destruction
- [ ] Convert owned raw pointer members to `unique_ptr`
- [ ] Document any remaining raw pointers as non-owning observers
- [ ] Verify no raw pointer parameters take ownership

### Verification

- [ ] Build compiles without warnings
- [ ] Run application and verify functionality
- [ ] Check for memory leaks (if tooling available)
- [ ] Verify proper cleanup on shutdown

## Files Changed per System

### PhysicsWorld
- `src/physics/PhysicsSystem.h` - New API
- `src/physics/PhysicsSystem.cpp` - Implementation + JoltRuntime
- `src/scene/Application.h` - `std::optional<PhysicsWorld>`
- `src/scene/Application.cpp` - Factory usage

### References (no changes needed)
Functions taking `PhysicsWorld&` or `const PhysicsWorld*` continue to work:
- `src/scene/SceneManager.cpp`
- `src/scene/SceneBuilder.cpp`
- `src/core/Renderer.cpp`
- `src/animation/AnimatedCharacter.cpp`
- `src/animation/AnimationStateMachine.cpp`

## Testing

After each system migration:
1. Build: `cmake --preset debug && cmake --build build/debug`
2. Run: `./run-debug.sh`
3. Verify system works (visual inspection, interaction)
4. Check for crashes on startup and shutdown
5. Test multiple app restarts if applicable

## Considerations

### Thread Safety
- Jolt Factory singleton needs mutex protection
- VulkanContext device/allocator shared across systems
- Consider thread-safe initialization for systems used from multiple threads

### Error Handling
- Factory returns `std::nullopt` on failure
- Callers must check and handle
- Errors logged at point of failure

### Performance
- `std::optional` has negligible overhead
- Move semantics prevent copies
- No runtime validity checks in methods

### Backward Compatibility
- During migration, can keep deprecated `init()`/`destroy()` that delegate to new pattern
- Remove deprecated API once all callsites updated
