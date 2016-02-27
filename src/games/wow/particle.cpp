#include "Bone.h"

#include "particle.h"

#include "GlobalSettings.h"
#include "logger/Logger.h"

#define MAX_PARTICLES 10000


template<class T>
T lifeRamp(float life, float mid, const T &a, const T &b, const T &c)
{
    if (life<=mid)
      return interpolate<T>(life / mid,a,b);
    else
      return interpolate<T>((life-mid) / (1.0f-mid),b,c);
}

void ParticleSystem::init(GameFile * f, M2ParticleDef &mta, uint32 *globals)
{
  flags = mta.flags;
  multitexture = flags & MODELPARTICLE_FLAGS_MULTITEXTURE;
  speed.init (mta.EmissionSpeed, f, globals);
  variation.init (mta.SpeedVariation, f, globals);
  spread.init (mta.VerticalRange, f, globals);
  lat.init (mta.HorizontalRange, f, globals);
  gravity.init (mta.Gravity, f, globals);
  lifespan.init (mta.Lifespan, f, globals);
  rate.init (mta.EmissionRate, f, globals);
  areal.init (mta.EmissionAreaLength, f, globals);
  areaw.init (mta.EmissionAreaWidth, f, globals);
  deacceleration.init (mta.Gravity2, f, globals);
  enabled.init (mta.EnabledIn, f, globals);
  particleColID = mta.ParticleColorIndex;

  Vec3D colors2[3];
  memcpy(colors2, f->getBuffer()+mta.p.colors.ofsKeys, sizeof(Vec3D)*3);

  for (size_t i=0; i<3; i++)
  {
    float opacity = *(short*)(f->getBuffer()+mta.p.opacity.ofsKeys+i*2);
    colors[i] = Vec4D(colors2[i].x/255.0f, colors2[i].y/255.0f,
        colors2[i].z/255.0f, opacity/32767.0f);
    sizes[i] = (*(float*)(f->getBuffer()+mta.p.sizes.ofsKeys+i*sizeof(Vec2D)))*mta.p.scales[i];
  }
  mid = 0.5; // mid can't be 0 or 1, TODO, Alfred

  slowdown = mta.p.slowdown;
  rotation = mta.p.rotation;
  pos = fixCoordSystem(mta.pos);
  texture = model->textures[mta.texture];
  blend = mta.blend;
  rows = mta.rows;
  if (rows == 0)
    rows = 1;
  cols = mta.cols;
  if (cols == 0)
    cols = 1;
  ParticleType = 0;
  //order = mta.s2;
  order = ParticleType > 0 ? -1 : 0;

  parent = model->bones + mta.bone;

  // transform = flags & 1024;

  if (multitexture)
  {
    //  when multitexture is flagged, three textures are packed into the one uint16
    //  (5 bits each, plus one bit left over):
    texture  = model->textures[mta.texture & 0x1f];
    texture2 = model->textures[(mta.texture >> 5) & 0x1f];
    texture3 = model->textures[(mta.texture >> 10) & 0x1f];
  }

  billboard = !(flags & MODELPARTICLE_FLAGS_DONOTBILLBOARD);

  // diagnosis test info
  EmitterType = mta.EmitterType;

  manim = mtime = 0;
  rem = 0;

  emitter = 0;
  switch (EmitterType)
  {
    case MODELPARTICLE_EMITTER_PLANE:
            emitter = new PlaneParticleEmitter(this);
            break;
    case MODELPARTICLE_EMITTER_SPHERE:
            emitter = new SphereParticleEmitter(this);
            break;
    case MODELPARTICLE_EMITTER_SPLINE: // Spline?
    default:
            LOG_ERROR << "Unknown Emitter:" << EmitterType;
            break;
  }

  tofs = frand();

  // init tiles, slice the texture
  for (int i=0; i<rows*cols; i++)
  {
    TexCoordSet tc;
    initTile(tc.tc, i);
    tiles.push_back(tc);
  }
}

void ParticleSystem::initTile(Vec2D *tc, int num)
{
  Vec2D otc[4];
  Vec2D a,b;
  int x = num % cols;
  int y = num / cols;
  a.x = x * (1.0f / cols);
  b.x = (x+1) * (1.0f / cols);
  a.y = y * (1.0f / rows);
  b.y = (y+1) * (1.0f / rows);

  otc[0] = a;
  otc[2] = b;
  otc[1].x = b.x;
  otc[1].y = a.y;
  otc[3].x = a.x;
  otc[3].y = b.y;

  for (size_t i=0; i<4; i++) {
    tc[(i+4-order) & 3] = otc[i];
  }
}


