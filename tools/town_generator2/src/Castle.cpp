#include "town_generator2/wards/AllWards.hpp"
#include "town_generator2/building/Model.hpp"

namespace town_generator2 {
namespace wards {

Castle::Castle(building::Model* model_, building::Patch* patch_)
    : Ward(model_, patch_)
{
    // Find vertices that border non-city patches (reserved for outer wall)
    geom::PointList reserved;
    for (const auto& v : patch_->shape) {
        auto patchesAtV = model_->patchByVertex(v);
        for (auto* p : patchesAtV) {
            if (!p->withinCity) {
                reserved.push_back(v);
                break;
            }
        }
    }

    std::vector<building::Patch*> patchVec = {patch_};
    wall = std::make_unique<building::CurtainWall>(true, *model_, patchVec, reserved);
}

} // namespace wards
} // namespace town_generator2
