#include "CustomizationBoneSelection.h"
#include <stdexcept>
#include <iostream>
using customization::BoneSetEffect;
static void expect(bool v) { if (!v) throw std::runtime_error("conditional bone selection regression"); }
int main()
{
  using customization::conflictingBoneSetEffects;
  const BoneSetEffect hunched{100,24,438}, upright{101,24,439};
  expect(!conflictingBoneSetEffects(hunched,upright));
  expect(!conflictingBoneSetEffects(upright,hunched));
  expect(!conflictingBoneSetEffects(hunched,hunched));
  expect(conflictingBoneSetEffects(hunched,{101,24,438}));
  expect(conflictingBoneSetEffects(hunched,{101,0,0}));
  expect(conflictingBoneSetEffects({100,0,0},upright));
  expect(conflictingBoneSetEffects(hunched,{101,25,439}));
  expect(conflictingBoneSetEffects({100,0,438},{101,0,439}));
  expect(conflictingBoneSetEffects({100,24,0},upright));
  std::cout << "PASS: mutually exclusive posture effects; overlapping, unconditional and unrelated effects rejected\n";
}
