#pragma once

/**
 * SystemGroupMacros.h - Helper macros for SystemGroup pattern
 *
 * These macros reduce boilerplate in SystemGroup structs that hold
 * non-owning pointers to related rendering systems.
 *
 * Pattern usage:
 *   struct MySystemGroup {
 *       // Members
 *       SYSTEM_MEMBER(FooSystem, foo);
 *       SYSTEM_MEMBER(BarSystem, bar);
 *
 *       // Required system accessors (returns reference, must not be null)
 *       REQUIRED_SYSTEM_ACCESSORS(FooSystem, foo)
 *
 *       // Optional system accessors (returns pointer, may be null)
 *       OPTIONAL_SYSTEM_ACCESSORS(BarSystem, bar, Bar)
 *
 *       bool isValid() const { return foo_; }
 *   };
 */

/**
 * Declares a system member pointer with nullptr default.
 * Generates: Type* name_ = nullptr;
 */
#define SYSTEM_MEMBER(Type, name) Type* name##_ = nullptr

/**
 * Generates reference accessors for a required system.
 * Assumes member name_ exists. Dereferences without null check.
 *
 * Generates:
 *   Type& name() { return *name_; }
 *   const Type& name() const { return *name_; }
 */
#define REQUIRED_SYSTEM_ACCESSORS(Type, name) \
    Type& name() { return *name##_; } \
    const Type& name() const { return *name##_; }

/**
 * Generates pointer accessors and has-check for an optional system.
 * Assumes member name_ exists. Returns pointer (may be null).
 *
 * HasName parameter is used to generate hasHasName() method.
 * Example: OPTIONAL_SYSTEM_ACCESSORS(TreeSystem, tree, Tree) generates hasTree()
 *
 * Generates:
 *   Type* name() { return name_; }
 *   const Type* name() const { return name_; }
 *   bool hasHasName() const { return name_ != nullptr; }
 */
#define OPTIONAL_SYSTEM_ACCESSORS(Type, name, HasName) \
    Type* name() { return name##_; } \
    const Type* name() const { return name##_; } \
    bool has##HasName() const { return name##_ != nullptr; }
