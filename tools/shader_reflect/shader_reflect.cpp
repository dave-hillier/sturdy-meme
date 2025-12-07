#include <spirv_reflect.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// UBO struct names that are manually defined in separate header files
// These modular UBOs are split from the main UniformBufferObject for cleaner organization
// All UBOs are now auto-generated from SPIR-V reflection
const std::set<std::string> MANUALLY_DEFINED_UBOS = {
    // All UBOs are now auto-generated - no manual definitions needed
};

struct UBOMember {
    std::string name;
    std::string type;
    std::string arraySpec;
    uint32_t offset;
    uint32_t size;
};

struct UBODefinition {
    std::string name;
    std::string structName;
    uint32_t binding;
    uint32_t set;
    uint32_t totalSize;
    bool hasNestedStructs;
    std::vector<UBOMember> members;
};

std::string getGLMType(const SpvReflectTypeDescription* typeDesc) {
    std::string baseType;

    switch (typeDesc->type_flags & 0xFF) {
        case SPV_REFLECT_TYPE_FLAG_BOOL:
            baseType = "bool";
            break;
        case SPV_REFLECT_TYPE_FLAG_INT:
            if (typeDesc->traits.numeric.scalar.signedness) {
                baseType = "int";
            } else {
                baseType = "uint32_t";
            }
            break;
        case SPV_REFLECT_TYPE_FLAG_FLOAT:
            baseType = "float";
            break;
        default:
            baseType = "unknown";
            break;
    }

    // Handle matrices BEFORE vectors (matrices have both flags set)
    if (typeDesc->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX) {
        uint32_t colCount = typeDesc->traits.numeric.matrix.column_count;
        uint32_t rowCount = typeDesc->traits.numeric.matrix.row_count;
        return "glm::mat" + std::to_string(colCount) + (colCount != rowCount ? "x" + std::to_string(rowCount) : "");
    }

    // Handle vectors
    if (typeDesc->type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) {
        uint32_t componentCount = typeDesc->traits.numeric.vector.component_count;
        if (baseType == "float") {
            return "glm::vec" + std::to_string(componentCount);
        } else if (baseType == "int") {
            return "glm::ivec" + std::to_string(componentCount);
        } else if (baseType == "uint32_t") {
            return "glm::uvec" + std::to_string(componentCount);
        }
    }

    return baseType;
}

UBOMember extractMember(const SpvReflectBlockVariable* member) {
    UBOMember m;
    m.name = member->name;
    m.type = getGLMType(member->type_description);
    m.offset = member->offset;
    m.size = member->size;

    // Handle arrays
    if (member->array.dims_count > 0) {
        for (uint32_t i = 0; i < member->array.dims_count; i++) {
            m.arraySpec += "[" + std::to_string(member->array.dims[i]) + "]";
        }
    }

    // Handle structs (nested)
    if (member->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT) {
        m.type = member->type_description->type_name;
    }

    return m;
}

UBODefinition reflectUBO(const SpvReflectDescriptorBinding* binding) {
    UBODefinition ubo;
    ubo.name = binding->name;
    ubo.structName = binding->type_description->type_name;
    ubo.binding = binding->binding;
    ubo.set = binding->set;
    ubo.totalSize = binding->block.size;
    ubo.hasNestedStructs = false;

    // Extract all members
    for (uint32_t i = 0; i < binding->block.member_count; i++) {
        const SpvReflectBlockVariable& member = binding->block.members[i];

        // Check for nested structs
        if (member.type_description->type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT) {
            ubo.hasNestedStructs = true;
        }

        ubo.members.push_back(extractMember(&member));
    }

    return ubo;
}

std::vector<UBODefinition> reflectSPIRV(const std::string& filepath) {
    std::vector<UBODefinition> ubos;

    // Read SPIR-V file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return ubos;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<char> spirvCode(fileSize);
    file.read(spirvCode.data(), fileSize);
    file.close();

    // Create reflection module
    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(
        spirvCode.size(),
        spirvCode.data(),
        &module
    );

    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        std::cerr << "Failed to reflect shader: " << filepath << std::endl;
        return ubos;
    }

    // Enumerate descriptor bindings
    uint32_t count = 0;
    result = spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        spvReflectDestroyShaderModule(&module);
        return ubos;
    }

    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    result = spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        spvReflectDestroyShaderModule(&module);
        return ubos;
    }

    // Extract UBOs
    for (auto* binding : bindings) {
        if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            ubos.push_back(reflectUBO(binding));
        }
    }

    spvReflectDestroyShaderModule(&module);
    return ubos;
}

