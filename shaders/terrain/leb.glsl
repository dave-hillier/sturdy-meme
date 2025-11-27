/* leb.glsl - Longest Edge Bisection library for Vulkan GLSL
 * Based on Jonathan Dupuy's libleb
 * Adapted for Vulkan/SPIR-V
 *
 * Requires cbt.glsl to be included first
 */

// Data structures
struct leb_DiamondParent {
    cbt_Node base, top;
};

// Forward declarations
leb_DiamondParent leb_DecodeDiamondParent(in const cbt_Node node);
leb_DiamondParent leb_DecodeDiamondParent_Square(in const cbt_Node node);
void leb_SplitNode(in const cbt_Node node);
void leb_SplitNode_Square(in const cbt_Node node);
void leb_MergeNode(in const cbt_Node node, in const leb_DiamondParent diamond);
void leb_MergeNode_Square(in const cbt_Node node, in const leb_DiamondParent diamond);

vec3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const vec3 data);
mat2x3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const mat2x3 data);
mat3x3 leb_DecodeNodeAttributeArray_mat3(in const cbt_Node node, in const mat3x3 data);
mat4x3 leb_DecodeNodeAttributeArray_mat4(in const cbt_Node node, in const mat4x3 data);

vec3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const vec3 data);
mat2x3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const mat2x3 data);
mat3x3 leb_DecodeNodeAttributeArray_Square_mat3(in const cbt_Node node, in const mat3x3 data);
mat4x3 leb_DecodeNodeAttributeArray_Square_mat4(in const cbt_Node node, in const mat4x3 data);

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

struct leb__SameDepthNeighborIDs {
    uint left, right, edge, node;
};

leb__SameDepthNeighborIDs leb__CreateSameDepthNeighborIDs(uint left, uint right, uint edge, uint node) {
    leb__SameDepthNeighborIDs neighborIDs;
    neighborIDs.left = left;
    neighborIDs.right = right;
    neighborIDs.edge = edge;
    neighborIDs.node = node;
    return neighborIDs;
}

leb_DiamondParent leb__CreateDiamondParent(in const cbt_Node base, in const cbt_Node top) {
    leb_DiamondParent diamond;
    diamond.base = base;
    diamond.top = top;
    return diamond;
}

uint leb__GetBitValue(const uint bitField, int bitID) {
    return ((bitField >> bitID) & 1u);
}

// Branchless version of split node IDs update
leb__SameDepthNeighborIDs leb__SplitNodeIDs(in const leb__SameDepthNeighborIDs nodeIDs, uint splitBit) {
    uint b = splitBit;
    uint c = splitBit ^ 1u;
    bool cb = bool(c);
    uvec4 idArray = uvec4(nodeIDs.left, nodeIDs.right, nodeIDs.edge, nodeIDs.node);
    return leb__CreateSameDepthNeighborIDs(
        (idArray[2 + b] << 1u) | uint(cb && bool(idArray[2 + b])),
        (idArray[2 + c] << 1u) | uint(cb && bool(idArray[2 + c])),
        (idArray[b    ] << 1u) | uint(cb && bool(idArray[b    ])),
        (idArray[3    ] << 1u) | b
    );
}

leb__SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs(in const cbt_Node node) {
    leb__SameDepthNeighborIDs nodeIDs = leb__CreateSameDepthNeighborIDs(0u, 0u, 0u, 1u);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        nodeIDs = leb__SplitNodeIDs(nodeIDs, leb__GetBitValue(node.id, bitID));
    }

    return nodeIDs;
}

leb__SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs_Square(in const cbt_Node node) {
    uint b = leb__GetBitValue(node.id, max(0, node.depth - 1));
    leb__SameDepthNeighborIDs nodeIDs = leb__CreateSameDepthNeighborIDs(0u, 0u, 3u - b, 2u + b);

    for (int bitID = node.depth - 2; bitID >= 0; --bitID) {
        nodeIDs = leb__SplitNodeIDs(nodeIDs, leb__GetBitValue(node.id, bitID));
    }

    return nodeIDs;
}

cbt_Node leb__EdgeNeighbor(in const cbt_Node node) {
    uint nodeID = leb_DecodeSameDepthNeighborIDs(node).edge;
    return cbt_CreateNode_Explicit(nodeID, (nodeID == 0u) ? 0 : node.depth);
}

cbt_Node leb__EdgeNeighbor_Square(in const cbt_Node node) {
    uint nodeID = leb_DecodeSameDepthNeighborIDs_Square(node).edge;
    return cbt_CreateNode_Explicit(nodeID, (nodeID == 0u) ? 0 : node.depth);
}

void leb_SplitNode(in const cbt_Node node) {
    if (!cbt_IsCeilNode(node)) {
        const uint minNodeID = 1u;
        cbt_Node nodeIterator = node;

        cbt_SplitNode(nodeIterator);
        nodeIterator = leb__EdgeNeighbor(nodeIterator);

        while (nodeIterator.id > minNodeID) {
            cbt_SplitNode(nodeIterator);
            nodeIterator = cbt_ParentNode_Fast(nodeIterator);
            cbt_SplitNode(nodeIterator);
            nodeIterator = leb__EdgeNeighbor(nodeIterator);
        }
    }
}

