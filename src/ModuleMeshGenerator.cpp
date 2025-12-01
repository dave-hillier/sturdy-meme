#include "ModuleMeshGenerator.h"
#include <glm/gtc/matrix_transform.hpp>

void ModuleMeshGenerator::addQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                   const glm::vec3& normal, const glm::vec2& uvScale,
                                   std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    uint32_t base = static_cast<uint32_t>(verts.size());

    // Calculate tangent from edge
    glm::vec3 edge = glm::normalize(p1 - p0);
    glm::vec4 tangent(edge, 1.0f);

    verts.push_back({p0, normal, glm::vec2(0, 0) * uvScale, tangent});
    verts.push_back({p1, normal, glm::vec2(1, 0) * uvScale, tangent});
    verts.push_back({p2, normal, glm::vec2(1, 1) * uvScale, tangent});
    verts.push_back({p3, normal, glm::vec2(0, 1) * uvScale, tangent});

    inds.push_back(base + 0);
    inds.push_back(base + 1);
    inds.push_back(base + 2);
    inds.push_back(base + 0);
    inds.push_back(base + 2);
    inds.push_back(base + 3);
}

void ModuleMeshGenerator::addTriangle(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                                       const glm::vec3& normal, std::vector<Vertex>& verts,
                                       std::vector<uint32_t>& inds) const {
    uint32_t base = static_cast<uint32_t>(verts.size());

    glm::vec3 edge = glm::normalize(p1 - p0);
    glm::vec4 tangent(edge, 1.0f);

    verts.push_back({p0, normal, glm::vec2(0, 0), tangent});
    verts.push_back({p1, normal, glm::vec2(1, 0), tangent});
    verts.push_back({p2, normal, glm::vec2(0.5f, 1), tangent});

    inds.push_back(base + 0);
    inds.push_back(base + 1);
    inds.push_back(base + 2);
}

void ModuleMeshGenerator::generateBox(const glm::vec3& min, const glm::vec3& max,
                                       std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    glm::vec3 size = max - min;

    // Front face (+Z)
    addQuad(
        glm::vec3(min.x, min.y, max.z), glm::vec3(max.x, min.y, max.z),
        glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z),
        glm::vec3(0, 0, 1), glm::vec2(size.x, size.y), verts, inds);

    // Back face (-Z)
    addQuad(
        glm::vec3(max.x, min.y, min.z), glm::vec3(min.x, min.y, min.z),
        glm::vec3(min.x, max.y, min.z), glm::vec3(max.x, max.y, min.z),
        glm::vec3(0, 0, -1), glm::vec2(size.x, size.y), verts, inds);

    // Right face (+X)
    addQuad(
        glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, min.y, min.z),
        glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z),
        glm::vec3(1, 0, 0), glm::vec2(size.z, size.y), verts, inds);

    // Left face (-X)
    addQuad(
        glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, min.y, max.z),
        glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z),
        glm::vec3(-1, 0, 0), glm::vec2(size.z, size.y), verts, inds);

    // Top face (+Y)
    addQuad(
        glm::vec3(min.x, max.y, max.z), glm::vec3(max.x, max.y, max.z),
        glm::vec3(max.x, max.y, min.z), glm::vec3(min.x, max.y, min.z),
        glm::vec3(0, 1, 0), glm::vec2(size.x, size.z), verts, inds);

    // Bottom face (-Y)
    addQuad(
        glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z),
        glm::vec3(max.x, min.y, max.z), glm::vec3(min.x, min.y, max.z),
        glm::vec3(0, -1, 0), glm::vec2(size.x, size.z), verts, inds);
}

