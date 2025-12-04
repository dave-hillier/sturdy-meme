#include "FBXLoader.h"
#include "SkinnedMesh.h"
#include "Animation.h"
#include <SDL3/SDL_log.h>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <memory>
#include <ofbx.h>

namespace FBXLoader {

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

glm::mat4 convertMatrix(const ofbx::DMatrix& m) {
    // OpenFBX uses row-major, GLM uses column-major
    return glm::mat4(
        static_cast<float>(m.m[0]),  static_cast<float>(m.m[1]),  static_cast<float>(m.m[2]),  static_cast<float>(m.m[3]),
        static_cast<float>(m.m[4]),  static_cast<float>(m.m[5]),  static_cast<float>(m.m[6]),  static_cast<float>(m.m[7]),
        static_cast<float>(m.m[8]),  static_cast<float>(m.m[9]),  static_cast<float>(m.m[10]), static_cast<float>(m.m[11]),
        static_cast<float>(m.m[12]), static_cast<float>(m.m[13]), static_cast<float>(m.m[14]), static_cast<float>(m.m[15])
    );
}

glm::vec3 convertVec3(const ofbx::DVec3& v) {
    return glm::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

glm::vec3 convertVec3(const ofbx::Vec3& v) {
    return glm::vec3(v.x, v.y, v.z);
}

glm::vec2 convertVec2(const ofbx::Vec2& v) {
    return glm::vec2(v.x, v.y);
}

// Convert Euler angles (degrees) to quaternion
// FBX uses XYZ intrinsic rotation order (rotate X, then Y, then Z in local space)
// This is equivalent to ZYX extrinsic rotation order
glm::quat eulerToQuat(const glm::vec3& eulerDeg) {
    glm::vec3 eulerRad = glm::radians(eulerDeg);
    // Build quaternion with XYZ intrinsic order: Q = Qz * Qy * Qx
    glm::quat qX = glm::angleAxis(eulerRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qY = glm::angleAxis(eulerRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat qZ = glm::angleAxis(eulerRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return qZ * qY * qX;
}

// Strip Mixamo bone name prefix
std::string normalizeBoneName(const char* name) {
    if (!name) return "";
    std::string str(name);
    const std::string prefix = "mixamorig:";
    if (str.find(prefix) == 0) {
        return str.substr(prefix.length());
    }
    return str;
}

// Calculate tangents for skinned vertices
void calculateTangents(std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices) {
    for (auto& v : vertices) {
        v.tangent = glm::vec4(0.0f);
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const glm::vec3& p0 = vertices[i0].position;
        const glm::vec3& p1 = vertices[i1].position;
        const glm::vec3& p2 = vertices[i2].position;

        const glm::vec2& uv0 = vertices[i0].texCoord;
        const glm::vec2& uv1 = vertices[i1].texCoord;
        const glm::vec2& uv2 = vertices[i2].texCoord;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;

        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(det) < 1e-8f) continue;

        float f = 1.0f / det;
        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        vertices[i0].tangent += glm::vec4(tangent, 0.0f);
        vertices[i1].tangent += glm::vec4(tangent, 0.0f);
        vertices[i2].tangent += glm::vec4(tangent, 0.0f);
    }

    for (auto& v : vertices) {
        glm::vec3 t = glm::vec3(v.tangent);
        if (glm::length(t) > 1e-8f) {
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));
            v.tangent = glm::vec4(t, 1.0f);
        } else {
            glm::vec3 up = std::abs(v.normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            v.tangent = glm::vec4(glm::normalize(glm::cross(up, v.normal)), 1.0f);
        }
    }
}

// FBX time to seconds conversion
double fbxTimeToSeconds(ofbx::i64 fbxTime) {
    // FBX time is in 1/46186158000 of a second units
    return static_cast<double>(fbxTime) / 46186158000.0;
}

// Custom deleter for ofbx::IScene that uses destroy() method
struct SceneDeleter {
    void operator()(ofbx::IScene* scene) const {
        if (scene) {
            scene->destroy();
        }
    }
};

using ScenePtr = std::unique_ptr<ofbx::IScene, SceneDeleter>;

// Extract texture path from FBX texture object
std::string getTexturePath(const ofbx::Texture* texture, const std::string& fbxDirectory) {
    if (!texture) return "";

    // Try relative filename first (usually what we want)
    ofbx::DataView relPath = texture->getRelativeFileName();
    if (relPath.begin && relPath.end > relPath.begin) {
        std::string path(reinterpret_cast<const char*>(relPath.begin),
                         static_cast<size_t>(relPath.end - relPath.begin));
        // Clean up the path - remove null terminator if present
        size_t nullPos = path.find('\0');
        if (nullPos != std::string::npos) {
            path = path.substr(0, nullPos);
        }
        if (!path.empty()) {
            // Make absolute if relative
            if (path[0] != '/' && path.find(':') == std::string::npos) {
                return fbxDirectory + "/" + path;
            }
            return path;
        }
    }

    // Fallback to absolute filename
    ofbx::DataView absPath = texture->getFileName();
    if (absPath.begin && absPath.end > absPath.begin) {
        std::string path(reinterpret_cast<const char*>(absPath.begin),
                         static_cast<size_t>(absPath.end - absPath.begin));
        size_t nullPos = path.find('\0');
        if (nullPos != std::string::npos) {
            path = path.substr(0, nullPos);
        }
        return path;
    }

    return "";
}

// Get directory from file path
std::string getDirectory(const std::string& path) {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash);
    }
    return ".";
}

// Sanitize float value - clamp to valid range and replace NaN/Inf
float sanitizeFloat(float value, float defaultVal = 0.0f, float minVal = 0.0f, float maxVal = 1.0f) {
    if (std::isnan(value) || std::isinf(value)) {
        return defaultVal;
    }
    return std::clamp(value, minVal, maxVal);
}

// Convert ofbx::Color to glm::vec3, sanitizing values
glm::vec3 convertColor(const ofbx::Color& c) {
    return glm::vec3(
        sanitizeFloat(static_cast<float>(c.r), 0.0f, 0.0f, 1.0f),
        sanitizeFloat(static_cast<float>(c.g), 0.0f, 0.0f, 1.0f),
        sanitizeFloat(static_cast<float>(c.b), 0.0f, 0.0f, 1.0f)
    );
}

// Extract material info from FBX material
MaterialInfo extractMaterialInfo(const ofbx::Material* mat, const std::string& fbxDirectory) {
    MaterialInfo info;

    if (!mat) {
        return info;
    }

    // Name
    if (mat->name[0] != '\0') {
        info.name = mat->name;
    }

    // Colors
    info.diffuseColor = convertColor(mat->getDiffuseColor());
    info.specularColor = convertColor(mat->getSpecularColor());
    info.emissiveColor = convertColor(mat->getEmissiveColor());

    // PBR properties
    // Convert shininess to roughness - use a sensible default for characters
    // Most FBX files from older software don't have proper PBR values
    // Typical Blinn-Phong shininess range: 10-1000 for meaningful specular
    double shininess = mat->getShininess();
    // Sanitize shininess - some FBX files return garbage
    if (std::isnan(shininess) || std::isinf(shininess) || shininess < 0.0) {
        shininess = 0.0;
    }
    if (shininess > 10.0) {
        // Meaningful shininess value - convert to roughness
        // Map shininess 10-500 to roughness 0.7-0.1
        float normalizedShininess = std::min(1.0f, static_cast<float>(shininess - 10.0) / 490.0f);
        info.roughness = std::max(0.1f, 0.7f - normalizedShininess * 0.6f);
    } else {
        // Low or no shininess data - use a reasonable default (slightly glossy skin/plastic look)
        info.roughness = 0.5f;
    }

    // Derive metallic from specular color intensity
    float specularIntensity = (info.specularColor.r + info.specularColor.g + info.specularColor.b) / 3.0f;
    if (specularIntensity > 0.3f) {
        // Moderate specular suggests some metallic quality
        info.metallic = std::min(0.5f, (specularIntensity - 0.3f));
    }

    // OpenFBX doesn't expose opacity directly, default to 1.0
    info.opacity = 1.0f;

    // Sanitize emissive factor - some FBX files return garbage values
    double rawEmissiveFactor = mat->getEmissiveFactor();
    if (std::isnan(rawEmissiveFactor) || std::isinf(rawEmissiveFactor) || rawEmissiveFactor < 0.0) {
        info.emissiveFactor = 0.0f;
    } else {
        info.emissiveFactor = std::min(static_cast<float>(rawEmissiveFactor), 100.0f);
    }

    // Texture paths
    const ofbx::Texture* diffuseTex = mat->getTexture(ofbx::Texture::TextureType::DIFFUSE);
    info.diffuseTexturePath = getTexturePath(diffuseTex, fbxDirectory);

    const ofbx::Texture* normalTex = mat->getTexture(ofbx::Texture::TextureType::NORMAL);
    info.normalTexturePath = getTexturePath(normalTex, fbxDirectory);

    const ofbx::Texture* specularTex = mat->getTexture(ofbx::Texture::TextureType::SPECULAR);
    info.specularTexturePath = getTexturePath(specularTex, fbxDirectory);

    const ofbx::Texture* emissiveTex = mat->getTexture(ofbx::Texture::TextureType::EMISSIVE);
    info.emissiveTexturePath = getTexturePath(emissiveTex, fbxDirectory);

    return info;
}

} // anonymous namespace

