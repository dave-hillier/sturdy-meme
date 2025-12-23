#include "TreeOptions.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <SDL3/SDL_log.h>

using json = nlohmann::json;

TreeOptions TreeOptions::defaultOak() {
    TreeOptions opts;
    opts.seed = 12345;
    opts.type = TreeType::Deciduous;

    opts.bark.type = "oak";
    opts.bark.tint = glm::vec3(1.0f);
    opts.bark.flatShading = false;
    opts.bark.textured = true;
    opts.bark.textureScale = glm::vec2(1.0f);

    opts.branch.levels = 3;
    opts.branch.angle = {0.0f, 70.0f, 60.0f, 60.0f};
    opts.branch.children = {7, 7, 5, 0};
    opts.branch.forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    opts.branch.forceStrength = 0.01f;
    opts.branch.gnarliness = {0.15f, 0.2f, 0.3f, 0.02f};
    opts.branch.length = {20.0f, 20.0f, 10.0f, 1.0f};
    opts.branch.radius = {1.5f, 0.7f, 0.7f, 0.7f};
    opts.branch.sections = {12, 10, 8, 6};
    opts.branch.segments = {8, 6, 4, 3};
    opts.branch.start = {0.0f, 0.4f, 0.3f, 0.3f};
    opts.branch.taper = {0.7f, 0.7f, 0.7f, 0.7f};
    opts.branch.twist = {0.0f, 0.0f, 0.0f, 0.0f};

    opts.leaves.type = "oak";
    opts.leaves.billboard = BillboardMode::Double;
    opts.leaves.angle = 10.0f;
    opts.leaves.count = 1;
    opts.leaves.start = 0.0f;
    opts.leaves.size = 2.5f;
    opts.leaves.sizeVariance = 0.7f;
    opts.leaves.tint = glm::vec3(1.0f);
    opts.leaves.alphaTest = 0.5f;

    return opts;
}

TreeOptions TreeOptions::defaultPine() {
    TreeOptions opts;
    opts.seed = 54321;
    opts.type = TreeType::Evergreen;

    opts.bark.type = "pine";
    opts.bark.tint = glm::vec3(0.8f, 0.7f, 0.6f);
    opts.bark.flatShading = false;
    opts.bark.textured = true;
    opts.bark.textureScale = glm::vec2(1.0f, 2.0f);

    opts.branch.levels = 3;
    opts.branch.angle = {0.0f, 80.0f, 70.0f, 60.0f};
    opts.branch.children = {12, 8, 4, 0};
    opts.branch.forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    opts.branch.forceStrength = 0.02f;
    opts.branch.gnarliness = {0.05f, 0.1f, 0.15f, 0.02f};
    opts.branch.length = {30.0f, 8.0f, 3.0f, 0.5f};
    opts.branch.radius = {1.0f, 0.3f, 0.15f, 0.05f};
    opts.branch.sections = {20, 6, 4, 3};
    opts.branch.segments = {8, 5, 4, 3};
    opts.branch.start = {0.0f, 0.2f, 0.2f, 0.3f};
    opts.branch.taper = {0.9f, 0.8f, 0.7f, 0.7f};
    opts.branch.twist = {0.0f, 0.0f, 0.0f, 0.0f};

    opts.leaves.type = "pine";
    opts.leaves.billboard = BillboardMode::Double;
    opts.leaves.angle = 30.0f;
    opts.leaves.count = 8;
    opts.leaves.start = 0.3f;
    opts.leaves.size = 1.5f;
    opts.leaves.sizeVariance = 0.3f;
    opts.leaves.tint = glm::vec3(0.3f, 0.5f, 0.3f);
    opts.leaves.alphaTest = 0.5f;

    return opts;
}