void ModuleMeshGenerator::generateModuleMesh(ModuleType type, std::vector<Vertex>& outVertices,
                                              std::vector<uint32_t>& outIndices) const {
    glm::vec3 offset(0.0f);

    switch (type) {
        case ModuleType::Air:
            // Empty - no mesh
            break;

        case ModuleType::FoundationWall:
            generateFoundationWall(offset, outVertices, outIndices);
            break;
        case ModuleType::FoundationCorner:
            generateFoundationCorner(offset, outVertices, outIndices);
            break;
        case ModuleType::FoundationDoor:
            generateFoundationDoor(offset, outVertices, outIndices);
            break;

        case ModuleType::WallPlain:
            generateWallPlain(offset, outVertices, outIndices);
            break;
        case ModuleType::WallWindow:
            generateWallWindow(offset, outVertices, outIndices);
            break;
        case ModuleType::WallHalfTimber:
            generateWallHalfTimber(offset, outVertices, outIndices);
            break;
        case ModuleType::WallHalfTimberWindow:
            generateWallWindow(offset, outVertices, outIndices);  // Same as window for now
            break;

        case ModuleType::CornerOuter:
            generateCornerOuter(offset, outVertices, outIndices);
            break;
        case ModuleType::CornerInner:
            generateCornerInner(offset, outVertices, outIndices);
            break;

        case ModuleType::FloorPlain:
            generateFloorPlain(offset, outVertices, outIndices);
            break;

        case ModuleType::RoofFlat:
            generateRoofFlat(offset, outVertices, outIndices);
            break;
        case ModuleType::RoofSlopeN:
            generateRoofSlope(offset, Direction::North, outVertices, outIndices);
            break;
        case ModuleType::RoofSlopeS:
            generateRoofSlope(offset, Direction::South, outVertices, outIndices);
            break;
        case ModuleType::RoofSlopeE:
            generateRoofSlope(offset, Direction::East, outVertices, outIndices);
            break;
        case ModuleType::RoofSlopeW:
            generateRoofSlope(offset, Direction::West, outVertices, outIndices);
            break;

        case ModuleType::RoofRidgeNS:
            generateRoofRidge(offset, false, outVertices, outIndices);
            break;
        case ModuleType::RoofRidgeEW:
            generateRoofRidge(offset, true, outVertices, outIndices);
            break;

        case ModuleType::RoofHipNE:
            generateRoofHip(offset, 0, outVertices, outIndices);
            break;
        case ModuleType::RoofHipNW:
            generateRoofHip(offset, 1, outVertices, outIndices);
            break;
        case ModuleType::RoofHipSE:
            generateRoofHip(offset, 2, outVertices, outIndices);
            break;
        case ModuleType::RoofHipSW:
            generateRoofHip(offset, 3, outVertices, outIndices);
            break;

        case ModuleType::RoofGableN:
            generateRoofGable(offset, Direction::North, outVertices, outIndices);
            break;
        case ModuleType::RoofGableS:
            generateRoofGable(offset, Direction::South, outVertices, outIndices);
            break;
        case ModuleType::RoofGableE:
            generateRoofGable(offset, Direction::East, outVertices, outIndices);
            break;
        case ModuleType::RoofGableW:
            generateRoofGable(offset, Direction::West, outVertices, outIndices);
            break;

        case ModuleType::Chimney:
            generateChimney(offset, outVertices, outIndices);
            break;

        default:
            break;
    }
}

void ModuleMeshGenerator::generateBuildingMesh(const BuildingWFC& wfc, const ModuleLibrary& library,
                                                const glm::vec3& worldOffset,
                                                std::vector<Vertex>& outVertices,
                                                std::vector<uint32_t>& outIndices) const {
    glm::ivec3 size = wfc.getSize();

    for (int z = 0; z < size.z; ++z) {
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const WFCCell& cell = wfc.getCell(x, y, z);
                if (!cell.collapsed) continue;

                const BuildingModule& mod = library.getModule(cell.chosenModule);
                if (mod.type == ModuleType::Air) continue;

                // Calculate module world position
                glm::vec3 moduleOffset = worldOffset + glm::vec3(x, y, z) * MODULE_SIZE;

                // Generate module mesh at this position
                std::vector<Vertex> moduleVerts;
                std::vector<uint32_t> moduleInds;
                generateModuleMesh(mod.type, moduleVerts, moduleInds);

                // Transform vertices to world position
                uint32_t baseVertex = static_cast<uint32_t>(outVertices.size());
                for (auto& v : moduleVerts) {
                    v.position += moduleOffset;
                    outVertices.push_back(v);
                }

                // Add indices with offset
                for (uint32_t idx : moduleInds) {
                    outIndices.push_back(baseVertex + idx);
                }
            }
        }
    }
}

