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
// The manual definitions are used instead of auto-generated to support bootstrapping
const std::set<std::string> MANUALLY_DEFINED_UBOS = {
    "SnowUBO",           // Defined in src/SnowUBO.h (binding 10)
    "CloudShadowUBO",    // Defined in src/CloudShadowUBO.h (binding 11)
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
        structDef << "struct " << ubo.structName << " {\n";

        // Sort members by offset to ensure correct order
        std::vector<UBOMember> sortedMembers = ubo.members;
        std::sort(sortedMembers.begin(), sortedMembers.end(),
            [](const UBOMember& a, const UBOMember& b) {
                return a.offset < b.offset;
            });

        // Generate members
        for (const auto& member : sortedMembers) {
            structDef << "    " << member.type << " " << member.name << member.arraySpec << ";\n";
        }

        structDef << "};";
    }

    return structDef.str();
}

std::string generateHeader(const std::map<std::string, UBODefinition>& uniqueUBOs) {
    std::ostringstream header;

    header << "// Auto-generated by shader_reflect tool\n";
    header << "// DO NOT EDIT MANUALLY - this file is generated from SPIR-V shaders\n";
    header << "\n";
    header << "#pragma once\n";
    header << "\n";
    header << "#include <glm/glm.hpp>\n";
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
