#ifndef WMV_SKELETON_ANIMATION_OVERLAY_H
#define WMV_SKELETON_ANIMATION_OVERLAY_H

#include "Bone.h"
#include <map>
#include <set>

// SKPD is inheritance, not replacement of the child by the parent. Resolve
// sequence identities before composing tracks; alias/variation links are local
// to their source SKS1 and must be translated too.
inline bool overlaySkeletonAnimations(std::vector<ModelAnimation> & sequences,
                                      std::vector<Bone> & bones,
                                      const std::vector<ModelAnimation> & child,
                                      const std::vector<Bone> & childBones)
{
  if (bones.size() != childBones.size() || child.empty()) return false;
  std::map<std::pair<int, int>, size_t> identities;
  for (size_t i = 0; i < sequences.size(); ++i)
    if (!identities.emplace(std::make_pair(sequences[i].animID, sequences[i].subAnimID), i).second) return false;
  std::vector<size_t> mapping;
  std::set<std::pair<int, int>> seen;
  size_t total = sequences.size();
  for (const auto & s : child)
  {
    const auto id = std::make_pair(s.animID, s.subAnimID);
    if (!seen.insert(id).second) return false;
    const auto found = identities.find(id);
    mapping.push_back(found == identities.end() ? total++ : found->second);
  }
  if (total > MAX_ANIMATED) return false;
  std::vector<int> childAt(total, -1);
  for (size_t i = 0; i < mapping.size(); ++i) childAt[mapping[i]] = (int)i;
  auto remap = [&](int16 index, int16 & result) {
    if (index == -1) { result = -1; return true; }
    if (index < 0 || index >= (int)mapping.size()) return false;
    result = (int16)mapping[index]; return true;
  };
  auto combined = sequences;
  combined.resize(total);
  for (size_t i = 0; i < child.size(); ++i)
  {
    auto s = child[i];
    if (!remap(s.NextAnimation, s.NextAnimation)) return false;
    if ((s.flags & 0x40) && !remap(s.Index, s.Index)) return false;
    combined[mapping[i]] = s;
  }
  std::vector<Bone> result(childBones.size());
  for (size_t b = 0; b < result.size(); ++b)
  {
    // The child's own hierarchy/pivots belong to its geometry, even for a clip
    // inherited from the parent. No remapping between unrelated rigs is implied.
    if (bones[b].parent != childBones[b].parent || bones[b].boneDef.keyboneid != childBones[b].boneDef.keyboneid)
      return false;
    result[b] = childBones[b];
    for (size_t i = 0; i < total; ++i)
    {
      const bool own = childAt[i] >= 0;
      const auto & source = own ? childBones[b] : bones[b];
      const size_t index = own ? (size_t)childAt[i] : i;
      result[b].trans.copySequence(i, source.trans, index);
      result[b].rot.copySequence(i, source.rot, index);
      result[b].scale.copySequence(i, source.scale, index);
    }
  }
  sequences.swap(combined); bones.swap(result);
  return true;
}

#endif
