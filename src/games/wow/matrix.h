#ifndef MATRIX_H
#define MATRIX_H

#include "quaternion.h"
#include "vec3d.h"

#undef minor

#include <cstdlib>
#include <limits>

class Matrix {
public:
	double m[4][4];

	Matrix()
	{
	}

	Matrix(const Matrix& p)
	{
        for (size_t j=0; j<4; j++) {
        	for (size_t i=0; i<4; i++) {
        		m[j][i] = p.m[j][i];
			}
		}
	}

	Matrix& operator= (const Matrix& p)
	{
        for (size_t j=0; j<4; j++) {
        	for (size_t i=0; i<4; i++) {
        		m[j][i] = p.m[j][i];
			}
		}
		return *this;
	}


	void zero()
	{
        for (size_t j=0; j<4; j++) {
        	for (size_t i=0; i<4; i++) {
        		m[j][i] = 0;
			}
		}
	}

	void unit()
	{
		zero();
        m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
	}

	static Matrix identity()
  {
	  Matrix m;
	  m.unit();
	  return m;
  }

	void translation(const Vec3D& tr)
	{
		/*
			100#
			010#
			001#
			0001
		*/
		unit();
		m[0][3]=tr.x;
		m[1][3]=tr.y;
		m[2][3]=tr.z;
	}

	static const Matrix newTranslation(const Vec3D& tr)
	{
		Matrix t;
		t.translation(tr);
		return t;
	}

	void scale(const Vec3D& sc)
	{
		/*
			#000
			0#00
			00#0
			0001
		*/
		zero();
		m[0][0]=sc.x;
		m[1][1]=sc.y;
		m[2][2]=sc.z;
		m[3][3]=1.0f;
	}

	static const Matrix newScale(const Vec3D& sc)
	{
		Matrix t;
		t.scale(sc);
		return t;
	}

	void quaternionRotate(const Quaternion& q)
	{
		/*
			###0
			###0
			###0
			0001
		*/
		m[0][0] = 1.0f - 2.0f * q.y * q.y - 2.0f * q.z * q.z;
		m[0][1] = 2.0f * q.x * q.y + 2.0f * q.w * q.z;
		m[0][2] = 2.0f * q.x * q.z - 2.0f * q.w * q.y;
		m[1][0] = 2.0f * q.x * q.y - 2.0f * q.w * q.z;
		m[1][1] = 1.0f - 2.0f * q.x * q.x - 2.0f * q.z * q.z;
		m[1][2] = 2.0f * q.y * q.z + 2.0f * q.w * q.x;
		m[2][0] = 2.0f * q.x * q.z + 2.0f * q.w * q.y;
		m[2][1] = 2.0f * q.y * q.z - 2.0f * q.w * q.x;
		m[2][2] = 1.0f - 2.0f * q.x * q.x - 2.0f * q.y * q.y;
		m[0][3] = m[1][3] = m[2][3] = m[3][0] = m[3][1] = m[3][2] = 0;
		m[3][3] = 1.0f;
	}

	void QRotate(const Quaternion& q){
		double x = q.x;
		double y = q.y;
		double z = q.z;
		double angle = q.w;
		/*
			R = u u^T + cos theta (I - u u^T) + sin theta S
			  = u u^T + I cos(theta) - u u^T cos(theta) + S sin(theta)
			  = u u^t (1 - cos(theta)) + I cos(theta) + S sin(theta)
			u = v/||v|| = (x' y' z')^T
			u u^T = (x'x'  x'y'  x'z')
					(y'x'  y'y'  y'z')
					(z'x'  z'y'  z'z')
			S = ( 0  -z'  y')
				( z'  0  -x')
				(-y'  x'  0 )

		*/
		unit();

		double l = sqrt(x*x + y*y + z*z);
		if(l == 0)
			return;

		x /= l;
		y /= l;
		z /= l;

		double sina = sin(angle);
		double cosa = cos(angle);

		m[0][0] = x*x * (1 - cosa) + cosa;
		m[0][1] = x*y * (1 - cosa)         - z * sina;
		m[0][2] = x*z * (1 - cosa)         + y * sina;

		m[1][0] = y*x * (1 - cosa)         + z * sina;
		m[1][1] = y*y * (1 - cosa) + cosa;
		m[1][2] = y*z * (1 - cosa)         - x * sina;

		m[2][0] = z*x * (1 - cosa)         - y * sina;
		m[2][1] = z*y * (1 - cosa)         + x * sina;
		m[2][2] = z*z * (1 - cosa) + cosa;
	}

	static const Matrix newQuatRotate(const Quaternion& qr)
	{
		Matrix t;
		t.quaternionRotate(qr);
		return t;
	}