std::optional<GLTFSkinnedLoadResult> loadSkinned(const std::string& path) {
    auto fileData = readFile(path);
    if (fileData.empty()) {
        SDL_Log("FBXLoader: Failed to read file: %s", path.c_str());
        return std::nullopt;
    }

    ScenePtr scene(ofbx::load(
        fileData.data(),
        static_cast<ofbx::usize>(fileData.size()),
        static_cast<ofbx::u16>(ofbx::LoadFlags::NONE)
    ));

    if (!scene) {
        SDL_Log("FBXLoader: Failed to parse FBX: %s", path.c_str());
        return std::nullopt;
    }

    // Get directory for resolving relative texture paths
    std::string fbxDirectory = getDirectory(path);

    GLTFSkinnedLoadResult result;

    // Build bone mapping from all skins/clusters
    std::unordered_map<const ofbx::Object*, int32_t> boneToIndex;
    std::vector<const ofbx::Object*> boneObjects;

    // First pass: collect all bones from skin clusters
    int meshCount = scene->getMeshCount();
    for (int meshIdx = 0; meshIdx < meshCount; ++meshIdx) {
        const ofbx::Mesh* mesh = scene->getMesh(meshIdx);
        const ofbx::Skin* skin = mesh->getSkin();

        if (!skin) continue;

        int clusterCount = skin->getClusterCount();
        for (int i = 0; i < clusterCount; ++i) {
            const ofbx::Cluster* cluster = skin->getCluster(i);
            const ofbx::Object* bone = cluster->getLink();

            if (bone && boneToIndex.find(bone) == boneToIndex.end()) {
                int32_t index = static_cast<int32_t>(boneObjects.size());
                boneToIndex[bone] = index;
                boneObjects.push_back(bone);
            }
        }
    }

    // Build skeleton from collected bones
    // We'll fill in the transforms later from cluster data
    result.skeleton.joints.resize(boneObjects.size());

    // Store global bind pose transforms (will compute local from these)
    std::vector<glm::mat4> globalBindPose(boneObjects.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < boneObjects.size(); ++i) {
        const ofbx::Object* bone = boneObjects[i];

        Joint& joint = result.skeleton.joints[i];
        joint.name = normalizeBoneName(bone->name);
        joint.parentIndex = -1;

        // Find parent
        const ofbx::Object* parent = bone->getParent();
        if (parent) {
            auto it = boneToIndex.find(parent);
            if (it != boneToIndex.end()) {
                joint.parentIndex = it->second;
            }
        }

        // Get FBX pre-rotation (affects how animated rotations are applied)
        ofbx::DVec3 preRotDeg = bone->getPreRotation();
        glm::vec3 preRotVec = convertVec3(preRotDeg);
        joint.preRotation = eulerToQuat(preRotVec);

        // Initialize with identity - will be set from cluster data
        joint.localTransform = glm::mat4(1.0f);
        joint.inverseBindMatrix = glm::mat4(1.0f);
    }

    SDL_Log("FBXLoader: Found %zu bones", boneObjects.size());

    // Process meshes
    for (int meshIdx = 0; meshIdx < meshCount; ++meshIdx) {
        const ofbx::Mesh* mesh = scene->getMesh(meshIdx);
        const ofbx::GeometryData& geomData = mesh->getGeometryData();

        if (!geomData.hasVertices()) {
            continue;
        }

        // Get geometry attributes
        ofbx::Vec3Attributes positions = geomData.getPositions();
        ofbx::Vec3Attributes normals = geomData.getNormals();
        ofbx::Vec2Attributes uvs = geomData.getUVs();
        ofbx::Vec3Attributes tangentsAttr = geomData.getTangents();

        int partitionCount = geomData.getPartitionCount();
        if (partitionCount == 0) {
            continue;
        }

        // Initialize bone weight storage per position value index
        std::vector<std::vector<std::pair<int32_t, float>>> vertexBoneWeights(positions.values_count);

        // Load bone weights from skin
        const ofbx::Skin* skin = mesh->getSkin();
        if (skin) {
            int clusterCount = skin->getClusterCount();
            for (int clusterIdx = 0; clusterIdx < clusterCount; ++clusterIdx) {
                const ofbx::Cluster* cluster = skin->getCluster(clusterIdx);
                const ofbx::Object* bone = cluster->getLink();

                if (!bone) continue;

                auto boneIt = boneToIndex.find(bone);
                if (boneIt == boneToIndex.end()) continue;

                int32_t boneIndex = boneIt->second;

                // Get bind pose transforms from cluster
                // TransformLinkMatrix is the global transform of the bone at bind time
                ofbx::DMatrix transformLink = cluster->getTransformLinkMatrix();
                glm::mat4 globalBind = convertMatrix(transformLink);
                globalBindPose[boneIndex] = globalBind;
                result.skeleton.joints[boneIndex].inverseBindMatrix = glm::inverse(globalBind);

                // Get weights
                int weightCount = cluster->getIndicesCount();
                const int* indices = cluster->getIndices();
                const double* weights = cluster->getWeights();

                for (int w = 0; w < weightCount; ++w) {
                    int vertIdx = indices[w];
                    float weight = static_cast<float>(weights[w]);
                    if (vertIdx >= 0 && vertIdx < positions.values_count && weight > 0.0001f) {
                        vertexBoneWeights[vertIdx].push_back({boneIndex, weight});
                    }
                }
            }
        }

        // Sort and limit bone influences to 4 per vertex
        for (auto& boneWeights : vertexBoneWeights) {
            std::sort(boneWeights.begin(), boneWeights.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            if (boneWeights.size() > 4) {
                boneWeights.resize(4);
            }
        }

        // Extract materials from mesh
        int materialCount = mesh->getMaterialCount();
        std::vector<MaterialInfo> meshMaterials;
        meshMaterials.reserve(materialCount);

        for (int matIdx = 0; matIdx < materialCount; ++matIdx) {
            const ofbx::Material* mat = mesh->getMaterial(matIdx);
            MaterialInfo matInfo = extractMaterialInfo(mat, fbxDirectory);
            meshMaterials.push_back(matInfo);

            SDL_Log("FBXLoader: Material %d '%s': diffuse=(%.2f, %.2f, %.2f) roughness=%.2f shininess=%.1f",
                    matIdx, matInfo.name.c_str(),
                    matInfo.diffuseColor.r, matInfo.diffuseColor.g, matInfo.diffuseColor.b,
                    matInfo.roughness, mat ? mat->getShininess() : 0.0);

            if (!matInfo.diffuseTexturePath.empty()) {
                SDL_Log("FBXLoader:   Diffuse texture: %s", matInfo.diffuseTexturePath.c_str());
            }
            if (!matInfo.normalTexturePath.empty()) {
                SDL_Log("FBXLoader:   Normal texture: %s", matInfo.normalTexturePath.c_str());
            }
        }

        // Process all partitions (material groups)
        for (int partIdx = 0; partIdx < partitionCount; ++partIdx) {
            ofbx::GeometryPartition partition = geomData.getPartition(partIdx);

            // Get material for this partition (partition index often corresponds to material index)
            glm::vec3 partitionDiffuseColor = glm::vec3(1.0f);
            if (partIdx < static_cast<int>(meshMaterials.size())) {
                partitionDiffuseColor = meshMaterials[partIdx].diffuseColor;

                // Track start index for this material's vertices
                meshMaterials[partIdx].startIndex = static_cast<uint32_t>(result.indices.size());
            }

            // Process each polygon in the partition
            for (int polyIdx = 0; polyIdx < partition.polygon_count; ++polyIdx) {
                const ofbx::GeometryPartition::Polygon& polygon = partition.polygons[polyIdx];

                // Allocate enough space for triangulated indices
                // max_polygon_triangles gives us the max triangles, each needs 3 indices
                std::vector<int> triIndices(partition.max_polygon_triangles * 3);
                ofbx::u32 numTriangles = ofbx::triangulate(geomData, polygon, triIndices.data());

                // triangulate returns the number of indices, not triangles
                // For a quad it returns 6 (2 triangles * 3 vertices each)
                ofbx::u32 numIndices = numTriangles;
                ofbx::u32 numTris = numIndices / 3;

                // Each triangle has 3 vertices
                for (ofbx::u32 tri = 0; tri < numTris; ++tri) {
                    for (int v = 0; v < 3; ++v) {
                        // triIndices already contains absolute vertex indices (from_vertex is added by triangulate)
                        int vertexIndex = triIndices[tri * 3 + v];

                        // Bounds check
                        if (vertexIndex < 0 || vertexIndex >= positions.count) {
                            SDL_Log("FBXLoader: Invalid vertex index %d (max %d)", vertexIndex, positions.count);
                            continue;
                        }

                        SkinnedVertex vertex{};

                        // Position - use the .get() accessor which handles indexed vs direct
                        vertex.position = convertVec3(positions.get(vertexIndex));

                        // Get position index for bone weight lookup
                        int posIdx = positions.indices ? positions.indices[vertexIndex] : vertexIndex;

                        // Normal
                        if (normals.values && normals.count > 0) {
                            vertex.normal = convertVec3(normals.get(vertexIndex));
                        } else {
                            vertex.normal = glm::vec3(0, 1, 0);
                        }

                        // UV
                        if (uvs.values && uvs.count > 0) {
                            vertex.texCoord = convertVec2(uvs.get(vertexIndex));
                            // Flip V coordinate (FBX uses bottom-left origin)
                            vertex.texCoord.y = 1.0f - vertex.texCoord.y;
                        } else {
                            vertex.texCoord = glm::vec2(0, 0);
                        }

                        // Tangent
                        if (tangentsAttr.values && tangentsAttr.count > 0) {
                            glm::vec3 t = convertVec3(tangentsAttr.get(vertexIndex));
                            vertex.tangent = glm::vec4(t, 1.0f);
                        }

                        // Bone weights - use position index for bone lookups
                        if (posIdx >= 0 && posIdx < static_cast<int>(vertexBoneWeights.size())) {
                            const auto& boneWeights = vertexBoneWeights[posIdx];
                            vertex.boneIndices = glm::uvec4(0);
                            vertex.boneWeights = glm::vec4(0.0f);

                            if (boneWeights.empty()) {
                                // No skinning - use negative weight marker
                                vertex.boneWeights = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
                            } else {
                                float totalWeight = 0.0f;
                                for (size_t j = 0; j < boneWeights.size() && j < 4; ++j) {
                                    vertex.boneIndices[j] = static_cast<uint32_t>(boneWeights[j].first);
                                    vertex.boneWeights[j] = boneWeights[j].second;
                                    totalWeight += boneWeights[j].second;
                                }
                                // Normalize weights
                                if (totalWeight > 0.0001f) {
                                    vertex.boneWeights /= totalWeight;
                                }
                            }
                        } else {
                            vertex.boneWeights = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
                        }

                        // Apply material diffuse color to vertex
                        vertex.color = glm::vec4(partitionDiffuseColor, 1.0f);

                        result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
                        result.vertices.push_back(vertex);
                    }
                }
            }

            // Update material index count after processing all polygons in this partition
            if (partIdx < static_cast<int>(meshMaterials.size())) {
                uint32_t endIndex = static_cast<uint32_t>(result.indices.size());
                meshMaterials[partIdx].indexCount = endIndex - meshMaterials[partIdx].startIndex;
            }
        }

        // Add mesh materials to result
        for (auto& mat : meshMaterials) {
            result.materials.push_back(std::move(mat));
        }
    }

    if (result.vertices.empty()) {
        SDL_Log("FBXLoader: No vertices loaded from %s", path.c_str());
        return std::nullopt;
    }

    // Compute local transforms from global bind poses
    // Local = Parent^-1 * Global
    for (size_t i = 0; i < result.skeleton.joints.size(); ++i) {
        Joint& joint = result.skeleton.joints[i];
        if (joint.parentIndex >= 0 && joint.parentIndex < static_cast<int32_t>(globalBindPose.size())) {
            glm::mat4 parentGlobal = globalBindPose[joint.parentIndex];
            glm::mat4 parentInverse = glm::inverse(parentGlobal);
            joint.localTransform = parentInverse * globalBindPose[i];
        } else {
            // Root bone - global is local
            joint.localTransform = globalBindPose[i];
        }

        // Debug: Print transforms and preRotation for first bones and arm-related bones
        bool isArmBone = joint.name.find("Shoulder") != std::string::npos ||
                         joint.name.find("Arm") != std::string::npos ||
                         joint.name.find("UpLeg") != std::string::npos;
        if (i < 5 || isArmBone) {
            glm::vec3 pos = glm::vec3(joint.localTransform[3]);
            glm::vec3 preRotEuler = glm::degrees(glm::eulerAngles(joint.preRotation));
            SDL_Log("FBXLoader: Bone %zu '%s' parent=%d local pos=(%.2f, %.2f, %.2f) preRot=(%.1f, %.1f, %.1f)",
                    i, joint.name.c_str(), joint.parentIndex, pos.x, pos.y, pos.z,
                    preRotEuler.x, preRotEuler.y, preRotEuler.z);
        }
    }

    // Calculate tangents if not present
    bool hasTangents = false;
    for (const auto& v : result.vertices) {
        if (glm::length(glm::vec3(v.tangent)) > 0.001f) {
            hasTangents = true;
            break;
        }
    }
    if (!hasTangents) {
        calculateTangents(result.vertices, result.indices);
    }

    // Load animations
    int animStackCount = scene->getAnimationStackCount();
    SDL_Log("FBXLoader: Found %d animation stacks", animStackCount);

    for (int stackIdx = 0; stackIdx < animStackCount; ++stackIdx) {
        const ofbx::AnimationStack* stack = scene->getAnimationStack(stackIdx);
        if (!stack) continue;

        const ofbx::AnimationLayer* layer = stack->getLayer(0);
        if (!layer) continue;

        AnimationClip clip;
        clip.name = stack->name;
        clip.duration = 0.0f;

        // Get animation time info
        const ofbx::TakeInfo* takeInfo = scene->getTakeInfo(stack->name);
        double localTimeFrom = 0.0;
        double localTimeTo = 0.0;
        if (takeInfo) {
            localTimeFrom = fbxTimeToSeconds(static_cast<ofbx::i64>(takeInfo->local_time_from));
            localTimeTo = fbxTimeToSeconds(static_cast<ofbx::i64>(takeInfo->local_time_to));
        }

        // Sample rate (30 FPS typical for Mixamo)
        const double fps = 30.0;
        const double frameTime = 1.0 / fps;

        // For each bone, extract animation curves
        for (size_t boneIdx = 0; boneIdx < boneObjects.size(); ++boneIdx) {
            const ofbx::Object* bone = boneObjects[boneIdx];

            // Try to get animation curve nodes for this bone
            const ofbx::AnimationCurveNode* transNode = layer->getCurveNode(*bone, "Lcl Translation");
            const ofbx::AnimationCurveNode* rotNode = layer->getCurveNode(*bone, "Lcl Rotation");
            const ofbx::AnimationCurveNode* scaleNode = layer->getCurveNode(*bone, "Lcl Scaling");

            if (!transNode && !rotNode && !scaleNode) {
                continue; // No animation for this bone
            }

            AnimationChannel channel;
            channel.jointIndex = static_cast<int32_t>(boneIdx);

            // Calculate animation duration
            double duration = localTimeTo - localTimeFrom;
            if (duration <= 0) {
                duration = 1.0; // Default 1 second
            }

            int numSamples = static_cast<int>(duration * fps) + 1;
            numSamples = std::max(2, std::min(numSamples, 1000)); // Clamp

            for (int s = 0; s < numSamples; ++s) {
                double t = localTimeFrom + (s * frameTime);
                float time = static_cast<float>(s * frameTime);

                if (transNode) {
                    ofbx::DVec3 trans = transNode->getNodeLocalTransform(t);
                    if (s == 0) {
                        channel.translation.times.reserve(numSamples);
                        channel.translation.values.reserve(numSamples);
                    }
                    channel.translation.times.push_back(time);
                    channel.translation.values.push_back(convertVec3(trans));
                }

                if (rotNode) {
                    ofbx::DVec3 rot = rotNode->getNodeLocalTransform(t);
                    glm::vec3 eulerDeg = convertVec3(rot);
                    glm::quat quat = eulerToQuat(eulerDeg);
                    if (s == 0) {
                        channel.rotation.times.reserve(numSamples);
                        channel.rotation.values.reserve(numSamples);
                    }
                    channel.rotation.times.push_back(time);
                    channel.rotation.values.push_back(quat);
                }

                if (scaleNode) {
                    ofbx::DVec3 scale = scaleNode->getNodeLocalTransform(t);
                    if (s == 0) {
                        channel.scale.times.reserve(numSamples);
                        channel.scale.values.reserve(numSamples);
                    }
                    channel.scale.times.push_back(time);
                    channel.scale.values.push_back(convertVec3(scale));
                }

                if (time > clip.duration) {
                    clip.duration = time;
                }
            }

            if (channel.hasTranslation() || channel.hasRotation() || channel.hasScale()) {
                clip.channels.push_back(channel);
            }
        }

        if (!clip.channels.empty()) {
            // Find root bone index (usually "Hips" for Mixamo)
            clip.rootBoneIndex = result.skeleton.findJointIndex("Hips");
            if (clip.rootBoneIndex < 0) {
                // Try alternative names
                clip.rootBoneIndex = result.skeleton.findJointIndex("Root");
            }

            // Calculate root motion per cycle from the root bone's translation channel
            if (clip.rootBoneIndex >= 0) {
                const AnimationChannel* rootChannel = clip.getChannelForJoint(clip.rootBoneIndex);
                if (rootChannel && rootChannel->hasTranslation() && !rootChannel->translation.values.empty()) {
                    glm::vec3 startPos = rootChannel->translation.values.front();
                    glm::vec3 endPos = rootChannel->translation.values.back();
                    clip.rootMotionPerCycle = endPos - startPos;
                }
            }

            SDL_Log("FBXLoader: Loaded animation '%s' with %zu channels, duration %.2fs, rootBone=%d, rootMotion=(%.2f, %.2f, %.2f)",
                    clip.name.c_str(), clip.channels.size(), clip.duration, clip.rootBoneIndex,
                    clip.rootMotionPerCycle.x, clip.rootMotionPerCycle.y, clip.rootMotionPerCycle.z);
            result.animations.push_back(std::move(clip));
        }
    }

    // Log mesh statistics
    glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
    int vertsWithWeights = 0;
    for (const auto& v : result.vertices) {
        minBounds = glm::min(minBounds, v.position);
        maxBounds = glm::max(maxBounds, v.position);
        float weightSum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
        if (weightSum > 0.99f) {
            vertsWithWeights++;
        }
    }

    SDL_Log("FBXLoader: Loaded %zu skinned vertices, %zu indices from %s",
            result.vertices.size(), result.indices.size(), path.c_str());
    SDL_Log("FBXLoader: %d/%zu vertices have bone weights",
            vertsWithWeights, result.vertices.size());
    SDL_Log("FBXLoader: Mesh bounds: min(%.2f, %.2f, %.2f) max(%.2f, %.2f, %.2f)",
            minBounds.x, minBounds.y, minBounds.z,
            maxBounds.x, maxBounds.y, maxBounds.z);
    SDL_Log("FBXLoader: Loaded %zu materials", result.materials.size());

    // Set legacy texture paths from first material (backward compatibility)
    if (!result.materials.empty()) {
        result.baseColorTexturePath = result.materials[0].diffuseTexturePath;
        result.normalTexturePath = result.materials[0].normalTexturePath;
    }

    return result;
}

std::optional<GLTFLoadResult> load(const std::string& path) {
    auto skinned = loadSkinned(path);
    if (!skinned) {
        return std::nullopt;
    }

    GLTFLoadResult result;
    result.vertices.reserve(skinned->vertices.size());

    // Convert SkinnedVertex to Vertex
    for (const auto& sv : skinned->vertices) {
        Vertex v;
        v.position = sv.position;
        v.normal = sv.normal;
        v.texCoord = sv.texCoord;
        v.tangent = sv.tangent;
        v.color = sv.color;
        result.vertices.push_back(v);
    }

    result.indices = std::move(skinned->indices);
    result.skeleton = std::move(skinned->skeleton);
    result.materials = std::move(skinned->materials);
    result.baseColorTexturePath = std::move(skinned->baseColorTexturePath);
    result.normalTexturePath = std::move(skinned->normalTexturePath);

    return result;
}

std::vector<AnimationClip> loadAnimations(const std::string& path, const Skeleton& skeleton) {
    std::vector<AnimationClip> result;

    auto fileData = readFile(path);
    if (fileData.empty()) {
        SDL_Log("FBXLoader: Failed to read animation file: %s", path.c_str());
        return result;
    }

    ScenePtr scene(ofbx::load(
        fileData.data(),
        static_cast<ofbx::usize>(fileData.size()),
        static_cast<ofbx::u16>(ofbx::LoadFlags::NONE)
    ));

    if (!scene) {
        SDL_Log("FBXLoader: Failed to parse animation FBX: %s", path.c_str());
        return result;
    }

    // Build bone name to index mapping from skeleton
    std::unordered_map<std::string, int32_t> boneNameToIndex;
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        boneNameToIndex[skeleton.joints[i].name] = static_cast<int32_t>(i);
    }

    // Collect bones from the animation file
    // First try via skin clusters (works for full character FBX)
    // Then fallback to getAllObjects for animation-only FBX files
    std::vector<const ofbx::Object*> boneObjects;
    std::unordered_map<const ofbx::Object*, int32_t> boneToIndex;

    int meshCount = scene->getMeshCount();
    for (int meshIdx = 0; meshIdx < meshCount; ++meshIdx) {
        const ofbx::Mesh* mesh = scene->getMesh(meshIdx);
        const ofbx::Skin* skin = mesh->getSkin();
        if (!skin) continue;

        int clusterCount = skin->getClusterCount();
        for (int i = 0; i < clusterCount; ++i) {
            const ofbx::Cluster* cluster = skin->getCluster(i);
            const ofbx::Object* bone = cluster->getLink();
            if (!bone || boneToIndex.find(bone) != boneToIndex.end()) continue;

            std::string boneName = normalizeBoneName(bone->name);
            auto it = boneNameToIndex.find(boneName);
            if (it != boneNameToIndex.end()) {
                boneToIndex[bone] = it->second;
                boneObjects.push_back(bone);
            }
        }
    }

    // Fallback: scan all objects for LIMB_NODE types (for animation-only FBX files)
    if (boneObjects.empty()) {
        const ofbx::Object* const* allObjects = scene->getAllObjects();
        int objectCount = scene->getAllObjectCount();

        for (int i = 0; i < objectCount; ++i) {
            const ofbx::Object* obj = allObjects[i];
            if (!obj) continue;

            // Check if it's a limb node or null node (bones can be either)
            if (obj->getType() == ofbx::Object::Type::LIMB_NODE ||
                obj->getType() == ofbx::Object::Type::NULL_NODE) {

                std::string boneName = normalizeBoneName(obj->name);
                auto it = boneNameToIndex.find(boneName);
                if (it != boneNameToIndex.end() && boneToIndex.find(obj) == boneToIndex.end()) {
                    boneToIndex[obj] = it->second;
                    boneObjects.push_back(obj);
                }
            }
        }
    }

    SDL_Log("FBXLoader: Found %zu matching bones in animation file", boneObjects.size());

    // Load animations
    int animStackCount = scene->getAnimationStackCount();
    SDL_Log("FBXLoader: Found %d animation stacks in %s", animStackCount, path.c_str());

    for (int stackIdx = 0; stackIdx < animStackCount; ++stackIdx) {
        const ofbx::AnimationStack* stack = scene->getAnimationStack(stackIdx);
        if (!stack) continue;

        const ofbx::AnimationLayer* layer = stack->getLayer(0);
        if (!layer) continue;

        AnimationClip clip;
        clip.name = stack->name;
        clip.duration = 0.0f;

        const ofbx::TakeInfo* takeInfo = scene->getTakeInfo(stack->name);
        double localTimeFrom = 0.0;
        double localTimeTo = 0.0;
        if (takeInfo) {
            localTimeFrom = fbxTimeToSeconds(static_cast<ofbx::i64>(takeInfo->local_time_from));
            localTimeTo = fbxTimeToSeconds(static_cast<ofbx::i64>(takeInfo->local_time_to));
        }

        // Always scan animation curves to find duration - TakeInfo is often unreliable
        {
            double curveTimeFrom = 0.0;
            double curveTimeTo = 0.0;

            // Iterate layer's curve nodes to find time range
            for (int i = 0; ; ++i) {
                const ofbx::AnimationCurveNode* curveNode = layer->getCurveNode(i);
                if (!curveNode) break;

                for (int axis = 0; axis < 3; ++axis) {
                    const ofbx::AnimationCurve* curve = curveNode->getCurve(axis);
                    if (!curve) continue;

                    int keyCount = curve->getKeyCount();
                    const ofbx::i64* keyTimes = curve->getKeyTime();
                    if (keyCount > 0 && keyTimes) {
                        double firstKey = fbxTimeToSeconds(keyTimes[0]);
                        double lastKey = fbxTimeToSeconds(keyTimes[keyCount - 1]);
                        if (firstKey < curveTimeFrom || curveTimeFrom == 0.0) curveTimeFrom = firstKey;
                        if (lastKey > curveTimeTo) curveTimeTo = lastKey;
                    }
                }
            }

            // Fallback: try getting curves through bones if layer iteration failed
            if (curveTimeTo <= curveTimeFrom) {
                for (const ofbx::Object* bone : boneObjects) {
                    const char* properties[] = {"Lcl Translation", "Lcl Rotation", "Lcl Scaling"};
                    for (const char* prop : properties) {
                        const ofbx::AnimationCurveNode* curveNode = layer->getCurveNode(*bone, prop);
                        if (!curveNode) continue;

                        for (int axis = 0; axis < 3; ++axis) {
                            const ofbx::AnimationCurve* curve = curveNode->getCurve(axis);
                            if (!curve) continue;

                            int keyCount = curve->getKeyCount();
                            const ofbx::i64* keyTimes = curve->getKeyTime();
                            if (keyCount > 0 && keyTimes) {
                                double firstKey = fbxTimeToSeconds(keyTimes[0]);
                                double lastKey = fbxTimeToSeconds(keyTimes[keyCount - 1]);
                                if (firstKey < curveTimeFrom || curveTimeFrom == 0.0) curveTimeFrom = firstKey;
                                if (lastKey > curveTimeTo) curveTimeTo = lastKey;
                            }
                        }
                    }
                }
            }

            // Use curve times if they're better than TakeInfo
            if (curveTimeTo > curveTimeFrom && curveTimeTo > (localTimeTo - localTimeFrom)) {
                localTimeFrom = curveTimeFrom;
                localTimeTo = curveTimeTo;
            }

            // Final fallback: use default duration
            if (localTimeTo <= localTimeFrom) {
                localTimeTo = 1.0;
            }
        }

        const double fps = 30.0;
        const double frameTime = 1.0 / fps;

        for (const ofbx::Object* bone : boneObjects) {
            auto boneIt = boneToIndex.find(bone);
            if (boneIt == boneToIndex.end()) continue;

            const ofbx::AnimationCurveNode* transNode = layer->getCurveNode(*bone, "Lcl Translation");
            const ofbx::AnimationCurveNode* rotNode = layer->getCurveNode(*bone, "Lcl Rotation");
            const ofbx::AnimationCurveNode* scaleNode = layer->getCurveNode(*bone, "Lcl Scaling");

            if (!transNode && !rotNode && !scaleNode) continue;

            AnimationChannel channel;
            channel.jointIndex = boneIt->second;

            double duration = localTimeTo - localTimeFrom;
            if (duration <= 0) duration = 1.0;

            int numSamples = static_cast<int>(duration * fps) + 1;
            numSamples = std::max(2, std::min(numSamples, 1000));

            for (int s = 0; s < numSamples; ++s) {
                double t = localTimeFrom + (s * frameTime);
                float time = static_cast<float>(s * frameTime);

                if (transNode) {
                    ofbx::DVec3 trans = transNode->getNodeLocalTransform(t);
                    if (s == 0) {
                        channel.translation.times.reserve(numSamples);
                        channel.translation.values.reserve(numSamples);
                    }
                    channel.translation.times.push_back(time);
                    channel.translation.values.push_back(convertVec3(trans));
                }

                if (rotNode) {
                    ofbx::DVec3 rot = rotNode->getNodeLocalTransform(t);
                    glm::vec3 eulerDeg = convertVec3(rot);
                    glm::quat quat = eulerToQuat(eulerDeg);
                    if (s == 0) {
                        channel.rotation.times.reserve(numSamples);
                        channel.rotation.values.reserve(numSamples);
                    }
                    channel.rotation.times.push_back(time);
                    channel.rotation.values.push_back(quat);
                }

                if (scaleNode) {
                    ofbx::DVec3 scale = scaleNode->getNodeLocalTransform(t);
                    if (s == 0) {
                        channel.scale.times.reserve(numSamples);
                        channel.scale.values.reserve(numSamples);
                    }
                    channel.scale.times.push_back(time);
                    channel.scale.values.push_back(convertVec3(scale));
                }

                if (time > clip.duration) {
                    clip.duration = time;
                }
            }

            if (channel.hasTranslation() || channel.hasRotation() || channel.hasScale()) {
                clip.channels.push_back(channel);
            }
        }

        if (!clip.channels.empty()) {
            // Derive animation name from filename if stack name is generic
            std::string lowerName = clip.name;
            for (char& c : lowerName) c = std::tolower(c);
            if (lowerName.find("mixamo") != std::string::npos) {
                // Extract meaningful name from file path
                size_t lastSlash = path.find_last_of("/\\");
                size_t lastDot = path.find_last_of('.');
                if (lastSlash != std::string::npos && lastDot != std::string::npos && lastDot > lastSlash) {
                    clip.name = path.substr(lastSlash + 1, lastDot - lastSlash - 1);
                }
            }

            // Find root bone index (usually "Hips" for Mixamo)
            clip.rootBoneIndex = skeleton.findJointIndex("Hips");
            if (clip.rootBoneIndex < 0) {
                clip.rootBoneIndex = skeleton.findJointIndex("Root");
            }

            // Calculate root motion per cycle from the root bone's translation channel
            if (clip.rootBoneIndex >= 0) {
                const AnimationChannel* rootChannel = clip.getChannelForJoint(clip.rootBoneIndex);
                if (rootChannel && rootChannel->hasTranslation() && !rootChannel->translation.values.empty()) {
                    glm::vec3 startPos = rootChannel->translation.values.front();
                    glm::vec3 endPos = rootChannel->translation.values.back();
                    clip.rootMotionPerCycle = endPos - startPos;
                }
            }

            SDL_Log("FBXLoader: Loaded animation '%s' with %zu channels, duration %.2fs, rootMotion=(%.2f, %.2f, %.2f)",
                    clip.name.c_str(), clip.channels.size(), clip.duration,
                    clip.rootMotionPerCycle.x, clip.rootMotionPerCycle.y, clip.rootMotionPerCycle.z);
            result.push_back(std::move(clip));
        }
    }

    return result;
}

} // namespace FBXLoader