TreeOptions TreeOptions::defaultBirch() {
    TreeOptions opts;
    opts.seed = 11111;
    opts.type = TreeType::Deciduous;

    opts.bark.type = "birch";
    opts.bark.tint = glm::vec3(1.0f);
    opts.bark.flatShading = false;
    opts.bark.textured = true;
    opts.bark.textureScale = glm::vec2(1.0f);

    opts.branch.levels = 3;
    opts.branch.angle = {0.0f, 50.0f, 45.0f, 40.0f};
    opts.branch.children = {5, 5, 4, 0};
    opts.branch.forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    opts.branch.forceStrength = 0.015f;
    opts.branch.gnarliness = {0.1f, 0.15f, 0.2f, 0.02f};
    opts.branch.length = {18.0f, 12.0f, 6.0f, 1.0f};
    opts.branch.radius = {0.8f, 0.4f, 0.2f, 0.1f};
    opts.branch.sections = {10, 8, 6, 4};
    opts.branch.segments = {6, 5, 4, 3};
    opts.branch.start = {0.0f, 0.5f, 0.4f, 0.3f};
    opts.branch.taper = {0.6f, 0.6f, 0.6f, 0.6f};
    opts.branch.twist = {0.0f, 0.0f, 0.0f, 0.0f};

    opts.leaves.type = "aspen";
    opts.leaves.billboard = BillboardMode::Double;
    opts.leaves.angle = 20.0f;
    opts.leaves.count = 3;
    opts.leaves.start = 0.2f;
    opts.leaves.size = 1.8f;
    opts.leaves.sizeVariance = 0.5f;
    opts.leaves.tint = glm::vec3(0.8f, 1.0f, 0.7f);
    opts.leaves.alphaTest = 0.5f;

    return opts;
}

TreeOptions TreeOptions::defaultWillow() {
    TreeOptions opts;
    opts.seed = 22222;
    opts.type = TreeType::Deciduous;

    opts.bark.type = "willow";
    opts.bark.tint = glm::vec3(0.7f, 0.65f, 0.5f);
    opts.bark.flatShading = false;
    opts.bark.textured = true;
    opts.bark.textureScale = glm::vec2(1.0f);

    opts.branch.levels = 3;
    opts.branch.angle = {0.0f, 60.0f, 80.0f, 90.0f};
    opts.branch.children = {6, 8, 6, 0};
    opts.branch.forceDirection = glm::vec3(0.0f, -0.5f, 0.0f);  // Drooping
    opts.branch.forceStrength = 0.03f;
    opts.branch.gnarliness = {0.1f, 0.2f, 0.4f, 0.1f};
    opts.branch.length = {15.0f, 15.0f, 12.0f, 3.0f};
    opts.branch.radius = {1.2f, 0.5f, 0.2f, 0.05f};
    opts.branch.sections = {10, 12, 10, 8};
    opts.branch.segments = {8, 6, 4, 3};
    opts.branch.start = {0.0f, 0.3f, 0.2f, 0.1f};
    opts.branch.taper = {0.5f, 0.6f, 0.7f, 0.8f};
    opts.branch.twist = {0.0f, 0.05f, 0.1f, 0.0f};

    opts.leaves.type = "ash";
    opts.leaves.billboard = BillboardMode::Single;
    opts.leaves.angle = 45.0f;
    opts.leaves.count = 5;
    opts.leaves.start = 0.1f;
    opts.leaves.size = 1.2f;
    opts.leaves.sizeVariance = 0.4f;
    opts.leaves.tint = glm::vec3(0.6f, 0.8f, 0.5f);
    opts.leaves.alphaTest = 0.5f;

    return opts;
}

TreeOptions TreeOptions::defaultAspen() {
    TreeOptions opts;
    opts.seed = 33333;
    opts.type = TreeType::Deciduous;

    opts.bark.type = "birch";
    opts.bark.tint = glm::vec3(0.95f, 0.95f, 0.9f);
    opts.bark.flatShading = false;
    opts.bark.textured = true;
    opts.bark.textureScale = glm::vec2(1.0f);

    opts.branch.levels = 3;
    opts.branch.angle = {0.0f, 55.0f, 50.0f, 45.0f};
    opts.branch.children = {6, 5, 4, 0};
    opts.branch.forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    opts.branch.forceStrength = 0.02f;
    opts.branch.gnarliness = {0.08f, 0.12f, 0.18f, 0.02f};
    opts.branch.length = {22.0f, 14.0f, 7.0f, 1.5f};
    opts.branch.radius = {0.9f, 0.45f, 0.22f, 0.1f};
    opts.branch.sections = {12, 8, 6, 4};
    opts.branch.segments = {6, 5, 4, 3};
    opts.branch.start = {0.0f, 0.45f, 0.35f, 0.3f};
    opts.branch.taper = {0.65f, 0.65f, 0.65f, 0.65f};
    opts.branch.twist = {0.0f, 0.0f, 0.0f, 0.0f};

    opts.leaves.type = "aspen";
    opts.leaves.billboard = BillboardMode::Double;
    opts.leaves.angle = 15.0f;
    opts.leaves.count = 2;
    opts.leaves.start = 0.15f;
    opts.leaves.size = 2.0f;
    opts.leaves.sizeVariance = 0.6f;
    opts.leaves.tint = glm::vec3(0.9f, 1.0f, 0.8f);
    opts.leaves.alphaTest = 0.5f;

    return opts;
}