void ParticleSystem::update(float dt)
{
  size_t l_manim = manim;
  if (GLOBALSETTINGS.bZeroParticle)
    l_manim = 0;
  float grav = gravity.getValue(l_manim, mtime);
  float deaccel = deacceleration.getValue(l_manim, mtime);

  // spawn new particles
  if (emitter) {
    float frate = rate.getValue(l_manim, mtime);
    float flife = lifespan.getValue(l_manim, mtime);

    float ftospawn;
    if (flife)
      ftospawn = (dt * frate / flife) + rem;
    else
      ftospawn = rem;
    if (ftospawn < 1.0f) {
      rem = ftospawn;
      if (rem < 0)
        rem = 0;
    } else {
      unsigned int tospawn = (int)ftospawn;

      if ((tospawn + particles.size()) > MAX_PARTICLES) // Error check to prevent the program from trying to load insane amounts of particles.
        tospawn = (unsigned int)(MAX_PARTICLES - particles.size());

      rem = ftospawn - (float)tospawn;

      float w = areal.getValue(l_manim, mtime) * 0.5f;
      float l = areaw.getValue(l_manim, mtime) * 0.5f;
      float spd = speed.getValue(l_manim, mtime);
      float var = variation.getValue(l_manim, mtime);
      float spr = spread.getValue(l_manim, mtime);
      float spr2 = lat.getValue(l_manim, mtime);
      bool en = true;
      if (enabled.uses(manim))
        en = enabled.getValue(manim, mtime)!=0;

      //rem = 0;
      if (en) {
        for (size_t i=0; i<tospawn; i++) {
          Particle p = emitter->newParticle(manim, mtime, w, l, spd, var, spr, spr2);
          // sanity check:
          if (particles.size() < MAX_PARTICLES) // No need to check this every loop iteration. Already checked above.
            particles.push_back(p);
        }
      }
    }
  }

  float mspeed = 1.0f;
  Vec4D colVals[3];

  if (replaceParticleColors && particleColID >= 11 && particleColID <= 13)
  {
    int id = particleColID - 11;
    colVals[0] = particleColorReplacements[id][0];
    colVals[1] = particleColorReplacements[id][1];
    colVals[2] = particleColorReplacements[id][2];
    // need to add the particle alphas:
    colVals[0][3] = colors[0][3];
    colVals[1][3] = colors[1][3];
    colVals[2][3] = colors[2][3];
  }
  else
  {
    colVals[0] = colors[0];
    colVals[1] = colors[1];
    colVals[2] = colors[2];
  }
  for (ParticleList::iterator it = particles.begin(); it != particles.end(); ) {
    Particle &p = *it;
    p.speed += p.down * grav * dt - p.dir * deaccel * dt;

    if (slowdown>0) {
      mspeed = expf(-1.0f * slowdown * p.life);
    }
    p.pos += p.speed * mspeed * dt;

    p.life += dt;
    float rlife = p.life / p.maxlife;
    // calculate size and color based on lifetime
    p.size = lifeRamp<float>(rlife, mid, sizes[0], sizes[1], sizes[2]);
    p.color = lifeRamp<Vec4D>(rlife, mid, colVals[0], colVals[1], colVals[2]);

    // kill off old particles
    if (rlife >= 1.0f)
      particles.erase(it++);
    else
      ++it;
  }
}

void ParticleSystem::setup(size_t anim, size_t time)
{
  manim = anim;
  mtime = time;

  /*
	if (transform) {
		// transform every particle by the parent trans matrix   - apparently this isn't needed
		Matrix m = parent->mat;
		for (ParticleList::iterator it = particles.begin(); it != particles.end(); ++it) {
			it->tpos = m * it->pos;
		}
	} else {
		for (ParticleList::iterator it = particles.begin(); it != particles.end(); ++it) {
			it->tpos = it->pos;
		}
	}
   */
}

