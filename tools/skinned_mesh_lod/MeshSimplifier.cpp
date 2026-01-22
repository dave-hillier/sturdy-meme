#include "MeshSimplifier.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <meshoptimizer.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_log.h>

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cstring>

namespace {

glm::mat4 toMat4(const std::array<float, 16>& arr) {
    return glm::mat4(
        arr[0], arr[1], arr[2], arr[3],
        arr[4], arr[5], arr[6], arr[7],
        arr[8], arr[9], arr[10], arr[11],
        arr[12], arr[13], arr[14], arr[15]
    );
}

// Calculate tangents using MikkTSpace-like algorithm
void calculateTangents(std::vector<SkinnedVertexData>& vertices, const std::vector<uint32_t>& indices) {
    // Initialize tangents to zero
    for (auto& v : vertices) {
        v.tangent = glm::vec4(0.0f);
    }

    // Accumulate tangent contributions from each triangle
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

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

    // Normalize tangents and calculate handedness
    for (auto& v : vertices) {
        glm::vec3 t = glm::vec3(v.tangent);
        if (glm::length(t) > 1e-8f) {
            // Gram-Schmidt orthogonalize
            t = glm::normalize(t - v.normal * glm::dot(v.normal, t));
            v.tangent = glm::vec4(t, 1.0f);
        } else {
            // Fallback tangent
            glm::vec3 up = std::abs(v.normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            v.tangent = glm::vec4(glm::normalize(glm::cross(up, v.normal)), 1.0f);
        }
    }
}

} // anonymous namespace

bool MeshSimplifier::loadGLTF(const std::string& path) {
    fastgltf::Parser parser;

    std::filesystem::path filePath(path);
    if (!std::filesystem::exists(filePath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "File not found: %s", path.c_str());
        return false;
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (data.error() != fastgltf::Error::None) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load file: %s", path.c_str());
        return false;
    }

    constexpr auto gltfOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::GenerateMeshIndices;

    auto asset = parser.loadGltf(data.get(), filePath.parent_path(), gltfOptions);
    if (asset.error() != fastgltf::Error::None) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse glTF: %s (error: %d)",
                path.c_str(), static_cast<int>(asset.error()));
        return false;
    }

    // Initialize LOD data with mesh name
    lodData_.name = filePath.stem().string();

    // Load skeleton data first
    if (!asset->skins.empty()) {
        const auto& skin = asset->skins[0];

        lodData_.skeleton.reserve(skin.joints.size());

        for (size_t i = 0; i < skin.joints.size(); ++i) {
            size_t nodeIndex = skin.joints[i];
            const auto& node = asset->nodes[nodeIndex];

            JointData joint;
            joint.name = std::string(node.name);
            joint.parentIndex = -1;

            // Get inverse bind matrix
            if (skin.inverseBindMatrices.has_value()) {
                const auto& ibmAccessor = asset->accessors[skin.inverseBindMatrices.value()];
                std::vector<glm::mat4> ibms(ibmAccessor.count);
                fastgltf::copyFromAccessor<glm::mat4>(asset.get(), ibmAccessor, ibms.data());
                if (i < ibms.size()) {
                    joint.inverseBindMatrix = ibms[i];
                } else {
                    joint.inverseBindMatrix = glm::mat4(1.0f);
                }
            } else {
                joint.inverseBindMatrix = glm::mat4(1.0f);
            }

            // Get local transform
            std::visit(fastgltf::visitor{
                [&](const fastgltf::TRS& trs) {
                    glm::mat4 T = glm::translate(glm::mat4(1.0f),
                        glm::vec3(trs.translation[0], trs.translation[1], trs.translation[2]));
                    glm::quat R(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]);
                    glm::mat4 S = glm::scale(glm::mat4(1.0f),
                        glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
                    joint.localTransform = T * glm::mat4_cast(R) * S;
                },
                [&](const fastgltf::math::fmat4x4& matrix) {
                    joint.localTransform = toMat4({
                        matrix[0][0], matrix[0][1], matrix[0][2], matrix[0][3],
                        matrix[1][0], matrix[1][1], matrix[1][2], matrix[1][3],
                        matrix[2][0], matrix[2][1], matrix[2][2], matrix[2][3],
                        matrix[3][0], matrix[3][1], matrix[3][2], matrix[3][3]
                    });
                }
            }, node.transform);

            lodData_.skeleton.push_back(joint);
        }

        // Compute parent indices by traversing node hierarchy
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            size_t nodeIndex = skin.joints[i];

            for (size_t parentNodeIdx = 0; parentNodeIdx < asset->nodes.size(); ++parentNodeIdx) {
                const auto& parentNode = asset->nodes[parentNodeIdx];
                for (size_t childIdx : parentNode.children) {
                    if (childIdx == nodeIndex) {
                        for (size_t j = 0; j < skin.joints.size(); ++j) {
                            if (skin.joints[j] == parentNodeIdx) {
                                lodData_.skeleton[i].parentIndex = static_cast<int32_t>(j);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        stats_.skeletonJoints = lodData_.skeleton.size();
        SDL_Log("Loaded skeleton with %zu joints", lodData_.skeleton.size());
    }

    // Create LOD 0 (original mesh)
    LODMeshData lod0;
    lod0.lodLevel = 0;
    lod0.targetRatio = 1.0f;
    lod0.actualRatio = 1.0f;

    // Process meshes
    for (const auto& mesh : asset->meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                continue;
            }

            size_t vertexOffset = lod0.vertices.size();

            // Get position accessor
            auto* positionIt = primitive.findAttribute("POSITION");
            if (positionIt == primitive.attributes.end()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Primitive missing POSITION attribute");
                continue;
            }

            const auto& posAccessor = asset->accessors[positionIt->accessorIndex];
            size_t vertexCount = posAccessor.count;

            // Reserve space for new vertices
            lod0.vertices.resize(vertexOffset + vertexCount);

            // Get material base color for this primitive
            glm::vec4 baseColor(1.0f);
            if (primitive.materialIndex.has_value()) {
                const auto& material = asset->materials[primitive.materialIndex.value()];
                const auto& pbr = material.pbrData;
                baseColor = glm::vec4(
                    pbr.baseColorFactor[0],
                    pbr.baseColorFactor[1],
                    pbr.baseColorFactor[2],
                    pbr.baseColorFactor[3]
                );
            }

            // Initialize all vertices with default values
            for (size_t i = 0; i < vertexCount; ++i) {
                lod0.vertices[vertexOffset + i].boneIndices = glm::uvec4(0);
                lod0.vertices[vertexOffset + i].boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                lod0.vertices[vertexOffset + i].color = baseColor;
            }

            // Load positions
            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                [&](glm::vec3 pos, size_t idx) {
                    lod0.vertices[vertexOffset + idx].position = pos;
                });

            // Load normals
            auto* normalIt = primitive.findAttribute("NORMAL");
            if (normalIt != primitive.attributes.end()) {
                const auto& normalAccessor = asset->accessors[normalIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), normalAccessor,
                    [&](glm::vec3 normal, size_t idx) {
                        lod0.vertices[vertexOffset + idx].normal = normal;
                    });
            } else {
                for (size_t i = 0; i < vertexCount; ++i) {
                    lod0.vertices[vertexOffset + i].normal = glm::vec3(0, 1, 0);
                }
            }

            // Load texture coordinates
            auto* texCoordIt = primitive.findAttribute("TEXCOORD_0");
            if (texCoordIt != primitive.attributes.end()) {
                const auto& texCoordAccessor = asset->accessors[texCoordIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), texCoordAccessor,
                    [&](glm::vec2 uv, size_t idx) {
                        lod0.vertices[vertexOffset + idx].texCoord = uv;
                    });
            }

            // Load tangents if available
            auto* tangentIt = primitive.findAttribute("TANGENT");
            if (tangentIt != primitive.attributes.end()) {
                const auto& tangentAccessor = asset->accessors[tangentIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), tangentAccessor,
                    [&](glm::vec4 tangent, size_t idx) {
                        lod0.vertices[vertexOffset + idx].tangent = tangent;
                    });
            }

            // Load bone indices (JOINTS_0)
            auto* jointsIt = primitive.findAttribute("JOINTS_0");
            if (jointsIt != primitive.attributes.end()) {
                const auto& jointsAccessor = asset->accessors[jointsIt->accessorIndex];

                if (jointsAccessor.componentType == fastgltf::ComponentType::UnsignedByte) {
                    fastgltf::iterateAccessorWithIndex<glm::u8vec4>(asset.get(), jointsAccessor,
                        [&](glm::u8vec4 joints, size_t idx) {
                            lod0.vertices[vertexOffset + idx].boneIndices = glm::uvec4(joints);
                        });
                } else if (jointsAccessor.componentType == fastgltf::ComponentType::UnsignedShort) {
                    fastgltf::iterateAccessorWithIndex<glm::u16vec4>(asset.get(), jointsAccessor,
                        [&](glm::u16vec4 joints, size_t idx) {
                            lod0.vertices[vertexOffset + idx].boneIndices = glm::uvec4(joints);
                        });
                }
            }

            // Load bone weights (WEIGHTS_0)
            auto* weightsIt = primitive.findAttribute("WEIGHTS_0");
            if (weightsIt != primitive.attributes.end()) {
                const auto& weightsAccessor = asset->accessors[weightsIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), weightsAccessor,
                    [&](glm::vec4 weights, size_t idx) {
                        lod0.vertices[vertexOffset + idx].boneWeights = weights;
                    });
            }

            // Load indices
            if (primitive.indicesAccessor.has_value()) {
                const auto& indexAccessor = asset->accessors[primitive.indicesAccessor.value()];
                size_t indexOffset = lod0.indices.size();
                lod0.indices.reserve(indexOffset + indexAccessor.count);

                fastgltf::iterateAccessor<uint32_t>(asset.get(), indexAccessor,
                    [&](uint32_t index) {
                        lod0.indices.push_back(static_cast<uint32_t>(vertexOffset) + index);
                    });
            }
        }
    }

    if (lod0.vertices.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No vertices loaded from %s", path.c_str());
        return false;
    }

    // Calculate tangents if they weren't in the file
    bool hasTangents = false;
    for (const auto& v : lod0.vertices) {
        if (glm::length(glm::vec3(v.tangent)) > 0.001f) {
            hasTangents = true;
            break;
        }
    }
    if (!hasTangents) {
        calculateTangents(lod0.vertices, lod0.indices);
    }

    // Update statistics
    stats_.originalVertices = lod0.vertices.size();
    stats_.originalTriangles = lod0.indices.size() / 3;

    // Store LOD 0
    lodData_.lods.push_back(std::move(lod0));

    SDL_Log("Loaded mesh '%s': %zu vertices, %zu triangles",
            lodData_.name.c_str(), stats_.originalVertices, stats_.originalTriangles);

    return true;
}