// Get the size in bytes for a C++ type matching std140 layout
uint32_t getStd140TypeSize(const std::string& type) {
    // Scalars
    if (type == "float" || type == "int" || type == "uint32_t" || type == "bool") return 4;
    // Vec2
    if (type == "glm::vec2" || type == "glm::ivec2" || type == "glm::uvec2") return 8;
    // Vec3 and Vec4 both occupy 16 bytes in std140
    if (type == "glm::vec3" || type == "glm::ivec3" || type == "glm::uvec3") return 12;  // But aligned to 16!
    if (type == "glm::vec4" || type == "glm::ivec4" || type == "glm::uvec4") return 16;
    // Matrices (column-major, each column is a vec4)
    if (type == "glm::mat2") return 32;   // 2 columns x 16 bytes
    if (type == "glm::mat3") return 48;   // 3 columns x 16 bytes
    if (type == "glm::mat4") return 64;   // 4 columns x 16 bytes
    return 4;  // Default to float size
}

// Get the alignment requirement for a C++ type matching std140 layout
uint32_t getStd140TypeAlignment(const std::string& type) {
    // Scalars align to their own size
    if (type == "float" || type == "int" || type == "uint32_t" || type == "bool") return 4;
    // Vec2 aligns to 8
    if (type == "glm::vec2" || type == "glm::ivec2" || type == "glm::uvec2") return 8;
    // Vec3 and Vec4 both align to 16 in std140
    if (type == "glm::vec3" || type == "glm::ivec3" || type == "glm::uvec3") return 16;
    if (type == "glm::vec4" || type == "glm::ivec4" || type == "glm::uvec4") return 16;
    // Matrices align to vec4 (16 bytes)
    if (type.find("glm::mat") != std::string::npos) return 16;
    return 4;
}

