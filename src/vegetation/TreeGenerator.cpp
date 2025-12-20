#include "TreeGenerator.h"
#include <glm/gtc/constants.hpp>
#include <cmath>

// TreeRNG implementation (matching ez-tree's RNG algorithm)
TreeRNG::TreeRNG(uint32_t seed)
    : m_w((123456789 + seed) & MASK)
    , m_z((987654321 - seed) & MASK) {
}

float TreeRNG::random(float max, float min) {
    m_z = (36969 * (m_z & 65535) + (m_z >> 16)) & MASK;
    m_w = (18000 * (m_w & 65535) + (m_w >> 16)) & MASK;
    uint32_t result = ((m_z << 16) + (m_w & 65535));
    float normalized = static_cast<float>(result) / 4294967296.0f;
    return (max - min) * normalized + min;
}

// TreeMeshData helpers
uint32_t TreeMeshData::totalBranchVertices() const {
    uint32_t count = 0;
    for (const auto& branch : branches) {
        // Each section has segmentCount+1 vertices (extra for UV wrap)
        // We have sectionCount+1 sections
        count += (branch.sectionCount + 1) * (branch.segmentCount + 1);
    }
    return count;
}

uint32_t TreeMeshData::totalBranchIndices() const {
    uint32_t count = 0;
    for (const auto& branch : branches) {
        // Each section has segmentCount quads, each quad = 6 indices
        count += branch.sectionCount * branch.segmentCount * 6;
    }
    return count;
}

uint32_t TreeMeshData::totalLeafVertices() const {
    // Each leaf is 4 vertices (quad), double billboard means 8
    uint32_t verticesPerLeaf = 4;  // We handle billboard mode in shader
    return static_cast<uint32_t>(leaves.size()) * verticesPerLeaf;
}

uint32_t TreeMeshData::totalLeafIndices() const {
    // Each leaf quad = 6 indices
    return static_cast<uint32_t>(leaves.size()) * 6;
}

// TreeGenerator implementation
TreeMeshData TreeGenerator::generate(const TreeOptions& options) {
    TreeMeshData meshData;
    TreeRNG rng(options.seed);

    std::queue<Branch> branchQueue;

    // Start with trunk
    Branch trunk;
    trunk.origin = glm::vec3(0.0f);
    trunk.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
    trunk.length = options.branch.length[0];
    trunk.radius = options.branch.radius[0];
    trunk.level = 0;
    trunk.sectionCount = options.branch.sections[0];
    trunk.segmentCount = options.branch.segments[0];

    branchQueue.push(trunk);

    while (!branchQueue.empty()) {
        Branch branch = branchQueue.front();
        branchQueue.pop();
        processBranch(branch, options, rng, meshData);

        // Generate children for this branch
        if (branch.level < options.branch.levels) {
            const BranchData& processedBranch = meshData.branches.back();

            if (options.type == TreeType::Deciduous && branch.level < options.branch.levels) {
                // Terminal branch from end
                if (branch.level + 1 <= options.branch.levels) {
                    const SectionData& lastSection = processedBranch.sections.back();
                    Branch child;
                    child.origin = lastSection.origin;
                    child.orientation = lastSection.orientation;
                    child.length = options.branch.length[branch.level + 1];
                    child.radius = lastSection.radius;
                    child.level = branch.level + 1;
                    child.sectionCount = processedBranch.sectionCount;  // Same as parent
                    child.segmentCount = processedBranch.segmentCount;
                    branchQueue.push(child);
                }
            }

            // Radial child branches
            if (branch.level < options.branch.levels) {
                generateChildBranches(
                    options.branch.children[branch.level],
                    branch.level + 1,
                    processedBranch.sections,
                    options, rng, branchQueue);
            }
        }

        // Generate leaves on final level
        if (branch.level == options.branch.levels) {
            const BranchData& processedBranch = meshData.branches.back();
            generateLeaves(processedBranch.sections, options, rng, meshData);
        }
    }

    return meshData;
}

