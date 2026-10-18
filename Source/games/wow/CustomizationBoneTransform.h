#pragma once

#include <array>

namespace customization
{
  // Column-major matrices multiplying column vectors. The correction is expressed in
  // the parent's axes, around the animated joint origin, not the unanimated bind pivot.
  // See docs/fbx-customization.md for the independent reference and FBX-space derivation.
  using Matrix = std::array<float, 16>;

  inline Matrix multiply(const Matrix & a, const Matrix & b)
  {
    Matrix out{};
    for (int column = 0; column < 4; ++column)
      for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k)
          out[column*4+row] += a[k*4+row] * b[column*4+k];
    return out;
  }

  inline Matrix translation(float x, float y, float z)
  { return {{1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1}}; }

  inline Matrix applyAtOrigin(const Matrix & animated, const Matrix & correction,
                              const std::array<float, 3> & origin)
  {
    return multiply(multiply(multiply(translation(origin[0], origin[1], origin[2]), correction),
      translation(-origin[0], -origin[1], -origin[2])), animated);
  }

  // Native M2 matrices map model-space vertices and rotate about a model-space pivot.
  inline Matrix applyToSkinLocal(const Matrix & animated, const Matrix & correction,
                                 const std::array<float, 3> & pivot)
  {
    std::array<float, 3> origin{};
    for (int row = 0; row < 3; ++row)
    {
      origin[row] = animated[12+row];
      for (int k = 0; k < 3; ++k) origin[row] += animated[k*4+row] * pivot[k];
    }
    return applyAtOrigin(animated, correction, origin);
  }

  // FBX local joint matrices already have their origin at the joint. Apply once to a
  // fresh animation/rest sample, then accumulate the corrected hierarchy. Never bake
  // the selected face into the shared bind pose or feed last frame's result back in.
  inline Matrix applyToJointLocal(const Matrix & animated, const Matrix & correction)
  { return applyAtOrigin(animated, correction, {{animated[12], animated[13], animated[14]}}); }

  inline Matrix changeUnits(Matrix matrix, float unitsPerSourceUnit)
  {
    for (int i = 12; i < 15; ++i) matrix[i] *= unitsPerSourceUnit;
    return matrix;
  }
}
