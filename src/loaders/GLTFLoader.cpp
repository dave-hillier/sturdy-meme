#include "GLTFLoader.h"
#include "SkinnedMesh.h"
#include "Animation.h"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <SDL3/SDL_log.h>
#include <filesystem>
#include <unordered_map>

namespace GLTFLoader {

namespace {

glm::vec3 toVec3(const std::array<float, 3>& arr) {
    return glm::vec3(arr[0], arr[1], arr[2]);
}

glm::vec4 toVec4(const std::array<float, 4>& arr) {
    return glm::vec4(arr[0], arr[1], arr[2], arr[3]);
}

glm::mat4 toMat4(const std::array<float, 16>& arr) {
    return glm::mat4(
        arr[0], arr[1], arr[2], arr[3],
        arr[4], arr[5], arr[6], arr[7],
        arr[8], arr[9], arr[10], arr[11],
        arr[12], arr[13], arr[14], arr[15]
    );
}

// Calculate tangents using MikkTSpace-like algorithm
void calculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
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
            // Calculate handedness (always positive for now, proper bitangent calculation would be needed)
            v.tangent = glm::vec4(t, 1.0f);
        } else {
            // Fallback tangent
            glm::vec3 up = std::abs(v.normal.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            v.tangent = glm::vec4(glm::normalize(glm::cross(up, v.normal)), 1.0f);
        }
    }
}

} // anonymous namespace

