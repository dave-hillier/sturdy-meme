#include "MeshSimplifier.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
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

    // Make a copy of LOD 0 since push_back may invalidate references
    LODMeshData lod0 = lodData_.lods[0];

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

    // Prepare position data for meshoptimizer
    std::vector<float> positions(source.vertices.size() * 3);
    for (size_t i = 0; i < source.vertices.size(); ++i) {
        positions[i * 3 + 0] = source.vertices[i].position.x;
        positions[i * 3 + 1] = source.vertices[i].position.y;
        positions[i * 3 + 2] = source.vertices[i].position.z;
    }

    // First, generate a vertex remap to weld duplicate positions
    // This helps simplification work better on meshes with split normals/UVs
    std::vector<unsigned int> positionRemap(source.vertices.size());
    size_t uniqueVertexCount = meshopt_generateVertexRemap(
        positionRemap.data(),
        source.indices.data(),
        source.indices.size(),
        positions.data(),
        source.vertices.size(),
        sizeof(float) * 3
    );

    // Create remapped indices for simplification
    std::vector<uint32_t> remappedIndices(source.indices.size());
    for (size_t i = 0; i < source.indices.size(); ++i) {
        remappedIndices[i] = positionRemap[source.indices[i]];
    }

    // Create position array for unique vertices only
    std::vector<float> uniquePositions(uniqueVertexCount * 3);
    for (size_t i = 0; i < source.vertices.size(); ++i) {
        size_t newIdx = positionRemap[i];
        uniquePositions[newIdx * 3 + 0] = source.vertices[i].position.x;
        uniquePositions[newIdx * 3 + 1] = source.vertices[i].position.y;
        uniquePositions[newIdx * 3 + 2] = source.vertices[i].position.z;
    }

    SDL_Log("  Vertex welding: %zu -> %zu unique positions", source.vertices.size(), uniqueVertexCount);

    // Simplify the mesh using welded positions
    std::vector<uint32_t> simplifiedIndices(source.indices.size());
    float resultError = 0.0f;

    unsigned int options = 0;
    if (config.lockBoundary) {
        options |= meshopt_SimplifyLockBorder;
    }

    size_t newIndexCount = meshopt_simplify(
        simplifiedIndices.data(),
        remappedIndices.data(),
        remappedIndices.size(),
        uniquePositions.data(),
        uniqueVertexCount,
        sizeof(float) * 3,
        targetIndexCount,
        config.targetError,
        options,
        &resultError
    );

    // If standard simplification didn't reduce much, try sloppy simplification
    if (newIndexCount > targetIndexCount * 1.5f && targetIndexCount < source.indices.size() * 0.9f) {
        SDL_Log("  Standard simplification insufficient, trying sloppy mode...");
        newIndexCount = meshopt_simplifySloppy(
            simplifiedIndices.data(),
            remappedIndices.data(),
            remappedIndices.size(),
            uniquePositions.data(),
            uniqueVertexCount,
            sizeof(float) * 3,
            targetIndexCount,
            config.targetError * 2.0f,  // Allow more error for sloppy mode
            &resultError
        );
    }

    simplifiedIndices.resize(newIndexCount);

    // Map simplified indices back to original vertex indices
    // We need to pick one original vertex for each welded position
    std::vector<uint32_t> reverseRemap(uniqueVertexCount, UINT32_MAX);
    for (size_t i = 0; i < source.vertices.size(); ++i) {
        size_t newIdx = positionRemap[i];
        if (reverseRemap[newIdx] == UINT32_MAX) {
            reverseRemap[newIdx] = static_cast<uint32_t>(i);
        }
    }

    // Convert simplified indices back to original vertex space
    for (size_t i = 0; i < simplifiedIndices.size(); ++i) {
        simplifiedIndices[i] = reverseRemap[simplifiedIndices[i]];
    }

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