// Foundation modules
void ModuleMeshGenerator::generateFoundationWall(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.3f;
    float h = s;  // Full height

    // Main wall block - facing south (+Z)
    generateBox(offset + glm::vec3(0, 0, s - wallThickness),
                offset + glm::vec3(s, h, s), verts, inds);

    // Stone foundation detail - slightly thicker at base
    float foundationH = 0.3f;
    generateBox(offset + glm::vec3(-0.05f, 0, s - wallThickness - 0.1f),
                offset + glm::vec3(s + 0.05f, foundationH, s + 0.05f), verts, inds);
}

void ModuleMeshGenerator::generateFoundationCorner(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.3f;
    float h = s;

    // South wall
    generateBox(offset + glm::vec3(0, 0, s - wallThickness),
                offset + glm::vec3(s, h, s), verts, inds);

    // East wall
    generateBox(offset + glm::vec3(s - wallThickness, 0, 0),
                offset + glm::vec3(s, h, s - wallThickness), verts, inds);

    // Corner post
    generateBox(offset + glm::vec3(s - wallThickness - 0.1f, 0, s - wallThickness - 0.1f),
                offset + glm::vec3(s + 0.05f, h + 0.1f, s + 0.05f), verts, inds);
}

void ModuleMeshGenerator::generateFoundationDoor(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.3f;
    float h = s;
    float doorWidth = 0.8f;
    float doorHeight = 1.6f;
    float doorStart = (s - doorWidth) / 2.0f;

    // Left wall section
    generateBox(offset + glm::vec3(0, 0, s - wallThickness),
                offset + glm::vec3(doorStart, h, s), verts, inds);

    // Right wall section
    generateBox(offset + glm::vec3(doorStart + doorWidth, 0, s - wallThickness),
                offset + glm::vec3(s, h, s), verts, inds);

    // Above door
    generateBox(offset + glm::vec3(doorStart, doorHeight, s - wallThickness),
                offset + glm::vec3(doorStart + doorWidth, h, s), verts, inds);

    // Door frame
    float frameWidth = 0.1f;
    generateBox(offset + glm::vec3(doorStart - frameWidth, 0, s - wallThickness - 0.05f),
                offset + glm::vec3(doorStart, doorHeight + frameWidth, s + 0.05f), verts, inds);
    generateBox(offset + glm::vec3(doorStart + doorWidth, 0, s - wallThickness - 0.05f),
                offset + glm::vec3(doorStart + doorWidth + frameWidth, doorHeight + frameWidth, s + 0.05f), verts, inds);
    generateBox(offset + glm::vec3(doorStart - frameWidth, doorHeight, s - wallThickness - 0.05f),
                offset + glm::vec3(doorStart + doorWidth + frameWidth, doorHeight + frameWidth, s + 0.05f), verts, inds);
}

// Wall modules
void ModuleMeshGenerator::generateWallPlain(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.25f;
    float h = s;

    // Main wall
    generateBox(offset + glm::vec3(0, 0, s - wallThickness),
                offset + glm::vec3(s, h, s), verts, inds);
}