bool MeshSimplifier::loadFBX(const std::string& path) {
    // FBX loading would go here - for now just return false
    // The main use case is GLTF which is more common for web/game assets
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "FBX loading not yet implemented in LOD tool. Use GLTF format.");
    return false;
}

bool MeshSimplifier::generateLODs(const LODConfig& config, ProgressCallback progress) {
    if (lodData_.lods.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No mesh loaded");
        return false;
    }

    const auto& lod0 = lodData_.lods[0];

    if (progress) {
        progress(0.0f, "Starting LOD generation...");
    }

    // Clear any existing LODs except LOD 0
    while (lodData_.lods.size() > 1) {
        lodData_.lods.pop_back();
    }

    stats_.lodVertices.clear();
    stats_.lodTriangles.clear();
    stats_.lodVertices.push_back(lod0.vertices.size());
    stats_.lodTriangles.push_back(lod0.indices.size() / 3);

    // Generate each LOD level
    for (size_t i = 1; i < config.lodRatios.size(); ++i) {
        if (progress) {
            float p = static_cast<float>(i) / config.lodRatios.size();
            progress(p, "Generating LOD " + std::to_string(i) + "...");
        }

        LODMeshData newLod = simplifyMesh(lod0, config.lodRatios[i], config);
        newLod.lodLevel = static_cast<uint32_t>(i);
        newLod.targetRatio = config.lodRatios[i];

        stats_.lodVertices.push_back(newLod.vertices.size());
        stats_.lodTriangles.push_back(newLod.indices.size() / 3);

        SDL_Log("LOD %u: %zu vertices, %zu triangles (%.1f%% of original)",
                newLod.lodLevel, newLod.vertices.size(), newLod.indices.size() / 3,
                newLod.actualRatio * 100.0f);

        lodData_.lods.push_back(std::move(newLod));
    }

    if (progress) {
        progress(1.0f, "LOD generation complete");
    }

    return true;
}