void ParticleSystem::draw()
{
  // ALPHA BLENDING
  // blend mode
  if (blend < 0)
    blend = 0;
  else if (blend > 7)
    blend = 2;
  switch (blend)
  {
    case BM_OPAQUE:	         // 0
      glDisable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case BM_TRANSPARENT:      // 1
      glDisable(GL_BLEND);
      glEnable(GL_ALPHA_TEST);
      glBlendFunc(GL_ONE, GL_ZERO);
      break;
    case BM_ALPHA_BLEND:      // 2
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case BM_ADDITIVE:         // 3
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_COLOR, GL_ONE);
      break;
    case BM_ADDITIVE_ALPHA:   // 4
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      break;
    case BM_MODULATE:	         // 5
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_DST_COLOR, GL_ZERO);
      break;
    case BM_MODULATEX2:	    // 6
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
      break;
    case BM_7:	               // 7, new in WoD
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    default:
      LOG_ERROR << "Unknown blendmode:" << blend;
      glEnable(GL_BLEND);
      glDisable(GL_ALPHA_TEST);
      glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
  }

  if (!multitexture)
  {
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  }
  else
  {
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 4.0);
    glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 4.0);
    if (texture2)
    {
      glActiveTextureARB(GL_TEXTURE1_ARB);
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texture2);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
      glActiveTextureARB(GL_TEXTURE0_ARB);
    }
    if (texture3)
    {
      glActiveTextureARB(GL_TEXTURE2_ARB);
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, texture3);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
      glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
      glActiveTextureARB(GL_TEXTURE0_ARB);
    }
  }
  Vec3D vRight(1,0,0);
  Vec3D vUp(0,1,0);

  // position stuff
  const float f = 1;//0.707106781f; // sqrt(2)/2
  Vec3D bv0 = Vec3D(-f,+f,0);
  Vec3D bv1 = Vec3D(+f,+f,0);

  if (billboard)
  {
    float modelview[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);

    vRight = Vec3D(modelview[0], modelview[4], modelview[8]);
    vUp = Vec3D(modelview[1], modelview[5], modelview[9]); // Spherical billboarding
    //vUp = Vec3D(0,1,0); // Cylindrical billboarding
  }

  /*
   * type:
   * 0	 "normal" particle
   * 1	large quad from the particle's origin to its position (used in Moonwell water effects)
   * 2	seems to be the same as 0 (found some in the Deeprun Tram blinky-lights-sign thing)
   */

  Vec3D vert1, vert2, vert3, vert4, pos;
  float size;

  glBegin(GL_QUADS);

  for (ParticleList::iterator it = particles.begin(); it != particles.end(); ++it)
  {
    if (tiles.size() - 1 < it->tile) // Alfred, 2009.08.07, error prevent
      break;
    glColor4fv(it->color);
    size = it->size;
    pos = it->pos;
    if (ParticleType == 0 || ParticleType > 1)
    {
      // TODO: figure out type 2 (deeprun tram subway sign)
      // - doesn't seem to be any different from 0 -_- regular particles
      if (billboard)
      {

        vert1 = pos - (vRight + vUp) * size;
        vert2 = pos + (vRight - vUp) * size;
        vert3 = pos + (vRight + vUp) * size;
        vert4 = pos - (vRight - vUp) * size;
      }
      else
      {
        vert1 = pos + it->corners[0] * size;
        vert2 = pos + it->corners[1] * size;
        vert3 = pos + it->corners[2] * size;
        vert4 = pos + it->corners[3] * size;
      }
    }
    else if (ParticleType == 1)
    {
      vert1 = pos + bv0 * size;
      vert2 = pos + bv1 * size;
      vert3 = it->origin + bv1 * size;
      vert4 = it->origin + bv0 * size;
    }

    glMultiTexCoord2fvARB(GL_TEXTURE0_ARB, tiles[it->tile].tc[0]);
    if (texture2)
      glMultiTexCoord2fvARB(GL_TEXTURE1_ARB, tiles[it->tile].tc[0]);
    if (texture3)
      glMultiTexCoord2fvARB(GL_TEXTURE2_ARB, tiles[it->tile].tc[0]);
    glVertex3fv(vert1);

    glMultiTexCoord2fvARB(GL_TEXTURE0_ARB, tiles[it->tile].tc[1]);
    if (texture2)
      glMultiTexCoord2fvARB(GL_TEXTURE1_ARB, tiles[it->tile].tc[1]);
    if (texture3)
      glMultiTexCoord2fvARB(GL_TEXTURE2_ARB, tiles[it->tile].tc[1]);
    glVertex3fv(vert2);

    glMultiTexCoord2fvARB(GL_TEXTURE0_ARB, tiles[it->tile].tc[2]);
    if (texture2)
      glMultiTexCoord2fvARB(GL_TEXTURE1_ARB, tiles[it->tile].tc[2]);
    if (texture3)
      glMultiTexCoord2fvARB(GL_TEXTURE2_ARB, tiles[it->tile].tc[2]);
    glVertex3fv(vert3);

    glMultiTexCoord2fvARB(GL_TEXTURE0_ARB, tiles[it->tile].tc[3]);
    if (texture2)
      glMultiTexCoord2fvARB(GL_TEXTURE1_ARB, tiles[it->tile].tc[3]);
    if (texture3)
      glMultiTexCoord2fvARB(GL_TEXTURE2_ARB, tiles[it->tile].tc[3]);
    glVertex3fv(vert4);
  }
  glEnd();

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0);
  glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0);
  glDisable(GL_TEXTURE_2D);
  if (texture2)
  {
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
  }
  if (texture3)
  {
    glActiveTextureARB(GL_TEXTURE2_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
  }
  glActiveTextureARB(GL_TEXTURE0_ARB);
}

