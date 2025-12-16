#include "CatmullClarkMesh.h"
#include <SDL3/SDL.h>
#include <cstring>

bool CatmullClarkMesh::uploadToGPU(VmaAllocator allocator) {
    // Upload vertices
    if (!ManagedBuffer::createStorageHostWritable(allocator, vertices.size() * sizeof(Vertex), vertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create vertex buffer for Catmull-Clark mesh");
        return false;
    }
    void* data = vertexBuffer_.map();
    memcpy(data, vertices.data(), vertices.size() * sizeof(Vertex));
    vertexBuffer_.unmap();

    // Upload halfedges
    if (!ManagedBuffer::createStorageHostWritable(allocator, halfedges.size() * sizeof(Halfedge), halfedgeBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create halfedge buffer for Catmull-Clark mesh");
        return false;
    }
    data = halfedgeBuffer_.map();
    memcpy(data, halfedges.data(), halfedges.size() * sizeof(Halfedge));
    halfedgeBuffer_.unmap();

    // Upload faces
    if (!ManagedBuffer::createStorageHostWritable(allocator, faces.size() * sizeof(Face), faceBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create face buffer for Catmull-Clark mesh");
        return false;
    }
    data = faceBuffer_.map();
    memcpy(data, faces.data(), faces.size() * sizeof(Face));
    faceBuffer_.unmap();

    SDL_Log("Catmull-Clark mesh uploaded: %zu vertices, %zu halfedges, %zu faces",
            vertices.size(), halfedges.size(), faces.size());

    return true;
}

void CatmullClarkMesh::destroy() {
    vertexBuffer_.destroy();
    halfedgeBuffer_.destroy();
    faceBuffer_.destroy();
}

CatmullClarkMesh CatmullClarkMesh::createCube() {
    CatmullClarkMesh mesh;

    // Cube vertices (8 vertices)
    mesh.vertices = {
        {{-1, -1, -1}, {0, 0, -1}, {0, 0}},  // 0: back-bottom-left
        {{ 1, -1, -1}, {0, 0, -1}, {1, 0}},  // 1: back-bottom-right
        {{ 1,  1, -1}, {0, 0, -1}, {1, 1}},  // 2: back-top-right
        {{-1,  1, -1}, {0, 0, -1}, {0, 1}},  // 3: back-top-left
        {{-1, -1,  1}, {0, 0,  1}, {0, 0}},  // 4: front-bottom-left
        {{ 1, -1,  1}, {0, 0,  1}, {1, 0}},  // 5: front-bottom-right
        {{ 1,  1,  1}, {0, 0,  1}, {1, 1}},  // 6: front-top-right
        {{-1,  1,  1}, {0, 0,  1}, {0, 1}},  // 7: front-top-left
    };

    // 6 faces, 4 halfedges per face = 24 halfedges
    // Face order: front, back, right, left, top, bottom

    // Front face (4, 5, 6, 7)
    mesh.halfedges.push_back({4, 1, 20, 0});   // 0
    mesh.halfedges.push_back({5, 2, 8, 0});    // 1
    mesh.halfedges.push_back({6, 3, 16, 0});   // 2
    mesh.halfedges.push_back({7, 0, 12, 0});   // 3

    // Back face (0, 3, 2, 1)
    mesh.halfedges.push_back({0, 5, 21, 1});   // 4
    mesh.halfedges.push_back({3, 6, 13, 1});   // 5
    mesh.halfedges.push_back({2, 7, 17, 1});   // 6
    mesh.halfedges.push_back({1, 4, 9, 1});    // 7

    // Right face (1, 2, 6, 5)
    mesh.halfedges.push_back({1, 9, 1, 2});    // 8
    mesh.halfedges.push_back({2, 10, 7, 2});   // 9
    mesh.halfedges.push_back({6, 11, 18, 2});  // 10
    mesh.halfedges.push_back({5, 8, 22, 2});   // 11

    // Left face (4, 7, 3, 0)
    mesh.halfedges.push_back({4, 13, 3, 3});   // 12
    mesh.halfedges.push_back({7, 14, 5, 3});   // 13
    mesh.halfedges.push_back({3, 15, 19, 3});  // 14
    mesh.halfedges.push_back({0, 12, 23, 3});  // 15

    // Top face (7, 6, 2, 3)
    mesh.halfedges.push_back({7, 17, 2, 4});   // 16
    mesh.halfedges.push_back({6, 18, 6, 4});   // 17
    mesh.halfedges.push_back({2, 19, 10, 4});  // 18
    mesh.halfedges.push_back({3, 16, 14, 4});  // 19

    // Bottom face (4, 0, 1, 5)
    mesh.halfedges.push_back({4, 21, 0, 5});   // 20
    mesh.halfedges.push_back({0, 22, 4, 5});   // 21
    mesh.halfedges.push_back({1, 23, 11, 5});  // 22
    mesh.halfedges.push_back({5, 20, 15, 5});  // 23

    // 6 faces (all quads)
    for (int i = 0; i < 6; ++i) {
        mesh.faces.push_back({static_cast<uint32_t>(i * 4), 4});
    }

    return mesh;
}

CatmullClarkMesh CatmullClarkMesh::createQuad() {
    CatmullClarkMesh mesh;

    // Single quad (4 vertices)
    mesh.vertices = {
        {{-1, 0, -1}, {0, 1, 0}, {0, 0}},  // 0
        {{ 1, 0, -1}, {0, 1, 0}, {1, 0}},  // 1
        {{ 1, 0,  1}, {0, 1, 0}, {1, 1}},  // 2
        {{-1, 0,  1}, {0, 1, 0}, {0, 1}},  // 3
    };

    // 4 halfedges for single face
    mesh.halfedges = {
        {0, 1, ~0u, 0},
        {1, 2, ~0u, 0},
        {2, 3, ~0u, 0},
        {3, 0, ~0u, 0},
    };

    // 1 face
    mesh.faces = {{0, 4}};

    return mesh;
}