TreeOptions TreeOptions::defaultBush() {
    TreeOptions opts;
    opts.seed = 44444;
    opts.type = TreeType::Deciduous;

    opts.bark.type = "oak";
    opts.bark.tint = glm::vec3(0.5f, 0.4f, 0.3f);
    opts.bark.flatShading = false;
    opts.bark.textured = true;
    opts.bark.textureScale = glm::vec2(0.5f);

    opts.branch.levels = 2;
    opts.branch.angle = {0.0f, 80.0f, 70.0f, 0.0f};
    opts.branch.children = {8, 5, 0, 0};
    opts.branch.forceDirection = glm::vec3(0.0f, 0.3f, 0.0f);
    opts.branch.forceStrength = 0.005f;
    opts.branch.gnarliness = {0.3f, 0.4f, 0.2f, 0.0f};
    opts.branch.length = {2.0f, 2.0f, 1.0f, 0.0f};
    opts.branch.radius = {0.3f, 0.15f, 0.08f, 0.0f};
    opts.branch.sections = {6, 4, 3, 0};
    opts.branch.segments = {5, 4, 3, 0};
    opts.branch.start = {0.0f, 0.1f, 0.2f, 0.0f};
    opts.branch.taper = {0.5f, 0.5f, 0.5f, 0.0f};
    opts.branch.twist = {0.1f, 0.1f, 0.0f, 0.0f};

    opts.leaves.type = "oak";
    opts.leaves.billboard = BillboardMode::Double;
    opts.leaves.angle = 45.0f;
    opts.leaves.count = 8;
    opts.leaves.start = 0.0f;
    opts.leaves.size = 0.8f;
    opts.leaves.sizeVariance = 0.3f;
    opts.leaves.tint = glm::vec3(0.4f, 0.6f, 0.3f);
    opts.leaves.alphaTest = 0.5f;

    return opts;
}

// Helper to convert hex color (as used in ez-tree JSON) to glm::vec3
static glm::vec3 hexToVec3(uint32_t hex) {
    float r = ((hex >> 16) & 0xFF) / 255.0f;
    float g = ((hex >> 8) & 0xFF) / 255.0f;
    float b = (hex & 0xFF) / 255.0f;
    return glm::vec3(r, g, b);
}

TreeOptions TreeOptions::loadFromJson(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeOptions: Failed to open preset file: %s", jsonPath.c_str());
        return defaultOak();
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromJsonString(content);
}