void ModuleMeshGenerator::generateWallWindow(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.25f;
    float h = s;

    float winWidth = 0.6f;
    float winHeight = 0.8f;
    float winBottom = 0.6f;
    float winStart = (s - winWidth) / 2.0f;

    // Left section
    generateBox(offset + glm::vec3(0, 0, s - wallThickness),
                offset + glm::vec3(winStart, h, s), verts, inds);

    // Right section
    generateBox(offset + glm::vec3(winStart + winWidth, 0, s - wallThickness),
                offset + glm::vec3(s, h, s), verts, inds);

    // Below window
    generateBox(offset + glm::vec3(winStart, 0, s - wallThickness),
                offset + glm::vec3(winStart + winWidth, winBottom, s), verts, inds);

    // Above window
    generateBox(offset + glm::vec3(winStart, winBottom + winHeight, s - wallThickness),
                offset + glm::vec3(winStart + winWidth, h, s), verts, inds);

    // Window sill
    generateBox(offset + glm::vec3(winStart - 0.1f, winBottom - 0.05f, s - wallThickness - 0.1f),
                offset + glm::vec3(winStart + winWidth + 0.1f, winBottom + 0.05f, s + 0.05f), verts, inds);

    // Window lintel
    generateBox(offset + glm::vec3(winStart - 0.05f, winBottom + winHeight, s - wallThickness - 0.05f),
                offset + glm::vec3(winStart + winWidth + 0.05f, winBottom + winHeight + 0.1f, s + 0.02f), verts, inds);
}

void ModuleMeshGenerator::generateWallHalfTimber(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.2f;
    float h = s;
    float timberWidth = 0.15f;

    // Infill (white plaster area)
    generateBox(offset + glm::vec3(timberWidth, timberWidth, s - wallThickness),
                offset + glm::vec3(s - timberWidth, h - timberWidth, s), verts, inds);

    // Timber frame
    // Bottom beam
    generateBox(offset + glm::vec3(0, 0, s - wallThickness - 0.05f),
                offset + glm::vec3(s, timberWidth, s + 0.02f), verts, inds);
    // Top beam
    generateBox(offset + glm::vec3(0, h - timberWidth, s - wallThickness - 0.05f),
                offset + glm::vec3(s, h, s + 0.02f), verts, inds);
    // Left post
    generateBox(offset + glm::vec3(0, 0, s - wallThickness - 0.05f),
                offset + glm::vec3(timberWidth, h, s + 0.02f), verts, inds);
    // Right post
    generateBox(offset + glm::vec3(s - timberWidth, 0, s - wallThickness - 0.05f),
                offset + glm::vec3(s, h, s + 0.02f), verts, inds);

    // Diagonal brace
    float braceW = 0.1f;
    // Simplified as a box - could be improved with proper diagonal geometry
    generateBox(offset + glm::vec3(s * 0.3f, h * 0.3f, s - wallThickness - 0.03f),
                offset + glm::vec3(s * 0.7f, h * 0.7f, s + 0.01f), verts, inds);
}

// Corner modules
void ModuleMeshGenerator::generateCornerOuter(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float wallThickness = 0.3f;
    float h = s;

    // Corner post
    generateBox(offset + glm::vec3(s - wallThickness, 0, s - wallThickness),
                offset + glm::vec3(s + 0.05f, h, s + 0.05f), verts, inds);
}

void ModuleMeshGenerator::generateCornerInner(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float h = s;

    // Just floor for interior corner
    generateBox(offset + glm::vec3(0, 0, 0),
                offset + glm::vec3(s, 0.1f, s), verts, inds);
}

// Floor module
void ModuleMeshGenerator::generateFloorPlain(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;

    // Floor planks
    generateBox(offset + glm::vec3(0, 0, 0),
                offset + glm::vec3(s, 0.15f, s), verts, inds);
}

// Roof modules
void ModuleMeshGenerator::generateRoofFlat(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float roofThickness = 0.15f;

    generateBox(offset + glm::vec3(-0.1f, s - roofThickness, -0.1f),
                offset + glm::vec3(s + 0.1f, s, s + 0.1f), verts, inds);
}