LODMeshData MeshSimplifier::simplifyMesh(const LODMeshData& source, float targetRatio,
                                          const LODConfig& config) {
    LODMeshData result;

    size_t targetIndexCount = static_cast<size_t>(source.indices.size() * targetRatio);
    // Ensure we have at least 3 indices (one triangle)
    targetIndexCount = std::max(targetIndexCount, size_t(3));
    // Round to multiple of 3
    targetIndexCount = (targetIndexCount / 3) * 3;

    // Prepare position data for meshoptimizer (it only needs positions for simplification)
    std::vector<float> positions(source.vertices.size() * 3);
    for (size_t i = 0; i < source.vertices.size(); ++i) {
        positions[i * 3 + 0] = source.vertices[i].position.x;
        positions[i * 3 + 1] = source.vertices[i].position.y;
        positions[i * 3 + 2] = source.vertices[i].position.z;
    }

    // Simplify the mesh
    std::vector<uint32_t> simplifiedIndices(source.indices.size());
    float resultError = 0.0f;

    unsigned int options = 0;
    if (config.lockBoundary) {
        options |= meshopt_SimplifyLockBorder;
    }

    size_t newIndexCount = meshopt_simplify(
        simplifiedIndices.data(),
        source.indices.data(),
        source.indices.size(),
        positions.data(),
        source.vertices.size(),
        sizeof(float) * 3,
        targetIndexCount,
        config.targetError,
        options,
        &resultError
    );

    simplifiedIndices.resize(newIndexCount);

    // Now we need to remap vertices - only keep vertices that are actually used
    std::vector<uint32_t> vertexRemap(source.vertices.size(), UINT32_MAX);
    uint32_t newVertexCount = 0;

    for (uint32_t idx : simplifiedIndices) {
        if (vertexRemap[idx] == UINT32_MAX) {
            vertexRemap[idx] = newVertexCount++;
        }
    }

    // Copy used vertices
    result.vertices.resize(newVertexCount);
    for (size_t i = 0; i < source.vertices.size(); ++i) {
        if (vertexRemap[i] != UINT32_MAX) {
            result.vertices[vertexRemap[i]] = source.vertices[i];
        }
    }

    // Remap indices
    result.indices.resize(simplifiedIndices.size());
    for (size_t i = 0; i < simplifiedIndices.size(); ++i) {
        result.indices[i] = vertexRemap[simplifiedIndices[i]];
    }

    // Optimize vertex cache and overdraw
    meshopt_optimizeVertexCache(
        result.indices.data(),
        result.indices.data(),
        result.indices.size(),
        result.vertices.size()
    );

    // Normalize bone weights after simplification
    normalizeBoneWeights(result);

    // Calculate actual ratio
    result.actualRatio = static_cast<float>(result.indices.size()) / source.indices.size();

    return result;
}