TreeOptions TreeOptions::loadFromJsonString(const std::string& jsonString) {
    TreeOptions opts;

    try {
        json j = json::parse(jsonString);

        // Seed
        opts.seed = j.value("seed", 0u);

        // Tree type
        std::string typeStr = j.value("type", "deciduous");
        opts.type = (typeStr == "evergreen") ? TreeType::Evergreen : TreeType::Deciduous;

        // Bark options
        if (j.contains("bark")) {
            const auto& bark = j["bark"];
            opts.bark.type = bark.value("type", "oak");
            if (bark.contains("tint")) {
                opts.bark.tint = hexToVec3(bark["tint"].get<uint32_t>());
            }
            opts.bark.flatShading = bark.value("flatShading", false);
            opts.bark.textured = bark.value("textured", true);
            if (bark.contains("textureScale")) {
                opts.bark.textureScale.x = bark["textureScale"].value("x", 1.0f);
                opts.bark.textureScale.y = bark["textureScale"].value("y", 1.0f);
            }
        }

        // Branch options
        if (j.contains("branch")) {
            const auto& branch = j["branch"];
            opts.branch.levels = branch.value("levels", 3);

            // Per-level arrays (indexed by string keys "0", "1", "2", "3")
            if (branch.contains("angle")) {
                const auto& a = branch["angle"];
                opts.branch.angle[1] = a.value("1", 60.0f);
                opts.branch.angle[2] = a.value("2", 45.0f);
                opts.branch.angle[3] = a.value("3", 30.0f);
            }
            if (branch.contains("children")) {
                const auto& c = branch["children"];
                opts.branch.children[0] = c.value("0", 7);
                opts.branch.children[1] = c.value("1", 5);
                opts.branch.children[2] = c.value("2", 3);
            }
            if (branch.contains("force")) {
                const auto& f = branch["force"];
                if (f.contains("direction")) {
                    opts.branch.forceDirection.x = f["direction"].value("x", 0.0f);
                    opts.branch.forceDirection.y = f["direction"].value("y", 1.0f);
                    opts.branch.forceDirection.z = f["direction"].value("z", 0.0f);
                }
                opts.branch.forceStrength = f.value("strength", 0.01f);
            }
            if (branch.contains("gnarliness")) {
                const auto& g = branch["gnarliness"];
                opts.branch.gnarliness[0] = g.value("0", 0.1f);
                opts.branch.gnarliness[1] = g.value("1", 0.15f);
                opts.branch.gnarliness[2] = g.value("2", 0.2f);
                opts.branch.gnarliness[3] = g.value("3", 0.05f);
            }
            if (branch.contains("length")) {
                const auto& l = branch["length"];
                opts.branch.length[0] = l.value("0", 20.0f);
                opts.branch.length[1] = l.value("1", 15.0f);
                opts.branch.length[2] = l.value("2", 10.0f);
                opts.branch.length[3] = l.value("3", 5.0f);
            }
            if (branch.contains("radius")) {
                const auto& r = branch["radius"];
                opts.branch.radius[0] = r.value("0", 1.5f);
                opts.branch.radius[1] = r.value("1", 0.7f);
                opts.branch.radius[2] = r.value("2", 0.5f);
                opts.branch.radius[3] = r.value("3", 0.3f);
            }
            if (branch.contains("sections")) {
                const auto& s = branch["sections"];
                opts.branch.sections[0] = s.value("0", 12);
                opts.branch.sections[1] = s.value("1", 8);
                opts.branch.sections[2] = s.value("2", 6);
                opts.branch.sections[3] = s.value("3", 4);
            }
            if (branch.contains("segments")) {
                const auto& s = branch["segments"];
                opts.branch.segments[0] = s.value("0", 8);
                opts.branch.segments[1] = s.value("1", 6);
                opts.branch.segments[2] = s.value("2", 4);
                opts.branch.segments[3] = s.value("3", 3);
            }
            if (branch.contains("start")) {
                const auto& s = branch["start"];
                opts.branch.start[1] = s.value("1", 0.4f);
                opts.branch.start[2] = s.value("2", 0.3f);
                opts.branch.start[3] = s.value("3", 0.2f);
            }
            if (branch.contains("taper")) {
                const auto& t = branch["taper"];
                opts.branch.taper[0] = t.value("0", 0.7f);
                opts.branch.taper[1] = t.value("1", 0.7f);
                opts.branch.taper[2] = t.value("2", 0.7f);
                opts.branch.taper[3] = t.value("3", 0.7f);
            }
            if (branch.contains("twist")) {
                const auto& t = branch["twist"];
                opts.branch.twist[0] = t.value("0", 0.0f);
                opts.branch.twist[1] = t.value("1", 0.0f);
                opts.branch.twist[2] = t.value("2", 0.0f);
                opts.branch.twist[3] = t.value("3", 0.0f);
            }
        }

        // Leaf options
        if (j.contains("leaves")) {
            const auto& leaves = j["leaves"];
            opts.leaves.type = leaves.value("type", "oak");
            std::string billboard = leaves.value("billboard", "double");
            opts.leaves.billboard = (billboard == "single") ? BillboardMode::Single : BillboardMode::Double;
            opts.leaves.angle = leaves.value("angle", 30.0f);
            opts.leaves.count = leaves.value("count", 5);
            opts.leaves.start = leaves.value("start", 0.0f);
            opts.leaves.size = leaves.value("size", 2.5f);
            opts.leaves.sizeVariance = leaves.value("sizeVariance", 0.5f);
            if (leaves.contains("tint")) {
                opts.leaves.tint = hexToVec3(leaves["tint"].get<uint32_t>());
            }
            opts.leaves.alphaTest = leaves.value("alphaTest", 0.5f);
        }

        SDL_Log("TreeOptions: Loaded preset with seed=%u, bark=%s, leaves=%s",
                opts.seed, opts.bark.type.c_str(), opts.leaves.type.c_str());

    } catch (const json::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeOptions: JSON parse error: %s", e.what());
        return defaultOak();
    }

    return opts;
}