//Generates the rotation matrix based on spread
static Matrix	SpreadMat;
void CalcSpreadMatrix(float Spread1,float Spread2, float w, float l)
{
  int i,j;
  float a[2],c[2],s[2];
  Matrix	Temp;

  SpreadMat.unit();

  a[0]=randfloat(-Spread1,Spread1)/2.0f;
  a[1]=randfloat(-Spread2,Spread2)/2.0f;

  /*SpreadMat.m[0][0]*=l;
	SpreadMat.m[1][1]*=l;
	SpreadMat.m[2][2]*=w;*/

  for(i=0;i<2;i++)
  {
    c[i]=cos(a[i]);
    s[i]=sin(a[i]);
  }
  Temp.unit();
  Temp.m[1][1]=c[0];
  Temp.m[2][1]=s[0];
  Temp.m[2][2]=c[0];
  Temp.m[1][2]=-s[0];

  SpreadMat=SpreadMat*Temp;

  Temp.unit();
  Temp.m[0][0]=c[1];
  Temp.m[1][0]=s[1];
  Temp.m[1][1]=c[1];
  Temp.m[0][1]=-s[1];

  SpreadMat=SpreadMat*Temp;

  float Size=abs(c[0])*l+abs(s[0])*w;
  for(i=0;i<3;i++)
    for(j=0;j<3;j++)
      SpreadMat.m[i][j]*=Size;
}

Particle PlaneParticleEmitter::newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2)
{
  Particle p;

  //Spread Calculation
  Matrix mrot;

  CalcSpreadMatrix(spr,spr,1.0f,1.0f);
  mrot=sys->parent->mrot*SpreadMat;

  if (sys->flags == 1041) { // Trans Halo
    p.pos = sys->parent->mat * (sys->pos + Vec3D(randfloat(-l,l), 0, randfloat(-w,w)));

    const float t = randfloat(0.0f, float(2*PI));

    p.pos = Vec3D(0.0f, sys->pos.y + 0.15f, sys->pos.z) + Vec3D(cos(t)/8, 0.0f, sin(t)/8); // Need to manually correct for the halo - why?

    // var isn't being used, which is set to 1.0f,  whats the importance of this?
    // why does this set of values differ from other particles

    Vec3D dir(0.0f, 1.0f, 0.0f);
    p.dir = dir;

    p.speed = dir.normalize() * spd * randfloat(0, var);
  } else if (sys->flags == 25 && sys->parent->parent<1) { // Weapon Flame
    p.pos = sys->parent->pivot * (sys->pos + Vec3D(randfloat(-l,l), randfloat(-l,l), randfloat(-w,w)));
    Vec3D dir = mrot * Vec3D(0.0f, 1.0f, 0.0f);
    p.dir = dir.normalize();
    //Vec3D dir = sys->model->bones[sys->parent->parent].mrot * sys->parent->mrot * Vec3D(0.0f, 1.0f, 0.0f);
    //p.speed = dir.normalize() * spd;

  } else if (sys->flags == 25 && sys->parent->parent > 0) { // Weapon with built-in Flame (Avenger lightsaber!)
    p.pos = sys->parent->mat * (sys->pos + Vec3D(randfloat(-l,l), randfloat(-l,l), randfloat(-w,w)));
    Vec3D dir = Vec3D(sys->parent->mat.m[1][0],sys->parent->mat.m[1][1], sys->parent->mat.m[1][2]) * Vec3D(0.0f, 1.0f, 0.0f);
    p.speed = dir.normalize() * spd * randfloat(0, var*2);

  } else if (sys->flags == 17 && sys->parent->parent<1) { // Weapon Glow
    p.pos = sys->parent->pivot * (sys->pos + Vec3D(randfloat(-l,l), randfloat(-l,l), randfloat(-w,w)));
    Vec3D dir = mrot * Vec3D(0,1,0);
    p.dir = dir.normalize();

  } else {
    p.pos = sys->pos + Vec3D(randfloat(-l,l), 0, randfloat(-w,w));
    p.pos = sys->parent->mat * p.pos;

    //Vec3D dir = mrot * Vec3D(0,1,0);
    Vec3D dir = sys->parent->mrot * Vec3D(0,1,0);

    p.dir = dir;//.normalize();
    p.down = Vec3D(0,-1.0f,0); // dir * -1.0f;
    p.speed = dir.normalize() * spd * (1.0f+randfloat(-var,var));
  }

  if(!sys->billboard)	{
    p.corners[0] = mrot * Vec3D(-1,0,+1);
    p.corners[1] = mrot * Vec3D(+1,0,+1);
    p.corners[2] = mrot * Vec3D(+1,0,-1);
    p.corners[3] = mrot * Vec3D(-1,0,-1);
  }

  p.life = 0;
  size_t l_anim = anim;
  if (GLOBALSETTINGS.bZeroParticle)
    l_anim = 0;
  p.maxlife = sys->lifespan.getValue(l_anim, time);
  if (p.maxlife == 0)
    p.maxlife = 1;

  p.origin = p.pos;

  p.tile = randint(0, sys->rows*sys->cols-1);
  return p;
}

