#ifndef VEC3D_H
#define VEC3D_H

#include <iostream>
#include <cmath>

#ifndef PI
    const double PI         = 3.141592653589793238462643383279502884197169399375105820974944592307816;
#endif
    const double PId        = PI;
    const float  PIf        = static_cast<float>(PI);

#ifndef HALFPI
    const double HALFPI     = PI / 2.0;
#endif
    const double HALFPId    = HALFPI;
    const float  HALFPIf    = static_cast<float>(HALFPI);

#ifndef TWOPI
    const double TWOPI      = 2.0 * PI;
#endif
    const double TWOPId     = TWOPI;
    const float  TWOPIf     = static_cast<float>(TWOPI);

#ifndef INVPI
    const double INVPI      = 1.0 / PI;
#endif
    const double INVPId     = INVPI;
    const float  INVPIf     = static_cast<float>(INVPI);

#ifndef PIOVER180
  const double PIOVER180    = PI/180.0;
#endif
  const double PIOVER180d    = PIOVER180;
  const float PIOVER180f    = static_cast<float>(PIOVER180);


inline void rotate(float x0, float y0, float *x, float *y, float angle)
{
  float xa = *x - x0, ya = *y - y0;
  *x = xa*cosf(angle) - ya*sinf(angle) + x0;
  *y = xa*sinf(angle) + ya*cosf(angle) + y0;
}



#endif


