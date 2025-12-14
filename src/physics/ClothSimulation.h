#pragma once

#include <glm/glm.hpp>
#include <vector>
#include "Mesh.h"

class WindSystem;

// Particle-based cloth simulation using Verlet integration
class ClothSimulation {
public:
    struct Particle {
        glm::vec3 position;
        glm::vec3 oldPosition;
        glm::vec3 acceleration;
        float mass;
        bool pinned;  // Fixed particles (e.g., attached to pole)
    };

    struct DistanceConstraint {
        int particleA;
        int particleB;
        float restLength;
    };

    ClothSimulation() = default;
    ~ClothSimulation() = default;

    // Create a rectangular cloth grid
    void create(int width, int height, float particleSpacing, const glm::vec3& topLeftPosition);

    // Pin particles (fix them in place) - useful for attaching to pole
    void pinParticle(int x, int y);

    // Set particle position (for updating pinned particle positions each frame)
    void setParticlePosition(int x, int y, const glm::vec3& position);

    // Simulation step
    void update(float deltaTime, const WindSystem* windSystem = nullptr);

    // Collision detection
    void addSphereCollision(const glm::vec3& center, float radius);
    void addCapsuleCollision(const glm::vec3& point1, const glm::vec3& point2, float radius);
    void clearCollisions();

    // Update mesh vertices from particle positions
    void updateMesh(Mesh& mesh) const;

    // Create initial mesh geometry
    void createMesh(Mesh& mesh) const;

    // Get cloth dimensions
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    struct SphereCollider {
        glm::vec3 center;
        float radius;
    };

    struct CapsuleCollider {
        glm::vec3 point1;  // First endpoint of capsule axis
        glm::vec3 point2;  // Second endpoint of capsule axis
        float radius;
    };

private:
    void applyForces(const WindSystem* windSystem);
    void satisfyConstraints();
    void updatePositions(float deltaTime);
    void handleCollisions();
    void generateMeshData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const;

    int getParticleIndex(int x, int y) const { return y * width + x; }
    void addConstraint(int x1, int y1, int x2, int y2);

    std::vector<Particle> particles;
    std::vector<DistanceConstraint> constraints;
    std::vector<SphereCollider> sphereColliders;
    std::vector<CapsuleCollider> capsuleColliders;

    int width = 0;
    int height = 0;
    float particleSpacing = 0.1f;

    // Simulation parameters
    static constexpr float DAMPING = 0.01f;
    static constexpr float GRAVITY = 9.81f;
    static constexpr int CONSTRAINT_ITERATIONS = 5;
};
