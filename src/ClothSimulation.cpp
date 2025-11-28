#include "ClothSimulation.h"
#include "WindSystem.h"
#include <cmath>

void ClothSimulation::create(int w, int h, float spacing, const glm::vec3& topLeftPosition) {
    width = w;
    height = h;
    particleSpacing = spacing;

    particles.clear();
    constraints.clear();

    // Create particle grid
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Particle p;
            p.position = topLeftPosition + glm::vec3(x * spacing, -y * spacing, 0.0f);
            // Give particles a small initial velocity by offsetting oldPosition slightly
            // This prevents the cloth from appearing "frozen" until something collides with it
            p.oldPosition = p.position - glm::vec3(0.0f, 0.001f, 0.0f);
            p.acceleration = glm::vec3(0.0f);
            p.mass = 1.0f;
            p.pinned = false;
            particles.push_back(p);
        }
    }

    // Create structural constraints (horizontal and vertical)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Horizontal constraint
            if (x < width - 1) {
                addConstraint(x, y, x + 1, y);
            }
            // Vertical constraint
            if (y < height - 1) {
                addConstraint(x, y, x, y + 1);
            }
        }
    }

    // Create shear constraints (diagonals for more stability)
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            addConstraint(x, y, x + 1, y + 1);
            addConstraint(x + 1, y, x, y + 1);
        }
    }

    // Create bending constraints (skip one particle for flexibility)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width - 2; ++x) {
            addConstraint(x, y, x + 2, y);
        }
    }
    for (int y = 0; y < height - 2; ++y) {
        for (int x = 0; x < width; ++x) {
            addConstraint(x, y, x, y + 2);
        }
    }
}

void ClothSimulation::pinParticle(int x, int y) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        particles[getParticleIndex(x, y)].pinned = true;
    }
}

void ClothSimulation::addConstraint(int x1, int y1, int x2, int y2) {
    int idx1 = getParticleIndex(x1, y1);
    int idx2 = getParticleIndex(x2, y2);

    glm::vec3 diff = particles[idx1].position - particles[idx2].position;
    float distance = glm::length(diff);

    DistanceConstraint constraint;
    constraint.particleA = idx1;
    constraint.particleB = idx2;
    constraint.restLength = distance;
    constraints.push_back(constraint);
}

void ClothSimulation::applyForces(const WindSystem* windSystem) {
    // Reset accelerations
    for (auto& p : particles) {
        p.acceleration = glm::vec3(0.0f);
    }

    // Apply gravity
    for (auto& p : particles) {
        if (!p.pinned) {
            p.acceleration.y -= GRAVITY;
        }
    }

    // Apply wind forces
    if (windSystem) {
        glm::vec2 windDir = windSystem->getWindDirection();
        float windStrength = windSystem->getWindStrength();

        for (auto& p : particles) {
            if (!p.pinned) {
                // Sample wind at particle position
                glm::vec2 worldPos2D(p.position.x, p.position.z);
                float windFactor = windSystem->sampleWindAtPosition(worldPos2D);

                // Apply wind force in wind direction
                // Increased force multiplier for more visible movement
                glm::vec3 windForce = glm::vec3(
                    windDir.x * windStrength * windFactor * 15.0f,
                    0.0f,
                    windDir.y * windStrength * windFactor * 15.0f
                );

                p.acceleration += windForce;
            }
        }
    }
}

void ClothSimulation::satisfyConstraints() {
    for (int iter = 0; iter < CONSTRAINT_ITERATIONS; ++iter) {
        for (const auto& constraint : constraints) {
            Particle& pA = particles[constraint.particleA];
            Particle& pB = particles[constraint.particleB];

            if (pA.pinned && pB.pinned) continue;

            glm::vec3 delta = pB.position - pA.position;
            float currentLength = glm::length(delta);

            if (currentLength < 0.0001f) continue;  // Avoid division by zero

            float difference = (currentLength - constraint.restLength) / currentLength;
            // Reduce stiffness factor from 0.5 to 0.3 for more flexibility
            glm::vec3 correction = delta * 0.3f * difference;

            if (!pA.pinned && !pB.pinned) {
                pA.position += correction;
                pB.position -= correction;
            } else if (!pA.pinned) {
                // Reduce correction multiplier for pinned constraints
                pA.position += correction * 1.5f;
            } else if (!pB.pinned) {
                pB.position -= correction * 1.5f;
            }
        }
    }
}

void ClothSimulation::updatePositions(float deltaTime) {
    for (auto& p : particles) {
        if (!p.pinned) {
            // Verlet integration
            glm::vec3 velocity = (p.position - p.oldPosition) * (1.0f - DAMPING);
            p.oldPosition = p.position;
            p.position = p.position + velocity + p.acceleration * deltaTime * deltaTime;
        }
    }
}