std::optional<GLTFLoadResult> load(const std::string& path) {
    fastgltf::Parser parser;

    std::filesystem::path filePath(path);
    if (!std::filesystem::exists(filePath)) {
        SDL_Log("GLTFLoader: File not found: %s", path.c_str());
        return std::nullopt;
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (data.error() != fastgltf::Error::None) {
        SDL_Log("GLTFLoader: Failed to load file: %s", path.c_str());
        return std::nullopt;
    }

    constexpr auto gltfOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::GenerateMeshIndices;

    auto asset = parser.loadGltf(data.get(), filePath.parent_path(), gltfOptions);
    if (asset.error() != fastgltf::Error::None) {
        SDL_Log("GLTFLoader: Failed to parse glTF: %s (error: %d)",
                path.c_str(), static_cast<int>(asset.error()));
        return std::nullopt;
    }

    GLTFLoadResult result;

    // Process meshes - for now, combine all primitives into one mesh
    for (const auto& mesh : asset->meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                continue;
            }

            size_t vertexOffset = result.vertices.size();

            // Get position accessor
            auto* positionIt = primitive.findAttribute("POSITION");
            if (positionIt == primitive.attributes.end()) {
                SDL_Log("GLTFLoader: Primitive missing POSITION attribute");
                continue;
            }

            const auto& posAccessor = asset->accessors[positionIt->accessorIndex];
            size_t vertexCount = posAccessor.count;

            // Reserve space for new vertices
            result.vertices.resize(vertexOffset + vertexCount);

            // Load positions
            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                [&](glm::vec3 pos, size_t idx) {
                    result.vertices[vertexOffset + idx].position = pos;
                });

            // Load normals
            auto* normalIt = primitive.findAttribute("NORMAL");
            if (normalIt != primitive.attributes.end()) {
                const auto& normalAccessor = asset->accessors[normalIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), normalAccessor,
                    [&](glm::vec3 normal, size_t idx) {
                        result.vertices[vertexOffset + idx].normal = normal;
                    });
            } else {
                // Default normals pointing up
                for (size_t i = 0; i < vertexCount; ++i) {
                    result.vertices[vertexOffset + i].normal = glm::vec3(0, 1, 0);
                }
            }

            // Load texture coordinates
            auto* texCoordIt = primitive.findAttribute("TEXCOORD_0");
            if (texCoordIt != primitive.attributes.end()) {
                const auto& texCoordAccessor = asset->accessors[texCoordIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), texCoordAccessor,
                    [&](glm::vec2 uv, size_t idx) {
                        result.vertices[vertexOffset + idx].texCoord = uv;
                    });
            } else {
                // Default UVs
                for (size_t i = 0; i < vertexCount; ++i) {
                    result.vertices[vertexOffset + i].texCoord = glm::vec2(0, 0);
                }
            }

            // Load tangents if available
            auto* tangentIt = primitive.findAttribute("TANGENT");
            if (tangentIt != primitive.attributes.end()) {
                const auto& tangentAccessor = asset->accessors[tangentIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), tangentAccessor,
                    [&](glm::vec4 tangent, size_t idx) {
                        result.vertices[vertexOffset + idx].tangent = tangent;
                    });
            }
            // Tangents will be calculated later if not present

            // Load indices
            if (primitive.indicesAccessor.has_value()) {
                const auto& indexAccessor = asset->accessors[primitive.indicesAccessor.value()];
                size_t indexOffset = result.indices.size();
                result.indices.reserve(indexOffset + indexAccessor.count);

                fastgltf::iterateAccessor<uint32_t>(asset.get(), indexAccessor,
                    [&](uint32_t index) {
                        result.indices.push_back(static_cast<uint32_t>(vertexOffset) + index);
                    });
            }
        }
    }

    if (result.vertices.empty()) {
        SDL_Log("GLTFLoader: No vertices loaded from %s", path.c_str());
        return std::nullopt;
    }

    // Calculate tangents if they weren't in the file
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

    // Load skeleton data (joints and inverse bind matrices)
    if (!asset->skins.empty()) {
        const auto& skin = asset->skins[0];  // Use first skin

        result.skeleton.joints.reserve(skin.joints.size());

        for (size_t i = 0; i < skin.joints.size(); ++i) {
            size_t nodeIndex = skin.joints[i];
            const auto& node = asset->nodes[nodeIndex];

            Joint joint;
            joint.name = std::string(node.name);
            joint.parentIndex = -1;  // Will be computed below

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

            result.skeleton.joints.push_back(joint);
        }

        // Compute parent indices by traversing node hierarchy
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            size_t nodeIndex = skin.joints[i];

            // Find parent in node tree
            for (size_t parentNodeIdx = 0; parentNodeIdx < asset->nodes.size(); ++parentNodeIdx) {
                const auto& parentNode = asset->nodes[parentNodeIdx];
                for (size_t childIdx : parentNode.children) {
                    if (childIdx == nodeIndex) {
                        // Found parent node, now check if it's in our joint list
                        for (size_t j = 0; j < skin.joints.size(); ++j) {
                            if (skin.joints[j] == parentNodeIdx) {
                                result.skeleton.joints[i].parentIndex = static_cast<int32_t>(j);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        SDL_Log("GLTFLoader: Loaded skeleton with %zu joints", result.skeleton.joints.size());
    }

    // Log available animations (for debugging)
    if (!asset->animations.empty()) {
        SDL_Log("GLTFLoader: File has %zu animations (use loadSkinned to load them)", asset->animations.size());
        for (const auto& anim : asset->animations) {
            SDL_Log("GLTFLoader:   - Animation: '%s' with %zu channels", std::string(anim.name).c_str(), anim.channels.size());
        }
    }

    // Log mesh bounds for debugging
    glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
    for (const auto& v : result.vertices) {
        minBounds = glm::min(minBounds, v.position);
        maxBounds = glm::max(maxBounds, v.position);
    }
    SDL_Log("GLTFLoader: Loaded %zu vertices, %zu indices from %s",
            result.vertices.size(), result.indices.size(), path.c_str());
    SDL_Log("GLTFLoader: Mesh bounds: min(%.2f, %.2f, %.2f) max(%.2f, %.2f, %.2f)",
            minBounds.x, minBounds.y, minBounds.z,
            maxBounds.x, maxBounds.y, maxBounds.z);

    return result;
}

std::optional<GLTFLoadResult> loadMeshOnly(const std::string& path) {
    // For Phase 1, this is the same as load() but we clear the skeleton
    auto result = load(path);
    if (result) {
        result->skeleton.joints.clear();
    }
    return result;
}

std::optional<GLTFSkinnedLoadResult> loadSkinned(const std::string& path) {
    fastgltf::Parser parser;

    std::filesystem::path filePath(path);
    if (!std::filesystem::exists(filePath)) {
        SDL_Log("GLTFLoader: File not found: %s", path.c_str());
        return std::nullopt;
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (data.error() != fastgltf::Error::None) {
        SDL_Log("GLTFLoader: Failed to load file: %s", path.c_str());
        return std::nullopt;
    }

    constexpr auto gltfOptions =
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::GenerateMeshIndices;

    auto asset = parser.loadGltf(data.get(), filePath.parent_path(), gltfOptions);
    if (asset.error() != fastgltf::Error::None) {
        SDL_Log("GLTFLoader: Failed to parse glTF: %s (error: %d)",
                path.c_str(), static_cast<int>(asset.error()));
        return std::nullopt;
    }

    GLTFSkinnedLoadResult result;

    // Load skeleton data FIRST (needed to bind non-skinned primitives to head bone)
    if (!asset->skins.empty()) {
        const auto& skin = asset->skins[0];

        result.skeleton.joints.reserve(skin.joints.size());

        for (size_t i = 0; i < skin.joints.size(); ++i) {
            size_t nodeIndex = skin.joints[i];
            const auto& node = asset->nodes[nodeIndex];

            Joint joint;
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

            result.skeleton.joints.push_back(joint);
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
                                result.skeleton.joints[i].parentIndex = static_cast<int32_t>(j);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        SDL_Log("GLTFLoader: Loaded skinned mesh with %zu joints", result.skeleton.joints.size());
    }

    // Process meshes - for now, combine all primitives into one mesh
    for (const auto& mesh : asset->meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                continue;
            }

            size_t vertexOffset = result.vertices.size();

            // Get position accessor
            auto* positionIt = primitive.findAttribute("POSITION");
            if (positionIt == primitive.attributes.end()) {
                SDL_Log("GLTFLoader: Primitive missing POSITION attribute");
                continue;
            }

            const auto& posAccessor = asset->accessors[positionIt->accessorIndex];
            size_t vertexCount = posAccessor.count;

            // Reserve space for new vertices
            result.vertices.resize(vertexOffset + vertexCount);

            // Get material base color for this primitive
            glm::vec4 baseColor(1.0f);  // Default white
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
            // Non-skinned primitives (like hair) use bone 0 (root) with a negative weight marker
            // The negative weight signals applySkinning to use global transform only (no inverse bind matrix)
            for (size_t i = 0; i < vertexCount; ++i) {
                result.vertices[vertexOffset + i].boneIndices = glm::uvec4(0);
                result.vertices[vertexOffset + i].boneWeights = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);  // Negative = no IBM
                result.vertices[vertexOffset + i].color = baseColor;
            }

            // Load positions
            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                [&](glm::vec3 pos, size_t idx) {
                    result.vertices[vertexOffset + idx].position = pos;
                });

            // Load normals
            auto* normalIt = primitive.findAttribute("NORMAL");
            if (normalIt != primitive.attributes.end()) {
                const auto& normalAccessor = asset->accessors[normalIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), normalAccessor,
                    [&](glm::vec3 normal, size_t idx) {
                        result.vertices[vertexOffset + idx].normal = normal;
                    });
            } else {
                for (size_t i = 0; i < vertexCount; ++i) {
                    result.vertices[vertexOffset + i].normal = glm::vec3(0, 1, 0);
                }
            }

            // Load texture coordinates
            auto* texCoordIt = primitive.findAttribute("TEXCOORD_0");
            if (texCoordIt != primitive.attributes.end()) {
                const auto& texCoordAccessor = asset->accessors[texCoordIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), texCoordAccessor,
                    [&](glm::vec2 uv, size_t idx) {
                        result.vertices[vertexOffset + idx].texCoord = uv;
                    });
            } else {
                for (size_t i = 0; i < vertexCount; ++i) {
                    result.vertices[vertexOffset + i].texCoord = glm::vec2(0, 0);
                }
            }

            // Load tangents if available
            auto* tangentIt = primitive.findAttribute("TANGENT");
            if (tangentIt != primitive.attributes.end()) {
                const auto& tangentAccessor = asset->accessors[tangentIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), tangentAccessor,
                    [&](glm::vec4 tangent, size_t idx) {
                        result.vertices[vertexOffset + idx].tangent = tangent;
                    });
            }

            // Load bone indices (JOINTS_0)
            auto* jointsIt = primitive.findAttribute("JOINTS_0");
            if (jointsIt != primitive.attributes.end()) {
                const auto& jointsAccessor = asset->accessors[jointsIt->accessorIndex];

                // JOINTS_0 can be either unsigned byte or unsigned short
                if (jointsAccessor.componentType == fastgltf::ComponentType::UnsignedByte) {
                    fastgltf::iterateAccessorWithIndex<glm::u8vec4>(asset.get(), jointsAccessor,
                        [&](glm::u8vec4 joints, size_t idx) {
                            result.vertices[vertexOffset + idx].boneIndices = glm::uvec4(joints);
                        });
                } else if (jointsAccessor.componentType == fastgltf::ComponentType::UnsignedShort) {
                    fastgltf::iterateAccessorWithIndex<glm::u16vec4>(asset.get(), jointsAccessor,
                        [&](glm::u16vec4 joints, size_t idx) {
                            result.vertices[vertexOffset + idx].boneIndices = glm::uvec4(joints);
                        });
                }
            }

            // Load bone weights (WEIGHTS_0)
            auto* weightsIt = primitive.findAttribute("WEIGHTS_0");
            if (weightsIt != primitive.attributes.end()) {
                const auto& weightsAccessor = asset->accessors[weightsIt->accessorIndex];
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), weightsAccessor,
                    [&](glm::vec4 weights, size_t idx) {
                        result.vertices[vertexOffset + idx].boneWeights = weights;
                    });
            }

            // Load indices
            if (primitive.indicesAccessor.has_value()) {
                const auto& indexAccessor = asset->accessors[primitive.indicesAccessor.value()];
                size_t indexOffset = result.indices.size();
                result.indices.reserve(indexOffset + indexAccessor.count);

                fastgltf::iterateAccessor<uint32_t>(asset.get(), indexAccessor,
                    [&](uint32_t index) {
                        result.indices.push_back(static_cast<uint32_t>(vertexOffset) + index);
                    });
            }
        }
    }

    if (result.vertices.empty()) {
        SDL_Log("GLTFLoader: No vertices loaded from %s", path.c_str());
        return std::nullopt;
    }

    // Calculate tangents if they weren't in the file
    bool hasTangents = false;
    for (const auto& v : result.vertices) {
        if (glm::length(glm::vec3(v.tangent)) > 0.001f) {
            hasTangents = true;
            break;
        }
    }
    if (!hasTangents) {
        // Calculate tangents for skinned vertices
        for (auto& v : result.vertices) {
            v.tangent = glm::vec4(0.0f);
        }

        for (size_t i = 0; i < result.indices.size(); i += 3) {
            uint32_t i0 = result.indices[i];
            uint32_t i1 = result.indices[i + 1];
            uint32_t i2 = result.indices[i + 2];

            const glm::vec3& p0 = result.vertices[i0].position;
            const glm::vec3& p1 = result.vertices[i1].position;
            const glm::vec3& p2 = result.vertices[i2].position;

            const glm::vec2& uv0 = result.vertices[i0].texCoord;
            const glm::vec2& uv1 = result.vertices[i1].texCoord;
            const glm::vec2& uv2 = result.vertices[i2].texCoord;

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

            result.vertices[i0].tangent += glm::vec4(tangent, 0.0f);
            result.vertices[i1].tangent += glm::vec4(tangent, 0.0f);
            result.vertices[i2].tangent += glm::vec4(tangent, 0.0f);
        }

        for (auto& v : result.vertices) {
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

    // Load animations
    for (const auto& animation : asset->animations) {
        AnimationClip clip;
        clip.name = std::string(animation.name);
        clip.duration = 0.0f;

        // Create a mapping from node index to joint index
        std::unordered_map<size_t, int32_t> nodeToJoint;
        if (!asset->skins.empty()) {
            const auto& skin = asset->skins[0];
            for (size_t i = 0; i < skin.joints.size(); ++i) {
                nodeToJoint[skin.joints[i]] = static_cast<int32_t>(i);
            }
        }

        for (const auto& channel : animation.channels) {
            if (!channel.nodeIndex.has_value()) {
                continue;  // Skip channels without a target node
            }
            size_t nodeIndex = channel.nodeIndex.value();
            auto jointIt = nodeToJoint.find(nodeIndex);
            if (jointIt == nodeToJoint.end()) {
                continue;  // This node isn't part of the skeleton
            }

            int32_t jointIndex = jointIt->second;

            // Find or create channel for this joint
            AnimationChannel* animChannel = nullptr;
            for (auto& ch : clip.channels) {
                if (ch.jointIndex == jointIndex) {
                    animChannel = &ch;
                    break;
                }
            }
            if (!animChannel) {
                clip.channels.push_back(AnimationChannel{});
                animChannel = &clip.channels.back();
                animChannel->jointIndex = jointIndex;
            }

            const auto& sampler = animation.samplers[channel.samplerIndex];
            const auto& inputAccessor = asset->accessors[sampler.inputAccessor];
            const auto& outputAccessor = asset->accessors[sampler.outputAccessor];

            // Load keyframe times
            std::vector<float> times(inputAccessor.count);
            fastgltf::copyFromAccessor<float>(asset.get(), inputAccessor, times.data());

            // Update clip duration
            if (!times.empty() && times.back() > clip.duration) {
                clip.duration = times.back();
            }

            // Load keyframe values based on path type
            switch (channel.path) {
                case fastgltf::AnimationPath::Translation: {
                    animChannel->translation.times = times;
                    animChannel->translation.values.resize(outputAccessor.count);
                    fastgltf::copyFromAccessor<glm::vec3>(asset.get(), outputAccessor, animChannel->translation.values.data());
                    break;
                }
                case fastgltf::AnimationPath::Rotation: {
                    animChannel->rotation.times = times;
                    animChannel->rotation.values.resize(outputAccessor.count);
                    // glTF stores quaternions as (x, y, z, w), glm expects (w, x, y, z)
                    // Use glm::vec4 and manually convert to glm::quat
                    std::vector<glm::vec4> rawQuats(outputAccessor.count);
                    fastgltf::copyFromAccessor<glm::vec4>(asset.get(), outputAccessor, rawQuats.data());
                    for (size_t i = 0; i < rawQuats.size(); ++i) {
                        // glTF: (x, y, z, w) -> glm::quat(w, x, y, z)
                        animChannel->rotation.values[i] = glm::quat(rawQuats[i].w, rawQuats[i].x, rawQuats[i].y, rawQuats[i].z);
                    }
                    break;
                }
                case fastgltf::AnimationPath::Scale: {
                    animChannel->scale.times = times;
                    animChannel->scale.values.resize(outputAccessor.count);
                    fastgltf::copyFromAccessor<glm::vec3>(asset.get(), outputAccessor, animChannel->scale.values.data());
                    break;
                }
                default:
                    break;
            }
        }

        if (!clip.channels.empty()) {
            result.animations.push_back(std::move(clip));
            SDL_Log("GLTFLoader: Loaded animation '%s' with %zu channels, duration %.2fs",
                    result.animations.back().name.c_str(),
                    result.animations.back().channels.size(),
                    result.animations.back().duration);
        }
    }

    if (!result.animations.empty()) {
        SDL_Log("GLTFLoader: Loaded %zu animations total", result.animations.size());
    }

    // Log mesh bounds and bone weights info
    glm::vec3 minBounds(FLT_MAX), maxBounds(-FLT_MAX);
    int vertsWithWeights = 0;
    for (const auto& v : result.vertices) {
        minBounds = glm::min(minBounds, v.position);
        maxBounds = glm::max(maxBounds, v.position);
        if (v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w > 0.99f) {
            vertsWithWeights++;
        }
    }
    SDL_Log("GLTFLoader: Loaded %zu skinned vertices, %zu indices from %s",
            result.vertices.size(), result.indices.size(), path.c_str());
    SDL_Log("GLTFLoader: %d/%zu vertices have bone weights",
            vertsWithWeights, result.vertices.size());
    SDL_Log("GLTFLoader: Mesh bounds: min(%.2f, %.2f, %.2f) max(%.2f, %.2f, %.2f)",
            minBounds.x, minBounds.y, minBounds.z,
            maxBounds.x, maxBounds.y, maxBounds.z);

    return result;
}

} // namespace GLTFLoader

void Skeleton::computeGlobalTransforms(std::vector<glm::mat4>& outGlobalTransforms) const {
    outGlobalTransforms.resize(joints.size());

    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].parentIndex < 0) {
            outGlobalTransforms[i] = joints[i].localTransform;
        } else {
            outGlobalTransforms[i] = outGlobalTransforms[joints[i].parentIndex] * joints[i].localTransform;
        }
    }
}

int32_t Skeleton::findJointIndex(const std::string& name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}