Particle SphereParticleEmitter::newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2)
{
  Particle p;
  Vec3D dir;
  float radius;

  radius = randfloat(0,1);

  // Old method
  //float t = randfloat(0,2*PI);

  // New
  // Spread should never be zero for sphere particles ?
  float t = 0;
  if (spr == 0)
    t = randfloat((float)-PI,(float)PI);
  else
    t = randfloat(-spr,spr);

  //Spread Calculation
  Matrix mrot;

  CalcSpreadMatrix(spr*2,spr2*2,w,l);
  mrot=sys->parent->mrot*SpreadMat;

  // New
  // Length should never technically be zero ?
  //if (l==0)
  //	l = w;

  // New method
  // Vec3D bdir(w*cosf(t), 0.0f, l*sinf(t));
  // --

  // TODO: fix shpere emitters to work properly
  /* // Old Method
	//Vec3D bdir(l*cosf(t), 0, w*sinf(t));
	//Vec3D bdir(0, w*cosf(t), l*sinf(t));


	float theta_range = sys->spread.getValue(anim, time);
	float theta = -0.5f* theta_range + randfloat(0, theta_range);
	Vec3D bdir(0, l*cosf(theta), w*sinf(theta));

	float phi_range = sys->lat.getValue(anim, time);
	float phi = randfloat(0, phi_range);
	rotate(0,0, &bdir.z, &bdir.x, phi);
   */

  if (sys->flags == 57 || sys->flags == 313) { // Faith Halo
    Vec3D bdir(w*cosf(t)*1.6, 0.0f, l*sinf(t)*1.6);

    p.pos = sys->pos + bdir;
    p.pos = sys->parent->mat * p.pos;

    if (bdir.lengthSquared()==0)
      p.speed = Vec3D(0,0,0);
    else {
      dir = sys->parent->mrot * (bdir.normalize());//mrot * Vec3D(0, 1.0f,0);
      p.speed = dir.normalize() * spd * (1.0f+randfloat(-var,var));   // ?
    }

  } else {
    Vec3D bdir;
    float temp;

    bdir = mrot * Vec3D(0,1,0) * radius;
    temp = bdir.z;
    bdir.z = bdir.y;
    bdir.y = temp;

    p.pos = sys->parent->mat * sys->pos + bdir;


    //p.pos = sys->pos + bdir;
    //p.pos = sys->parent->mat * p.pos;


    if ((bdir.lengthSquared()==0) && ((sys->flags&0x100)!=0x100))
    {
      p.speed = Vec3D(0,0,0);
      dir = sys->parent->mrot * Vec3D(0,1,0);
    }
    else {
      if(sys->flags&0x100)
        dir = sys->parent->mrot * Vec3D(0,1,0);
      else
        dir = bdir.normalize();

      p.speed = dir.normalize() * spd * (1.0f+randfloat(-var,var));   // ?
    }
  }

  p.dir =  dir.normalize();//mrot * Vec3D(0, 1.0f,0);
  p.down = Vec3D(0,-1.0f,0);

  p.life = 0;
  size_t l_anim = anim;
  if (GLOBALSETTINGS.bZeroParticle)
    l_anim = 0;
  p.maxlife = sys->lifespan.getValue(l_anim, time);
  if (p.maxlife == 0)
    p.maxlife = 1;

  p.origin = p.pos;

  p.tile = randint(0, sys->rows*sys->cols-1);
  return p;
}




