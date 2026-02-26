// Step 3.1: SLA support tree strategy types.
// Aligned with PrusaSlicer naming (SupportTreeType).
// Note: PillarConnectionMode is defined in SupportTree.hpp (pre-existing in PhrozenOrca).
//
#ifndef SUPPORTTREESTRATEGIES_HPP
#define SUPPORTTREESTRATEGIES_HPP

namespace Slic3r { namespace sla {

// Support tree building strategy.
// "Default" = pillar-based SupportTreeBuildsteps algorithm (PhrozenOrca existing).
// "Branching" = organic branching tree (Step 3.3, requires BranchingTreeSLA).
// "Organic" = TODO (Phase 4+).
enum class SupportTreeType { Default, Branching, Organic };

}} // namespace Slic3r::sla

#endif // SUPPORTTREESTRATEGIES_HPP
