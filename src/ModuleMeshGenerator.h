#pragma once

#include "BuildingModules.h"
#include "Mesh.h"
#include <vector>

// Generates meshes for individual building modules
class ModuleMeshGenerator {
public:
    // Module size in world units
    static constexpr float MODULE_SIZE = 2.0f;

    // Generate mesh for a specific module type
    void generateModuleMesh(ModuleType type, std::vector<Vertex>& outVertices,
                           std::vector<uint32_t>& outIndices) const;

    // Generate mesh from WFC result (assembled building)
    void generateBuildingMesh(const BuildingWFC& wfc, const ModuleLibrary& library,
                             const glm::vec3& worldOffset,
                             std::vector<Vertex>& outVertices,
                             std::vector<uint32_t>& outIndices) const;

private:
    // Helper to add a quad
    void addQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                 const glm::vec3& normal, const glm::vec2& uvScale,
                 std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    // Helper to add a triangle
    void addTriangle(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                    const glm::vec3& normal, std::vector<Vertex>& verts,
                    std::vector<uint32_t>& inds) const;

    // Module-specific mesh generators
    void generateFoundationWall(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateFoundationCorner(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateFoundationDoor(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    void generateWallPlain(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateWallWindow(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateWallHalfTimber(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    void generateCornerOuter(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateCornerInner(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    void generateFloorPlain(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    void generateRoofFlat(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateRoofSlope(const glm::vec3& offset, Direction slopeDir, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateRoofRidge(const glm::vec3& offset, bool eastWest, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateRoofHip(const glm::vec3& offset, int corner, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
    void generateRoofGable(const glm::vec3& offset, Direction gableDir, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    void generateChimney(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;

    // Generate a box primitive
    void generateBox(const glm::vec3& min, const glm::vec3& max,
                    std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const;
};