void RibbonEmitter::init(GameFile * f, ModelRibbonEmitterDef &mta, uint32 *globals)
{
  color.init(mta.color, f, globals);
  opacity.init(mta.opacity, f, globals);
  above.init(mta.above, f, globals);
  below.init(mta.below, f, globals);

  parent = model->bones + mta.bone;
  int *texlist = (int*)(f->getBuffer() + mta.ofsTextures);
  // just use the first texture for now; most models I've checked only had one
  texture = model->textures[texlist[0]];

  tpos = pos = fixCoordSystem(mta.pos);

  // TODO: figure out actual correct way to calculate length
  // in BFD, res is 60 and len is 0.6, the trails are very short (too long here)
  // in CoT, res and len are like 10 but the trails are supposed to be much longer (too short here)
  numsegs = (int)mta.res;
  seglen = mta.length;
  length = mta.res * seglen;

  // create first segment
  RibbonSegment rs;
  rs.pos = tpos;
  rs.len = 0;
  segs.push_back(rs);
}

void RibbonEmitter::setup(size_t anim, size_t time)
{
  Vec3D ntpos = parent->mat * pos;
  Vec3D ntup = parent->mat * (pos + Vec3D(0,0,1));
  ntup -= ntpos;
  ntup.normalize();
  float dlen = (ntpos-tpos).length();

  manim = anim;
  mtime = time;

  // move first segment
  RibbonSegment &first = *segs.begin();
  if (first.len > seglen) {
    // add new segment
    first.back = (tpos-ntpos).normalize();
    first.len0 = first.len;
    RibbonSegment newseg;
    newseg.pos = ntpos;
    newseg.up = ntup;
    newseg.len = dlen;
    segs.push_front(newseg);
  } else {
    first.up = ntup;
    first.pos = ntpos;
    first.len += dlen;
  }

  // kill stuff from the end
  float l = 0;
  bool erasemode = false;
  for (std::list<RibbonSegment>::iterator it = segs.begin(); it != segs.end(); ) {
    if (!erasemode) {
      l += it->len;
      if (l > length) {
        it->len = l - length;
        erasemode = true;
      }
      ++it;
    } else {
      segs.erase(it++);
    }
  }

  tpos = ntpos;
  tcolor = Vec4D(color.getValue(anim, time), opacity.getValue(anim, time));

  tabove = above.getValue(anim, time);
  tbelow = below.getValue(anim, time);
}

void RibbonEmitter::draw()
{
  /*
	// placeholders
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);
	glColor4f(1,1,1,1);
	glBegin(GL_TRIANGLES);
	glVertex3fv(tpos);
	glVertex3fv(tpos + Vec3D(1,1,0));
	glVertex3fv(tpos + Vec3D(-1,1,0));
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
   */

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  glEnable(GL_BLEND);
  glDisable(GL_LIGHTING);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glColor4fv(tcolor);

  glBegin(GL_QUAD_STRIP);
  std::list<RibbonSegment>::iterator it = segs.begin();
  float l = 0;
  for (; it != segs.end(); ++it) {
    float u = l/length;

    glTexCoord2f(u,0);
    glVertex3fv(it->pos + tabove * it->up);
    glTexCoord2f(u,1);
    glVertex3fv(it->pos - tbelow * it->up);

    l += it->len;
  }

  if (segs.size() > 1) {
    // last segment...?
    --it;
    glTexCoord2f(1,0);
    glVertex3fv(it->pos + tabove * it->up + (it->len/it->len0) * it->back);
    glTexCoord2f(1,1);
    glVertex3fv(it->pos - tbelow * it->up + (it->len/it->len0) * it->back);
  }
  glEnd();

  glColor4f(1,1,1,1);
  glEnable(GL_LIGHTING);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_TRUE);
}





