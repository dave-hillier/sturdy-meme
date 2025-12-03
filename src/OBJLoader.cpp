#include "OBJLoader.h"
#include <SDL.h>
#include <fstream>
#include <sstream>
#include <map>
#include <limits>

namespace OBJLoader {

CatmullClarkMesh loadQuadMesh(const std::string& path) {
    CatmullClarkMesh mesh;

    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open OBJ file: %s", path.c_str());
        return mesh;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<std::vector<uint32_t>> faceVertexIndices;
    std::vector<std::vector<uint32_t>> faceNormalIndices;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        } else if (prefix == "vn") {
            glm::vec3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            normals.push_back(norm);
        } else if (prefix == "f") {
            std::vector<uint32_t> faceVerts;
            std::vector<uint32_t> faceNorms;
            std::string vertexData;
            while (iss >> vertexData) {
                uint32_t vIdx = 0, vtIdx = 0, vnIdx = 0;
                size_t slash1 = vertexData.find('/');
                if (slash1 == std::string::npos) {
                    vIdx = std::stoi(vertexData);
                } else {
                    vIdx = std::stoi(vertexData.substr(0, slash1));
                    size_t slash2 = vertexData.find('/', slash1 + 1);
                    if (slash2 != std::string::npos) {
                        if (slash2 > slash1 + 1) {
                            vtIdx = std::stoi(vertexData.substr(slash1 + 1, slash2 - slash1 - 1));
                        }
                        if (slash2 + 1 < vertexData.size()) {
                            vnIdx = std::stoi(vertexData.substr(slash2 + 1));
                        }
                    } else {
                        vtIdx = std::stoi(vertexData.substr(slash1 + 1));
                    }
                }
                faceVerts.push_back(vIdx - 1);
                if (vnIdx > 0) {
                    faceNorms.push_back(vnIdx - 1);
                }
            }
            if (faceVerts.size() >= 3) {
                faceVertexIndices.push_back(faceVerts);
                faceNormalIndices.push_back(faceNorms);
            }
        }
    }

    // Center the mesh at origin and normalize scale
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    for (const auto& pos : positions) {
        minBounds = glm::min(minBounds, pos);
        maxBounds = glm::max(maxBounds, pos);
    }
    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float maxExtent = glm::max(maxBounds.x - minBounds.x,
                               glm::max(maxBounds.y - minBounds.y, maxBounds.z - minBounds.z));
    float scale = (maxExtent > 0.0001f) ? 2.0f / maxExtent : 1.0f;

    for (auto& pos : positions) {
        pos = (pos - center) * scale;
    }

    // Build vertices with averaged normals per position
    std::vector<glm::vec3> avgNormals(positions.size(), glm::vec3(0.0f));
    for (size_t f = 0; f < faceVertexIndices.size(); ++f) {
        const auto& faceVerts = faceVertexIndices[f];
        const auto& faceNorms = faceNormalIndices[f];
        for (size_t i = 0; i < faceVerts.size(); ++i) {
            if (i < faceNorms.size() && faceNorms[i] < normals.size()) {
                avgNormals[faceVerts[i]] += normals[faceNorms[i]];
            }
        }
    }
    for (auto& n : avgNormals) {
        if (glm::length(n) > 0.0001f) {
            n = glm::normalize(n);
        }
    }

    // Create vertices
    for (size_t i = 0; i < positions.size(); ++i) {
        CatmullClarkMesh::Vertex v;
        v.position = positions[i];
        v.normal = avgNormals[i];
        v.uv = glm::vec2(0.0f);
        mesh.vertices.push_back(v);
    }

    // Build halfedge structure
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgeToHalfedge;

    for (size_t f = 0; f < faceVertexIndices.size(); ++f) {
        const auto& faceVerts = faceVertexIndices[f];
        uint32_t faceIdx = static_cast<uint32_t>(mesh.faces.size());
        uint32_t firstHalfedge = static_cast<uint32_t>(mesh.halfedges.size());
        uint32_t valence = static_cast<uint32_t>(faceVerts.size());

        for (size_t i = 0; i < faceVerts.size(); ++i) {
            uint32_t v0 = faceVerts[i];
            uint32_t v1 = faceVerts[(i + 1) % faceVerts.size()];
            uint32_t heIdx = static_cast<uint32_t>(mesh.halfedges.size());
            uint32_t nextIdx = firstHalfedge + static_cast<uint32_t>((i + 1) % faceVerts.size());

            CatmullClarkMesh::Halfedge he;
            he.vertexID = v0;
            he.nextID = nextIdx;
            he.twinID = ~0u;
            he.faceID = faceIdx;
            mesh.halfedges.push_back(he);

            edgeToHalfedge[{v0, v1}] = heIdx;
        }

        CatmullClarkMesh::Face face;
        face.halfedgeID = firstHalfedge;
        face.valence = valence;
        mesh.faces.push_back(face);
    }

    // Link twin halfedges
    for (auto& [edge, heIdx] : edgeToHalfedge) {
        auto twinEdge = std::make_pair(edge.second, edge.first);
        auto it = edgeToHalfedge.find(twinEdge);
        if (it != edgeToHalfedge.end()) {
            mesh.halfedges[heIdx].twinID = it->second;
        }
    }

    SDL_Log("Loaded OBJ: %zu vertices, %zu halfedges, %zu faces from %s",
            mesh.vertices.size(), mesh.halfedges.size(), mesh.faces.size(), path.c_str());

    return mesh;
}

}
