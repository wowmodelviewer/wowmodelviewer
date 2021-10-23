#ifndef MESH_H
#define MESH_H

#include <glm/glm.hpp>

#include <vector>


class ShaderProgram;

struct Vertex
{
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoords;
  glm::vec3 color;
};

class Mesh
{
public:
  Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<unsigned int> textures);

  ~Mesh();

  void draw(ShaderProgram &) const;

  unsigned int blendmode_;

private:

  void setup();

  std::vector<Vertex> vertices_;
  std::vector<unsigned int> indices_;
  std::vector<unsigned int> textures_;

  unsigned int vao_;
  unsigned int vbo_;
  unsigned int ebo_;
};

#endif