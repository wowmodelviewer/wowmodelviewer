#ifndef QUATERNION_H
#define QUATERNION_H

#include <cmath>

#include "vec3d.h"

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
	  double x, y, z, w;

	Vec4D(double x0 = 0.0f, double y0 = 0.0f, double z0 = 0.0f, double w0 = 0.0f) : x(x0), y(y0), z(z0), w(w0) {}

    Vec4D(const Vec4D& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

    Vec4D(const Vec3D& v, const double w0) : x(v.x), y(v.y), z(v.z), w(w0) {}

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

	const Vec4D operator* (double d) const
	  {
      Vec4D r(x*d,y*d,z*d,w*d);
      return r;
	  }

	friend Vec4D operator* (double d, const Vec4D& v)
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

	Vec4D& operator*= (double d)
	  {
      x *= d;
      y *= d;
      z *= d;
      w *= d;
      return *this;
	  }

    double operator* (const Vec4D &v) const
	  {
      return x*v.x + y*v.y + z*v.z + w*v.w;
	  }

    friend std::ostream& operator<<(std::ostream& out, const Vec4D& v)
    {
      out << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
      return out;
    }

	double lengthSquared() const
    {
      return x*x+y*y+z*z+w*w;
    }

	double length() const
    {
      return sqrt(x*x+y*y+z*z+w*w);
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

    Vec3D xyz() const
    {
      return Vec3D(x,y,z);
    }
};

class _QUATERNION_API_ Quaternion: public Vec4D
{
  public:
	Quaternion(double x0 = 0.0f, double y0 = 0.0f, double z0 = 0.0f, double w0 = 1.0f) : Vec4D(x0, y0, z0, w0) {}

	Quaternion(const Vec4D& v) : Vec4D(v) {}

	Quaternion(const Vec3D& v, const double w0) : Vec4D(v, w0) {}

	static const Quaternion slerp(const double r, const Quaternion &v1, const Quaternion &v2);

	static const Quaternion lerp(const double r, const Quaternion &v1, const Quaternion &v2);

	Vec3D GetHPB();

	Matrix toMat();

	Vec3D toEulerXYZ();
};


#endif