	Vec3D operator* (const Vec3D& v) const
	{
		Vec3D o;
		o.x = m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3];
		o.y = m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3];
		o.z = m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3];
        return o;
	}

	Matrix operator* (const Matrix& p) const
	{
		Matrix o;
		o.m[0][0] = m[0][0]*p.m[0][0] + m[0][1]*p.m[1][0] + m[0][2]*p.m[2][0] + m[0][3]*p.m[3][0];
		o.m[0][1] = m[0][0]*p.m[0][1] + m[0][1]*p.m[1][1] + m[0][2]*p.m[2][1] + m[0][3]*p.m[3][1];
		o.m[0][2] = m[0][0]*p.m[0][2] + m[0][1]*p.m[1][2] + m[0][2]*p.m[2][2] + m[0][3]*p.m[3][2];
		o.m[0][3] = m[0][0]*p.m[0][3] + m[0][1]*p.m[1][3] + m[0][2]*p.m[2][3] + m[0][3]*p.m[3][3];

		o.m[1][0] = m[1][0]*p.m[0][0] + m[1][1]*p.m[1][0] + m[1][2]*p.m[2][0] + m[1][3]*p.m[3][0];
		o.m[1][1] = m[1][0]*p.m[0][1] + m[1][1]*p.m[1][1] + m[1][2]*p.m[2][1] + m[1][3]*p.m[3][1];
		o.m[1][2] = m[1][0]*p.m[0][2] + m[1][1]*p.m[1][2] + m[1][2]*p.m[2][2] + m[1][3]*p.m[3][2];
		o.m[1][3] = m[1][0]*p.m[0][3] + m[1][1]*p.m[1][3] + m[1][2]*p.m[2][3] + m[1][3]*p.m[3][3];

		o.m[2][0] = m[2][0]*p.m[0][0] + m[2][1]*p.m[1][0] + m[2][2]*p.m[2][0] + m[2][3]*p.m[3][0];
		o.m[2][1] = m[2][0]*p.m[0][1] + m[2][1]*p.m[1][1] + m[2][2]*p.m[2][1] + m[2][3]*p.m[3][1];
		o.m[2][2] = m[2][0]*p.m[0][2] + m[2][1]*p.m[1][2] + m[2][2]*p.m[2][2] + m[2][3]*p.m[3][2];
		o.m[2][3] = m[2][0]*p.m[0][3] + m[2][1]*p.m[1][3] + m[2][2]*p.m[2][3] + m[2][3]*p.m[3][3];

		o.m[3][0] = m[3][0]*p.m[0][0] + m[3][1]*p.m[1][0] + m[3][2]*p.m[2][0] + m[3][3]*p.m[3][0];
		o.m[3][1] = m[3][0]*p.m[0][1] + m[3][1]*p.m[1][1] + m[3][2]*p.m[2][1] + m[3][3]*p.m[3][1];
		o.m[3][2] = m[3][0]*p.m[0][2] + m[3][1]*p.m[1][2] + m[3][2]*p.m[2][2] + m[3][3]*p.m[3][2];
		o.m[3][3] = m[3][0]*p.m[0][3] + m[3][1]*p.m[1][3] + m[3][2]*p.m[2][3] + m[3][3]*p.m[3][3];

		return o;
	}

	Quaternion GetQuaternion(){
		Quaternion q(Vec4D(0,0,0,0));

		double trace = m[0][0] + m[1][1] + m[2][2];

		if (trace > 0){
			double s = 0.5 / sqrt(trace + 1.0);
			q.w = 0.25f / s;
			q.x = (m[2][1] - m[1][2]) * s;
			q.y = (m[0][2] - m[2][0]) * s;
			q.z = (m[1][0] - m[0][1]) * s;
		}else{
			if ((m[0][0] > m[1][1]) && (m[0][0] > m[2][2])){
				double s = 2.0 * sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
				q.w = (m[2][1] - m[1][2] ) / s;
				q.x = 0.25f * s;
				q.y = (m[0][1] + m[1][0] ) / s;
				q.z = (m[0][2] + m[2][0] ) / s;
			} else if (m[1][1] > m[2][2]) {
				double s = 2.0f * sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
				q.w = (m[0][2] - m[2][0] ) / s;
				q.x = (m[0][1] + m[1][0] ) / s;
				q.y = 0.25 * s;
				q.z = (m[1][2] + m[2][1] ) / s;
			} else {
				double s = 2.0 * sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
				q.w = (m[1][0] - m[0][1] ) / s;
				q.x = (m[0][2] + m[2][0] ) / s;
				q.y = (m[1][2] + m[2][1] ) / s;
				q.z = 0.25 * s;
			}
		}

		return q;
	}

	Vec3D GetTranslation() {
	  return Vec3D(m[0][3], m[1][3], m[2][3]);
	}

	Vec3D GetScale() {
	  return Vec3D(m[0][0], m[1][1], m[2][2]);
	}


	double determinant() const
	{
		#define SUB(a,b) (m[2][a]*m[3][b] - m[3][a]*m[2][b])
		return
			 m[0][0] * (m[1][1]*SUB(2,3) - m[1][2]*SUB(1,3) + m[1][3]*SUB(1,2))
			-m[0][1] * (m[1][0]*SUB(2,3) - m[1][2]*SUB(0,3) + m[1][3]*SUB(0,2))
			+m[0][2] * (m[1][0]*SUB(1,3) - m[1][1]*SUB(0,3) + m[1][3]*SUB(0,1))
			-m[0][3] * (m[1][0]*SUB(1,2) - m[1][1]*SUB(0,2) + m[1][2]*SUB(0,1));
		#undef SUB
	}

	double minor(size_t x, size_t y) const
	{
		double s[3][3];
		for (size_t j=0, v=0; j<4; j++) {
			if (j==y) continue;
			for (size_t i=0, u=0; i<4; i++) {
				if (i!=x) {
					s[v][u++] = m[j][i];
				}
			}
			v++;
		}
		#define SUB(a,b) (s[1][a]*s[2][b] - s[2][a]*s[1][b])
		return s[0][0] * SUB(1,2) - s[0][1] * SUB(0,2) + s[0][2] * SUB(0,1);
		#undef SUB
	}
	
	const Matrix adjoint() const
	{
		Matrix a;
		for (size_t j=0; j<4; j++) {
			for (size_t i=0; i<4; i++) {
				a.m[i][j] = (((i+j)&1)?-1.0f:1.0f) * minor(i,j);
			}
		}
		return a;
	}
	
	void invert()
	{
		Matrix adj = this->adjoint();
		double invdet = 1.0f / this->determinant();
        for (size_t j=0; j<4; j++) {
        	for (size_t i=0; i<4; i++) {
				m[j][i] = adj.m[j][i] * invdet;
			}
		}
	}

	void transpose()
	{
        for (size_t j=1; j<4; j++) {
        	for (size_t i=0; i<j; i++) {
				double f = m[j][i];
				m[j][i] = m[i][j];
				m[i][j] = f;
			}
		}
	}

	Matrix& operator*= (const Matrix& p)
	{
		return *this = this->operator*(p);
	}

	operator float*()
	{
		return (float*)this;
	}

	void Rotate_HPB(const double h, const double p, const double b){
		double sinh = sin(h);
		double cosh = cos(h);
		double sinp = sin(p);
		double cosp = cos(p);
		double sinb = sin(b);
		double cosb = cos(b);

		m[0][0] = cosh * cosb + sinh * sinp * sinb;
		m[1][0] = cosp * sinb;
		m[2][0] = -sinh * cosb + cosh * sinp * sinb;
		m[0][1] = -cosh * sinb + sinh * sinp * cosb;
		m[1][1] = cosp * cosb;
		m[2][1] = sinh * sinb + cosh * sinp * cosb;
		m[0][2] = sinh * cosp;
		m[1][2] = -sinp;
		m[2][2] = cosh * cosp;
	}

	Matrix newRotate_HPB(const double h, const double p, double b){
		Matrix dest;
		dest.unit();
		dest.Rotate_HPB(h,p,b);
		return dest;
	}

	Vec3D GetHPB(){
		Vec3D y(m[0][1], m[1][1], m[2][1]);
		Vec3D z(m[0][2], m[1][2], m[2][2]);

		Vec3D hpb;

		// First get RX and RY
		bool zzero[3] = {
			(abs(z.x) <= std::numeric_limits<double>::epsilon()),
			(abs(z.y) <= std::numeric_limits<double>::epsilon()),
			(abs(z.z) <= std::numeric_limits<double>::epsilon())
		};

		if (zzero[0] && zzero[2])
		{
			hpb.x = 0;
			if (!zzero[1])
				hpb.y = (z.y < 0.0) ? HALFPI : -HALFPI;
			else
				hpb.y = 0;
		}
		else
		{
			if (zzero[2])
				hpb.x = (z.x < 0.0) ? -HALFPI : HALFPI;
			else
				hpb.x = atan2(z.x, z.z);
			double hyp = sqrt(z.x * z.x + z.z * z.z);
			if (hyp <= std::numeric_limits<double>::epsilon())
				hpb.y = (z.y < 0.0) ? HALFPI : -HALFPI;
			else
				hpb.y = -atan2(z.y, hyp);
		}

		// Find RZ
		Matrix rot_hp(newRotate_HPB(hpb.x, hpb.y, 0));
		rot_hp.invert();
		Vec3D rot_y(rot_hp * y);
		bool rot_yzero[3] = {
			(abs(rot_y.x) <= std::numeric_limits<double>::epsilon()),
			(abs(rot_y.y) <= std::numeric_limits<double>::epsilon()),
			(abs(rot_y.z) <= std::numeric_limits<double>::epsilon())
		};
		if(rot_yzero[0] && rot_yzero[1])
			hpb.z = 0;
		else if(rot_yzero[1])
			hpb.z = (rot_y.x < 0.0) ? HALFPI : -HALFPI;
		else
			hpb.z = atan2(-rot_y.x, rot_y.y);

		return hpb;
	}
};

#endif