void TreeGenerator::processBranch(const Branch& branch, const TreeOptions& options,
                                   TreeRNG& rng, TreeMeshData& meshData) {
    BranchData branchData;
    branchData.origin = branch.origin;
    branchData.orientation = branch.orientation;
    branchData.length = branch.length;
    branchData.radius = branch.radius;
    branchData.level = branch.level;
    branchData.sectionCount = branch.sectionCount;
    branchData.segmentCount = branch.segmentCount;

    glm::quat sectionOrientation = branch.orientation;
    glm::vec3 sectionOrigin = branch.origin;

    float sectionLength = branch.length / static_cast<float>(branch.sectionCount);
    if (options.type == TreeType::Deciduous && options.branch.levels > 1) {
        sectionLength /= static_cast<float>(options.branch.levels - 1);
    }

    for (int i = 0; i <= branch.sectionCount; ++i) {
        float sectionRadius = branch.radius;

        // Calculate taper
        if (i == branch.sectionCount && branch.level == options.branch.levels) {
            sectionRadius = 0.001f;  // Cap at end
        } else if (options.type == TreeType::Deciduous) {
            float t = static_cast<float>(i) / static_cast<float>(branch.sectionCount);
            sectionRadius *= 1.0f - options.branch.taper[branch.level] * t;
        } else {
            // Evergreen: full taper
            float t = static_cast<float>(i) / static_cast<float>(branch.sectionCount);
            sectionRadius *= 1.0f - t;
        }

        SectionData section;
        section.origin = sectionOrigin;
        section.orientation = sectionOrientation;
        section.radius = sectionRadius;
        branchData.sections.push_back(section);

        // Move origin to next section
        glm::vec3 up(0.0f, sectionLength, 0.0f);
        sectionOrigin += sectionOrientation * up;

        // Apply gnarliness perturbation
        float gnarliness = std::max(1.0f, 1.0f / std::sqrt(sectionRadius)) *
                          options.branch.gnarliness[branch.level];

        glm::vec3 euler = glm::eulerAngles(sectionOrientation);
        euler.x += rng.random(gnarliness, -gnarliness);
        euler.z += rng.random(gnarliness, -gnarliness);

        // Rebuild quaternion from modified euler angles
        sectionOrientation = glm::quat(euler);

        // Apply twist
        glm::quat twist = glm::angleAxis(options.branch.twist[branch.level], glm::vec3(0.0f, 1.0f, 0.0f));
        sectionOrientation = sectionOrientation * twist;

        // Apply growth force
        if (options.branch.forceStrength > 0.0f) {
            glm::vec3 forceDir = glm::normalize(options.branch.forceDirection);
            // Compute rotation from up (0,1,0) to forceDir
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            float dot = glm::dot(up, forceDir);
            glm::quat forceQuat;
            if (dot < -0.999f) {
                // Vectors are nearly opposite
                forceQuat = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            } else if (dot > 0.999f) {
                // Vectors are nearly aligned
                forceQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            } else {
                glm::vec3 axis = glm::normalize(glm::cross(up, forceDir));
                float angle = std::acos(dot);
                forceQuat = glm::angleAxis(angle, axis);
            }
            float strength = options.branch.forceStrength / sectionRadius;
            sectionOrientation = glm::slerp(sectionOrientation, forceQuat, glm::clamp(strength, 0.0f, 1.0f));
        }
    }

    meshData.branches.push_back(branchData);
}

