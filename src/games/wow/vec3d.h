#ifndef VEC3D_H
#define VEC3D_H

#include <iostream>
#include <cmath>

#ifndef PI
const double PI = 3.141592653589793238462643383279502884197169399375105820974944592307816;
#endif
const double PId = PI;
const float  PIf = static_cast<float>(PI);

#ifndef HALFPI
const double HALFPI = PI / 2.0;
#endif
const double HALFPId = HALFPI;
const float  HALFPIf = static_cast<float>(HALFPI);

#ifndef TWOPI
const double TWOPI = 2.0 * PI;
#endif
const double TWOPId = TWOPI;
const float  TWOPIf = static_cast<float>(TWOPI);

#ifndef INVPI
const double INVPI = 1.0 / PI;
#endif
const double INVPId = INVPI;
const float  INVPIf = static_cast<float>(INVPI);

#ifndef PIOVER180
const double PIOVER180 = PI / 180.0;
#endif
const double PIOVER180d = PIOVER180;
const float PIOVER180f = static_cast<float>(PIOVER180);

class Vec3D;
class Vec3F {
public:
  float x, y, z;

  Vec3F(float x0 = 0.0f, float y0 = 0.0f, float z0 = 0.0f) : x(x0), y(y0), z(z0) {}
  Vec3F(double x0, double y0 = 0.0, double z0 = 0.0) : x((float)x0), y((float)y0), z((float)z0) {}
  Vec3F(int x0, int y0, int z0 = 0) : x((float)x0), y((float)y0), z((float)z0) {}     // Assume all ints are float values.

  Vec3F(const Vec3F& v) : x(v.x), y(v.y), z(v.z) {}
  Vec3F(const Vec3D& v);

  void reset() {
    x = y = z = 0.0f;
  }

  Vec3F& operator= (const Vec3F &v) {
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
  }

  /*
  double[3] operator= (const Vec3D &v) {
    double f[3] = {v.x, v.y, v.z};
    return f;
  }
  */

  Vec3F operator+ (const Vec3F &v) const
  {
    Vec3F r(x + v.x, y + v.y, z + v.z);
    return r;
  }

  Vec3F operator- (const Vec3F &v) const
  {
    Vec3F r(x - v.x, y - v.y, z - v.z);
    return r;
  }

  float operator* (const Vec3F &v) const
  {
    return x * v.x + y * v.y + z * v.z;
  }

  Vec3F operator* (float d) const
  {
    Vec3F r(x*d, y*d, z*d);
    return r;
  }

  Vec3F operator/ (float d) const
  {
    Vec3F r(x / d, y / d, z / d);
    return r;
  }

  friend Vec3F operator* (float d, const Vec3F& v)
  {
    return v * d;
  }

  // Cross Product
  Vec3F operator% (const Vec3F &v) const
  {
    Vec3F r(y*v.z - z * v.y, z*v.x - x * v.z, x*v.y - y * v.x);
    return r;
  }

  Vec3F& operator+= (const Vec3F &v)
  {
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }

  Vec3F& operator-= (const Vec3F &v)
  {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }

  Vec3F& operator*= (float d)
  {
    x *= d;
    y *= d;
    z *= d;
    return *this;
  }

  float lengthSquared() const
  {
    return x * x + y * y + z * z;
  }

  float length() const
  {
    return sqrt(x*x + y * y + z * z);
  }

  Vec3F& normalize()
  {
    this->operator*= (1.0f / length());
    return *this;
  }

  Vec3F operator~ () const
  {
    Vec3F r(*this);
    r.normalize();
    return r;
  }

  friend std::istream& operator>>(std::istream& in, Vec3F& v)
  {
    in >> v.x >> v.y >> v.z;
    return in;
  }

  friend std::ostream& operator<<(std::ostream& out, const Vec3F& v)
  {
    out << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return out;
  }

  friend bool operator==(const Vec3F& lhs, const Vec3F& rhs)
  {
    return (abs(lhs.x - rhs.x) < 0.0001) && (abs(lhs.y - rhs.y) < 0.0001) && (abs(lhs.z - rhs.z) < 0.0001);
  }

  operator float*()
  {
    return (float*)this;
  }

