#pragma once

#define _PARTICLE_API_

class WoWModel;
class ParticleSystem;
class RibbonEmitter;

#include "animated.h"

#include <iostream>
#include <list>
#include <glad/gl.h>
#include "glm/glm.hpp"

class Bone;

/// @brief A single particle instance in a particle system.
struct Particle
{
	glm::vec3 pos, speed, down, origin, dir;  ///< Position, velocity, gravity, origin, and direction.
	glm::vec3 corners[4];   ///< Billboard corner positions.
	glm::vec3 tpos;         ///< Transformed position.
	float size, life, maxlife;  ///< Current size, remaining life, and maximum lifespan.
	size_t tile;            ///< Current texture tile index.
	glm::vec4 color;        ///< Current RGBA colour.
};

typedef std::list<Particle> ParticleList;

/// @brief Abstract base class for particle emitters.
class ParticleEmitter
{
protected:
	ParticleSystem* sys;  ///< Owning particle system.

public:
	ParticleEmitter(ParticleSystem* sys): sys(sys)
	{
	}

	/// @brief Create a new particle with the given emission parameters.
	virtual Particle newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr,
								 float spr2) = 0;

	virtual ~ParticleEmitter() = default;
};

/// @brief Emits particles from a rectangular plane.
class PlaneParticleEmitter : public ParticleEmitter
{
public:
	PlaneParticleEmitter(ParticleSystem* sys): ParticleEmitter(sys)
	{
	}

	Particle newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2);
};

/// @brief Emits particles from the surface of a sphere.
class SphereParticleEmitter : public ParticleEmitter
{
public:
	SphereParticleEmitter(ParticleSystem* sys): ParticleEmitter(sys)
	{
	}

	Particle newParticle(size_t anim, size_t time, float w, float l, float spd, float var, float spr, float spr2);
};

/// @brief A set of 4 texture coordinates for a particle tile.
struct TexCoordSet
{
	glm::vec2 tc[4];  ///< Texture coordinates for the four corners.
};

/// @brief M2 particle system — manages emission, simulation, and rendering of particles.
class _PARTICLE_API_ ParticleSystem
{
	float mid, slowdown, rotation;
	glm::vec3 pos, tpos;
	GLuint texture, texture2, texture3;
	ParticleEmitter* emitter;
	ParticleList particles;
	int order, ParticleType;
	size_t manim, mtime;
	int rows, cols;
	std::vector<TexCoordSet> tiles;
	void initTile(glm::vec2* tc, int num);
	bool billboard;
	float rem;
	//bool transform;
	// unknown parameters omitted for now ...
	int32 flags;
	int16 EmitterType;
	Bone* parent;

public:
	int blend;
	WoWModel* model;
	float tofs;
	Animated<uint16> enabled;
	Animated<float> speed, variation, spread, lat, gravity, lifespan, rate, areal, areaw;
	//Animated<float>  deacceleration;
	glm::vec4 colors[3];
	float sizes[3];
	bool multitexture, doNotTrail;
	int particleColID;
	bool replaceParticleColors;
	// Start, Mid and End colours, for cases where the model's particle colours
	// are overridden by values from ParticleColor.dbc, indexed from CreatureDisplayInfo:
	typedef std::vector<glm::vec4> particleColorSet;
	// The particle will get its replacement colour set from 0, 1 or 2, depending on
	// whether its ParticleColorIndex is set to 11, 12 or 13:
	std::vector<particleColorSet> particleColorReplacements;

	ParticleSystem()
    : mid(0), slowdown(0), rotation(0), pos(), tpos(),
      texture(0), texture2(0), texture3(0),
      emitter(nullptr), order(0), ParticleType(0),
      manim(0), mtime(0), rows(0), cols(0),
      tiles(), billboard(false), rem(0),
      flags(0), EmitterType(0), parent(nullptr),
      blend(0), model(nullptr), tofs(0),
      enabled(), speed(), variation(), spread(), lat(), gravity(), lifespan(), rate(), areal(), areaw(),
      multitexture(false), doNotTrail(false), particleColID(0), replaceParticleColors(false),
      particleColorReplacements()
{
    // Optionally, initialize arrays and vectors if needed
    for (int i = 0; i < 3; ++i) {
        colors[i] = glm::vec4(0.0f);
        sizes[i] = 0.0f;
    }
}

