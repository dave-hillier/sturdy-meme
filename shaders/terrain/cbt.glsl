/* cbt.glsl - Concurrent Binary Tree library for Vulkan GLSL
 * Based on Jonathan Dupuy's libcbt
 * Adapted for Vulkan/SPIR-V
 */

// CBT Buffer binding - must be defined before including this file
#ifndef CBT_BUFFER_BINDING
#   define CBT_BUFFER_BINDING 0
#endif

// CBT buffer layout
layout(std430, binding = CBT_BUFFER_BINDING) buffer CBTBuffer {
    uint heap[];
} cbtBuffer;

// Data structure for a CBT node
struct cbt_Node {
    uint id;    // heap index
    int depth;  // findMSB(id) = node depth
};

// Forward declarations
void cbt_SplitNode_Fast(in const cbt_Node node);
void cbt_SplitNode(in const cbt_Node node);
void cbt_MergeNode_Fast(in const cbt_Node node);
void cbt_MergeNode(in const cbt_Node node);

uint cbt_HeapRead(in const cbt_Node node);
int cbt_MaxDepth();
uint cbt_NodeCount();
bool cbt_IsLeafNode(in const cbt_Node node);
bool cbt_IsCeilNode(in const cbt_Node node);
bool cbt_IsRootNode(in const cbt_Node node);
bool cbt_IsNullNode(in const cbt_Node node);

uint cbt_EncodeNode(in const cbt_Node node);
cbt_Node cbt_DecodeNode(uint nodeID);

cbt_Node cbt_CreateNode(uint id);
cbt_Node cbt_CreateNode_Explicit(uint id, int depth);
cbt_Node cbt_ParentNode(in const cbt_Node node);
cbt_Node cbt_ParentNode_Fast(in const cbt_Node node);
cbt_Node cbt_SiblingNode(in const cbt_Node node);
cbt_Node cbt_SiblingNode_Fast(in const cbt_Node node);
cbt_Node cbt_LeftSiblingNode(in const cbt_Node node);
cbt_Node cbt_LeftSiblingNode_Fast(in const cbt_Node node);
cbt_Node cbt_RightSiblingNode(in const cbt_Node node);
cbt_Node cbt_RightSiblingNode_Fast(in const cbt_Node node);
cbt_Node cbt_LeftChildNode(in const cbt_Node node);
cbt_Node cbt_LeftChildNode_Fast(in const cbt_Node node);
cbt_Node cbt_RightChildNode(in const cbt_Node node);
cbt_Node cbt_RightChildNode_Fast(in const cbt_Node node);

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

uint cbt__GetBitValue(uint bitField, uint bitID) {
    return ((bitField >> bitID) & 1u);
}

void cbt__SetBitValue(uint bufferID, uint bitID, uint bitValue) {
    const uint bitMask = ~(1u << bitID);
    atomicAnd(cbtBuffer.heap[bufferID], bitMask);
    atomicOr(cbtBuffer.heap[bufferID], bitValue << bitID);
}

void cbt__BitFieldInsert(uint bufferID, uint bitOffset, uint bitCount, uint bitData) {
    uint bitMask = ~(~(0xFFFFFFFFu << bitCount) << bitOffset);
    atomicAnd(cbtBuffer.heap[bufferID], bitMask);
    atomicOr(cbtBuffer.heap[bufferID], bitData << bitOffset);
}

uint cbt__BitFieldExtract(uint bitField, uint bitOffset, uint bitCount) {
    uint bitMask = ~(0xFFFFFFFFu << bitCount);
    return (bitField >> bitOffset) & bitMask;
}

bool cbt_IsCeilNode(in const cbt_Node node) {
    return (node.depth == cbt_MaxDepth());
}

bool cbt_IsRootNode(in const cbt_Node node) {
    return (node.id == 1u);
}

bool cbt_IsNullNode(in const cbt_Node node) {
    return (node.id == 0u);
}

