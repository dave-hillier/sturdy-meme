#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>

// DEPRECATED: This class is deprecated. Use proper RAII with static create() factory methods instead.
//
// All resource-owning classes should:
// 1. Have a static create() factory that returns std::unique_ptr<T>
// 2. Store handles needed for cleanup internally
// 3. Have a destructor that performs cleanup
// 4. Be move-only (delete copy constructor/assignment)
//
// Example replacement:
//   // Old:
//   std::optional<RAIIAdapter<Texture>> texture = RAIIAdapter<Texture>::create(
//       [&](auto& t) { return t.load(...); },
//       [&](auto& t) { t.destroy(device, allocator); }
//   );
//
//   // New:
//   std::unique_ptr<Texture> texture = Texture::loadFromFile(...);
//
// This file is kept for reference only. Do not use RAIIAdapter in new code.

// Legacy adapter - adapts classes with init/destroy pattern to RAII semantics.
// DEPRECATED - prefer using classes with proper RAII (static create() factory methods).

template<typename T>
class RAIIAdapter {
public:
    template<typename InitFunc, typename DestroyFunc>
    static std::optional<RAIIAdapter> create(InitFunc&& init, DestroyFunc&& destroy) {
        RAIIAdapter adapter(std::forward<DestroyFunc>(destroy));
        if (!init(*adapter.value_)) {
            return std::nullopt;
        }
        return adapter;
    }

    ~RAIIAdapter() {
        if (value_ && destroy_) destroy_(*value_);
    }

    // Move-only
    RAIIAdapter(RAIIAdapter&& other) noexcept
        : value_(std::move(other.value_))
        , destroy_(std::move(other.destroy_)) {
        other.destroy_ = nullptr;
    }

    RAIIAdapter& operator=(RAIIAdapter&& other) noexcept {
        if (this != &other) {
            if (value_ && destroy_) destroy_(*value_);
            value_ = std::move(other.value_);
            destroy_ = std::move(other.destroy_);
            other.destroy_ = nullptr;
        }
        return *this;
    }

    RAIIAdapter(const RAIIAdapter&) = delete;
    RAIIAdapter& operator=(const RAIIAdapter&) = delete;

    T& get() { return *value_; }
    const T& get() const { return *value_; }
    T* operator->() { return value_.get(); }
    const T* operator->() const { return value_.get(); }
    T& operator*() { return *value_; }
    const T& operator*() const { return *value_; }

private:
    template<typename DestroyFunc>
    explicit RAIIAdapter(DestroyFunc&& destroy)
        : value_(std::make_unique<T>())
        , destroy_(std::forward<DestroyFunc>(destroy)) {}

    std::unique_ptr<T> value_;
    std::function<void(T&)> destroy_;
};
