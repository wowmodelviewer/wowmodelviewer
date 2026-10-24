#pragma once
#include <cstdint>

namespace customization
{
  struct BoneSetEffect
  {
    uint32_t set;
    uint32_t relatedOption;
    uint32_t relatedChoice;
  };

  // One option selects one choice. Different choices of that same option
  // cannot activate together (e.g. hunched versus upright posture).
  inline bool conflictingBoneSetEffects(const BoneSetEffect & a, const BoneSetEffect & b)
  {
    if (a.set == b.set) return false;
    const bool exclusive = a.relatedOption != 0 && a.relatedOption == b.relatedOption &&
      a.relatedChoice != 0 && b.relatedChoice != 0 && a.relatedChoice != b.relatedChoice;
    return !exclusive;
  }
}
