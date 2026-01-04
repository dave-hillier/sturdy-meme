#include <doctest/doctest.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>
#include <vector>

// Forward declarations of IKUtils functions to test
namespace IKUtils {
    void decomposeTransform(
        const glm::mat4& transform,
        glm::vec3& translation,
        glm::quat& rotation,
        glm::vec3& scale
    );

    glm::mat4 composeTransform(
        const glm::vec3& translation,
        const glm::quat& rotation,
        const glm::vec3& scale
    );

    glm::vec3 getWorldPosition(const glm::mat4& globalTransform);

    float getBoneLength(
        const std::vector<glm::mat4>& globalTransforms,
        int32_t boneIndex,
        int32_t childBoneIndex
    );

    glm::quat aimAt(
        const glm::vec3& currentDir,
        const glm::vec3& targetDir,
        const glm::vec3& upHint
    );
}

TEST_SUITE("IKUtils") {
    TEST_CASE("getWorldPosition extracts translation") {
        glm::mat4 identity(1.0f);
        CHECK(IKUtils::getWorldPosition(identity) == glm::vec3(0.0f));

        glm::mat4 translated = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f));
        glm::vec3 pos = IKUtils::getWorldPosition(translated);
        CHECK(pos.x == doctest::Approx(10.0f));
        CHECK(pos.y == doctest::Approx(20.0f));
        CHECK(pos.z == doctest::Approx(30.0f));
    }

    TEST_CASE("decomposeTransform extracts translation") {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 10.0f, 15.0f));

        glm::vec3 translation, scale;
        glm::quat rotation;
        IKUtils::decomposeTransform(transform, translation, rotation, scale);

        CHECK(translation.x == doctest::Approx(5.0f));
        CHECK(translation.y == doctest::Approx(10.0f));
        CHECK(translation.z == doctest::Approx(15.0f));
    }

    TEST_CASE("decomposeTransform extracts uniform scale") {
        glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));

        glm::vec3 translation, scale;
        glm::quat rotation;
        IKUtils::decomposeTransform(transform, translation, rotation, scale);

        CHECK(scale.x == doctest::Approx(2.0f));
        CHECK(scale.y == doctest::Approx(2.0f));
        CHECK(scale.z == doctest::Approx(2.0f));
    }

    TEST_CASE("decomposeTransform extracts non-uniform scale") {
        glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

        glm::vec3 translation, scale;
        glm::quat rotation;
        IKUtils::decomposeTransform(transform, translation, rotation, scale);

        CHECK(scale.x == doctest::Approx(1.0f));
        CHECK(scale.y == doctest::Approx(2.0f));
        CHECK(scale.z == doctest::Approx(3.0f));
    }

    TEST_CASE("decomposeTransform extracts rotation") {
        glm::quat inputRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        glm::mat4 transform = glm::mat4_cast(inputRot);

        glm::vec3 translation, scale;
        glm::quat rotation;
        IKUtils::decomposeTransform(transform, translation, rotation, scale);

        // Check rotation by applying to a vector
        glm::vec3 forward(0, 0, 1);
        glm::vec3 rotatedInput = inputRot * forward;
        glm::vec3 rotatedOutput = rotation * forward;

        CHECK(rotatedOutput.x == doctest::Approx(rotatedInput.x).epsilon(0.0001));
        CHECK(rotatedOutput.y == doctest::Approx(rotatedInput.y).epsilon(0.0001));
        CHECK(rotatedOutput.z == doctest::Approx(rotatedInput.z).epsilon(0.0001));
    }

    TEST_CASE("decomposeTransform handles combined TRS") {
        glm::vec3 inputT(1.0f, 2.0f, 3.0f);
        glm::quat inputR = glm::angleAxis(glm::radians(45.0f), glm::normalize(glm::vec3(1, 1, 0)));
        glm::vec3 inputS(1.5f, 2.0f, 0.5f);

        glm::mat4 T = glm::translate(glm::mat4(1.0f), inputT);
        glm::mat4 R = glm::mat4_cast(inputR);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), inputS);
        glm::mat4 transform = T * R * S;

        glm::vec3 translation, scale;
        glm::quat rotation;
        IKUtils::decomposeTransform(transform, translation, rotation, scale);

        CHECK(translation.x == doctest::Approx(inputT.x).epsilon(0.001));
        CHECK(translation.y == doctest::Approx(inputT.y).epsilon(0.001));
        CHECK(translation.z == doctest::Approx(inputT.z).epsilon(0.001));

        CHECK(scale.x == doctest::Approx(inputS.x).epsilon(0.001));
        CHECK(scale.y == doctest::Approx(inputS.y).epsilon(0.001));
        CHECK(scale.z == doctest::Approx(inputS.z).epsilon(0.001));
    }

    TEST_CASE("composeTransform creates correct matrix") {
        glm::vec3 translation(10.0f, 20.0f, 30.0f);
        glm::quat rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
        glm::vec3 scale(2.0f, 2.0f, 2.0f);

        glm::mat4 result = IKUtils::composeTransform(translation, rotation, scale);

        // Extract position
        glm::vec3 pos = IKUtils::getWorldPosition(result);
        CHECK(pos.x == doctest::Approx(10.0f));
        CHECK(pos.y == doctest::Approx(20.0f));
        CHECK(pos.z == doctest::Approx(30.0f));
    }

    TEST_CASE("compose and decompose are inverse operations") {
        glm::vec3 origT(5.0f, -3.0f, 8.0f);
        glm::quat origR = glm::normalize(glm::quat(0.5f, 0.5f, 0.5f, 0.5f));
        glm::vec3 origS(1.0f, 1.5f, 2.0f);

        glm::mat4 composed = IKUtils::composeTransform(origT, origR, origS);

        glm::vec3 extractedT, extractedS;
        glm::quat extractedR;
        IKUtils::decomposeTransform(composed, extractedT, extractedR, extractedS);

        CHECK(extractedT.x == doctest::Approx(origT.x).epsilon(0.001));
        CHECK(extractedT.y == doctest::Approx(origT.y).epsilon(0.001));
        CHECK(extractedT.z == doctest::Approx(origT.z).epsilon(0.001));

        CHECK(extractedS.x == doctest::Approx(origS.x).epsilon(0.001));
        CHECK(extractedS.y == doctest::Approx(origS.y).epsilon(0.001));
        CHECK(extractedS.z == doctest::Approx(origS.z).epsilon(0.001));

        // Quaternions can be equivalent with opposite signs
        bool sameSign = glm::dot(origR, extractedR) > 0;
        glm::quat compareR = sameSign ? extractedR : -extractedR;
        CHECK(compareR.w == doctest::Approx(origR.w).epsilon(0.001));
        CHECK(compareR.x == doctest::Approx(origR.x).epsilon(0.001));
        CHECK(compareR.y == doctest::Approx(origR.y).epsilon(0.001));
        CHECK(compareR.z == doctest::Approx(origR.z).epsilon(0.001));
    }

    TEST_CASE("getBoneLength with valid indices") {
        std::vector<glm::mat4> transforms = {
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
            glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 4.0f, 0.0f)),
            glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 4.0f, 5.0f))
        };

        // Distance from origin to (3, 4, 0) is 5
        float len01 = IKUtils::getBoneLength(transforms, 0, 1);
        CHECK(len01 == doctest::Approx(5.0f));

        // Distance from (3, 4, 0) to (3, 4, 5) is 5
        float len12 = IKUtils::getBoneLength(transforms, 1, 2);
        CHECK(len12 == doctest::Approx(5.0f));

        // Distance from origin to (3, 4, 5) is sqrt(50)
        float len02 = IKUtils::getBoneLength(transforms, 0, 2);
        CHECK(len02 == doctest::Approx(std::sqrt(50.0f)));
    }

    TEST_CASE("getBoneLength with invalid indices") {
        std::vector<glm::mat4> transforms = {
            glm::mat4(1.0f),
            glm::mat4(1.0f)
        };

        CHECK(IKUtils::getBoneLength(transforms, -1, 0) == 0.0f);
        CHECK(IKUtils::getBoneLength(transforms, 0, -1) == 0.0f);
        CHECK(IKUtils::getBoneLength(transforms, 10, 0) == 0.0f);
        CHECK(IKUtils::getBoneLength(transforms, 0, 10) == 0.0f);
    }

    TEST_CASE("getBoneLength with empty transforms") {
        std::vector<glm::mat4> transforms;
        CHECK(IKUtils::getBoneLength(transforms, 0, 1) == 0.0f);
    }

    TEST_CASE("aimAt with aligned vectors returns identity") {
        glm::vec3 forward(0, 0, 1);
        glm::vec3 up(0, 1, 0);

        glm::quat result = IKUtils::aimAt(forward, forward, up);

        // Should be identity or very close to it
        CHECK(result.w == doctest::Approx(1.0f).epsilon(0.001));
        CHECK(std::abs(result.x) < 0.001f);
        CHECK(std::abs(result.y) < 0.001f);
        CHECK(std::abs(result.z) < 0.001f);
    }

    TEST_CASE("aimAt rotates from to target direction") {
        glm::vec3 from(1, 0, 0);
        glm::vec3 to(0, 1, 0);
        glm::vec3 up(0, 0, 1);

        glm::quat result = IKUtils::aimAt(from, to, up);

        // Apply rotation to 'from' - should get 'to'
        glm::vec3 rotated = result * from;
        CHECK(rotated.x == doctest::Approx(to.x).epsilon(0.001));
        CHECK(rotated.y == doctest::Approx(to.y).epsilon(0.001));
        CHECK(rotated.z == doctest::Approx(to.z).epsilon(0.001));
    }

    TEST_CASE("aimAt 90 degree rotation") {
        glm::vec3 forward(0, 0, 1);
        glm::vec3 right(1, 0, 0);
        glm::vec3 up(0, 1, 0);

        glm::quat result = IKUtils::aimAt(forward, right, up);
        glm::vec3 rotated = result * forward;

        CHECK(rotated.x == doctest::Approx(1.0f).epsilon(0.001));
        CHECK(rotated.y == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(rotated.z == doctest::Approx(0.0f).epsilon(0.001));
    }

    TEST_CASE("aimAt 180 degree rotation") {
        glm::vec3 forward(0, 0, 1);
        glm::vec3 backward(0, 0, -1);
        glm::vec3 up(0, 1, 0);

        glm::quat result = IKUtils::aimAt(forward, backward, up);
        glm::vec3 rotated = result * forward;

        CHECK(rotated.x == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(rotated.y == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(rotated.z == doctest::Approx(-1.0f).epsilon(0.001));
    }

    TEST_CASE("aimAt produces unit quaternion") {
        // Test various directions
        std::vector<std::pair<glm::vec3, glm::vec3>> testCases = {
            {{1, 0, 0}, {0, 1, 0}},
            {{0, 1, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}},
            {{1, 1, 0}, {0, 1, 1}},
            {{1, 1, 1}, {-1, 1, -1}}
        };

        glm::vec3 up(0, 1, 0);

        for (const auto& [from, to] : testCases) {
            glm::quat result = IKUtils::aimAt(from, to, up);
            float len = glm::length(result);
            CHECK(len == doctest::Approx(1.0f).epsilon(0.0001));
        }
    }
}