TreeParamsGPU TreeParamsGPU::fromOptions(const TreeOptions& opts) {
    TreeParamsGPU gpu{};

    gpu.seed = opts.seed;
    gpu.type = static_cast<uint32_t>(opts.type);
    gpu.branchLevels = static_cast<uint32_t>(opts.branch.levels);

    gpu.branchAngle = glm::vec4(
        opts.branch.angle[0], opts.branch.angle[1],
        opts.branch.angle[2], opts.branch.angle[3]);
    gpu.branchChildren = glm::vec4(
        static_cast<float>(opts.branch.children[0]),
        static_cast<float>(opts.branch.children[1]),
        static_cast<float>(opts.branch.children[2]),
        static_cast<float>(opts.branch.children[3]));
    gpu.branchGnarliness = glm::vec4(
        opts.branch.gnarliness[0], opts.branch.gnarliness[1],
        opts.branch.gnarliness[2], opts.branch.gnarliness[3]);
    gpu.branchLength = glm::vec4(
        opts.branch.length[0], opts.branch.length[1],
        opts.branch.length[2], opts.branch.length[3]);
    gpu.branchRadius = glm::vec4(
        opts.branch.radius[0], opts.branch.radius[1],
        opts.branch.radius[2], opts.branch.radius[3]);
    gpu.branchSections = glm::vec4(
        static_cast<float>(opts.branch.sections[0]),
        static_cast<float>(opts.branch.sections[1]),
        static_cast<float>(opts.branch.sections[2]),
        static_cast<float>(opts.branch.sections[3]));
    gpu.branchSegments = glm::vec4(
        static_cast<float>(opts.branch.segments[0]),
        static_cast<float>(opts.branch.segments[1]),
        static_cast<float>(opts.branch.segments[2]),
        static_cast<float>(opts.branch.segments[3]));
    gpu.branchStart = glm::vec4(
        opts.branch.start[0], opts.branch.start[1],
        opts.branch.start[2], opts.branch.start[3]);
    gpu.branchTaper = glm::vec4(
        opts.branch.taper[0], opts.branch.taper[1],
        opts.branch.taper[2], opts.branch.taper[3]);
    gpu.branchTwist = glm::vec4(
        opts.branch.twist[0], opts.branch.twist[1],
        opts.branch.twist[2], opts.branch.twist[3]);

    gpu.forceDirectionAndStrength = glm::vec4(
        opts.branch.forceDirection, opts.branch.forceStrength);

    gpu.leafBillboard = static_cast<uint32_t>(opts.leaves.billboard);
    gpu.leafAngle = opts.leaves.angle;
    gpu.leafCount = static_cast<uint32_t>(opts.leaves.count);
    gpu.leafStart = opts.leaves.start;
    gpu.leafSize = opts.leaves.size;
    gpu.leafSizeVariance = opts.leaves.sizeVariance;
    gpu.leafAlphaTest = opts.leaves.alphaTest;

    // Convert string bark type to index for GPU (not used for texture selection, just for reference)
    gpu.barkType = 0;  // Default
    if (opts.bark.type == "birch") gpu.barkType = 0;
    else if (opts.bark.type == "oak") gpu.barkType = 1;
    else if (opts.bark.type == "pine") gpu.barkType = 2;
    else if (opts.bark.type == "willow") gpu.barkType = 3;
    gpu.barkTextured = opts.bark.textured ? 1 : 0;
    gpu.barkTextureScale = opts.bark.textureScale;
    gpu.barkTint = glm::vec4(opts.bark.tint, 1.0f);
    gpu.leafTint = glm::vec4(opts.leaves.tint, opts.leaves.autumnHueShift);

    return gpu;
}

TreeInstanceGPU TreeInstanceGPU::fromInstance(const TreeInstance& inst) {
    TreeInstanceGPU gpu{};
    gpu.positionAndRotation = glm::vec4(inst.position, inst.rotation);
    gpu.scaleAndIndices = glm::vec4(
        inst.scale,
        static_cast<float>(inst.optionsIndex),
        0.0f, 0.0f);
    return gpu;
}