void leb_SplitNode_Square(in const cbt_Node node) {
    if (!cbt_IsCeilNode(node)) {
        const uint minNodeID = 1u;
        cbt_Node nodeIterator = node;

        cbt_SplitNode(nodeIterator);
        nodeIterator = leb__EdgeNeighbor_Square(nodeIterator);

        while (nodeIterator.id > minNodeID) {
            cbt_SplitNode(nodeIterator);
            nodeIterator = cbt_ParentNode_Fast(nodeIterator);

            if (nodeIterator.id > minNodeID) {
                cbt_SplitNode(nodeIterator);
                nodeIterator = leb__EdgeNeighbor_Square(nodeIterator);
            }
        }
    }
}

leb_DiamondParent leb_DecodeDiamondParent(in const cbt_Node node) {
    cbt_Node parentNode = cbt_ParentNode_Fast(node);
    uint edgeNeighborID = leb_DecodeSameDepthNeighborIDs(parentNode).edge;
    cbt_Node edgeNeighborNode = cbt_CreateNode_Explicit(
        edgeNeighborID > 0u ? edgeNeighborID : parentNode.id,
        parentNode.depth
    );

    return leb__CreateDiamondParent(parentNode, edgeNeighborNode);
}

leb_DiamondParent leb_DecodeDiamondParent_Square(in const cbt_Node node) {
    cbt_Node parentNode = cbt_ParentNode_Fast(node);
    uint edgeNeighborID = leb_DecodeSameDepthNeighborIDs_Square(parentNode).edge;
    cbt_Node edgeNeighborNode = cbt_CreateNode_Explicit(
        edgeNeighborID > 0u ? edgeNeighborID : parentNode.id,
        parentNode.depth
    );

    return leb__CreateDiamondParent(parentNode, edgeNeighborNode);
}

bool leb__HasDiamondParent(in const leb_DiamondParent diamondParent) {
    bool canMergeBase = cbt_HeapRead(diamondParent.base) <= 2u;
    bool canMergeTop  = cbt_HeapRead(diamondParent.top) <= 2u;
    return canMergeBase && canMergeTop;
}

void leb_MergeNode(in const cbt_Node node, in const leb_DiamondParent diamondParent) {
    if (!cbt_IsRootNode(node) && leb__HasDiamondParent(diamondParent)) {
        cbt_MergeNode(node);
    }
}

void leb_MergeNode_Square(in const cbt_Node node, in const leb_DiamondParent diamondParent) {
    if ((node.depth > 1) && leb__HasDiamondParent(diamondParent)) {
        cbt_MergeNode(node);
    }
}

// Splitting matrix for LEB
mat3 leb__SplittingMatrix(uint splitBit) {
    float b = float(splitBit);
    float c = 1.0f - b;

    return transpose(mat3(
        c   , b   , 0.0f,
        0.5f, 0.0f, 0.5f,
        0.0f,    c,    b
    ));
}

// Square mapping matrix
mat3 leb__SquareMatrix(uint quadBit) {
    float b = float(quadBit);
    float c = 1.0f - b;

    return transpose(mat3(
        c, 0.0f, b,
        b, c   , b,
        b, 0.0f, c
    ));
}

// Winding correction matrix
mat3 leb__WindingMatrix(uint mirrorBit) {
    float b = float(mirrorBit);
    float c = 1.0f - b;

    return mat3(
        c, 0.0f, b,
        0, 1.0f, 0,
        b, 0.0f, c
    );
}

mat3 leb__DecodeTransformationMatrix(in const cbt_Node node) {
    mat3 xf = mat3(1.0f);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        xf = leb__SplittingMatrix(leb__GetBitValue(node.id, bitID)) * xf;
    }

    return leb__WindingMatrix(uint(node.depth) & 1u) * xf;
}

mat3 leb__DecodeTransformationMatrix_Square(in const cbt_Node node) {
    int bitID = max(0, node.depth - 1);
    uint quadBit = leb__GetBitValue(node.id, bitID);
    mat3 xf = leb__SquareMatrix(quadBit);

    for (bitID = node.depth - 2; bitID >= 0; --bitID) {
        xf = leb__SplittingMatrix(leb__GetBitValue(node.id, bitID)) * xf;
    }

    // Winding correction: account for which root triangle (quadBit) the node descended from.
    // The two root triangles from leb__SquareMatrix have opposite winding, so we XOR with quadBit
    // to flip the parity for the quadBit=1 half while preserving the original behavior for quadBit=0.
    return leb__WindingMatrix((uint(node.depth) ^ quadBit ^ 1u) & 1u) * xf;
}

vec3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const vec3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

mat2x3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const mat2x3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

mat3x3 leb_DecodeNodeAttributeArray_mat3(in const cbt_Node node, in const mat3x3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

mat4x3 leb_DecodeNodeAttributeArray_mat4(in const cbt_Node node, in const mat4x3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

vec3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const vec3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

mat2x3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const mat2x3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

mat3x3 leb_DecodeNodeAttributeArray_Square_mat3(in const cbt_Node node, in const mat3x3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

mat4x3 leb_DecodeNodeAttributeArray_Square_mat4(in const cbt_Node node, in const mat4x3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

// Utility function to decode triangle vertices from a node
// Returns triangle vertices in [0,1]^2 UV space
void leb_DecodeTriangleVertices(in const cbt_Node node, out vec2 v0, out vec2 v1, out vec2 v2) {
    vec3 xPos = vec3(0, 0, 1);
    vec3 yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Square(node, mat2x3(xPos, yPos));

    v0 = vec2(pos[0][0], pos[1][0]);
    v1 = vec2(pos[0][1], pos[1][1]);
    v2 = vec2(pos[0][2], pos[1][2]);
}