void ModuleMeshGenerator::generateRoofSlope(const glm::vec3& offset, Direction slopeDir, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float overhang = 0.2f;
    float roofHeight = s * 0.5f;

    glm::vec3 p0, p1, p2, p3;  // Bottom edge (low side)
    glm::vec3 p4, p5, p6, p7;  // Top edge (high side)

    switch (slopeDir) {
        case Direction::North:  // Slopes down toward -Z
            p0 = offset + glm::vec3(-overhang, 0, -overhang);
            p1 = offset + glm::vec3(s + overhang, 0, -overhang);
            p2 = offset + glm::vec3(s + overhang, roofHeight, s + overhang);
            p3 = offset + glm::vec3(-overhang, roofHeight, s + overhang);
            break;
        case Direction::South:  // Slopes down toward +Z
            p0 = offset + glm::vec3(-overhang, roofHeight, -overhang);
            p1 = offset + glm::vec3(s + overhang, roofHeight, -overhang);
            p2 = offset + glm::vec3(s + overhang, 0, s + overhang);
            p3 = offset + glm::vec3(-overhang, 0, s + overhang);
            break;
        case Direction::East:  // Slopes down toward +X
            p0 = offset + glm::vec3(-overhang, roofHeight, -overhang);
            p1 = offset + glm::vec3(s + overhang, 0, -overhang);
            p2 = offset + glm::vec3(s + overhang, 0, s + overhang);
            p3 = offset + glm::vec3(-overhang, roofHeight, s + overhang);
            break;
        case Direction::West:  // Slopes down toward -X
            p0 = offset + glm::vec3(-overhang, 0, -overhang);
            p1 = offset + glm::vec3(s + overhang, roofHeight, -overhang);
            p2 = offset + glm::vec3(s + overhang, roofHeight, s + overhang);
            p3 = offset + glm::vec3(-overhang, 0, s + overhang);
            break;
        default:
            return;
    }

    // Calculate normal for sloped surface
    glm::vec3 edge1 = p1 - p0;
    glm::vec3 edge2 = p3 - p0;
    glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

    // Top surface
    addQuad(p0, p1, p2, p3, normal, glm::vec2(s, s), verts, inds);

    // Underside
    addQuad(p3, p2, p1, p0, -normal, glm::vec2(s, s), verts, inds);
}

void ModuleMeshGenerator::generateRoofRidge(const glm::vec3& offset, bool eastWest, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float overhang = 0.2f;
    float roofHeight = s * 0.5f;
    float ridgeHeight = s * 0.7f;

    if (eastWest) {
        // Ridge runs E-W, slopes N and S
        glm::vec3 ridgeStart = offset + glm::vec3(-overhang, ridgeHeight, s * 0.5f);
        glm::vec3 ridgeEnd = offset + glm::vec3(s + overhang, ridgeHeight, s * 0.5f);

        // North slope
        addQuad(
            offset + glm::vec3(-overhang, roofHeight, -overhang),
            offset + glm::vec3(s + overhang, roofHeight, -overhang),
            ridgeEnd, ridgeStart,
            glm::normalize(glm::vec3(0, 0.7f, -0.7f)), glm::vec2(s, s), verts, inds);

        // South slope
        addQuad(
            offset + glm::vec3(s + overhang, roofHeight, s + overhang),
            offset + glm::vec3(-overhang, roofHeight, s + overhang),
            ridgeStart, ridgeEnd,
            glm::normalize(glm::vec3(0, 0.7f, 0.7f)), glm::vec2(s, s), verts, inds);
    } else {
        // Ridge runs N-S, slopes E and W
        glm::vec3 ridgeStart = offset + glm::vec3(s * 0.5f, ridgeHeight, -overhang);
        glm::vec3 ridgeEnd = offset + glm::vec3(s * 0.5f, ridgeHeight, s + overhang);

        // West slope
        addQuad(
            offset + glm::vec3(-overhang, roofHeight, -overhang),
            ridgeStart, ridgeEnd,
            offset + glm::vec3(-overhang, roofHeight, s + overhang),
            glm::normalize(glm::vec3(-0.7f, 0.7f, 0)), glm::vec2(s, s), verts, inds);

        // East slope
        addQuad(
            ridgeStart,
            offset + glm::vec3(s + overhang, roofHeight, -overhang),
            offset + glm::vec3(s + overhang, roofHeight, s + overhang),
            ridgeEnd,
            glm::normalize(glm::vec3(0.7f, 0.7f, 0)), glm::vec2(s, s), verts, inds);
    }
}