void TreeGenerator::generateChildBranches(int count, int level,
                                          const std::vector<SectionData>& sections,
                                          const TreeOptions& options, TreeRNG& rng,
                                          std::queue<Branch>& branchQueue) {
    float radialOffset = rng.random();

    for (int i = 0; i < count; ++i) {
        // Where along the parent branch this child starts
        float childStart = rng.random(1.0f, options.branch.start[level]);

        // Find sections on either side
        int sectionIndex = static_cast<int>(childStart * (sections.size() - 1));
        sectionIndex = glm::clamp(sectionIndex, 0, static_cast<int>(sections.size()) - 1);

        const SectionData& sectionA = sections[sectionIndex];
        const SectionData& sectionB = (sectionIndex < static_cast<int>(sections.size()) - 1)
                                       ? sections[sectionIndex + 1]
                                       : sectionA;

        // Interpolation factor
        float alpha = (sections.size() > 1)
            ? (childStart - static_cast<float>(sectionIndex) / (sections.size() - 1)) /
              (1.0f / (sections.size() - 1))
            : 0.0f;
        alpha = glm::clamp(alpha, 0.0f, 1.0f);

        // Interpolate origin
        glm::vec3 childOrigin = glm::mix(sectionA.origin, sectionB.origin, alpha);

        // Interpolate radius
        float childRadius = options.branch.radius[level] *
                           glm::mix(sectionA.radius, sectionB.radius, alpha);

        // Interpolate orientation
        glm::quat parentOrientation = glm::slerp(sectionA.orientation, sectionB.orientation, alpha);

        // Calculate child branch angle and radial position
        float radialAngle = 2.0f * glm::pi<float>() * (radialOffset + static_cast<float>(i) / count);
        float branchAngle = glm::radians(options.branch.angle[level]);

        // Build child orientation: rotate from parent by branch angle, then rotate around Y
        glm::quat angleRotation = glm::angleAxis(branchAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat radialRotation = glm::angleAxis(radialAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat childOrientation = parentOrientation * radialRotation * angleRotation;

        // Child length (evergreen: shorter at top)
        float childLength = options.branch.length[level];
        if (options.type == TreeType::Evergreen) {
            childLength *= (1.0f - childStart);
        }

        Branch child;
        child.origin = childOrigin;
        child.orientation = childOrientation;
        child.length = childLength;
        child.radius = childRadius;
        child.level = level;
        child.sectionCount = options.branch.sections[level];
        child.segmentCount = options.branch.segments[level];

        branchQueue.push(child);
    }
}

void TreeGenerator::generateLeaves(const std::vector<SectionData>& sections,
                                   const TreeOptions& options, TreeRNG& rng,
                                   TreeMeshData& meshData) {
    float radialOffset = rng.random();

    for (int i = 0; i < options.leaves.count; ++i) {
        // Where along the branch this leaf is placed
        float leafStart = rng.random(1.0f, options.leaves.start);

        // Find sections on either side
        int sectionIndex = static_cast<int>(leafStart * (sections.size() - 1));
        sectionIndex = glm::clamp(sectionIndex, 0, static_cast<int>(sections.size()) - 1);

        const SectionData& sectionA = sections[sectionIndex];
        const SectionData& sectionB = (sectionIndex < static_cast<int>(sections.size()) - 1)
                                       ? sections[sectionIndex + 1]
                                       : sectionA;

        // Interpolation factor
        float alpha = (sections.size() > 1)
            ? (leafStart - static_cast<float>(sectionIndex) / (sections.size() - 1)) /
              (1.0f / (sections.size() - 1))
            : 0.0f;
        alpha = glm::clamp(alpha, 0.0f, 1.0f);

        // Interpolate origin
        glm::vec3 leafOrigin = glm::mix(sectionA.origin, sectionB.origin, alpha);

        // Interpolate orientation
        glm::quat parentOrientation = glm::slerp(sectionA.orientation, sectionB.orientation, alpha);

        // Calculate leaf orientation
        float radialAngle = 2.0f * glm::pi<float>() * (radialOffset + static_cast<float>(i) / options.leaves.count);
        float leafAngle = glm::radians(options.leaves.angle);

        glm::quat angleRotation = glm::angleAxis(leafAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat radialRotation = glm::angleAxis(radialAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat leafOrientation = parentOrientation * radialRotation * angleRotation;

        generateLeaf(leafOrigin, leafOrientation, options, rng, meshData);
    }
}

void TreeGenerator::generateLeaf(const glm::vec3& origin, const glm::quat& orientation,
                                 const TreeOptions& options, TreeRNG& rng,
                                 TreeMeshData& meshData) {
    float sizeVariance = rng.random(options.leaves.sizeVariance, -options.leaves.sizeVariance);
    float leafSize = options.leaves.size * (1.0f + sizeVariance);

    LeafData leaf;
    leaf.position = origin;
    leaf.orientation = orientation;
    leaf.size = leafSize;

    meshData.leaves.push_back(leaf);
}

void TreeGenerator::toGPUFormat(const TreeMeshData& meshData,
                                std::vector<BranchDataGPU>& branchesOut,
                                std::vector<SectionDataGPU>& sectionsOut,
                                std::vector<LeafDataGPU>& leavesOut) {
    branchesOut.clear();
    sectionsOut.clear();
    leavesOut.clear();

    uint32_t sectionOffset = 0;

    for (const auto& branch : meshData.branches) {
        BranchDataGPU gpu;
        gpu.origin = glm::vec4(branch.origin, branch.radius);
        gpu.orientation = glm::vec4(branch.orientation.x, branch.orientation.y,
                                    branch.orientation.z, branch.orientation.w);
        gpu.params = glm::vec4(branch.length,
                               static_cast<float>(branch.level),
                               static_cast<float>(branch.sectionCount),
                               static_cast<float>(branch.segmentCount));
        gpu.sectionStart = glm::vec4(static_cast<float>(sectionOffset),
                                     static_cast<float>(branch.sections.size()),
                                     0.0f, 0.0f);
        branchesOut.push_back(gpu);

        for (const auto& section : branch.sections) {
            SectionDataGPU sGpu;
            sGpu.origin = glm::vec4(section.origin, section.radius);
            sGpu.orientation = glm::vec4(section.orientation.x, section.orientation.y,
                                         section.orientation.z, section.orientation.w);
            sectionsOut.push_back(sGpu);
        }

        sectionOffset += static_cast<uint32_t>(branch.sections.size());
    }

    for (const auto& leaf : meshData.leaves) {
        LeafDataGPU lGpu;
        lGpu.positionAndSize = glm::vec4(leaf.position, leaf.size);
        lGpu.orientation = glm::vec4(leaf.orientation.x, leaf.orientation.y,
                                     leaf.orientation.z, leaf.orientation.w);
        leavesOut.push_back(lGpu);
    }
}
