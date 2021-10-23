#include "Mesh.h"

#include <GL/glew.h>

#include "Logger.h"

#include "ShaderProgram.h"

//#include <utility>

Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned> indices, std::vector<unsigned> textures)
  : vertices_(std::move(vertices)), indices_(std::move(indices)), textures_(std::move(textures))
{
  const auto r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
  const auto g = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
  const auto b = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);

  for (auto & v : vertices_)
    v.color = glm::vec3(r,g,b);

  setup();
}

Mesh::~Mesh()
{
  glDeleteVertexArrays(1, &vao_);
  glDeleteBuffers(1, &vbo_);
  glDeleteBuffers(1, &ebo_);
}


void Mesh::draw(ShaderProgram & shader) const
{
  switch (blendmode_)
  {
  case 0:           // 0
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case 1:      // 1
    glEnable(GL_ALPHA_TEST);
    glBlendFunc(GL_ONE, GL_ZERO);
    break;
  case 2:      // 2
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;
  case 3:         // 3
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_COLOR, GL_ONE);
    break;
  case 4:   // 4
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    break;
  case 5:           // 5
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    break;
  case 6:      // 6
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
    break;
  case 7:                 // 7, new in WoD
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    break;
  default:
    LOG_ERROR << "Unknown blendmode:" << blendmode_;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  if (!textures_.empty())
  {
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(shader.get(), "t1"), 0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
  }


 // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glBindVertexArray(vao_);
  glDrawElements(GL_TRIANGLES, indices_.size(), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void Mesh::setup()
{
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  // bind overall buffer
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), &vertices_[0], GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);

  // vertex positions
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), static_cast<void*>(nullptr));

  // vertex normals
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));

  // texture coords
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texCoords)));

  // color
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));


  glBindVertexArray(0);
}