void ModuleMeshGenerator::generateRoofHip(const glm::vec3& offset, int corner, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float overhang = 0.2f;
    float peakHeight = s * 0.6f;

    glm::vec3 peak = offset + glm::vec3(s * 0.5f, peakHeight, s * 0.5f);

    // Four triangular faces meeting at peak
    // The corner parameter determines which two edges are the "low" edges

    glm::vec3 corners[4] = {
        offset + glm::vec3(-overhang, 0, -overhang),  // NW
        offset + glm::vec3(s + overhang, 0, -overhang),  // NE
        offset + glm::vec3(s + overhang, 0, s + overhang),  // SE
        offset + glm::vec3(-overhang, 0, s + overhang)   // SW
    };

    // Add all four triangular faces
    for (int i = 0; i < 4; ++i) {
        glm::vec3 p0 = corners[i];
        glm::vec3 p1 = corners[(i + 1) % 4];

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = peak - p0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

        addTriangle(p0, p1, peak, normal, verts, inds);
    }
}

void ModuleMeshGenerator::generateRoofGable(const glm::vec3& offset, Direction gableDir, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float overhang = 0.2f;
    float peakHeight = s * 0.5f;
    float wallThickness = 0.2f;

    // Gable wall (triangular)
    glm::vec3 p0, p1, peak;

    switch (gableDir) {
        case Direction::North:
            p0 = offset + glm::vec3(0, 0, 0);
            p1 = offset + glm::vec3(s, 0, 0);
            peak = offset + glm::vec3(s * 0.5f, peakHeight, 0);

            // Gable wall
            addTriangle(p0, p1, peak, glm::vec3(0, 0, -1), verts, inds);
            addTriangle(p1, p0, peak + glm::vec3(0, 0, wallThickness), glm::vec3(0, 0, 1), verts, inds);

            // Roof slopes on sides
            generateRoofSlope(offset, Direction::East, verts, inds);
            generateRoofSlope(offset, Direction::West, verts, inds);
            break;

        case Direction::South:
            p0 = offset + glm::vec3(s, 0, s);
            p1 = offset + glm::vec3(0, 0, s);
            peak = offset + glm::vec3(s * 0.5f, peakHeight, s);

            addTriangle(p0, p1, peak, glm::vec3(0, 0, 1), verts, inds);
            addTriangle(p1, p0, peak - glm::vec3(0, 0, wallThickness), glm::vec3(0, 0, -1), verts, inds);
            break;

        case Direction::East:
            p0 = offset + glm::vec3(s, 0, 0);
            p1 = offset + glm::vec3(s, 0, s);
            peak = offset + glm::vec3(s, peakHeight, s * 0.5f);

            addTriangle(p0, p1, peak, glm::vec3(1, 0, 0), verts, inds);
            addTriangle(p1, p0, peak - glm::vec3(wallThickness, 0, 0), glm::vec3(-1, 0, 0), verts, inds);
            break;

        case Direction::West:
            p0 = offset + glm::vec3(0, 0, s);
            p1 = offset + glm::vec3(0, 0, 0);
            peak = offset + glm::vec3(0, peakHeight, s * 0.5f);

            addTriangle(p0, p1, peak, glm::vec3(-1, 0, 0), verts, inds);
            addTriangle(p1, p0, peak + glm::vec3(wallThickness, 0, 0), glm::vec3(1, 0, 0), verts, inds);
            break;

        default:
            break;
    }
}

void ModuleMeshGenerator::generateChimney(const glm::vec3& offset, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) const {
    float s = MODULE_SIZE;
    float chimneySize = 0.5f;
    float chimneyHeight = s * 0.8f;

    glm::vec3 center = offset + glm::vec3(s * 0.5f, 0, s * 0.5f);

    generateBox(center + glm::vec3(-chimneySize * 0.5f, 0, -chimneySize * 0.5f),
                center + glm::vec3(chimneySize * 0.5f, chimneyHeight, chimneySize * 0.5f),
                verts, inds);

    // Cap
    generateBox(center + glm::vec3(-chimneySize * 0.6f, chimneyHeight, -chimneySize * 0.6f),
                center + glm::vec3(chimneySize * 0.6f, chimneyHeight + 0.1f, chimneySize * 0.6f),
                verts, inds);
}
