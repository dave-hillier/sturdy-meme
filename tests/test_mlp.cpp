#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdint>

#include "ml/Tensor.h"
#include "ml/MLPNetwork.h"
#include "ml/ModelLoader.h"

using namespace ml;

// ---------------------------------------------------------------------------
// Tensor tests
// ---------------------------------------------------------------------------

TEST_SUITE("Tensor") {
    TEST_CASE("default construction") {
        Tensor t;
        CHECK(t.size() == 0);
        CHECK(t.empty());
    }

    TEST_CASE("1D construction") {
        Tensor t(4);
        CHECK(t.size() == 4);
        CHECK(t.rows() == 1);
        CHECK(t.cols() == 4);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(t[i] == 0.0f);
        }
    }

    TEST_CASE("2D construction") {
        Tensor t(3, 2);
        CHECK(t.size() == 6);
        CHECK(t.rows() == 3);
        CHECK(t.cols() == 2);
    }

    TEST_CASE("construction with data") {
        Tensor t(2, 3, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f});
        CHECK(t(0, 0) == 1.0f);
        CHECK(t(0, 2) == 3.0f);
        CHECK(t(1, 0) == 4.0f);
        CHECK(t(1, 2) == 6.0f);
    }

    TEST_CASE("matVecMul") {
        // Matrix: [[1, 2], [3, 4], [5, 6]]  (3x2)
        // Vector: [1, 1]  (2)
        // Result: [3, 7, 11]  (3)
        Tensor mat(3, 2, {1, 2, 3, 4, 5, 6});
        Tensor vec(1, 2, {1, 1});
        Tensor out(3);

        Tensor::matVecMul(mat, vec, out);
        CHECK(out[0] == doctest::Approx(3.0f));
        CHECK(out[1] == doctest::Approx(7.0f));
        CHECK(out[2] == doctest::Approx(11.0f));
    }

    TEST_CASE("matVecMul identity") {
        // Identity matrix
        Tensor mat(2, 2, {1, 0, 0, 1});
        Tensor vec(1, 2, {3.5f, -2.0f});
        Tensor out(2);

        Tensor::matVecMul(mat, vec, out);
        CHECK(out[0] == doctest::Approx(3.5f));
        CHECK(out[1] == doctest::Approx(-2.0f));
    }

    TEST_CASE("addBias") {
        Tensor t(1, 3, {1, 2, 3});
        Tensor bias(1, 3, {10, 20, 30});
        Tensor::addBias(t, bias);
        CHECK(t[0] == doctest::Approx(11.0f));
        CHECK(t[1] == doctest::Approx(22.0f));
        CHECK(t[2] == doctest::Approx(33.0f));
    }

    TEST_CASE("relu") {
        Tensor t(1, 4, {-2.0f, 0.0f, 1.0f, -0.5f});
        Tensor::relu(t);
        CHECK(t[0] == 0.0f);
        CHECK(t[1] == 0.0f);
        CHECK(t[2] == 1.0f);
        CHECK(t[3] == 0.0f);
    }

    TEST_CASE("tanh") {
        Tensor t(1, 3, {0.0f, 1.0f, -1.0f});
        Tensor::tanh(t);
        CHECK(t[0] == doctest::Approx(0.0f));
        CHECK(t[1] == doctest::Approx(std::tanh(1.0f)));
        CHECK(t[2] == doctest::Approx(std::tanh(-1.0f)));
    }

    TEST_CASE("l2Normalize") {
        Tensor t(1, 2, {3.0f, 4.0f});
        Tensor::l2Normalize(t);
        CHECK(t[0] == doctest::Approx(0.6f));
        CHECK(t[1] == doctest::Approx(0.8f));
        CHECK(t.l2Norm() == doctest::Approx(1.0f));
    }

    TEST_CASE("l2Normalize zero vector") {
        Tensor t(3);
        Tensor::l2Normalize(t);
        CHECK(t[0] == 0.0f);
        CHECK(t[1] == 0.0f);
        CHECK(t[2] == 0.0f);
    }

    TEST_CASE("concat") {
        Tensor a(1, 2, {1.0f, 2.0f});
        Tensor b(1, 3, {3.0f, 4.0f, 5.0f});
        Tensor c = Tensor::concat(a, b);
        CHECK(c.size() == 5);
        CHECK(c[0] == 1.0f);
        CHECK(c[1] == 2.0f);
        CHECK(c[2] == 3.0f);
        CHECK(c[3] == 4.0f);
        CHECK(c[4] == 5.0f);
    }

    TEST_CASE("copyFrom") {
        float data[] = {10.0f, 20.0f, 30.0f};
        Tensor t(3);
        t.copyFrom(data, 3);
        CHECK(t[0] == 10.0f);
        CHECK(t[1] == 20.0f);
        CHECK(t[2] == 30.0f);
    }

    TEST_CASE("fill") {
        Tensor t(4);
        t.fill(7.0f);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(t[i] == 7.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// MLPNetwork tests
// ---------------------------------------------------------------------------

TEST_SUITE("MLPNetwork") {
    TEST_CASE("single linear layer (no activation)") {
        // y = W*x + b where W=[[1,2],[3,4]], b=[0.5, -0.5], x=[1,1]
        // y = [3+0.5, 7-0.5] = [3.5, 6.5]
        MLPNetwork net;
        net.addLayer(2, 2, Activation::None);
        net.setLayerWeights(0,
            {1, 2, 3, 4},   // weights
            {0.5f, -0.5f}   // bias
        );

        Tensor input(1, 2, {1.0f, 1.0f});
        Tensor output;
        net.forward(input, output);

        CHECK(output.size() == 2);
        CHECK(output[0] == doctest::Approx(3.5f));
        CHECK(output[1] == doctest::Approx(6.5f));
    }

    TEST_CASE("single layer with ReLU") {
        MLPNetwork net;
        net.addLayer(2, 3, Activation::ReLU);
        net.setLayerWeights(0,
            {1, -1, -1, 1, 2, 2},     // 3x2 weights
            {0.0f, 0.0f, -10.0f}       // bias
        );

        // x = [3, 1]
        // W*x = [3-1, -3+1, 6+2] = [2, -2, 8]
        // + bias = [2, -2, -2]
        // relu = [2, 0, 0]
        Tensor input(1, 2, {3.0f, 1.0f});
        Tensor output;
        net.forward(input, output);

        CHECK(output.size() == 3);
        CHECK(output[0] == doctest::Approx(2.0f));
        CHECK(output[1] == doctest::Approx(0.0f));
        CHECK(output[2] == doctest::Approx(0.0f));
    }

    TEST_CASE("two-layer network") {
        // Layer 0: 2 → 2, ReLU, W=Identity, b=0
        // Layer 1: 2 → 1, None, W=[1,1], b=[0]
        MLPNetwork net;
        net.addLayer(2, 2, Activation::ReLU);
        net.addLayer(2, 1, Activation::None);
        net.setLayerWeights(0, {1, 0, 0, 1}, {0, 0});
        net.setLayerWeights(1, {1, 1}, {0});

        // x = [3, -2]
        // Layer 0: relu([3, -2]) = [3, 0]
        // Layer 1: [3+0] = [3]
        Tensor input(1, 2, {3.0f, -2.0f});
        Tensor output;
        net.forward(input, output);

        CHECK(output.size() == 1);
        CHECK(output[0] == doctest::Approx(3.0f));
    }

    TEST_CASE("CALM-sized network dimensions") {
        // Test that a CALM-scale network (1024,1024,512) handles dimensions correctly
        MLPNetwork net;
        net.addLayer(128, 64, Activation::ReLU);
        net.addLayer(64, 32, Activation::ReLU);
        net.addLayer(32, 16, Activation::None);

        CHECK(net.inputSize() == 128);
        CHECK(net.outputSize() == 16);
        CHECK(net.numLayers() == 3);

        // Forward with zeros should produce bias outputs
        Tensor input(128);
        Tensor output;
        net.forward(input, output);
        CHECK(output.size() == 16);
    }

    TEST_CASE("inputSize and outputSize") {
        MLPNetwork net;
        CHECK(net.inputSize() == 0);
        CHECK(net.outputSize() == 0);

        net.addLayer(10, 5, Activation::ReLU);
        CHECK(net.inputSize() == 10);
        CHECK(net.outputSize() == 5);

        net.addLayer(5, 3, Activation::None);
        CHECK(net.inputSize() == 10);
        CHECK(net.outputSize() == 3);
    }

    TEST_CASE("repeated forward calls produce consistent results") {
        MLPNetwork net;
        net.addLayer(3, 2, Activation::ReLU);
        net.setLayerWeights(0, {1, 0, 0, 0, 1, 0}, {0, 0});

        Tensor input(1, 3, {5.0f, 3.0f, -1.0f});
        Tensor out1, out2;

        net.forward(input, out1);
        net.forward(input, out2);

        CHECK(out1[0] == out2[0]);
        CHECK(out1[1] == out2[1]);
    }
}

// ---------------------------------------------------------------------------
// StyleConditionedNetwork tests
// ---------------------------------------------------------------------------

TEST_SUITE("StyleConditionedNetwork") {
    TEST_CASE("basic forward pass") {
        // Style MLP: 4 → 2, Tanh
        // Main MLP: (2 + 3) = 5 → 1, None
        MLPNetwork styleMLP;
        styleMLP.addLayer(4, 2, Activation::Tanh);
        // Identity-like weights for simple testing
        styleMLP.setLayerWeights(0,
            {1, 0, 0, 0, 0, 1, 0, 0},  // 2x4
            {0, 0}
        );

        MLPNetwork mainMLP;
        mainMLP.addLayer(5, 1, Activation::None);
        mainMLP.setLayerWeights(0,
            {1, 1, 1, 1, 1},  // 1x5
            {0}
        );

        StyleConditionedNetwork net;
        net.setStyleMLP(std::move(styleMLP));
        net.setMainMLP(std::move(mainMLP));

        Tensor latent(1, 4, {1.0f, 0.5f, 0.0f, 0.0f});
        Tensor obs(1, 3, {1.0f, 2.0f, 3.0f});
        Tensor output;

        net.forward(latent, obs, output);

        // Style MLP: tanh([1, 0.5]) ≈ [0.7616, 0.4621]
        // Combined: [0.7616, 0.4621, 1, 2, 3]
        // Main: sum = 0.7616 + 0.4621 + 1 + 2 + 3 = 7.2237
        CHECK(output.size() == 1);
        float expected = std::tanh(1.0f) + std::tanh(0.5f) + 1.0f + 2.0f + 3.0f;
        CHECK(output[0] == doctest::Approx(expected).epsilon(0.001));
    }

    TEST_CASE("forwardNoStyle uses zero embedding") {
        MLPNetwork styleMLP;
        styleMLP.addLayer(2, 2, Activation::Tanh);
        styleMLP.setLayerWeights(0, {1, 0, 0, 1}, {0, 0});

        MLPNetwork mainMLP;
        mainMLP.addLayer(4, 1, Activation::None);
        mainMLP.setLayerWeights(0, {1, 1, 1, 1}, {0});

        StyleConditionedNetwork net;
        net.setStyleMLP(std::move(styleMLP));
        net.setMainMLP(std::move(mainMLP));

        Tensor obs(1, 2, {3.0f, 4.0f});
        Tensor output;

        net.forwardNoStyle(obs, output);

        // Style embed is zero, combined = [0, 0, 3, 4], sum = 7
        CHECK(output.size() == 1);
        CHECK(output[0] == doctest::Approx(7.0f));
    }
}

// ---------------------------------------------------------------------------
// ModelLoader tests (save/load round-trip)
// ---------------------------------------------------------------------------

TEST_SUITE("ModelLoader") {
    TEST_CASE("save and load round-trip") {
        // Create a small network
        MLPNetwork original;
        original.addLayer(3, 2, Activation::ReLU);
        original.addLayer(2, 1, Activation::None);
        original.setLayerWeights(0,
            {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
            {0.1f, 0.2f}
        );
        original.setLayerWeights(1,
            {1.0f, -1.0f},
            {0.5f}
        );

        std::vector<Activation> activations = {Activation::ReLU, Activation::None};

        // Save
        std::string path = "/tmp/test_mlp_model.bin";
        REQUIRE(ModelLoader::saveMLP(path, original, activations));

        // Load
        MLPNetwork loaded;
        REQUIRE(ModelLoader::loadMLP(path, loaded));

        // Verify structure
        CHECK(loaded.numLayers() == 2);
        CHECK(loaded.inputSize() == 3);
        CHECK(loaded.outputSize() == 1);

        // Verify forward pass produces same result
        Tensor input(1, 3, {1.0f, 1.0f, 1.0f});
        Tensor outOrig, outLoaded;
        original.forward(input, outOrig);
        loaded.forward(input, outLoaded);

        REQUIRE(outOrig.size() == outLoaded.size());
        for (size_t i = 0; i < outOrig.size(); ++i) {
            CHECK(outOrig[i] == doctest::Approx(outLoaded[i]));
        }

        // Clean up
        std::remove(path.c_str());
    }

    TEST_CASE("load non-existent file fails gracefully") {
        MLPNetwork net;
        CHECK_FALSE(ModelLoader::loadMLP("/tmp/does_not_exist_12345.bin", net));
    }

    TEST_CASE("load file with wrong magic fails") {
        std::string path = "/tmp/test_bad_magic.bin";
        {
            std::ofstream f(path, std::ios::binary);
            uint32_t badMagic = 0xDEADBEEF;
            f.write(reinterpret_cast<char*>(&badMagic), sizeof(uint32_t));
        }
        MLPNetwork net;
        CHECK_FALSE(ModelLoader::loadMLP(path, net));
        std::remove(path.c_str());
    }

    TEST_CASE("round-trip preserves numerical accuracy") {
        MLPNetwork original;
        original.addLayer(4, 8, Activation::ReLU);
        original.addLayer(8, 4, Activation::Tanh);
        original.addLayer(4, 2, Activation::None);

        // Fill with specific values
        std::vector<float> w0(32), b0(8);
        for (int i = 0; i < 32; ++i) w0[i] = static_cast<float>(i) * 0.1f - 1.6f;
        for (int i = 0; i < 8; ++i) b0[i] = static_cast<float>(i) * 0.01f;
        original.setLayerWeights(0, w0, b0);

        std::vector<float> w1(32), b1(4);
        for (int i = 0; i < 32; ++i) w1[i] = static_cast<float>(i) * -0.05f + 0.8f;
        for (int i = 0; i < 4; ++i) b1[i] = -0.1f * static_cast<float>(i);
        original.setLayerWeights(1, w1, b1);

        std::vector<float> w2(8), b2(2);
        for (int i = 0; i < 8; ++i) w2[i] = static_cast<float>(i) * 0.25f - 1.0f;
        b2 = {0.5f, -0.5f};
        original.setLayerWeights(2, w2, b2);

        std::vector<Activation> acts = {Activation::ReLU, Activation::Tanh, Activation::None};

        std::string path = "/tmp/test_mlp_accuracy.bin";
        REQUIRE(ModelLoader::saveMLP(path, original, acts));

        MLPNetwork loaded;
        REQUIRE(ModelLoader::loadMLP(path, loaded));

        // Test with several inputs
        std::vector<std::vector<float>> testInputs = {
            {1.0f, 0.0f, -1.0f, 0.5f},
            {0.0f, 0.0f, 0.0f, 0.0f},
            {-2.0f, 3.0f, -1.0f, 0.1f},
        };

        for (const auto& inp : testInputs) {
            Tensor input(1, 4, std::vector<float>(inp));
            Tensor outOrig, outLoaded;
            original.forward(input, outOrig);
            loaded.forward(input, outLoaded);

            REQUIRE(outOrig.size() == outLoaded.size());
            for (size_t i = 0; i < outOrig.size(); ++i) {
                CHECK(outOrig[i] == doctest::Approx(outLoaded[i]));
            }
        }

        std::remove(path.c_str());
    }
}
