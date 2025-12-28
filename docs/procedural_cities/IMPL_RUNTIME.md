# Implementation - Runtime Layer

[← Back to Index](README.md)

---

## 4. Runtime Layer

Systems that run in the game engine.

### 4.1 Streaming System

Loads settlement data on demand.

```cpp
// src/settlements/SettlementStreaming.h

class SettlementStreamingSystem {
public:
    SettlementStreamingSystem(
        vk::Device device,
        const std::filesystem::path& settlementDataPath
    );

    // Called each frame with camera position
    void update(const glm::vec3& cameraPosition, float dt);

    // Get visible settlements for rendering
    std::vector<const LoadedSettlement*> getVisibleSettlements() const;

private:
    struct SettlementSlot {
        std::string id;
        enum State { Unloaded, Loading, Loaded, Unloading } state;
        float distanceToCamera;
        std::unique_ptr<LoadedSettlement> data;
    };

    std::vector<SettlementSlot> slots_;
    std::priority_queue<LoadRequest> loadQueue_;

    void prioritizeLoading(const glm::vec3& cameraPosition);
    void processLoadQueue();
    void unloadDistant(const glm::vec3& cameraPosition, float maxDistance);
};
```

---

### 4.2 LOD System

Manages level-of-detail transitions.

```cpp
// src/settlements/SettlementLOD.h

class SettlementLODSystem {
public:
    struct LODLevel {
        float maxDistance;
        Mesh* mesh;
        bool useImpostor;
    };

    void update(
        const glm::vec3& cameraPosition,
        const std::vector<LoadedSettlement*>& settlements
    );

    // Get renderable instances for this frame
    std::vector<RenderInstance> getRenderInstances() const;

private:
    // Select LOD level based on distance and screen size
    int selectLODLevel(
        const glm::vec3& objectPosition,
        float objectRadius,
        const glm::vec3& cameraPosition
    );

    // Impostor atlas for distant buildings
    ImpostorAtlas impostorAtlas_;
};
```

---
