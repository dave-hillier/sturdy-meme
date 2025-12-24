#pragma once

#include "interfaces/ITreeControl.h"

class TreeSystem;
class RendererSystems;

/**
 * TreeControlSubsystem - Implements ITreeControl
 * Provides access to TreeSystem and RendererSystems for tree/vegetation control.
 */
class TreeControlSubsystem : public ITreeControl {
public:
    TreeControlSubsystem(TreeSystem* tree, RendererSystems& systems)
        : tree_(tree)
        , systems_(systems) {}

    TreeSystem* getTreeSystem() override;
    const TreeSystem* getTreeSystem() const override;
    RendererSystems& getSystems() override { return systems_; }
    const RendererSystems& getSystems() const override { return systems_; }

    // Allow updating tree pointer if it's created later
    void setTreeSystem(TreeSystem* tree) { tree_ = tree; }

private:
    TreeSystem* tree_;
    RendererSystems& systems_;
};