bool MeshSimplifier::saveGLB(const std::string& outputDir) const {
    // Create output directory
    std::filesystem::create_directories(outputDir);

    // Save each LOD as a separate GLB file
    for (const auto& lod : lodData_.lods) {
        std::string filename = outputDir + "/" + lodData_.name + "_lod" +
                              std::to_string(lod.lodLevel) + ".glb";

        // Build binary buffer with all mesh data
        std::vector<uint8_t> binBuffer;
        size_t bufferOffset = 0;

        // Helper to align buffer to 4-byte boundary
        auto alignBuffer = [&binBuffer]() {
            while (binBuffer.size() % 4 != 0) {
                binBuffer.push_back(0);
            }
        };

        // Calculate mesh bounds for accessor min/max
        glm::vec3 posMin(FLT_MAX), posMax(-FLT_MAX);
        for (const auto& v : lod.vertices) {
            posMin = glm::min(posMin, v.position);
            posMax = glm::max(posMax, v.position);
        }

        // Write indices to buffer
        size_t indicesOffset = binBuffer.size();
        size_t indicesSize = lod.indices.size() * sizeof(uint32_t);
        binBuffer.resize(binBuffer.size() + indicesSize);
        std::memcpy(binBuffer.data() + indicesOffset, lod.indices.data(), indicesSize);
        alignBuffer();

        // Write positions to buffer
        size_t positionsOffset = binBuffer.size();
        size_t positionsSize = lod.vertices.size() * sizeof(glm::vec3);
        binBuffer.resize(binBuffer.size() + positionsSize);
        for (size_t i = 0; i < lod.vertices.size(); ++i) {
            std::memcpy(binBuffer.data() + positionsOffset + i * sizeof(glm::vec3),
                       &lod.vertices[i].position, sizeof(glm::vec3));
        }
        alignBuffer();

        // Write normals to buffer
        size_t normalsOffset = binBuffer.size();
        size_t normalsSize = lod.vertices.size() * sizeof(glm::vec3);
        binBuffer.resize(binBuffer.size() + normalsSize);
        for (size_t i = 0; i < lod.vertices.size(); ++i) {
            std::memcpy(binBuffer.data() + normalsOffset + i * sizeof(glm::vec3),
                       &lod.vertices[i].normal, sizeof(glm::vec3));
        }
        alignBuffer();

        // Write texcoords to buffer
        size_t texcoordsOffset = binBuffer.size();
        size_t texcoordsSize = lod.vertices.size() * sizeof(glm::vec2);
        binBuffer.resize(binBuffer.size() + texcoordsSize);
        for (size_t i = 0; i < lod.vertices.size(); ++i) {
            std::memcpy(binBuffer.data() + texcoordsOffset + i * sizeof(glm::vec2),
                       &lod.vertices[i].texCoord, sizeof(glm::vec2));
        }
        alignBuffer();

        // Write tangents to buffer
        size_t tangentsOffset = binBuffer.size();
        size_t tangentsSize = lod.vertices.size() * sizeof(glm::vec4);
        binBuffer.resize(binBuffer.size() + tangentsSize);
        for (size_t i = 0; i < lod.vertices.size(); ++i) {
            std::memcpy(binBuffer.data() + tangentsOffset + i * sizeof(glm::vec4),
                       &lod.vertices[i].tangent, sizeof(glm::vec4));
        }
        alignBuffer();

        // Write bone indices (JOINTS_0) as unsigned short vec4
        size_t jointsOffset = binBuffer.size();
        size_t jointsSize = lod.vertices.size() * sizeof(uint16_t) * 4;
        binBuffer.resize(binBuffer.size() + jointsSize);
        for (size_t i = 0; i < lod.vertices.size(); ++i) {
            uint16_t joints[4] = {
                static_cast<uint16_t>(lod.vertices[i].boneIndices.x),
                static_cast<uint16_t>(lod.vertices[i].boneIndices.y),
                static_cast<uint16_t>(lod.vertices[i].boneIndices.z),
                static_cast<uint16_t>(lod.vertices[i].boneIndices.w)
            };
            std::memcpy(binBuffer.data() + jointsOffset + i * sizeof(joints), joints, sizeof(joints));
        }
        alignBuffer();

        // Write bone weights (WEIGHTS_0) as float vec4
        size_t weightsOffset = binBuffer.size();
        size_t weightsSize = lod.vertices.size() * sizeof(glm::vec4);
        binBuffer.resize(binBuffer.size() + weightsSize);
        for (size_t i = 0; i < lod.vertices.size(); ++i) {
            std::memcpy(binBuffer.data() + weightsOffset + i * sizeof(glm::vec4),
                       &lod.vertices[i].boneWeights, sizeof(glm::vec4));
        }
        alignBuffer();

        // Write inverse bind matrices for skeleton
        size_t ibmOffset = binBuffer.size();
        size_t ibmSize = lodData_.skeleton.size() * sizeof(glm::mat4);
        binBuffer.resize(binBuffer.size() + ibmSize);
        for (size_t i = 0; i < lodData_.skeleton.size(); ++i) {
            std::memcpy(binBuffer.data() + ibmOffset + i * sizeof(glm::mat4),
                       &lodData_.skeleton[i].inverseBindMatrix, sizeof(glm::mat4));
        }
        alignBuffer();

        // Build GLTF JSON structure
        nlohmann::json gltf;
        gltf["asset"]["version"] = "2.0";
        gltf["asset"]["generator"] = "skinned_mesh_lod tool";

        // Buffer
        gltf["buffers"] = nlohmann::json::array();
        gltf["buffers"].push_back({{"byteLength", binBuffer.size()}});

        // Buffer views
        int bvIndex = 0;
        gltf["bufferViews"] = nlohmann::json::array();

        // Indices buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", indicesOffset},
            {"byteLength", indicesSize},
            {"target", 34963}  // ELEMENT_ARRAY_BUFFER
        });
        int indicesBV = bvIndex++;

        // Positions buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", positionsOffset},
            {"byteLength", positionsSize},
            {"target", 34962}  // ARRAY_BUFFER
        });
        int positionsBV = bvIndex++;

        // Normals buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", normalsOffset},
            {"byteLength", normalsSize},
            {"target", 34962}
        });
        int normalsBV = bvIndex++;

        // Texcoords buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", texcoordsOffset},
            {"byteLength", texcoordsSize},
            {"target", 34962}
        });
        int texcoordsBV = bvIndex++;

        // Tangents buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", tangentsOffset},
            {"byteLength", tangentsSize},
            {"target", 34962}
        });
        int tangentsBV = bvIndex++;

        // Joints buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", jointsOffset},
            {"byteLength", jointsSize},
            {"target", 34962}
        });
        int jointsBV = bvIndex++;

        // Weights buffer view
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", weightsOffset},
            {"byteLength", weightsSize},
            {"target", 34962}
        });
        int weightsBV = bvIndex++;

        // Inverse bind matrices buffer view (no target for non-vertex data)
        gltf["bufferViews"].push_back({
            {"buffer", 0},
            {"byteOffset", ibmOffset},
            {"byteLength", ibmSize}
        });
        int ibmBV = bvIndex++;

        // Accessors
        int accIndex = 0;
        gltf["accessors"] = nlohmann::json::array();

        // Indices accessor
        gltf["accessors"].push_back({
            {"bufferView", indicesBV},
            {"componentType", 5125},  // UNSIGNED_INT
            {"count", lod.indices.size()},
            {"type", "SCALAR"}
        });
        int indicesAcc = accIndex++;

        // Positions accessor
        gltf["accessors"].push_back({
            {"bufferView", positionsBV},
            {"componentType", 5126},  // FLOAT
            {"count", lod.vertices.size()},
            {"type", "VEC3"},
            {"min", {posMin.x, posMin.y, posMin.z}},
            {"max", {posMax.x, posMax.y, posMax.z}}
        });
        int positionsAcc = accIndex++;

        // Normals accessor
        gltf["accessors"].push_back({
            {"bufferView", normalsBV},
            {"componentType", 5126},
            {"count", lod.vertices.size()},
            {"type", "VEC3"}
        });
        int normalsAcc = accIndex++;

        // Texcoords accessor
        gltf["accessors"].push_back({
            {"bufferView", texcoordsBV},
            {"componentType", 5126},
            {"count", lod.vertices.size()},
            {"type", "VEC2"}
        });
        int texcoordsAcc = accIndex++;

        // Tangents accessor
        gltf["accessors"].push_back({
            {"bufferView", tangentsBV},
            {"componentType", 5126},
            {"count", lod.vertices.size()},
            {"type", "VEC4"}
        });
        int tangentsAcc = accIndex++;

        // Joints accessor
        gltf["accessors"].push_back({
            {"bufferView", jointsBV},
            {"componentType", 5123},  // UNSIGNED_SHORT
            {"count", lod.vertices.size()},
            {"type", "VEC4"}
        });
        int jointsAcc = accIndex++;

        // Weights accessor
        gltf["accessors"].push_back({
            {"bufferView", weightsBV},
            {"componentType", 5126},
            {"count", lod.vertices.size()},
            {"type", "VEC4"}
        });
        int weightsAcc = accIndex++;

        // Inverse bind matrices accessor
        gltf["accessors"].push_back({
            {"bufferView", ibmBV},
            {"componentType", 5126},
            {"count", lodData_.skeleton.size()},
            {"type", "MAT4"}
        });
        int ibmAcc = accIndex++;

        // Mesh
        gltf["meshes"] = nlohmann::json::array();
        nlohmann::json primitive;
        primitive["attributes"]["POSITION"] = positionsAcc;
        primitive["attributes"]["NORMAL"] = normalsAcc;
        primitive["attributes"]["TEXCOORD_0"] = texcoordsAcc;
        primitive["attributes"]["TANGENT"] = tangentsAcc;
        primitive["attributes"]["JOINTS_0"] = jointsAcc;
        primitive["attributes"]["WEIGHTS_0"] = weightsAcc;
        primitive["indices"] = indicesAcc;
        primitive["mode"] = 4;  // TRIANGLES

        gltf["meshes"].push_back({
            {"name", lodData_.name + "_lod" + std::to_string(lod.lodLevel)},
            {"primitives", nlohmann::json::array({primitive})}
        });

        // Nodes for skeleton
        gltf["nodes"] = nlohmann::json::array();
        std::vector<int> jointNodeIndices;

        for (size_t i = 0; i < lodData_.skeleton.size(); ++i) {
            const auto& joint = lodData_.skeleton[i];

            // Decompose local transform to TRS
            glm::vec3 translation = glm::vec3(joint.localTransform[3]);
            glm::vec3 scale;
            scale.x = glm::length(glm::vec3(joint.localTransform[0]));
            scale.y = glm::length(glm::vec3(joint.localTransform[1]));
            scale.z = glm::length(glm::vec3(joint.localTransform[2]));

            glm::mat3 rotMat(
                glm::vec3(joint.localTransform[0]) / scale.x,
                glm::vec3(joint.localTransform[1]) / scale.y,
                glm::vec3(joint.localTransform[2]) / scale.z
            );
            glm::quat rotation = glm::quat_cast(rotMat);

            nlohmann::json node;
            node["name"] = joint.name;
            node["translation"] = {translation.x, translation.y, translation.z};
            node["rotation"] = {rotation.x, rotation.y, rotation.z, rotation.w};
            node["scale"] = {scale.x, scale.y, scale.z};

            gltf["nodes"].push_back(node);
            jointNodeIndices.push_back(static_cast<int>(i));
        }

        // Add children to parent nodes
        for (size_t i = 0; i < lodData_.skeleton.size(); ++i) {
            int parent = lodData_.skeleton[i].parentIndex;
            if (parent >= 0 && parent < static_cast<int>(lodData_.skeleton.size())) {
                if (!gltf["nodes"][parent].contains("children")) {
                    gltf["nodes"][parent]["children"] = nlohmann::json::array();
                }
                gltf["nodes"][parent]["children"].push_back(static_cast<int>(i));
            }
        }

        // Mesh node (references mesh and skin)
        int meshNodeIndex = static_cast<int>(gltf["nodes"].size());
        gltf["nodes"].push_back({
            {"name", lodData_.name},
            {"mesh", 0},
            {"skin", 0}
        });

        // Skin
        gltf["skins"] = nlohmann::json::array();
        gltf["skins"].push_back({
            {"inverseBindMatrices", ibmAcc},
            {"joints", jointNodeIndices},
            {"name", lodData_.name + "_skin"}
        });

        // Find root joints (joints with no parent in skeleton)
        std::vector<int> rootJoints;
        for (size_t i = 0; i < lodData_.skeleton.size(); ++i) {
            if (lodData_.skeleton[i].parentIndex < 0) {
                rootJoints.push_back(static_cast<int>(i));
            }
        }

        // Scene
        std::vector<int> sceneNodes = rootJoints;
        sceneNodes.push_back(meshNodeIndex);

        gltf["scenes"] = nlohmann::json::array();
        gltf["scenes"].push_back({
            {"name", "Scene"},
            {"nodes", sceneNodes}
        });
        gltf["scene"] = 0;

        // Serialize JSON
        std::string jsonStr = gltf.dump();

        // Pad JSON to 4-byte alignment
        while (jsonStr.size() % 4 != 0) {
            jsonStr += ' ';
        }

        // Write GLB file
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create file: %s", filename.c_str());
            return false;
        }

        // GLB Header (12 bytes)
        uint32_t glbMagic = 0x46546C67;  // "glTF"
        uint32_t glbVersion = 2;
        uint32_t glbLength = 12 + 8 + static_cast<uint32_t>(jsonStr.size()) + 8 + static_cast<uint32_t>(binBuffer.size());

        file.write(reinterpret_cast<const char*>(&glbMagic), 4);
        file.write(reinterpret_cast<const char*>(&glbVersion), 4);
        file.write(reinterpret_cast<const char*>(&glbLength), 4);

        // JSON Chunk
        uint32_t jsonChunkLength = static_cast<uint32_t>(jsonStr.size());
        uint32_t jsonChunkType = 0x4E4F534A;  // "JSON"
        file.write(reinterpret_cast<const char*>(&jsonChunkLength), 4);
        file.write(reinterpret_cast<const char*>(&jsonChunkType), 4);
        file.write(jsonStr.data(), jsonStr.size());

        // BIN Chunk
        uint32_t binChunkLength = static_cast<uint32_t>(binBuffer.size());
        uint32_t binChunkType = 0x004E4942;  // "BIN\0"
        file.write(reinterpret_cast<const char*>(&binChunkLength), 4);
        file.write(reinterpret_cast<const char*>(&binChunkType), 4);
        file.write(reinterpret_cast<const char*>(binBuffer.data()), binBuffer.size());

        file.close();
        SDL_Log("Saved LOD %u to %s (%zu vertices, %zu triangles)",
                lod.lodLevel, filename.c_str(), lod.vertices.size(), lod.indices.size() / 3);
    }

    // Also write a manifest JSON for convenience (lists all LOD files)
    nlohmann::json manifest;
    manifest["name"] = lodData_.name;
    manifest["format"] = "glb";
    manifest["lodCount"] = lodData_.lods.size();

    nlohmann::json lodsJson = nlohmann::json::array();
    for (const auto& lod : lodData_.lods) {
        nlohmann::json lodJson;
        lodJson["level"] = lod.lodLevel;
        lodJson["file"] = lodData_.name + "_lod" + std::to_string(lod.lodLevel) + ".glb";
        lodJson["targetRatio"] = lod.targetRatio;
        lodJson["actualRatio"] = lod.actualRatio;
        lodJson["vertices"] = lod.vertices.size();
        lodJson["triangles"] = lod.indices.size() / 3;
        lodsJson.push_back(lodJson);
    }
    manifest["lods"] = lodsJson;

    std::string manifestPath = outputDir + "/" + lodData_.name + "_lods.json";
    std::ofstream manifestFile(manifestPath);
    if (manifestFile) {
        manifestFile << manifest.dump(2);
        manifestFile.close();
        SDL_Log("Saved LOD manifest to %s", manifestPath.c_str());
    }

    return true;
}