std::string generateStructDef(const UBODefinition& ubo) {
    std::ostringstream structDef;

    // Check if this UBO is manually defined in a separate header file
    if (MANUALLY_DEFINED_UBOS.find(ubo.structName) != MANUALLY_DEFINED_UBOS.end()) {
        structDef << "// " << ubo.structName << " - defined in src/" << ubo.structName << ".h\n";
        structDef << "// This modular UBO is part of the split UBO architecture\n";
        structDef << "// Binding: " << ubo.binding << ", Set: " << ubo.set << ", Size: " << ubo.totalSize << " bytes";
    } else if (ubo.hasNestedStructs) {
        structDef << "// SKIPPED: " << ubo.structName
                  << " (contains nested struct types - define manually)\n";
        structDef << "// This struct is defined in its corresponding system header file\n";
        structDef << "// Binding: " << ubo.binding << ", Set: " << ubo.set;
    } else {
        // Use alignas(16) to ensure the struct itself aligns correctly for UBO usage
        structDef << "struct alignas(16) " << ubo.structName << " {\n";

        // Sort members by offset to ensure correct order
        std::vector<UBOMember> sortedMembers = ubo.members;
        std::sort(sortedMembers.begin(), sortedMembers.end(),
            [](const UBOMember& a, const UBOMember& b) {
                return a.offset < b.offset;
            });

        // Generate members with explicit padding for std140 compatibility
        uint32_t currentOffset = 0;
        int paddingIndex = 0;

        for (const auto& member : sortedMembers) {
            // Add padding if there's a gap between current offset and member offset
            if (member.offset > currentOffset) {
                uint32_t paddingNeeded = member.offset - currentOffset;
                // Generate padding as uint8_t array
                structDef << "    uint8_t _pad" << paddingIndex++ << "[" << paddingNeeded << "];  // std140 alignment padding\n";
            }

            // Get std140 alignment requirement
            uint32_t alignment = getStd140TypeAlignment(member.type);

            // Detect if this is a scalar array (float[], int[], etc.) which needs special handling
            // In std140, scalar arrays have each element rounded to 16 bytes
            bool isScalarArray = !member.arraySpec.empty() &&
                                 (member.type == "float" || member.type == "int" || member.type == "uint32_t" || member.type == "bool");

            if (isScalarArray) {
                // For scalar arrays, use uint8_t to preserve exact std140 size
                // member.size is the std140 size (e.g., 48 for float[3], not 12)
                structDef << "    uint8_t " << member.name << "[" << member.size << "];  // std140: " << member.type << member.arraySpec << " (16-byte stride)\n";
            } else {
                // Add alignas for any type that needs alignment > 4 (C++ default)
                // This ensures vec3 (align 16), vec4 (align 16), and matrices (align 16) are correctly aligned
                if (alignment > 4) {
                    structDef << "    alignas(" << alignment << ") " << member.type << " " << member.name << member.arraySpec << ";\n";
                } else {
                    structDef << "    " << member.type << " " << member.name << member.arraySpec << ";\n";
                }
            }

            // Calculate size (use SPIR-V reported size, which is std140 size)
            uint32_t memberSize = member.size;
            currentOffset = member.offset + memberSize;
        }

        // Add trailing padding if struct size is less than total reported size
        if (currentOffset < ubo.totalSize) {
            uint32_t trailingPadding = ubo.totalSize - currentOffset;
            structDef << "    uint8_t _paddingEnd[" << trailingPadding << "];  // std140 trailing padding\n";
        }

        structDef << "};\n";
        structDef << "static_assert(sizeof(" << ubo.structName << ") == " << ubo.totalSize
                  << ", \"" << ubo.structName << " size mismatch with std140 layout\");";
    }

    return structDef.str();
}

std::string generateHeader(const std::map<std::string, UBODefinition>& uniqueUBOs) {
    std::ostringstream header;

    header << "// Auto-generated by shader_reflect tool\n";
    header << "// DO NOT EDIT MANUALLY - this file is generated from SPIR-V shaders\n";
    header << "// Structs are laid out to match std140 GLSL layout for UBO compatibility\n";
    header << "\n";
    header << "#pragma once\n";
    header << "\n";
    header << "#include <glm/glm.hpp>\n";
    header << "#include <cstdint>\n";
    header << "\n";

    for (const auto& [name, ubo] : uniqueUBOs) {
        header << "// Binding: " << ubo.binding << ", Set: " << ubo.set << "\n";
        header << generateStructDef(ubo) << "\n\n";
    }

    return header.str();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: shader_reflect <output_header> <spirv_file1> [spirv_file2 ...]\n";
        return 1;
    }

    std::string outputPath = argv[1];
    std::map<std::string, UBODefinition> uniqueUBOs;

    // Process all SPIR-V files
    for (int i = 2; i < argc; i++) {
        std::string spirvPath = argv[i];

        auto ubos = reflectSPIRV(spirvPath);

        // Merge UBOs by struct name - keep the one with most members (largest definition)
        for (const auto& ubo : ubos) {
            const std::string& structName = ubo.structName;

            auto it = uniqueUBOs.find(structName);
            if (it == uniqueUBOs.end()) {
                // First time seeing this struct
                uniqueUBOs[structName] = ubo;
            } else {
                // Keep the definition with more members (more complete)
                if (ubo.members.size() > it->second.members.size()) {
                    it->second = ubo;
                }
                // If same member count, keep the one with larger total size
                else if (ubo.members.size() == it->second.members.size() &&
                         ubo.totalSize > it->second.totalSize) {
                    it->second = ubo;
                }
            }
        }
    }

    // Generate header file
    std::string headerContent = generateHeader(uniqueUBOs);

    // Write to file
    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Failed to write output file: " << outputPath << std::endl;
        return 1;
    }

    outFile << headerContent;
    outFile.close();

    std::cout << "Generated " << outputPath << " with " << uniqueUBOs.size() << " UBO definitions\n";

    return 0;
}