  static Vec3F nullVec()
  {
    Vec3F result(0,0,0);
    return result;
  }
};

class Vec3D {
public:
  double x, y, z;

  Vec3D(double x0 = 0.0f, double y0 = 0.0f, double z0 = 0.0f) : x(x0), y(y0), z(z0) {}
  Vec3D(float x0, float y0, float z0) : x(x0), y(y0), z(z0) {}
  Vec3D(int x0, int y0, int z0) : x((double)x0), y((double)y0), z((double)z0) {}     // Assume all ints are double values.

  Vec3D(const Vec3F& v) : x(v.x), y(v.y), z(v.z) {}
  Vec3D(const Vec3D& v) : x(v.x), y(v.y), z(v.z) {}

  void reset() {
    x = y = z = 0.0f;
  }

  Vec3D& operator= (const Vec3D &v) {
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
  }
  Vec3D& operator= (const Vec3F &v) {
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
  }

  /*
  double[3] operator= (const Vec3D &v) {
    double f[3] = {v.x, v.y, v.z};
    return f;
  }
  */

  Vec3D operator+ (const Vec3D &v) const
  {
    Vec3D r(x + v.x, y + v.y, z + v.z);
    return r;
  }

  Vec3D operator- (const Vec3D &v) const
  {
    Vec3D r(x - v.x, y - v.y, z - v.z);
    return r;
  }

  double operator* (const Vec3D &v) const
  {
    return x * v.x + y * v.y + z * v.z;
  }

  Vec3D operator* (double d) const
  {
    Vec3D r(x*d, y*d, z*d);
    return r;
  }

  Vec3D operator/ (double d) const
  {
    Vec3D r(x / d, y / d, z / d);
    return r;
  }

  friend Vec3D operator* (double d, const Vec3D& v)
  {
    return v * d;
  }

  // Cross Product
  Vec3D operator% (const Vec3D &v) const
  {
    Vec3D r(y*v.z - z * v.y, z*v.x - x * v.z, x*v.y - y * v.x);
    return r;
  }

  Vec3D& operator+= (const Vec3D &v)
  {
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }

  Vec3D& operator-= (const Vec3D &v)
  {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }

  Vec3D& operator*= (double d)
  {
    x *= d;
    y *= d;
    z *= d;
    return *this;
  }

  double lengthSquared() const
  {
    return x * x + y * y + z * z;
  }

  double length() const
  {
    return sqrt(x*x + y * y + z * z);
  }

  Vec3D& normalize()
  {
    this->operator*= (1.0f / length());
    return *this;
  }

  Vec3D operator~ () const
  {
    Vec3D r(*this);
    r.normalize();
    return r;
  }

  friend std::istream& operator>>(std::istream& in, Vec3D& v)
  {
    in >> v.x >> v.y >> v.z;
    return in;
  }

  friend std::ostream& operator<<(std::ostream& out, const Vec3D& v)
  {
    out << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return out;
  }

  friend bool operator==(const Vec3D& lhs, const Vec3D& rhs)
  {
    return (abs(lhs.x - rhs.x) < 0.0001) && (abs(lhs.y - rhs.y) < 0.0001) && (abs(lhs.z - rhs.z) < 0.0001);
  }

  operator float*()
  {
    return (float*)this;
  }

  static Vec3D nullVec()
  {
    Vec3D result;
    return result;
  }
};

class Vec2D;
class Vec2F {
public:
  float x, y;

  Vec2F(float x0 = 0.0f, float y0 = 0.0f) : x(x0), y(y0) {}
  Vec2F(double x0, double y0) : x((float)x0), y((float)y0) {}
  Vec2F(int x0, int y0) : x((float)x0), y((float)y0) {}     // Assume all ints are float values.

  Vec2F(const Vec2F& v) : x(v.x), y(v.y) {}
  Vec2F(const Vec2D& v);

  Vec2F& operator= (const Vec2F &v) {
    x = v.x;
    y = v.y;
    return *this;
  }

  Vec2F operator+ (const Vec2F &v) const
  {
    Vec2F r(x + v.x, y + v.y);
    return r;
  }

  Vec2F operator- (const Vec2F &v) const
  {
    Vec2F r(x - v.x, y - v.y);
    return r;
  }