cbt_Node cbt_CreateNode(uint id) {
    cbt_Node node;
    node.id = id;
    node.depth = findMSB(id);
    return node;
}

cbt_Node cbt_CreateNode_Explicit(uint id, int depth) {
    cbt_Node node;
    node.id = id;
    node.depth = depth;
    return node;
}

cbt_Node cbt_ParentNode_Fast(in const cbt_Node node) {
    return cbt_CreateNode_Explicit(node.id >> 1, node.depth - 1);
}

cbt_Node cbt_ParentNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt_ParentNode_Fast(node);
}

cbt_Node cbt__CeilNode_Fast(in const cbt_Node node) {
    int maxDepth = cbt_MaxDepth();
    return cbt_CreateNode_Explicit(node.id << (maxDepth - node.depth), maxDepth);
}

cbt_Node cbt__CeilNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt__CeilNode_Fast(node);
}

cbt_Node cbt_SiblingNode_Fast(in const cbt_Node node) {
    return cbt_CreateNode_Explicit(node.id ^ 1u, node.depth);
}

cbt_Node cbt_SiblingNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt_SiblingNode_Fast(node);
}

cbt_Node cbt_RightSiblingNode_Fast(in const cbt_Node node) {
    return cbt_CreateNode_Explicit(node.id | 1u, node.depth);
}

cbt_Node cbt_RightSiblingNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt_RightSiblingNode_Fast(node);
}

cbt_Node cbt_LeftSiblingNode_Fast(in const cbt_Node node) {
    return cbt_CreateNode_Explicit(node.id & (~1u), node.depth);
}

cbt_Node cbt_LeftSiblingNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt_LeftSiblingNode_Fast(node);
}

cbt_Node cbt_RightChildNode_Fast(in const cbt_Node node) {
    return cbt_CreateNode_Explicit((node.id << 1) | 1u, node.depth + 1);
}

cbt_Node cbt_RightChildNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt_RightChildNode_Fast(node);
}

cbt_Node cbt_LeftChildNode_Fast(in const cbt_Node node) {
    return cbt_CreateNode_Explicit(node.id << 1, node.depth + 1);
}

cbt_Node cbt_LeftChildNode(in const cbt_Node node) {
    return cbt_IsNullNode(node) ? node : cbt_LeftChildNode_Fast(node);
}

uint cbt__HeapByteSize(uint cbtMaxDepth) {
    return 1u << (cbtMaxDepth - 1);
}

uint cbt__HeapUint32Size(uint cbtMaxDepth) {
    return cbt__HeapByteSize(cbtMaxDepth) >> 2;
}

uint cbt__NodeBitID(in const cbt_Node node) {
    uint tmp1 = 2u << node.depth;
    uint tmp2 = uint(1 + cbt_MaxDepth() - node.depth);
    return tmp1 + node.id * tmp2;
}

uint cbt__NodeBitID_BitField(in const cbt_Node node) {
    return cbt__NodeBitID(cbt__CeilNode(node));
}

int cbt__NodeBitSize(in const cbt_Node node) {
    return cbt_MaxDepth() - node.depth + 1;
}

struct cbt__HeapArgs {
    uint heapIndexLSB, heapIndexMSB;
    uint bitOffsetLSB;
    uint bitCountLSB, bitCountMSB;
};

cbt__HeapArgs cbt__CreateHeapArgs(in const cbt_Node node, int bitCount) {
    uint alignedBitOffset = cbt__NodeBitID(node);
    uint maxHeapIndex = cbt__HeapUint32Size(uint(cbt_MaxDepth())) - 1u;
    uint heapIndexLSB = (alignedBitOffset >> 5u);
    uint heapIndexMSB = min(heapIndexLSB + 1, maxHeapIndex);
    cbt__HeapArgs args;

    args.bitOffsetLSB = alignedBitOffset & 31u;
    args.bitCountLSB = min(32u - args.bitOffsetLSB, uint(bitCount));
    args.bitCountMSB = uint(bitCount) - args.bitCountLSB;
    args.heapIndexLSB = heapIndexLSB;
    args.heapIndexMSB = heapIndexMSB;

    return args;
}

