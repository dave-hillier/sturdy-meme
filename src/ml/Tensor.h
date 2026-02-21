#pragma once

#include <vector>
#include <cstddef>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <numeric>

namespace ml {

// Lightweight 1D/2D tensor for neural network inference.
// Owns its data via std::vector<float>. No dynamic computation graph —
// just storage + basic math ops needed for MLP forward passes.
class Tensor {
public:
    Tensor() = default;

    explicit Tensor(size_t size)
        : data_(size, 0.0f), rows_(1), cols_(size) {}

    Tensor(size_t rows, size_t cols)
        : data_(rows * cols, 0.0f), rows_(rows), cols_(cols) {}

    Tensor(size_t rows, size_t cols, std::vector<float> data)
        : data_(std::move(data)), rows_(rows), cols_(cols) {
        assert(data_.size() == rows_ * cols_);
    }

    // Accessors
    float& operator()(size_t row, size_t col) { return data_[row * cols_ + col]; }
    const float& operator()(size_t row, size_t col) const { return data_[row * cols_ + col]; }

    float& operator[](size_t i) { return data_[i]; }
    const float& operator[](size_t i) const { return data_[i]; }

    float* data() { return data_.data(); }
    const float* data() const { return data_.data(); }

    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    // Matrix-vector multiply: out = M * v
    // M is this tensor (rows x cols), v is input (cols), out is (rows)
    static void matVecMul(const Tensor& matrix, const Tensor& vec, Tensor& out) {
        assert(matrix.cols_ == vec.size());
        assert(out.size() == matrix.rows_);

        const float* m = matrix.data_.data();
        const float* v = vec.data_.data();
        float* o = out.data_.data();
        const size_t rows = matrix.rows_;
        const size_t cols = matrix.cols_;

        for (size_t r = 0; r < rows; ++r) {
            float sum = 0.0f;
            const float* row = m + r * cols;
            for (size_t c = 0; c < cols; ++c) {
                sum += row[c] * v[c];
            }
            o[r] = sum;
        }
    }

    // Element-wise add: out[i] += bias[i]
    static void addBias(Tensor& out, const Tensor& bias) {
        assert(out.size() == bias.size());
        for (size_t i = 0; i < out.size(); ++i) {
            out.data_[i] += bias.data_[i];
        }
    }

    // ReLU activation in-place
    static void relu(Tensor& t) {
        for (float& v : t.data_) {
            v = std::max(0.0f, v);
        }
    }

    // Tanh activation in-place
    static void tanh(Tensor& t) {
        for (float& v : t.data_) {
            v = std::tanh(v);
        }
    }

    // L2 normalize in-place
    static void l2Normalize(Tensor& t) {
        float norm = 0.0f;
        for (float v : t.data_) {
            norm += v * v;
        }
        norm = std::sqrt(norm);
        if (norm > 1e-8f) {
            float invNorm = 1.0f / norm;
            for (float& v : t.data_) {
                v *= invNorm;
            }
        }
    }

    // Concatenate two 1D tensors: out = [a, b]
    static Tensor concat(const Tensor& a, const Tensor& b) {
        std::vector<float> data;
        data.reserve(a.size() + b.size());
        data.insert(data.end(), a.data_.begin(), a.data_.end());
        data.insert(data.end(), b.data_.begin(), b.data_.end());
        return Tensor(1, a.size() + b.size(), std::move(data));
    }

    // Copy from raw float span
    void copyFrom(const float* src, size_t count) {
        assert(count <= data_.size());
        std::copy(src, src + count, data_.data());
    }

    // Fill with value
    void fill(float value) {
        std::fill(data_.begin(), data_.end(), value);
    }

    // L2 norm
    float l2Norm() const {
        float sum = 0.0f;
        for (float v : data_) {
            sum += v * v;
        }
        return std::sqrt(sum);
    }

private:
    std::vector<float> data_;
    size_t rows_ = 0;
    size_t cols_ = 0;
};

} // namespace ml
