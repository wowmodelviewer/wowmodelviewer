#ifndef QUATERNION_H
#define QUATERNION_H

#include "glm/glm.hpp"

class Matrix;

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _QUATERNION_API_ __declspec(dllexport)
#    else
#        define _QUATERNION_API_ __declspec(dllimport)
#    endif
#else
#    define _QUATERNION_API_
#endif

class _QUATERNION_API_ Vec4D
{
  public:
    float x,y,z,w;

    Vec4D(float x0=0.0f, float y0=0.0f, float z0=0.0f, float w0=0.0f) : x(x0), y(y0), z(z0), w(w0) {}

    Vec4D(const Vec4D& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

    Vec4D(const glm::vec3& v, const float w0) : x(v.x), y(v.y), z(v.z), w(w0) {}

    Vec4D& operator= (const Vec4D &v) 
    {
      x = v.x;
      y = v.y;
      z = v.z;
      w = v.w;
      return *this;
    }

    const Vec4D operator+ (const Vec4D &v) const
    {
      Vec4D r(x+v.x,y+v.y,z+v.z,w+v.w);
      return r;
    }

    const Vec4D operator- (const Vec4D &v) const
    {
      Vec4D r(x-v.x,y-v.y,z-v.z,w-v.w);
      return r;
    }

    const Vec4D operator* (float d) const
    {
      Vec4D r(x*d,y*d,z*d,w*d);
      return r;
    }

    friend Vec4D operator* (float d, const Vec4D& v)
    {
      return v * d;
    }

    Vec4D& operator+= (const Vec4D &v)
    {
      x += v.x;
      y += v.y;
      z += v.z;
      w += v.w;
      return *this;
    }

    Vec4D& operator-= (const Vec4D &v)
    {
      x -= v.x;
      y -= v.y;
      z -= v.z;
      w -= v.w;
      return *this;
    }

    Vec4D& operator*= (float d)
    {
      x *= d;
      y *= d;
      z *= d;
      w *= d;
      return *this;
    }

    float operator* (const Vec4D &v) const
    {
      return x*v.x + y*v.y + z*v.z + w*v.w;
    }


    float lengthSquared() const
    {
      return x*x+y*y+z*z+w*w;
    }

    float length() const
    {
      return sqrtf(x*x+y*y+z*z+w*w);
    }

    Vec4D& normalize()
    {
      this->operator*= (1.0f/length());
      return *this;
    }

    operator float*()
    {
      return (float*)this;
    }

    glm::vec3 xyz() const
    {
      return glm::vec3(x,y,z);
    }
};

#endif


