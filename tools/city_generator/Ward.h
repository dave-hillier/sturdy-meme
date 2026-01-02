// Ward: District types that occupy patches in the city
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// - Each Ward type has a rateLocation() that scores patch suitability
// - Lower scores = better fit for that ward type
// - Ward::createGeometry() generates building polygons
// - Streets widths: MAIN_STREET=2.0, REGULAR_STREET=1.0, ALLEY=0.6
// - createAlleys() recursively subdivides blocks into building plots

#pragma once

#include "Geometry.h"
#include "Patch.h"
#include <vector>
#include <random>
#include <algorithm>
#include <limits>
#include <string>

namespace city {

// Forward declarations
class Model;

// Street width constants (in city units)
constexpr float MAIN_STREET = 2.0f;
constexpr float REGULAR_STREET = 1.0f;
constexpr float ALLEY = 0.6f;

// Ward type enumeration
enum class WardType {
    Castle,
    Cathedral,
    Market,
    Patriciate,     // Wealthy residential
    Craftsmen,      // Artisan district
    Merchants,      // Commercial
    Administration, // Government buildings
    Military,       // Barracks
    Slum,           // Poor housing
    Farm,           // Agricultural
    Park,           // Green space
    Gate,           // City entrance
    Common          // Generic residential
};

inline const char* wardTypeName(WardType type) {
    switch (type) {
        case WardType::Castle: return "castle";
        case WardType::Cathedral: return "cathedral";
        case WardType::Market: return "market";
        case WardType::Patriciate: return "patriciate";
        case WardType::Craftsmen: return "craftsmen";
        case WardType::Merchants: return "merchants";
        case WardType::Administration: return "administration";
        case WardType::Military: return "military";
        case WardType::Slum: return "slum";
        case WardType::Farm: return "farm";
        case WardType::Park: return "park";
        case WardType::Gate: return "gate";
        case WardType::Common: return "common";
        default: return "unknown";
    }
}

// Base Ward class
class Ward {
public:
    Model* model = nullptr;
    Patch* patch = nullptr;
    WardType type = WardType::Common;
    std::vector<Polygon> geometry;  // Building footprints

    Ward(Model* m, Patch* p, WardType t) : model(m), patch(p), type(t) {}
    virtual ~Ward() = default;

    // Generate building geometry for this ward
    virtual void createGeometry(std::mt19937& rng);

    // Get the city block (patch shape inset by street width)
    Polygon getCityBlock() const;

    // Filter out buildings in outskirts based on density
    void filterOutskirts(std::mt19937& rng, float emptyProb = 0.2f);

    // Get display label for this ward
    virtual std::string getLabel() const { return ""; }

    // Rate how suitable a patch is for this ward type
    // Returns infinity for unsuitable, lower = better
    static float rateLocation(const Model& model, const Patch& patch, WardType type);

    // Recursively subdivide polygon into building plots (alleys)
    static std::vector<Polygon> createAlleys(
        const Polygon& block,
        float minArea,
        float gridChaos,
        float sizeChaos,
        std::mt19937& rng);

    // Create an orthogonal (rectangular) building within a polygon
    static Polygon createOrthoBuilding(
        const Polygon& poly,
        float ratio,
        std::mt19937& rng);

protected:
    // Find the longest edge of a polygon
    static std::pair<size_t, float> findLongestEdge(const Polygon& poly);
};

// Castle ward - central fortification
class CastleWard : public Ward {
public:
    Polygon curtainWall;  // Castle's own wall

    CastleWard(Model* m, Patch* p) : Ward(m, p, WardType::Castle) {}

    void createGeometry(std::mt19937& rng) override;
    std::string getLabel() const override { return "Castle"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Cathedral ward - major church
class CathedralWard : public Ward {
public:
    CathedralWard(Model* m, Patch* p) : Ward(m, p, WardType::Cathedral) {}

    void createGeometry(std::mt19937& rng) override;
    std::string getLabel() const override { return "Cathedral"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Market ward - central marketplace
class MarketWard : public Ward {
public:
    Polygon fountain;  // Central fountain or statue

    MarketWard(Model* m, Patch* p) : Ward(m, p, WardType::Market) {}

    void createGeometry(std::mt19937& rng) override;
    std::string getLabel() const override { return "Market"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Common residential ward - configurable building density
class CommonWard : public Ward {
public:
    float minBuildingArea = 20.0f;
    float gridChaos = 0.0f;
    float sizeChaos = 0.0f;
    float emptyProb = 0.0f;

    CommonWard(Model* m, Patch* p, WardType t,
               float minArea, float chaos, float sizeVar, float empty)
        : Ward(m, p, t)
        , minBuildingArea(minArea)
        , gridChaos(chaos)
        , sizeChaos(sizeVar)
        , emptyProb(empty) {}

    void createGeometry(std::mt19937& rng) override;
};

// Patriciate ward - wealthy residential
class PatriciateWard : public CommonWard {
public:
    PatriciateWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Patriciate, 80.0f, 0.0f, 0.4f, 0.2f) {}

    std::string getLabel() const override { return "Patriciate"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Craftsmen ward - artisan workshops
class CraftsmenWard : public CommonWard {
public:
    CraftsmenWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Craftsmen, 20.0f, 0.4f, 0.8f, 0.1f) {}

    std::string getLabel() const override { return "Craftsmen"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Merchants ward - shops and commerce
class MerchantsWard : public CommonWard {
public:
    MerchantsWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Merchants, 30.0f, 0.3f, 0.6f, 0.1f) {}

    std::string getLabel() const override { return "Merchants"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Administration ward - government
class AdministrationWard : public CommonWard {
public:
    AdministrationWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Administration, 100.0f, 0.0f, 0.3f, 0.3f) {}

    std::string getLabel() const override { return "Administration"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Military ward - barracks
class MilitaryWard : public CommonWard {
public:
    MilitaryWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Military, 50.0f, 0.1f, 0.2f, 0.3f) {}

    std::string getLabel() const override { return "Military"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Slum ward - poor housing
class SlumWard : public CommonWard {
public:
    SlumWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Slum, 10.0f, 0.8f, 0.9f, 0.0f) {}

    std::string getLabel() const override { return "Slum"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Farm ward - agricultural
class FarmWard : public Ward {
public:
    FarmWard(Model* m, Patch* p) : Ward(m, p, WardType::Farm) {}

    void createGeometry(std::mt19937& rng) override;
    std::string getLabel() const override { return "Farm"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Park ward - gardens/green space
class ParkWard : public Ward {
public:
    ParkWard(Model* m, Patch* p) : Ward(m, p, WardType::Park) {}

    void createGeometry(std::mt19937& rng) override;
    std::string getLabel() const override { return "Park"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

// Gate ward - city entrance
class GateWard : public CommonWard {
public:
    GateWard(Model* m, Patch* p)
        : CommonWard(m, p, WardType::Gate, 25.0f, 0.3f, 0.6f, 0.15f) {}

    std::string getLabel() const override { return "Gate"; }

    static float rateLocation(const Model& model, const Patch& patch);
};

} // namespace city
