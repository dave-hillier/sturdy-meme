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

struct UBODefinition {
    std::string name;
    uint32_t binding;
    uint32_t set;
    std::string structDef;
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

    // Handle matrices
    if (typeDesc->type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX) {
        uint32_t colCount = typeDesc->traits.numeric.matrix.column_count;
        uint32_t rowCount = typeDesc->traits.numeric.matrix.row_count;
        return "glm::mat" + std::to_string(colCount) + (colCount != rowCount ? "x" + std::to_string(rowCount) : "");
    }

    return baseType;
}

std::string generateStructMember(const SpvReflectBlockVariable* member, int indent = 1) {
    std::ostringstream oss;
    std::string indentStr(indent * 4, ' ');

    std::string type = getGLMType(member->type_description);

    // Handle arrays
    std::string arraySpec;
    if (member->array.dims_count > 0) {
        for (uint32_t i = 0; i < member->array.dims_count; i++) {
            arraySpec += "[" + std::to_string(member->array.dims[i]) + "]";
        }
    }

    // Handle structs (nested)
    if (member->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT) {
        // For nested structs, we'd need to recurse, but for now keep it simple
        type = member->type_description->type_name;
    }

    oss << indentStr << type << " " << member->name << arraySpec << ";";

    return oss.str();
}

UBODefinition reflectUBO(const SpvReflectDescriptorBinding* binding) {
    UBODefinition ubo;
    ubo.name = binding->name;
    ubo.binding = binding->binding;
    ubo.set = binding->set;

    std::ostringstream structDef;
    structDef << "struct " << binding->type_description->type_name << " {\n";

    // Generate members
    for (uint32_t i = 0; i < binding->block.member_count; i++) {
        const SpvReflectBlockVariable& member = binding->block.members[i];
        structDef << generateStructMember(&member) << "\n";
    }

    structDef << "};";

    ubo.structDef = structDef.str();
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
        header << ubo.structDef << "\n\n";
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

        // Deduplicate UBOs by struct name
        for (const auto& ubo : ubos) {
            std::string structName = ubo.structDef.substr(
                ubo.structDef.find("struct ") + 7,
                ubo.structDef.find(" {") - 7
            );

            // Only add if we haven't seen this struct name before
            if (uniqueUBOs.find(structName) == uniqueUBOs.end()) {
                uniqueUBOs[structName] = ubo;
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
