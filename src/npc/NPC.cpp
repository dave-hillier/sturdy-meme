#include "NPC.h"
#include "BehaviorTree.h"

// Default destructor defined here where BehaviorTree is complete
// This is required because unique_ptr<BehaviorTree> needs the full type for deletion
NPC::~NPC() = default;

// Default move operations defined here for the same reason
NPC::NPC(NPC&&) noexcept = default;
NPC& NPC::operator=(NPC&&) noexcept = default;