void MeshSimplifier::remapBoneWeights(LODMeshData& mesh) {
    // This would be used if we needed to merge bone weights during vertex welding
    // For now, simplification preserves original vertex data
}

void MeshSimplifier::normalizeBoneWeights(LODMeshData& mesh) {
    for (auto& v : mesh.vertices) {
        float sum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
        if (sum > 0.0001f && std::abs(sum - 1.0f) > 0.0001f) {
            v.boneWeights /= sum;
        }
    }
}

bool MeshSimplifier::saveGLTF(const std::string& outputDir) const {
    // Create output directory
    std::filesystem::create_directories(outputDir);

    // For each LOD, we'll save a separate GLTF file
    // This is a simplified output - a full implementation would write proper GLTF
    for (const auto& lod : lodData_.lods) {
        std::string filename = outputDir + "/" + lodData_.name + "_lod" +
                              std::to_string(lod.lodLevel) + ".bin";

        // Write binary vertex + index data
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create file: %s", filename.c_str());
            return false;
        }

        // Header: vertex count, index count
        uint32_t vertexCount = static_cast<uint32_t>(lod.vertices.size());
        uint32_t indexCount = static_cast<uint32_t>(lod.indices.size());
        file.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
        file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

        // Vertices
        file.write(reinterpret_cast<const char*>(lod.vertices.data()),
                   lod.vertices.size() * sizeof(SkinnedVertexData));

        // Indices
        file.write(reinterpret_cast<const char*>(lod.indices.data()),
                   lod.indices.size() * sizeof(uint32_t));

        file.close();
        SDL_Log("Saved LOD %u to %s", lod.lodLevel, filename.c_str());
    }

    // Write manifest JSON
    nlohmann::json manifest;
    manifest["name"] = lodData_.name;
    manifest["lodCount"] = lodData_.lods.size();

    // LOD info
    nlohmann::json lodsJson = nlohmann::json::array();
    for (const auto& lod : lodData_.lods) {
        nlohmann::json lodJson;
        lodJson["level"] = lod.lodLevel;
        lodJson["targetRatio"] = lod.targetRatio;
        lodJson["actualRatio"] = lod.actualRatio;
        lodJson["vertices"] = lod.vertices.size();
        lodJson["triangles"] = lod.indices.size() / 3;
        lodJson["file"] = lodData_.name + "_lod" + std::to_string(lod.lodLevel) + ".bin";
        lodsJson.push_back(lodJson);
    }
    manifest["lods"] = lodsJson;

    // Skeleton info
    nlohmann::json skeletonJson = nlohmann::json::array();
    for (const auto& joint : lodData_.skeleton) {
        nlohmann::json jointJson;
        jointJson["name"] = joint.name;
        jointJson["parent"] = joint.parentIndex;
        skeletonJson.push_back(jointJson);
    }
    manifest["skeleton"] = skeletonJson;

    std::string manifestPath = outputDir + "/" + lodData_.name + "_manifest.json";
    std::ofstream manifestFile(manifestPath);
    if (!manifestFile) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create manifest: %s", manifestPath.c_str());
        return false;
    }
    manifestFile << manifest.dump(2);
    manifestFile.close();

    SDL_Log("Saved manifest to %s", manifestPath.c_str());
    return true;
}