void cbt__HeapWriteExplicit(in const cbt_Node node, int bitCount, uint bitData) {
    cbt__HeapArgs args = cbt__CreateHeapArgs(node, bitCount);

    cbt__BitFieldInsert(args.heapIndexLSB, args.bitOffsetLSB, args.bitCountLSB, bitData);
    cbt__BitFieldInsert(args.heapIndexMSB, 0u, args.bitCountMSB, bitData >> args.bitCountLSB);
}

void cbt__HeapWrite(in const cbt_Node node, uint bitData) {
    cbt__HeapWriteExplicit(node, cbt__NodeBitSize(node), bitData);
}

uint cbt__HeapReadExplicit(in const cbt_Node node, int bitCount) {
    cbt__HeapArgs args = cbt__CreateHeapArgs(node, bitCount);
    uint lsb = cbt__BitFieldExtract(cbtBuffer.heap[args.heapIndexLSB], args.bitOffsetLSB, args.bitCountLSB);
    uint msb = cbt__BitFieldExtract(cbtBuffer.heap[args.heapIndexMSB], 0u, args.bitCountMSB);

    return (lsb | (msb << args.bitCountLSB));
}

uint cbt_HeapRead(in const cbt_Node node) {
    return cbt__HeapReadExplicit(node, cbt__NodeBitSize(node));
}

void cbt__HeapWrite_BitField(in const cbt_Node node, uint bitValue) {
    uint bitID = cbt__NodeBitID_BitField(node);
    cbt__SetBitValue(bitID >> 5u, bitID & 31u, bitValue);
}

uint cbt__HeapRead_BitField(in const cbt_Node node) {
    uint bitID = cbt__NodeBitID_BitField(node);
    return cbt__GetBitValue(cbtBuffer.heap[bitID >> 5u], bitID & 31u);
}

bool cbt_IsLeafNode(in const cbt_Node node) {
    return (cbt_HeapRead(node) == 1u);
}

void cbt_SplitNode_Fast(in const cbt_Node node) {
    cbt__HeapWrite_BitField(cbt_RightChildNode(node), 1u);
}

void cbt_SplitNode(in const cbt_Node node) {
    if (!cbt_IsCeilNode(node))
        cbt_SplitNode_Fast(node);
}

void cbt_MergeNode_Fast(in const cbt_Node node) {
    cbt__HeapWrite_BitField(cbt_RightSiblingNode(node), 0u);
}

void cbt_MergeNode(in const cbt_Node node) {
    if (!cbt_IsRootNode(node))
        cbt_MergeNode_Fast(node);
}

int cbt_MaxDepth() {
    return findLSB(cbtBuffer.heap[0]);
}

uint cbt_NodeCount() {
    return cbt_HeapRead(cbt_CreateNode_Explicit(1u, 0));
}

cbt_Node cbt_DecodeNode(uint nodeID) {
    cbt_Node node = cbt_CreateNode_Explicit(1u, 0);

    while (cbt_HeapRead(node) > 1u) {
        cbt_Node leftChild = cbt_LeftChildNode_Fast(node);
        uint cmp = cbt_HeapRead(leftChild);
        uint b = nodeID < cmp ? 0u : 1u;

        node = leftChild;
        node.id |= b;
        nodeID -= cmp * b;
    }

    return node;
}

uint cbt_EncodeNode(in const cbt_Node node) {
    uint nodeID = 0u;
    cbt_Node nodeIterator = node;

    while (nodeIterator.id > 1u) {
        cbt_Node sibling = cbt_LeftSiblingNode_Fast(nodeIterator);
        uint nodeCount = cbt_HeapRead(sibling);

        nodeID += (nodeIterator.id & 1u) * nodeCount;
        nodeIterator = cbt_ParentNode(nodeIterator);
    }

    return nodeID;
}