	~ParticleSystem() { delete emitter; }

	void init(GameFile* f, M2ParticleDef& mta, std::vector<uint32>& globals);
	void update(float dt);

	void setup(size_t anim, size_t time);
	void draw();

	friend class PlaneParticleEmitter;
	friend class SphereParticleEmitter;
	//int BlendValueForMode(int mode);

	friend std::ostream& operator<<(std::ostream& out, const ParticleSystem& v)
	{
		out << "    <colors>" << v.colors[0].x << " " << v.colors[0].y << " " << v.colors[0].z << "</colors>" <<
			std::endl;
		out << "    <colors>" << v.colors[1].x << " " << v.colors[1].y << " " << v.colors[1].z << "</colors>" <<
			std::endl;
		out << "    <colors>" << v.colors[2].x << " " << v.colors[2].y << " " << v.colors[2].z << "</colors>" <<
			std::endl;
		out << "    <sizes>" << v.sizes[0] << "</sizes>" << std::endl;
		out << "    <sizes>" << v.sizes[1] << "</sizes>" << std::endl;
		out << "    <sizes>" << v.sizes[2] << "</sizes>" << std::endl;
		out << "    <mid>" << v.mid << "</mid>" << std::endl;
		out << "    <slowdown>" << v.slowdown << "</slowdown>" << std::endl;
		out << "    <rotation>" << v.rotation << "</rotation>" << std::endl;
		out << "    <pos>" << v.pos.x << " " << v.pos.y << " " << v.pos.z << "</pos>" << std::endl;
		out << "    <texture>" << v.texture << "</texture>" << std::endl;
		out << "    <blend>" << v.blend << "</blend>" << std::endl;
		out << "    <order>" << v.order << "</order>" << std::endl;
		out << "    <ParticleType>" << v.ParticleType << "</ParticleType>" << std::endl;
		out << "    <manim>" << v.manim << "</manim>" << std::endl;
		out << "    <mtime>" << v.mtime << "</mtime>" << std::endl;
		out << "    <rows>" << v.rows << "</rows>" << std::endl;
		out << "    <cols>" << v.cols << "</cols>" << std::endl;
		out << "    <billboard>" << v.billboard << "</billboard>" << std::endl;
		out << "    <rem>" << v.rem << "</rem>" << std::endl;
		out << "    <flags>" << v.flags << "</flags>" << std::endl;
		out << "    <EmitterType>" << v.EmitterType << "</EmitterType>" << std::endl;
		out << "    <tofs>" << v.tofs << "</tofs>" << std::endl;
		return out;
	}

	static void useDoNotTrailInfo()
	{
		ParticleSystem::useDoNotTrail = true;
	}

	static bool useDoNotTrail;
};

/// @brief A single segment in a ribbon trail.
struct RibbonSegment
{
	glm::vec3 pos, up, back;  ///< Segment position, up vector, and back vector.
	float len, len0;          ///< Current length and initial length.
};

/// @brief M2 ribbon emitter — produces trailing ribbon effects attached to bones.
class RibbonEmitter
{
	Animated<glm::vec3> color;
	AnimatedShort opacity;
	Animated<float> above, below;

	Bone* parent;

	glm::vec3 pos;

	size_t manim, mtime;
	float length, seglen;
	int numsegs;

	glm::vec3 tpos;
	glm::vec4 tcolor;
	float tabove, tbelow;

	GLuint texture;

	std::list<RibbonSegment> segs;

public:
	WoWModel* model;

	void init(GameFile* f, ModelRibbonEmitterDef& mta, std::vector<uint32>& globals);
	void setup(size_t anim, size_t time);
	void draw();
};
