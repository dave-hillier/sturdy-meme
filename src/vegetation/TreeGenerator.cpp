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

        const BranchData& processedBranch = meshData.branches.back();

        // Deciduous trees have a terminal branch that grows out of the end of the parent branch
        if (options.type == TreeType::Deciduous) {
            const SectionData& lastSection = processedBranch.sections.back();
            if (branch.level < options.branch.levels) {
                // Add terminal branch from tip
                Branch child;
                child.origin = lastSection.origin;
                child.orientation = lastSection.orientation;
                child.length = options.branch.length[branch.level + 1];
                child.radius = lastSection.radius;
                child.level = branch.level + 1;
                child.sectionCount = processedBranch.sectionCount;  // Same as parent
                child.segmentCount = processedBranch.segmentCount;
                branchQueue.push(child);
            } else {
                // At final level - just add a single leaf at the tip
                generateLeaf(lastSection.origin, lastSection.orientation, options, rng, meshData);
            }
        }

        // Generate leaves on final level branches
        if (branch.level == options.branch.levels) {
            generateLeaves(processedBranch.sections, options, rng, meshData);
        } else if (branch.level < options.branch.levels) {
            // Radial child branches
            generateChildBranches(
                options.branch.children[branch.level],
                branch.level + 1,
                processedBranch.sections,
                options, rng, branchQueue);
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

    // Match ez-tree: section length is simply branch.length / sectionCount
    // (ez-tree has a bug where it tries to divide by levels-1 for deciduous but fails due to case mismatch)
    float sectionLength = branch.length / static_cast<float>(branch.sectionCount);

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

        // Apply growth force (matching ez-tree's rotateTowards behavior)
        // Force strength can be positive (towards forceDirection) or negative (away from it)
        if (std::abs(options.branch.forceStrength) > 0.0001f) {
            glm::vec3 forceDir = glm::normalize(options.branch.forceDirection);

            // Compute target quaternion that rotates Y-up to forceDirection
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            float dot = glm::dot(up, forceDir);
            glm::quat forceQuat;
            if (dot < -0.999f) {
                forceQuat = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            } else if (dot > 0.999f) {
                forceQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            } else {
                glm::vec3 axis = glm::normalize(glm::cross(up, forceDir));
                float angle = std::acos(dot);
                forceQuat = glm::angleAxis(angle, axis);
            }

            // rotateTowards: rotate by the given angle towards forceQuat
            // positive strength rotates towards, negative rotates away
            float maxAngle = options.branch.forceStrength / sectionRadius;

            // Calculate angle between current and target orientation
            // angle = 2 * acos(|dot(q1, q2)|)
            float dotProduct = glm::dot(sectionOrientation, forceQuat);
            float angleBetween = 2.0f * std::acos(glm::clamp(std::abs(dotProduct), 0.0f, 1.0f));

            if (angleBetween > 0.0001f) {
                // Clamp rotation to maxAngle (can be negative to rotate away)
                float t = glm::clamp(maxAngle / angleBetween, -1.0f, 1.0f);
                sectionOrientation = glm::slerp(sectionOrientation, forceQuat, t);
            }
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

        // Interpolate orientation: match ez-tree's qB.slerp(qA, alpha) which goes FROM qB TOWARDS qA
        glm::quat parentOrientation = glm::slerp(sectionB.orientation, sectionA.orientation, alpha);

        // Calculate child branch angle and radial position
        float radialAngle = 2.0f * glm::pi<float>() * (radialOffset + static_cast<float>(i) / count);
        float branchAngle = glm::radians(options.branch.angle[level]);

        // Build child orientation: rotate from parent by branch angle, then rotate around Y
        // Negate angle to match ez-tree's coordinate system
        glm::quat angleRotation = glm::angleAxis(-branchAngle, glm::vec3(1.0f, 0.0f, 0.0f));
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

        // Interpolate orientation: match ez-tree's qB.slerp(qA, alpha) which goes FROM qB TOWARDS qA
        glm::quat parentOrientation = glm::slerp(sectionB.orientation, sectionA.orientation, alpha);

        // Calculate leaf orientation
        float radialAngle = 2.0f * glm::pi<float>() * (radialOffset + static_cast<float>(i) / options.leaves.count);
        float leafAngle = glm::radians(options.leaves.angle);

        // Negate angle to match ez-tree's coordinate system
        glm::quat angleRotation = glm::angleAxis(-leafAngle, glm::vec3(1.0f, 0.0f, 0.0f));
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
