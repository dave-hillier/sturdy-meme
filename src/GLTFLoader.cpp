#include "GLTFLoader.h"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <SDL3/SDL_log.h>
#include <filesystem>

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

    SDL_Log("GLTFLoader: Loaded %zu vertices, %zu indices from %s",
            result.vertices.size(), result.indices.size(), path.c_str());

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