void ClothSimulation::update(float deltaTime, const WindSystem* windSystem) {
    // Use smaller fixed timesteps for stability
    const float fixedDt = 0.016f;  // ~60 FPS
    static float accumulator = 0.0f;
    accumulator += deltaTime;

    while (accumulator >= fixedDt) {
        applyForces(windSystem);
        updatePositions(fixedDt);
        handleCollisions();
        satisfyConstraints();
        accumulator -= fixedDt;
    }
}

void ClothSimulation::addSphereCollision(const glm::vec3& center, float radius) {
    sphereColliders.push_back({center, radius});
}

void ClothSimulation::clearCollisions() {
    sphereColliders.clear();
}

void ClothSimulation::handleCollisions() {
    // Handle sphere collisions
    for (const auto& sphere : sphereColliders) {
        for (auto& p : particles) {
            if (p.pinned) continue;

            glm::vec3 toParticle = p.position - sphere.center;
            float dist = glm::length(toParticle);

            // If particle is inside the sphere, push it out
            if (dist < sphere.radius) {
                if (dist < 0.0001f) {
                    // Particle exactly at center, push in arbitrary direction
                    p.position = sphere.center + glm::vec3(sphere.radius, 0.0f, 0.0f);
                } else {
                    // Push particle to surface of sphere
                    glm::vec3 normal = toParticle / dist;
                    p.position = sphere.center + normal * sphere.radius;
                }
            }
        }
    }
}

void ClothSimulation::createMesh(Mesh& mesh) const {
    // Create initial mesh geometry - this will be updated each frame
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    generateMeshData(vertices, indices);
    mesh.setCustomGeometry(vertices, indices);
}

void ClothSimulation::updateMesh(Mesh& mesh) const {
    // Update the mesh with current particle positions
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    generateMeshData(vertices, indices);
    mesh.setCustomGeometry(vertices, indices);
}

void ClothSimulation::generateMeshData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const {
    vertices.clear();
    indices.clear();

    int vertexCount = width * height;

    // Create vertices from particles (front side)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = getParticleIndex(x, y);
            const Particle& p = particles[idx];

            Vertex v;
            v.position = p.position;
            v.texCoord = glm::vec2(
                static_cast<float>(x) / (width - 1),
                static_cast<float>(y) / (height - 1)
            );
            v.normal = glm::vec3(0.0f, 0.0f, 1.0f);  // Will be computed below
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);  // Default tangent

            vertices.push_back(v);
        }
    }

    // Create indices for front-facing triangles
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            int topLeft = y * width + x;
            int topRight = topLeft + 1;
            int bottomLeft = (y + 1) * width + x;
            int bottomRight = bottomLeft + 1;

            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // Compute normals for front side
    std::vector<glm::vec3> normals(vertexCount, glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        glm::vec3 v0 = vertices[i0].position;
        glm::vec3 v1 = vertices[i1].position;
        glm::vec3 v2 = vertices[i2].position;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

        normals[i0] += normal;
        normals[i1] += normal;
        normals[i2] += normal;
    }

    // Normalize accumulated normals for front side
    for (int i = 0; i < vertexCount; ++i) {
        if (glm::length(normals[i]) > 0.0001f) {
            vertices[i].normal = glm::normalize(normals[i]);
        }
    }

    // Compute tangents for front side
    std::vector<glm::vec3> tangents(vertexCount, glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(vertexCount, glm::vec3(0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const glm::vec3& v0 = vertices[i0].position;
        const glm::vec3& v1 = vertices[i1].position;
        const glm::vec3& v2 = vertices[i2].position;

        const glm::vec2& uv0 = vertices[i0].texCoord;
        const glm::vec2& uv1 = vertices[i1].texCoord;
        const glm::vec2& uv2 = vertices[i2].texCoord;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        if (!std::isfinite(f)) f = 1.0f;

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;
    }

    // Orthogonalize and store tangents for front side
    for (int i = 0; i < vertexCount; ++i) {
        const glm::vec3& n = vertices[i].normal;
        const glm::vec3& t = tangents[i];

        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
        float handedness = (glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].tangent = glm::vec4(tangent, handedness);
    }

    // Create back side vertices (duplicates with flipped normals)
    size_t frontIndexCount = indices.size();
    for (int i = 0; i < vertexCount; ++i) {
        Vertex v = vertices[i];
        v.normal = -v.normal;
        v.tangent = glm::vec4(-glm::vec3(v.tangent), v.tangent.w);
        vertices.push_back(v);
    }

    // Create back-facing triangles (reversed winding)
    for (size_t i = 0; i < frontIndexCount; i += 3) {
        indices.push_back(indices[i] + vertexCount);
        indices.push_back(indices[i + 2] + vertexCount);  // Swap i+1 and i+2 to reverse winding
        indices.push_back(indices[i + 1] + vertexCount);
    }
}
