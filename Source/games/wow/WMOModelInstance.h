#pragma once

#include "glm/glm.hpp"

#include <QString>

class GameFile;
class ModelManager;
class WoWModel;

class WMOModelInstance
{
public:
	// header
	glm::vec3 pos; // Position
	float w; // W for Quat Rotation
	glm::vec3 dir; // Direction for Quat Rotation
	float sc; // Scale Factor
	unsigned int d1;

	WoWModel* model;
	QString filename;
	int id;
	unsigned int scale;
	int light;
	glm::vec3 ldir;
	glm::vec3 lcol;

	WMOModelInstance() = default;

	void init(char* fname, GameFile& f);
	void draw();

	void loadModel(ModelManager& mm);
	void unloadModel(ModelManager& mm);
};