  float operator* (const Vec2F &v) const
  {
    return x * v.x + y * v.y;
  }

  Vec2F operator* (float d) const
  {
    Vec2F r(x*d, y*d);
    return r;
  }

  friend Vec2F operator* (float d, const Vec2F& v)
  {
    return v * d;
  }

  Vec2F& operator+= (const Vec2F &v)
  {
    x += v.x;
    y += v.y;
    return *this;
  }

  Vec2F& operator-= (const Vec2F &v)
  {
    x -= v.x;
    y -= v.y;
    return *this;
  }

  Vec2F& operator*= (float d)
  {
    x *= d;
    y *= d;
    return *this;
  }
  Vec2F& operator*= (double d) { return operator*=((float)d); }

  float lengthSquared() const
  {
    return x * x + y * y;
  }

  float length() const
  {
    return sqrt(x*x + y * y);
  }

  Vec2F& normalize()
  {
    this->operator*= (1.0f / length());
    return *this;
  }

  Vec2F operator~ () const
  {
    Vec2F r(*this);
    r.normalize();
    return r;
  }

  friend std::istream& operator>>(std::istream& in, Vec2F& v)
  {
    in >> v.x >> v.y;
    return in;
  }

  friend std::ostream& operator<<(std::ostream& out, Vec2F& v)
  {
    out << "(" << v.x << ", " << v.y << ")";
    return out;
  }

  operator float*()
  {
    return (float*)this;
  }

};

class Vec2D {
public:
  double x, y;

  Vec2D(double x0 = 0.0f, double y0 = 0.0f) : x(x0), y(y0) {}
  Vec2D(float x0, float y0) : x(x0), y(y0) {}
  Vec2D(int x0, int y0) : x((double)x0), y((double)y0) {}     // Assume all ints are double values.

  Vec2D(const Vec2D& v) : x(v.x), y(v.y) {}
  Vec2D(const Vec2F& v) : x(v.x), y(v.y) {}

  Vec2D& operator= (const Vec2D &v) {
    x = v.x;
    y = v.y;
    return *this;
  }

  Vec2D operator+ (const Vec2D &v) const
  {
    Vec2D r(x + v.x, y + v.y);
    return r;
  }

  Vec2D operator- (const Vec2D &v) const
  {
    Vec2D r(x - v.x, y - v.y);
    return r;
  }

  double operator* (const Vec2D &v) const
  {
    return x * v.x + y * v.y;
  }

  Vec2D operator* (double d) const
  {
    Vec2D r(x*d, y*d);
    return r;
  }

  friend Vec2D operator* (double d, const Vec2D& v)
  {
    return v * d;
  }

  Vec2D& operator+= (const Vec2D &v)
  {
    x += v.x;
    y += v.y;
    return *this;
  }

  Vec2D& operator-= (const Vec2D &v)
  {
    x -= v.x;
    y -= v.y;
    return *this;
  }

  Vec2D& operator*= (double d)
  {
    x *= d;
    y *= d;
    return *this;
  }

  double lengthSquared() const
  {
    return x * x + y * y;
  }

  double length() const
  {
    return sqrt(x*x + y * y);
  }

  Vec2D& normalize()
  {
    this->operator*= (1.0f / length());
    return *this;
  }

  Vec2D operator~ () const
  {
    Vec2D r(*this);
    r.normalize();
    return r;
  }


  friend std::istream& operator>>(std::istream& in, Vec2D& v)
  {
    in >> v.x >> v.y;
    return in;
  }

  friend std::ostream& operator<<(std::ostream& out, Vec2D& v)
  {
    out << "(" << v.x << ", " << v.y << ")";
    return out;
  }

  operator float*()
  {
    return (float*)this;
  }

};

inline Vec3F::Vec3F(const Vec3D & v) : x((float)v.x), y((float)v.y), z((float)v.z) {}
inline Vec2F::Vec2F(const Vec2D & v) : x((float)v.x), y((float)v.y) {}

inline void rotate(double x0, double y0, double *x, double *y, double angle)
{
  double xa = *x - x0, ya = *y - y0;
  *x = xa * cos(angle) - ya * sin(angle) + x0;
  *y = xa * sin(angle) + ya * cos(angle) + y0;
}

#endif