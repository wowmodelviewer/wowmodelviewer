#ifndef PARTICLE_H
#define PARTICLE_H

class WoWModel;
class ParticleSystem;
class RibbonEmitter;

#include "WoWModel.h"
#include "animated.h"

#include <list>

class Bone;

struct Particle
{
    Vec3D pos, speed, down, origin, dir;
    Vec3D	corners[4];
    //Vec3D tpos;
    float size, life, maxlife;
    size_t tile;
    Vec4D color;
};

typedef std::list<Particle> ParticleList;

class ParticleEmitter
{
  protected:
    ParticleSystem *sys;
  public:
    ParticleEmitter(ParticleSystem *sys): sys(sys) {}
    virtual Particle newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2) = 0;
    virtual ~ParticleEmitter() {}
};

class PlaneParticleEmitter: public ParticleEmitter
{
  public:
    PlaneParticleEmitter(ParticleSystem *sys): ParticleEmitter(sys) {}
    Particle newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2);
};

class SphereParticleEmitter: public ParticleEmitter
{
  public:
    SphereParticleEmitter(ParticleSystem *sys): ParticleEmitter(sys) {}
    Particle newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2);
};

struct TexCoordSet
{
    Vec2D tc[4];
};

class ParticleSystem
{
    float mid, slowdown, rotation;
    Vec3D pos;
    GLuint texture, texture2, texture3;
    ParticleEmitter *emitter;
    ParticleList particles;
    int order, ParticleType;
    size_t manim, mtime;
    int rows, cols;
    std::vector<TexCoordSet> tiles;
    void initTile(Vec2D *tc, int num);
    bool billboard;
    float rem;
    //bool transform;
    // unknown parameters omitted for now ...
    int32 flags;
    int16 EmitterType;
    Bone *parent;

  public:
    int blend;
    WoWModel *model;
    float tofs;
    Animated<uint16> enabled;
    Animated<float> speed, variation, spread, lat, gravity, lifespan, rate, areal, areaw, deacceleration;
    Vec4D colors[3];
    float sizes[3];
    bool multitexture;

    ParticleSystem(): mid(0), emitter(0), rem(0)
    {
      multitexture = 0;
      blend = 0;
      order = 0;
      ParticleType = 0;
      manim = 0;
      mtime = 0;
      rows = 0;
      cols = 0;
      model = 0;
      parent = 0;
      texture = 0;
      texture2 = 0;
      texture3 = 0;
      slowdown = 0;
      rotation = 0;
      tofs = 0;
    }
    ~ParticleSystem() { delete emitter; }

    void init(GameFile * f, M2ParticleDef &mta, uint32 *globals);
    void update(float dt);

    void setup(size_t anim, size_t time);
    void draw();

    friend class PlaneParticleEmitter;
    friend class SphereParticleEmitter;
    int BlendValueForMode(int mode);
    friend std::ostream& operator<<(std::ostream& out, ParticleSystem& v)
    {
      out << "        <colors>" << v.colors[0] << "</colors>" << std::endl;
      out << "        <colors>" << v.colors[1] << "</colors>" << std::endl;
      out << "        <colors>" << v.colors[2] << "</colors>" << std::endl;
      out << "        <sizes>" << v.sizes[0] << "</sizes>" << std::endl;
      out << "        <sizes>" << v.sizes[1] << "</sizes>" << std::endl;
      out << "        <sizes>" << v.sizes[2] << "</sizes>" << std::endl;
      out << "        <mid>" << v.mid << "</mid>" << std::endl;
      out << "        <slowdown>" << v.slowdown << "</slowdown>" << std::endl;
      out << "        <rotation>" << v.rotation << "</rotation>" << std::endl;
      out << "        <pos>" << v.pos << "</pos>" << std::endl;
      out << "        <texture>" << v.texture << "</texture>" << std::endl;
      out << "        <blend>" << v.blend << "</blend>" << std::endl;
      out << "        <order>" << v.order << "</order>" << std::endl;
      out << "        <ParticleType>" << v.ParticleType << "</ParticleType>" << std::endl;
      out << "        <manim>" << v.manim << "</manim>" << std::endl;
      out << "        <mtime>" << v.mtime << "</mtime>" << std::endl;
      out << "        <rows>" << v.rows << "</rows>" << std::endl;
      out << "        <cols>" << v.cols << "</cols>" << std::endl;
      out << "        <billboard>" << v.billboard << "</billboard>" << std::endl;
      out << "        <rem>" << v.rem << "</rem>" << std::endl;
      out << "        <flags>" << v.flags << "</flags>" << std::endl;
      out << "        <EmitterType>" << v.EmitterType << "</EmitterType>" << std::endl;
      out << "        <tofs>" << v.tofs << "</tofs>" << std::endl;
      return out;
    }
};


struct RibbonSegment
{
    Vec3D pos, up, back;
    float len,len0;
};

class RibbonEmitter
{
    Animated<Vec3D> color;
    AnimatedShort opacity;
    Animated<float> above, below;

    Bone *parent;
    float f1, f2;

    Vec3D pos;

    size_t manim, mtime;
    float length, seglen;
    int numsegs;

    Vec3D tpos;
    Vec4D tcolor;
    float tabove, tbelow;

    GLuint texture;

    std::list<RibbonSegment> segs;

  public:
    WoWModel *model;

    void init(GameFile * f, ModelRibbonEmitterDef &mta, uint32 *globals);
    void setup(size_t anim, size_t time);
    void draw();
};



#endif
