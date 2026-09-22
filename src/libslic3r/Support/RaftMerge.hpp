#ifndef slic3r_RaftMerge_hpp_
#define slic3r_RaftMerge_hpp_

namespace Slic3r {
class Print;

// Restore object-local data before support generation/cache reuse.
void restore_unmerged_rafts(Print& print);

// Merge compatible overlapping Cura rafts before tool ordering and G-code export.
// Different recipes retain their settings and partition the overlap. Object
// placement, model layers and arrange margins are unchanged.
void merge_overlapping_rafts(Print& print);
} // namespace Slic3r

#endif