bool MeshSimplifier::saveBinary(const std::string& outputPath) const {
    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create file: %s", outputPath.c_str());
        return false;
    }

    // Magic number and version
    const char magic[4] = {'S', 'M', 'L', 'D'}; // Skinned Mesh LOD Data
    uint32_t version = 1;
    file.write(magic, 4);
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // LOD count
    uint32_t lodCount = static_cast<uint32_t>(lodData_.lods.size());
    file.write(reinterpret_cast<const char*>(&lodCount), sizeof(lodCount));

    // Joint count
    uint32_t jointCount = static_cast<uint32_t>(lodData_.skeleton.size());
    file.write(reinterpret_cast<const char*>(&jointCount), sizeof(jointCount));

    // Write skeleton
    for (const auto& joint : lodData_.skeleton) {
        // Name length and name
        uint32_t nameLen = static_cast<uint32_t>(joint.name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(joint.name.data(), nameLen);

        // Parent index
        file.write(reinterpret_cast<const char*>(&joint.parentIndex), sizeof(joint.parentIndex));

        // Inverse bind matrix (16 floats)
        file.write(reinterpret_cast<const char*>(&joint.inverseBindMatrix), sizeof(glm::mat4));

        // Local transform (16 floats)
        file.write(reinterpret_cast<const char*>(&joint.localTransform), sizeof(glm::mat4));
    }

    // Write each LOD
    for (const auto& lod : lodData_.lods) {
        uint32_t vertexCount = static_cast<uint32_t>(lod.vertices.size());
        uint32_t indexCount = static_cast<uint32_t>(lod.indices.size());

        file.write(reinterpret_cast<const char*>(&lod.lodLevel), sizeof(lod.lodLevel));
        file.write(reinterpret_cast<const char*>(&lod.targetRatio), sizeof(lod.targetRatio));
        file.write(reinterpret_cast<const char*>(&lod.actualRatio), sizeof(lod.actualRatio));
        file.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
        file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

        // Vertices
        file.write(reinterpret_cast<const char*>(lod.vertices.data()),
                   lod.vertices.size() * sizeof(SkinnedVertexData));

        // Indices
        file.write(reinterpret_cast<const char*>(lod.indices.data()),
                   lod.indices.size() * sizeof(uint32_t));
    }

    file.close();
    SDL_Log("Saved binary LOD data to %s", outputPath.c_str());
    return true;
}
