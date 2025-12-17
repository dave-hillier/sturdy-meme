#include "SpaceColonisationStrategy.h"
#include <SDL3/SDL.h>
#include <map>

void SpaceColonisationStrategy::generate(const TreeParameters& params,
                                         std::mt19937& rng,
                                         TreeStructure& outTree) {
    // Use existing SpaceColonisationGenerator to create nodes
    SpaceColonisationGenerator scGen(rng);
    std::vector<TreeNode> nodes;

    scGen.generate(params, nodes);

    if (nodes.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SpaceColonisationStrategy: No nodes generated");
        return;
    }

    // Convert flat node structure to hierarchical Branch tree
    convertNodesToTree(nodes, params, outTree);

    SDL_Log("SpaceColonisationStrategy: Generated tree with %zu branches from %zu nodes",
            outTree.getTotalBranchCount(), nodes.size());
}

void SpaceColonisationStrategy::convertNodesToTree(const std::vector<TreeNode>& nodes,
                                                    const TreeParameters& params,
                                                    TreeStructure& outTree) {
    if (nodes.empty()) return;

    // Build a map from node index to Branch pointer for connecting children
    std::map<int, Branch*> nodeToBranch;

    // Find root node (parentIndex == -1)
    int rootIdx = -1;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parentIndex == -1) {
            rootIdx = static_cast<int>(i);
            break;
        }
    }

    if (rootIdx == -1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SpaceColonisationStrategy: No root node found");
        return;
    }

    // Helper to create branch properties from a node
    auto createBranchProps = [&params](const TreeNode& node, const TreeNode* parent) {
        Branch::Properties props;

        // Calculate length from parent to this node
        if (parent) {
            props.length = glm::length(node.position - parent->position);
        } else {
            props.length = 0.01f;  // Root has minimal length
        }

        props.startRadius = node.thickness;
        props.endRadius = node.thickness * 0.8f;
        props.level = node.depth;
        props.radialSegments = params.spaceColonisation.radialSegments;
        props.lengthSegments = std::max(2, params.spaceColonisation.curveSubdivisions);

        return props;
    };

    // Helper to calculate orientation from parent to child
    auto calculateOrientation = [](const glm::vec3& from, const glm::vec3& to) {
        glm::vec3 dir = to - from;
        float len = glm::length(dir);
        if (len < 0.0001f) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
        }
        dir /= len;

        // Calculate quaternion that rotates (0,1,0) to dir
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        float dot = glm::dot(up, dir);

        if (dot > 0.9999f) {
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Same direction
        }
        if (dot < -0.9999f) {
            // Opposite direction - rotate 180 around X
            return glm::quat(0.0f, 1.0f, 0.0f, 0.0f);
        }

        glm::vec3 axis = glm::normalize(glm::cross(up, dir));
        float angle = std::acos(dot);
        return glm::angleAxis(angle, axis);
    };

    // Process nodes in order (parent before children due to how space colonisation builds)
    // First, create the root
    const TreeNode& rootNode = nodes[rootIdx];

    // For root, we need to find a child to determine orientation
    glm::quat rootOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    if (!rootNode.childIndices.empty()) {
        rootOrientation = calculateOrientation(rootNode.position,
                                               nodes[rootNode.childIndices[0]].position);
    }

    Branch::Properties rootProps = createBranchProps(rootNode, nullptr);
    rootProps.startRadius = params.spaceColonisation.baseThickness;
    rootProps.endRadius = rootNode.thickness;

    Branch root(rootNode.position, rootOrientation, rootProps);
    nodeToBranch[rootIdx] = &root;

    // Process nodes breadth-first to ensure parents exist before children
    std::vector<int> toProcess;
    toProcess.push_back(rootIdx);

    while (!toProcess.empty()) {
        int currentIdx = toProcess.front();
        toProcess.erase(toProcess.begin());

        const TreeNode& currentNode = nodes[currentIdx];
        Branch* currentBranch = nodeToBranch[currentIdx];

        if (!currentBranch) continue;

        // Add children
        for (int childIdx : currentNode.childIndices) {
            if (childIdx < 0 || childIdx >= static_cast<int>(nodes.size())) continue;

            const TreeNode& childNode = nodes[childIdx];

            glm::quat childOrientation;
            if (!childNode.childIndices.empty()) {
                childOrientation = calculateOrientation(childNode.position,
                                                       nodes[childNode.childIndices[0]].position);
            } else {
                // Terminal node - use direction from parent
                childOrientation = calculateOrientation(currentNode.position, childNode.position);
            }

            Branch::Properties childProps = createBranchProps(childNode, &currentNode);

            Branch& newChild = currentBranch->addChild(currentNode.position,
                                                        childOrientation,
                                                        childProps);
            nodeToBranch[childIdx] = &newChild;

            toProcess.push_back(childIdx);
        }
    }

    outTree.setRoot(std::move(root));
}
